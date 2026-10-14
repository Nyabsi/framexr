// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "document/Actions.hpp"

#include <array>
#include <format>
#include <utility>

#include "Json.hpp"
#include "Osc.hpp"
#include "TaskQueue.hpp"
#include "document/FormParser.hpp"

namespace
{

constexpr auto kSystemActions = std::to_array<std::pair<SystemAction, std::string_view>>({
    {SystemAction::ToggleAllowSliding, "ToggleAllowSliding"},
    {SystemAction::Recenter, "Recenter"},
    {SystemAction::ShowHide, "ShowHide"},
    {SystemAction::ToggleNotificationSounds, "ToggleNotificationSounds"},
    {SystemAction::ToggleNotifications, "ToggleNotifications"},
    {SystemAction::ToggleRotateLock, "ToggleRotateLock"},
    {SystemAction::PlayspaceResetOffset, "PlayspaceResetOffset"},
    {SystemAction::PlayspaceFixFloor, "PlayspaceFixFloor"},
    {SystemAction::RecalculateExtent, "RecalculateExtent"},
    {SystemAction::PersistConfig, "PersistConfig"},
    {SystemAction::PersistLayout, "PersistLayout"},
    {SystemAction::Edit, "Edit"},
});

constexpr auto kHighlightTests = std::to_array<std::pair<HighlightTest, std::string_view>>({
    {HighlightTest::AllowSliding, "AllowSliding"},
    {HighlightTest::NotificationSounds, "NotificationSounds"},
    {HighlightTest::Notifications, "Notifications"},
    {HighlightTest::RotateLock, "RorateLock"},
});

constexpr auto kWindowActions = std::to_array<std::pair<WindowAction, std::string_view>>({
    {WindowAction::ShowMirror, "ShowMirror"},
    {WindowAction::ShowUi, "ShowUi"},
    {WindowAction::Destroy, "Destroy"},
});

constexpr auto kColorChannels = std::to_array<std::pair<ColorChannel, std::string_view>>({
    {ColorChannel::R, "R"},
    {ColorChannel::G, "G"},
    {ColorChannel::B, "B"},
    {ColorChannel::All, "All"},
});

constexpr auto kAxes = std::to_array<std::pair<Axis, std::string_view>>({
    {Axis::X, "X"},
    {Axis::Y, "Y"},
    {Axis::Z, "Z"},
});

constexpr auto kViewAngleKinds = std::to_array<std::pair<ViewAngleKind, std::string_view>>({
    {ViewAngleKind::MinOpacity, "MinOpacity"},
    {ViewAngleKind::MaxOpacity, "MaxOpacity"},
});

constexpr auto kPressRelease = std::to_array<std::pair<PressRelease, std::string_view>>({
    {PressRelease::Release, "Release"},
    {PressRelease::Press, "Press"},
});

auto ParseWatchAction(const nlohmann::json& json) -> WatchAction
{
    const auto name = Json::VariantName(json);
    const auto& payload = Json::VariantPayload(json);
    if (name == "Hide")
    {
        return WatchHide{};
    }
    if (name == "SwitchHands")
    {
        return WatchSwitchHands{};
    }
    if (name == "ViewAngle")
    {
        return WatchViewAngle{
            .kind = FormParser::ParseName(payload.at("kind"), kViewAngleKinds, "ViewAngleKind"),
            .delta = payload.at("delta").get<float>()
        };
    }
    if (name == "Rotation")
    {
        return WatchRotation{
            .axis = FormParser::ParseName(payload.at("axis"), kAxes, "Axis"),
            .delta = payload.at("delta").get<float>()
        };
    }
    if (name == "Position")
    {
        return WatchPosition{
            .axis = FormParser::ParseName(payload.at("axis"), kAxes, "Axis"),
            .delta = payload.at("delta").get<float>()
        };
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for WatchAction", name), &json);
}

auto ParseOscValue(const nlohmann::json& json) -> OscValue
{
    const auto type = json.at("type").get<std::string>();
    const auto& value = json.at("value");
    if (type == "Int")
    {
        return value.get<int32_t>();
    }
    if (type == "Float")
    {
        return value.get<float>();
    }
    if (type == "String")
    {
        return value.get<std::string>();
    }
    if (type == "Bool")
    {
        return value.get<bool>();
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for OscValue", type), &json);
}

auto WatchActionJson(const WatchAction& action) -> nlohmann::ordered_json
{
    return std::visit(
        [](const auto& item) -> nlohmann::ordered_json
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, WatchHide>)
            {
                return "Hide";
            }
            else if constexpr (std::is_same_v<Item, WatchSwitchHands>)
            {
                return "SwitchHands";
            }
            else if constexpr (std::is_same_v<Item, WatchViewAngle>)
            {
                return {
                    {"ViewAngle",
                     {{"kind", FormParser::NameOf(item.kind, kViewAngleKinds)}, {"delta", Json::ShortestFloat(item.delta)}}}
                };
            }
            else if constexpr (std::is_same_v<Item, WatchRotation>)
            {
                return {{"Rotation", {{"axis", FormParser::NameOf(item.axis, kAxes)}, {"delta", Json::ShortestFloat(item.delta)}}}};
            }
            else
            {
                return {{"Position", {{"axis", FormParser::NameOf(item.axis, kAxes)}, {"delta", Json::ShortestFloat(item.delta)}}}};
            }
        },
        action
    );
}

auto OscValueJson(const OscValue& value) -> nlohmann::ordered_json
{
    return std::visit(
        [](const auto& item) -> nlohmann::ordered_json
        {
            using Item = std::decay_t<decltype(item)>;
            nlohmann::ordered_json json;
            if constexpr (std::is_same_v<Item, int32_t>)
            {
                json["type"] = "Int";
                json["value"] = item;
            }
            else if constexpr (std::is_same_v<Item, float>)
            {
                json["type"] = "Float";
                json["value"] = Json::ShortestFloat(item);
            }
            else if constexpr (std::is_same_v<Item, std::string>)
            {
                json["type"] = "String";
                json["value"] = item;
            }
            else
            {
                json["type"] = "Bool";
                json["value"] = item;
            }
            return json;
        },
        value
    );
}

auto OptionalActions(const nlohmann::json& json, std::string_view key) -> std::optional<std::vector<ButtonAction>>
{
    const auto it = json.find(key);
    if (it == json.end() || it->is_null())
    {
        return std::nullopt;
    }
    return it->get<std::vector<ButtonAction>>();
}

} // namespace

