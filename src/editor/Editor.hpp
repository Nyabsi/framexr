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

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Geometry>

#include "Container.hpp"
#include "Drafts.hpp"
#include "FormDesigner.hpp"
#include "FormTheme.hpp"
#include "History.hpp"
#include "KeyboardDesigner.hpp"
#include "PanelPlacement.hpp"
#include "TextEntry.hpp"
#include "ColorDialog.hpp"

enum class EditTarget : uint8_t {
    Watch,
    Keyboard,
    Theme,
};

enum class DialogKind : uint8_t {
    None,
    Color,
    Actions,
    KeyPicker,
    Timezone,
    Font,
};

enum class ButtonTrigger : uint8_t {
    Press,
    Click,
    LongClick,
    RightPress,
    RightClick,
    LongRightClick,
    MiddlePress,
    MiddleClick,
    LongMiddleClick,
    ScrollUp,
    ScrollDown,
};

using PaletteItem = std::variant<FormPart, KeyTemplate>;

class Editor final : public std::enable_shared_from_this<Editor> {
  public:
    static auto Create() -> std::shared_ptr<Editor>;

    Editor(FormDocument watch, KeyboardLayout keyboard, std::string watch_profile);

    [[nodiscard]] auto Target() const -> EditTarget { return m_target; }
    [[nodiscard]] auto WatchDesign() const -> const FormDesigner& { return m_watch_design; }
    [[nodiscard]] auto WatchDesign() -> FormDesigner& { return m_watch_design; }
    [[nodiscard]] auto KeyboardDesign() const -> const KeyboardDesigner& { return m_keyboard_design; }
    [[nodiscard]] auto KeyboardDesign() -> KeyboardDesigner& { return m_keyboard_design; }
    [[nodiscard]] auto ThemeHistory() const -> const History<ThemeDefinition>& { return m_theme_history; }
    [[nodiscard]] auto ThemeHistory() -> History<ThemeDefinition>& { return m_theme_history; }
    [[nodiscard]] auto DraftFiles() -> Drafts& { return m_drafts; }
    [[nodiscard]] auto Placement() const -> const PanelPlacement& { return m_placement; }
    [[nodiscard]] auto Placement() -> PanelPlacement& { return m_placement; }
    [[nodiscard]] auto Text() const -> const TextEntry& { return *m_text; }
    [[nodiscard]] auto Text() -> TextEntry& { return *m_text; }
    [[nodiscard]] auto Colors() const -> const ColorDialog& { return m_colors; }
    [[nodiscard]] auto Colors() -> ColorDialog& { return m_colors; }
    [[nodiscard]] auto Trigger() const -> ButtonTrigger { return m_trigger; }
    [[nodiscard]] auto KeyCategory() const -> size_t { return m_key_category; }
    [[nodiscard]] auto TimezoneElement() const -> size_t { return m_timezone_element; }
    [[nodiscard]] auto TimezoneQuery() const -> const std::string& { return m_timezone_query; }
    [[nodiscard]] auto FontQuery() const -> const std::string& { return m_font_query; }
    [[nodiscard]] auto Dialog() const -> DialogKind { return m_dialog; }
    [[nodiscard]] auto Carrying() const -> const std::optional<PaletteItem>& { return m_carrying; }
    [[nodiscard]] auto CarryPointer() const -> size_t { return m_carry_pointer; }
    [[nodiscard]] auto Owner() const -> std::optional<size_t> { return m_owner; }
    [[nodiscard]] auto Allows(size_t pointer) const -> bool { return !m_owner || *m_owner == pointer; }
    [[nodiscard]] auto Layers() const -> bool { return m_exploded; }
    [[nodiscard]] auto Exploded() const -> bool { return m_exploded && m_target == EditTarget::Watch; }
    [[nodiscard]] auto Hidden() const -> bool { return m_hidden; }
    [[nodiscard]] auto Closing() const -> bool { return m_closing; }
    [[nodiscard]] auto Finished() const -> bool { return m_finished; }
    [[nodiscard]] auto ClosingPrompt() const -> bool { return m_closing_prompt; }
    [[nodiscard]] auto RevertArmed() const -> bool { return m_revert_armed.has_value(); }
    [[nodiscard]] auto DeleteArmed() const -> bool { return m_delete_armed.has_value(); }
    [[nodiscard]] auto WatchProfile() const -> const std::string& { return m_watch_profile; }
    [[nodiscard]] auto KeyboardFile() const -> const std::string& { return m_keyboard_file; }
    [[nodiscard]] auto Dirty() const -> bool;
    [[nodiscard]] auto Revision(PanelKind kind) const -> uint64_t;
    [[nodiscard]] auto PanelSize(PanelKind kind) const -> Eigen::Vector2f;
    [[nodiscard]] auto Accepts(PanelKind kind, const Eigen::Vector2f& point) const -> bool;
    [[nodiscard]] auto Lines() const -> std::vector<EditorLine>;

