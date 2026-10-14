// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-only

#include "IbusContext.hpp"

#include <array>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include "Dbus.hpp"
#include "Process.hpp"
#include "VirtualKeys.hpp"

namespace {

constexpr const char* kService = "org.freedesktop.IBus";
constexpr const char* kServicePath = "/org/freedesktop/IBus";
constexpr const char* kInputContext = "org.freedesktop.IBus.InputContext";
constexpr const char* kServiceInterface = "org.freedesktop.IBus.Service";
constexpr uint32_t kCapabilities = (1U << 0U) | (1U << 2U) | (1U << 3U);
constexpr uint32_t kReleaseMask = 1U << 30U;
constexpr int kTimeoutMs = 500;
constexpr uint32_t kHangulKeysym = 0xff31;
constexpr uint32_t kEvdevOffset = 8;

constexpr std::array<std::array<std::string_view, 3>, 4> kEngines = {{
    {},
    {"hangul", "", ""},
    {"mozc-jp", "anthy", "kkc"},
    {"table:cangjie5", "table:cangjie3", "table:cangjie-big"},
}};

struct ModifierMask {
    uint8_t modifier;
    uint32_t state;
};

constexpr ModifierMask kModifierMasks[] = {
    {kModShift, 1U << 0U},
    {kModCapsLock, 1U << 1U},
    {kModCtrl, 1U << 2U},
    {kModAlt, 1U << 3U},
    {kModSuper, 1U << 6U},
    {kModMeta, 1U << 7U},
};

auto State(uint8_t modifiers) -> uint32_t {
    uint32_t state = 0;
    for (const auto& mask : kModifierMasks) {
        if ((modifiers & mask.modifier) != 0) {
            state |= mask.state;
        }
    }
    return state;
}

auto CallOn(
    DBusConnection* connection,
    const std::string& path,
    const char* interface,
    const char* method,
    const std::function<void(DBusMessageIter&)>& append
) -> Result<DBusMessage*> {
    DBusMessage* message = dbus_message_new_method_call(kService, path.c_str(), interface, method);
    if (message == nullptr) {
        return Unexpected("Out of memory");
    }
    if (append) {
        DBusMessageIter arguments;
        dbus_message_iter_init_append(message, &arguments);
        append(arguments);
    }
    return Dbus::CallMethod(connection, message, kTimeoutMs);
}

auto Discard(Result<DBusMessage*> reply) -> Result<> {
    if (!reply) {
        return Unexpected(reply.error().Message());
    }
    dbus_message_unref(*reply);
    return {};
}

auto Unwrap(DBusMessageIter& iter) -> DBusMessageIter {
    DBusMessageIter inner = iter;
    while (dbus_message_iter_get_arg_type(&inner) == DBUS_TYPE_VARIANT) {
        DBusMessageIter next;
        dbus_message_iter_recurse(&inner, &next);
        inner = next;
    }
    return inner;
}

auto Fields(DBusMessageIter& iter) -> std::optional<DBusMessageIter> {
    DBusMessageIter value = Unwrap(iter);
    if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_STRUCT) {
        return std::nullopt;
    }
    DBusMessageIter fields;
    dbus_message_iter_recurse(&value, &fields);
    dbus_message_iter_next(&fields);
    dbus_message_iter_next(&fields);
    return fields;
}

auto ReadText(DBusMessageIter& iter) -> std::string {
    auto fields = Fields(iter);
    if (!fields || dbus_message_iter_get_arg_type(&*fields) != DBUS_TYPE_STRING) {
        return {};
    }
    const char* text = nullptr;
    dbus_message_iter_get_basic(&*fields, static_cast<void*>(&text));
    return text != nullptr ? std::string(text) : std::string();
}

auto ReadUint(DBusMessageIter& iter) -> uint32_t {
    uint32_t value = 0;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UINT32) {
        dbus_message_iter_get_basic(&iter, &value);
    }
    dbus_message_iter_next(&iter);
    return value;
}

auto ReadCandidates(DBusMessageIter& iter) -> std::vector<std::string> {
    auto fields = Fields(iter);
    if (!fields) {
        return {};
    }
    const uint32_t page_size = ReadUint(*fields);
    const uint32_t cursor = ReadUint(*fields);
    for (int skipped = 0; skipped < 3; ++skipped) {
        dbus_message_iter_next(&*fields);
    }
    if (dbus_message_iter_get_arg_type(&*fields) != DBUS_TYPE_ARRAY) {
        return {};
    }
    const uint32_t first = page_size > 0 ? cursor / page_size * page_size : 0;
    std::vector<std::string> candidates;
    DBusMessageIter item;
    dbus_message_iter_recurse(&*fields, &item);
    for (uint32_t index = 0; dbus_message_iter_get_arg_type(&item) != DBUS_TYPE_INVALID; ++index) {
        if (index >= first && (page_size == 0 || index < first + page_size)) {
            candidates.push_back(ReadText(item));
        }
        dbus_message_iter_next(&item);
    }
    return candidates;
}

auto ReadBool(DBusMessageIter& iter) -> bool {
    dbus_bool_t value = FALSE;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_BOOLEAN) {
        dbus_message_iter_get_basic(&iter, &value);
    }
    return value != FALSE;
}

} // namespace