auto from_json(const nlohmann::json& json, OverlaySelector& value) -> void
{
    if (json.is_number())
    {
        value = static_cast<OverlayId>(json.get<uint64_t>());
    }
    else
    {
        value = json.get<std::string>();
    }
}

auto from_json(const nlohmann::json& json, OverlayAction& value) -> void
{
    const auto name = Json::VariantName(json);
    if (name == "ToggleVisible")
    {
        value = OverlayToggleVisible{};
    }
    else if (name == "ToggleImmovable")
    {
        value = OverlayToggleImmovable{};
    }
    else if (name == "ToggleInteraction")
    {
        value = OverlayToggleInteraction{};
    }
    else if (name == "Opacity")
    {
        value = OverlayOpacity{.delta = Json::VariantPayload(json).at("delta").get<float>()};
    }
    else
    {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for OverlayAction", name), &json);
    }
}

auto from_json(const nlohmann::json& json, ButtonAction& value) -> void
{
    const auto type = json.at("type").get<std::string>();
    if (type == "Exec")
    {
        value = ExecButtonAction{
            .command = json.at("command").get<std::vector<std::string>>(),
            .toast = FormParser::Optional<std::string>(json, "toast")
        };
    }
    else if (type == "VirtualKey")
    {
        const auto key_name = json.at("keycode").get<std::string>();
        const auto key = VirtualKeys::Parse(key_name);
        if (!key)
        {
            throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for VirtualKey", key_name), &json);
        }
        value = VirtualKeyButtonAction{
            .keycode = *key,
            .action = FormParser::ParseName(json.at("action"), kPressRelease, "PressRelease")
        };
    }
    else if (type == "Watch")
    {
        value = WatchButtonAction{.action = ParseWatchAction(json.at("action"))};
    }
    else if (type == "Overlay")
    {
        value = OverlayButtonAction{
            .target = json.at("target").get<OverlaySelector>(),
            .action = json.at("action").get<OverlayAction>()
        };
    }
    else if (type == "Window")
    {
        value = WindowButtonAction{
            .target = json.at("target").get<std::string>(),
            .action = FormParser::ParseName(json.at("action"), kWindowActions, "WindowAction")
        };
    }
    else if (type == "Toast")
    {
        ToastButtonAction toast{
            .message = json.at("message").get<std::string>(),
            .body = FormParser::Optional<std::string>(json, "body")
        };
        if (const auto it = json.find("seconds"); it != json.end() && !it->is_null())
        {
            toast.seconds = it->get<float>();
        }
        value = std::move(toast);
    }
    else if (type == "ColorAdjust")
    {
        value = ColorAdjustButtonAction{
            .channel = FormParser::ParseName(json.at("channel"), kColorChannels, "ColorChannel"),
            .delta = json.at("delta").get<float>()
        };
    }
    else if (type == "DragMultiplier")
    {
        value = DragMultiplierButtonAction{.delta = json.at("delta").get<float>()};
    }
    else if (type == "System")
    {
        value = SystemButtonAction{.action = FormParser::ParseName(json.at("action"), kSystemActions, "SystemAction")};
    }
    else if (type == "SendOscValue")
    {
        SendOscButtonAction osc{.parameter = json.at("parameter").get<std::string>()};
        if (const auto it = json.find("values"); it != json.end() && !it->is_null())
        {
            std::vector<OscValue> values;
            for (const auto& item : *it)
            {
                values.push_back(ParseOscValue(item));
            }
            osc.values = std::move(values);
        }
        value = std::move(osc);
    }
    else if (type == "Shortcut")
    {
        ShortcutButtonAction shortcut;
        for (const auto& name : json.at("keys"))
        {
            const auto key_name = name.get<std::string>();
            const auto key = VirtualKeys::Parse(key_name);
            if (!key)
            {
                throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for VirtualKey", key_name), &json);
            }
            shortcut.keys.push_back(*key);
        }
        value = std::move(shortcut);
    }
    else if (type == "TypeText")
    {
        value = TypeTextButtonAction{.text = json.at("text").get<std::string>()};
    }
    else if (type == "KeyboardProfile")
    {
        value = KeyboardProfileButtonAction{.profile = FormParser::Optional<std::string>(json, "profile")};
    }
    else
    {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ButtonAction", type), &json);
    }
}

