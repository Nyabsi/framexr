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

#include "Drafts.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Editor.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"

namespace {

constexpr auto kInterval = std::chrono::seconds(1);

template <typename Document>
auto Keep(EditTarget target, const History<Document>& history, uint64_t& drafted) -> void {
    const auto path = Drafts::File(target);
    std::error_code error;
    if (!history.Dirty()) {
        std::filesystem::remove(path, error);
        drafted = 0;
        return;
    }
    if (history.Version() == drafted) {
        return;
    }
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    if (ConfigStore::WriteFile(temporary, Yaml::Emit(nlohmann::ordered_json(history.Current())))) {
        std::filesystem::rename(temporary, path, error);
        drafted = history.Version();
    }
}

} // namespace

auto Drafts::File(EditTarget target) -> std::filesystem::path {
    switch (target) {
    case EditTarget::Watch: break;
    case EditTarget::Keyboard: return ConfigStore::Root() / "drafts" / "keyboard.yaml";
    case EditTarget::Theme: return ConfigStore::Root() / "drafts" / "theme.yaml";
    }
    return ConfigStore::Root() / "drafts" / "watch.yaml";
}

auto Drafts::Save(const Editor& editor, bool force) -> void {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - m_last < kInterval) {
        return;
    }
    m_last = now;
    Keep(EditTarget::Watch, editor.WatchDesign().Revisions(), m_watch);
    Keep(EditTarget::Keyboard, editor.KeyboardDesign().Revisions(), m_keyboard);
    Keep(EditTarget::Theme, editor.ThemeHistory(), m_theme);
}

auto Drafts::Restore(Editor& editor) -> bool {
    bool restored = false;
    if (const auto text = ConfigStore::ReadFile(File(EditTarget::Watch))) {
        if (const auto parsed = Yaml::Parse(*text)) {
            try {
                auto draft = parsed->get<FormDocument>();
                editor.WatchDesign().Revisions().Edit([&draft](FormDocument& document) -> void { document = std::move(draft); });
                m_watch = editor.WatchDesign().Revisions().Version();
                editor.WatchChanged();
                restored = true;
            } catch (const nlohmann::json::exception& error) {
                LOG_WARN("Ignoring broken watch draft: {}", error.what());
            }
        }
    }
    if (const auto text = ConfigStore::ReadFile(File(EditTarget::Keyboard))) {
        if (auto draft = KeyboardLayout::Parse(*text)) {
            auto& revisions = editor.KeyboardDesign().Revisions();
            const auto& profiles = KeyboardLayout::All();
            if (const auto saved = std::ranges::find(profiles, draft->Name(), &KeyboardLayout::Name); saved != profiles.end()) {
                revisions.Reset(*saved);
                draft->SetSource(saved->Source());
            }
            editor.SetKeyboardFile(draft->Name());
            revisions.Edit([&draft](KeyboardLayout& layout) -> void { layout = std::move(*draft); });
            m_keyboard = revisions.Version();
            restored = true;
        } else {
            LOG_WARN("Ignoring broken keyboard draft: {}", draft.error().Message());
        }
    }
    if (const auto text = ConfigStore::ReadFile(File(EditTarget::Theme))) {
        auto& history = editor.ThemeHistory();
        if (auto draft = FormTheme::Parse(*text); draft && draft->name == history.Current().name) {
            draft->source = history.Current().source;
            editor.EditTheme([&draft](ThemeDefinition& edited) -> void { edited = std::move(*draft); });
            m_theme = history.Version();
            restored = true;
        }
    }
    return restored;
}