auto IbusContext::Create(const std::string& address, ImeScript script) -> Result<std::unique_ptr<IbusContext>> {
    ASSIGN_OR_RETURN(auto* connection, Dbus::ConnectAddress(address));
    auto created = CallOn(connection, kServicePath, kService, "CreateInputContext", [](DBusMessageIter& arguments) -> void {
        const char* name = "FrameXR";
        dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, static_cast<const void*>(&name));
    });
    if (!created) {
        Dbus::CloseConnection(connection);
        return Unexpected(created.error().Message());
    }
    const char* path = nullptr;
    dbus_message_get_args(*created, nullptr, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
    std::unique_ptr<IbusContext> context(new IbusContext(connection, path != nullptr ? path : ""));
    dbus_message_unref(*created);
    if (context->m_path.empty()) {
        return Unexpected("IBus did not create an input context");
    }
    dbus_bus_add_match(
        connection,
        std::format("type='signal',interface='{}',path='{}'", kInputContext, context->m_path).c_str(),
        nullptr
    );
    RETURN_IF_ERROR(Discard(context->Call(kInputContext, "SetCapabilities", [](DBusMessageIter& arguments) -> void {
        dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &kCapabilities);
    })));
    RETURN_IF_ERROR(Discard(context->Call(kInputContext, "FocusIn", nullptr)));
    RETURN_IF_ERROR(context->SelectEngine(script));
    return context;
}

auto IbusContext::Selected(const std::string& name) -> bool {
    auto current = Call(kInputContext, "GetEngine", nullptr);
    if (!current) {
        return false;
    }
    DBusMessageIter description;
    const bool selected = dbus_message_iter_init(*current, &description) != FALSE && ReadText(description) == name;
    dbus_message_unref(*current);
    return selected;
}

auto IbusContext::SelectEngine(ImeScript script) -> Result<> {
    for (const auto engine : kEngines.at(static_cast<size_t>(script))) {
        if (engine.empty()) {
            continue;
        }
        const std::string name(engine);
        const auto selected = Discard(
            CallOn(m_connection, kServicePath, kService, "SetGlobalEngine", [&name](DBusMessageIter& arguments) -> void {
                const char* value = name.c_str();
                dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, static_cast<const void*>(&value));
            })
        );
        if (!selected || !Selected(name)) {
            continue;
        }
        if (name == "hangul"
            && Process::Output({"gsettings", "get", "org.freedesktop.ibus.engine.hangul", "initial-input-mode"}) == "'latin'") {
            Key(VirtualKey::Hangul, kHangulKeysym, 0);
        }
        Poll();
        m_preedit.clear();
        m_candidates.clear();
        return {};
    }
    return Unexpected("none of the IBus engines for this script are installed");
}

IbusContext::IbusContext(DBusConnection* connection, std::string path) : m_connection(connection), m_path(std::move(path)) {
}

IbusContext::~IbusContext() {
    if (!m_path.empty()) {
        for (const auto& [interface, method] : {std::pair{kInputContext, "FocusOut"}, std::pair{kServiceInterface, "Destroy"}}) {
            if (!Discard(Call(interface, method, nullptr))) {
                break;
            }
        }
    }
    Dbus::CloseConnection(m_connection);
}

auto IbusContext::Key(VirtualKey key, uint32_t keysym, uint8_t modifiers) -> bool {
    const auto keycode = static_cast<uint32_t>(key) - kEvdevOffset;
    bool handled = false;
    for (const uint32_t state : {State(modifiers), State(modifiers) | kReleaseMask}) {
        auto reply = Call(kInputContext, "ProcessKeyEvent", [keysym, keycode, state](DBusMessageIter& arguments) -> void {
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &keysym);
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &keycode);
            dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &state);
        });
        if (!reply) {
            break;
        }
        dbus_bool_t result = FALSE;
        dbus_message_get_args(*reply, nullptr, DBUS_TYPE_BOOLEAN, &result, DBUS_TYPE_INVALID);
        dbus_message_unref(*reply);
        handled = handled || ((state & kReleaseMask) == 0 && result != FALSE);
    }
    Poll();
    return handled;
}

auto IbusContext::Poll() -> void {
    dbus_connection_read_write(m_connection, 0);
    while (DBusMessage* message = dbus_connection_pop_message(m_connection)) {
        Dispatch(message);
        dbus_message_unref(message);
    }
}

auto IbusContext::Call(const char* interface, const char* method, const std::function<void(DBusMessageIter&)>& append)
    -> Result<DBusMessage*> {
    return CallOn(m_connection, m_path, interface, method, append);
}

auto IbusContext::Dispatch(DBusMessage* message) -> void {
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL || m_path != dbus_message_get_path(message)) {
        return;
    }
    const std::string_view member = dbus_message_get_member(message);
    DBusMessageIter arguments;
    const bool has_arguments = dbus_message_iter_init(message, &arguments) != FALSE;
    if (member == "CommitText" && has_arguments) {
        m_preedit.clear();
        if (m_on_commit) {
            m_on_commit(ReadText(arguments));
        }
    } else if (member == "UpdatePreeditText" && has_arguments) {
        auto text = ReadText(arguments);
        dbus_message_iter_next(&arguments);
        dbus_message_iter_next(&arguments);
        m_preedit = ReadBool(arguments) ? std::move(text) : std::string();
    } else if (member == "HidePreeditText") {
        m_preedit.clear();
    } else if (member == "UpdateLookupTable" && has_arguments) {
        auto candidates = ReadCandidates(arguments);
        dbus_message_iter_next(&arguments);
        m_candidates = ReadBool(arguments) ? std::move(candidates) : std::vector<std::string>();
    } else if (member == "HideLookupTable") {
        m_candidates.clear();
    } else if (member == "ForwardKeyEvent" && has_arguments) {
        const uint32_t keysym = ReadUint(arguments);
        ReadUint(arguments);
        if ((ReadUint(arguments) & kReleaseMask) == 0 && m_on_forward) {
            m_on_forward(keysym);
        }
        return;
    } else {
        return;
    }
    if (m_on_change) {
        m_on_change();
    }
}