    auto Open() -> void;
    auto Update() -> void;
    auto SetHidden(bool hidden) -> void;
    auto Claim(size_t pointer) -> void;
    auto Unclaim(size_t pointer) -> void;

    auto PreviewHover(const Eigen::Vector2f& point) -> std::optional<Haptics>;
    auto PreviewLeft() -> void;
    auto PreviewPointer(PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed) -> void;
    auto PanelReleased(PanelKind from, const PointerHit& hit) -> bool;

    auto SwitchTarget(EditTarget next) -> void;
    auto SetExploded(bool exploded) -> void;
    auto StartCarry(PaletteItem item) -> void;
    auto Drop(std::optional<Eigen::Vector2f> point) -> void;
    auto Undo() -> void;
    auto Redo() -> void;
    auto SetAmend(bool amend) -> void { m_amend = amend; }
    auto EditWatch(const std::function<void(FormDocument&)>& change) -> void;
    auto EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void;
    auto EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void;
    auto WatchChanged() -> void;
    auto SetKeyboardFile(std::string name) -> void { m_keyboard_file = std::move(name); }
    auto SetKeyCategory(size_t category) -> void;
    auto SetTimezoneQuery(std::string query) -> void { m_timezone_query = std::move(query); }
    auto SetFontQuery(std::string query) -> void { m_font_query = std::move(query); }

    auto SelectWatch(const std::string& name) -> void;
    auto NewWatch() -> void;
    auto RenameWatch(const std::string& name) -> void;
    auto DeleteWatch() -> void;
    auto SelectKeyboard(const std::string& name) -> void;
    auto DeleteKeyboard() -> void;
    auto SelectTheme(const std::string& name) -> void;
    auto NewTheme() -> void;
    auto RenameTheme(const std::string& name) -> void;
    auto DeleteTheme() -> void;

    auto Save() -> void;
    auto SaveAll() -> void;
    auto SaveAsNew() -> void;
    auto Revert() -> void;
    auto Done() -> void;
    auto KeepEditing() -> void;
    auto Discard() -> void;
    auto Suspend() -> void;
    auto Close() -> void;

    auto OpenColor(ColorTarget target) -> void;
    auto OpenActions(ButtonTrigger trigger) -> void;
    auto OpenKeyPicker() -> void;
    auto OpenTimezones(size_t element) -> void;
    auto OpenFonts() -> void;
    auto CloseDialog() -> void;

    auto Touch() -> void;
    static auto Notify(std::string text) -> void;

  private:
    auto ApplyWatch() -> void;
    auto ApplyTheme() -> void;
    auto ApplyKeyboard() -> void;
    auto ArmDelete() -> bool;
    auto SaveTarget(EditTarget target) -> void;

    PanelPlacement m_placement;
    EditTarget m_target = EditTarget::Watch;
    FormDesigner m_watch_design;
    KeyboardDesigner m_keyboard_design;
    History<ThemeDefinition> m_theme_history;
    std::shared_ptr<TextEntry> m_text;
    Drafts m_drafts;
    DialogKind m_dialog = DialogKind::None;
    ColorDialog m_colors;
    ButtonTrigger m_trigger = ButtonTrigger::Click;
    size_t m_key_category = 0;
    size_t m_timezone_element = 0;
    std::string m_timezone_query;
    std::string m_font_query;
    std::optional<PaletteItem> m_carrying;
    size_t m_carry_pointer = 1;
    std::optional<size_t> m_owner;
    Eigen::Vector2f m_last_point = Eigen::Vector2f::Zero();
    std::optional<std::chrono::steady_clock::time_point> m_revert_armed;
    std::optional<std::chrono::steady_clock::time_point> m_delete_armed;
    std::chrono::steady_clock::time_point m_last_restyle;
    bool m_amend = false;
    bool m_exploded = false;
    bool m_hidden = false;
    bool m_closing_prompt = false;
    bool m_closing = false;
    bool m_finished = false;
    bool m_watch_apply = false;
    bool m_restore_saved_watch = false;
    bool m_restyle_keyboard = false;
    std::string m_keyboard_file;
    std::string m_watch_profile;
    uint64_t m_revision = 1;
    uint64_t m_preview_revision = 1;
};