auto from_json(const nlohmann::json& json, ButtonData& value) -> void
{
    value.click_down = OptionalActions(json, "click_down");
    value.click_up = OptionalActions(json, "click_up");
    value.long_click_up = OptionalActions(json, "long_click_up");
    value.right_down = OptionalActions(json, "right_down");
    value.right_up = OptionalActions(json, "right_up");
    value.long_right_up = OptionalActions(json, "long_right_up");
    value.middle_down = OptionalActions(json, "middle_down");
    value.middle_up = OptionalActions(json, "middle_up");
    value.long_middle_up = OptionalActions(json, "long_middle_up");
    value.scroll_down = OptionalActions(json, "scroll_down");
    value.scroll_up = OptionalActions(json, "scroll_up");
    if (const auto it = json.find("highlight"); it != json.end() && !it->is_null())
    {
        value.highlight = FormParser::ParseName(*it, kHighlightTests, "HighlightTest");
    }
}

auto to_json(nlohmann::ordered_json& json, const OverlaySelector& value) -> void
{
    if (const auto* id = std::get_if<OverlayId>(&value))
    {
        json = static_cast<uint64_t>(*id);
    }
    else
    {
        json = std::get<std::string>(value);
    }
}

auto to_json(nlohmann::ordered_json& json, const OverlayAction& value) -> void
{
    std::visit(
        [&json](const auto& item) -> void
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, OverlayToggleVisible>)
            {
                json = "ToggleVisible";
            }
            else if constexpr (std::is_same_v<Item, OverlayToggleImmovable>)
            {
                json = "ToggleImmovable";
            }
            else if constexpr (std::is_same_v<Item, OverlayToggleInteraction>)
            {
                json = "ToggleInteraction";
            }
            else
            {
                json = {{"Opacity", {{"delta", Json::ShortestFloat(item.delta)}}}};
            }
        },
        value
    );
}

auto to_json(nlohmann::ordered_json& json, const ButtonAction& value) -> void
{
    json = nlohmann::ordered_json::object();
    std::visit(
        [&json](const auto& item) -> void
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ExecButtonAction>)
            {
                json["type"] = "Exec";
                json["command"] = item.command;
                if (item.toast)
                {
                    json["toast"] = *item.toast;
                }
            }
            else if constexpr (std::is_same_v<Item, VirtualKeyButtonAction>)
            {
                json["type"] = "VirtualKey";
                json["keycode"] = VirtualKeys::Name(item.keycode);
                json["action"] = FormParser::NameOf(item.action, kPressRelease);
            }
            else if constexpr (std::is_same_v<Item, WatchButtonAction>)
            {
                json["type"] = "Watch";
                json["action"] = WatchActionJson(item.action);
            }
            else if constexpr (std::is_same_v<Item, OverlayButtonAction>)
            {
                json["type"] = "Overlay";
                json["target"] = item.target;
                json["action"] = item.action;
            }
            else if constexpr (std::is_same_v<Item, WindowButtonAction>)
            {
                json["type"] = "Window";
                json["target"] = item.target;
                json["action"] = FormParser::NameOf(item.action, kWindowActions);
            }
            else if constexpr (std::is_same_v<Item, ToastButtonAction>)
            {
                json["type"] = "Toast";
                json["message"] = item.message;
                if (item.body)
                {
                    json["body"] = *item.body;
                }
                if (item.seconds)
                {
                    json["seconds"] = Json::ShortestFloat(*item.seconds);
                }
            }
            else if constexpr (std::is_same_v<Item, ColorAdjustButtonAction>)
            {
                json["type"] = "ColorAdjust";
                json["channel"] = FormParser::NameOf(item.channel, kColorChannels);
                json["delta"] = Json::ShortestFloat(item.delta);
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierButtonAction>)
            {
                json["type"] = "DragMultiplier";
                json["delta"] = Json::ShortestFloat(item.delta);
            }
            else if constexpr (std::is_same_v<Item, SystemButtonAction>)
            {
                json["type"] = "System";
                json["action"] = FormParser::NameOf(item.action, kSystemActions);
            }
            else if constexpr (std::is_same_v<Item, ShortcutButtonAction>)
            {
                json["type"] = "Shortcut";
                auto keys = nlohmann::ordered_json::array();
                for (const auto key : item.keys)
                {
                    keys.push_back(VirtualKeys::Name(key));
                }
                json["keys"] = std::move(keys);
            }
            else if constexpr (std::is_same_v<Item, TypeTextButtonAction>)
            {
                json["type"] = "TypeText";
                json["text"] = item.text;
            }
            else if constexpr (std::is_same_v<Item, KeyboardProfileButtonAction>)
            {
                json["type"] = "KeyboardProfile";
                if (item.profile)
                {
                    json["profile"] = *item.profile;
                }
            }
            else
            {
                json["type"] = "SendOscValue";
                json["parameter"] = item.parameter;
                if (item.values)
                {
                    auto values = nlohmann::ordered_json::array();
                    for (const auto& osc_value : *item.values)
                    {
                        values.push_back(OscValueJson(osc_value));
                    }
                    json["values"] = std::move(values);
                }
            }
        },
        value
    );
}

auto to_json(nlohmann::ordered_json& json, const ButtonData& value) -> void
{
    json = nlohmann::ordered_json::object();
    const auto put = [&json](std::string_view key, const std::optional<std::vector<ButtonAction>>& actions) -> void
    {
        if (actions)
        {
            json[key] = *actions;
        }
    };
    put("click_down", value.click_down);
    put("click_up", value.click_up);
    put("long_click_up", value.long_click_up);
    put("right_down", value.right_down);
    put("right_up", value.right_up);
    put("long_right_up", value.long_right_up);
    put("middle_down", value.middle_down);
    put("middle_up", value.middle_up);
    put("long_middle_up", value.long_middle_up);
    put("scroll_down", value.scroll_down);
    put("scroll_up", value.scroll_up);
    if (value.highlight)
    {
        json["highlight"] = FormParser::NameOf(*value.highlight, kHighlightTests);
    }
}
