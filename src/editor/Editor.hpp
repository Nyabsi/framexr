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
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Eigen/Geometry>

#include "Canvas.hpp"
#include "FormDesigner.hpp"
#include "Input.hpp"
#include "KeyboardDesigner.hpp"
#include "Overlay.hpp"
#include "OverlayContainer.hpp"
#include "PanelBuilder.hpp"
#include "TextInput.hpp"
#include "Theme.hpp"

struct KeyCategory {
    std::string_view name;
    std::span<const VirtualKey> keys;
};

class Editor;

enum class EditTarget : uint8_t {
    Watch,
    Keyboard,
    Theme,
};

enum class PanelKind : uint8_t {
    Preview,
    Toolbar,
    Palette,
    Properties,
    Dialog,
    Ghost,
    BackLayer,
    MiddleLayer,
    FrontLayer,
};

enum class DialogKind : uint8_t {
    None,
    Color,
    Actions,
    KeyPicker,
    Timezone,
};

enum class ButtonTrigger : uint8_t {
    LeftClick,
    RightClick,
    ScrollUp,
    ScrollDown,
};

using PaletteItem = std::variant<FormPart, KeyTemplate>;

struct ColorDialog {
    std::string title;
    std::function<std::string(const Editor&)> get;
    std::function<void(Editor&, const std::string&)> set;
    std::function<void(Editor&)> reset;
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 1.0f;
    bool tokens = true;
};

enum class ColorDrag : uint8_t {
    None,
    Wheel,
    Brightness,
};

struct TextFocus {
    std::string id;
    std::string buffer;
    std::function<void(Editor&, const std::string&)> commit;
};

struct KeyCapture {
    bool shortcut = false;
    std::vector<VirtualKey> held;
    std::function<void(Editor&, const std::vector<VirtualKey>&)> done;
};

class Editor final : public std::enable_shared_from_this<Editor> {
  public:
    static constexpr std::array<PanelKind, 9> kPanels{
        PanelKind::Preview,
        PanelKind::Toolbar,
        PanelKind::Palette,
        PanelKind::Properties,
        PanelKind::Dialog,
        PanelKind::Ghost,
        PanelKind::BackLayer,
        PanelKind::MiddleLayer,
        PanelKind::FrontLayer,
    };
    static constexpr float kMetersPerPixel = 0.0009f;
    static constexpr float kRadius = 0.75f;
    static constexpr float kLayerSpacing = 0.07f;

    static auto Create() -> std::shared_ptr<Editor>;
    static auto PanelName(PanelKind kind) -> std::string_view;
    static auto LayerOf(PanelKind kind) -> std::optional<int>;
    static auto SplitCommand(std::string_view text) -> std::vector<std::string>;
    static auto JoinCommand(const std::vector<std::string>& command) -> std::string;
    static auto Describe(const ButtonAction& action) -> std::string;

    Editor(ModularUiConfig watch_config, KeyboardLayout keyboard_layout, std::string watch_profile);

    [[nodiscard]] auto Target() const -> const EditTarget& { return m_target; }
    [[nodiscard]] auto Target() -> EditTarget& { return m_target; }
    [[nodiscard]] auto WatchDesign() const -> const FormDesigner& { return m_watch_design; }
    [[nodiscard]] auto WatchDesign() -> FormDesigner& { return m_watch_design; }
    [[nodiscard]] auto KeyboardDesign() const -> const KeyboardDesigner& { return m_keyboard_design; }
    [[nodiscard]] auto KeyboardDesign() -> KeyboardDesigner& { return m_keyboard_design; }
    [[nodiscard]] auto ThemeHistory() const -> const History<ThemeDefinition>& { return m_theme_history; }
    [[nodiscard]] auto ThemeHistory() -> History<ThemeDefinition>& { return m_theme_history; }

    auto Open() -> void;
    auto Recenter() -> void;
    auto SetHidden(bool hidden) -> void;
    [[nodiscard]] auto Hidden() const -> bool { return m_hidden; }
    auto Update() -> void;
    [[nodiscard]] auto Lines() const -> std::vector<EditorLine>;
    [[nodiscard]] auto Finished() const -> bool { return m_finished; }
    [[nodiscard]] auto Closing() const -> bool { return m_closing; }

    [[nodiscard]] auto PanelSize(PanelKind kind) const -> Eigen::Vector2f;
    [[nodiscard]] auto Revision(PanelKind kind) const -> uint64_t;
    [[nodiscard]] auto LocalTransform(PanelKind kind) const -> Eigen::Affine3f;
    [[nodiscard]] auto Dialog() const -> DialogKind { return m_dialog; }
    [[nodiscard]] auto Carrying() const -> std::optional<PaletteItem> { return m_carrying; }
    [[nodiscard]] auto Focused() const -> const std::optional<TextFocus>& { return m_focus; }
    [[nodiscard]] auto Capturing() const -> bool { return m_capture.has_value(); }
    [[nodiscard]] auto Dirty() const -> bool;
    [[nodiscard]] auto Exploded() const -> bool { return m_exploded && Target() == EditTarget::Watch; }
    [[nodiscard]] auto Accepts(PanelKind kind, const Eigen::Vector2f& point) const -> bool;
    [[nodiscard]] auto Allows(size_t pointer) const -> bool { return !m_owner || *m_owner == pointer; }
    [[nodiscard]] auto Owner() const -> std::optional<size_t> { return m_owner; }
    auto Claim(size_t pointer) -> void;
    auto Unclaim(size_t pointer) -> void;

    auto BuildPanel(PanelKind kind, Canvas& canvas) -> void;
    auto PreviewHover(const Eigen::Vector2f& point) -> std::optional<Haptics>;
    auto PreviewLeft() -> void;
    auto PreviewPointer(PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed) -> void;
    auto SetExploded(bool exploded) -> void;
    auto PreviewScroll(const Eigen::Vector2f& point, float delta) -> void;
    auto PanelReleased(PanelKind from, const PointerHit& hit) -> bool;
    auto DialogPointer(const Eigen::Vector2f& point, bool pressed) -> bool;
    auto DialogHover(const Eigen::Vector2f& point) -> bool;
    [[nodiscard]] auto WheelRect() const -> const CanvasRect& { return m_wheel_rect; }
    [[nodiscard]] auto BrightnessRect() const -> const CanvasRect& { return m_brightness_rect; }
    [[nodiscard]] auto Color() const -> const std::optional<ColorDialog>& { return m_color; }

    static auto HsvColor(float hue, float saturation, float value) -> std::string;

    auto SwitchTarget(EditTarget next) -> void;
    auto StartCarry(PaletteItem item) -> void;
    auto Drop(std::optional<Eigen::Vector2f> point) -> void;
    auto Undo() -> void;
    auto Redo() -> void;
    auto Save() -> void;
    auto SaveAsNew() -> void;
    auto Revert() -> void;
    auto Done() -> void;
    auto Discard() -> void;
    auto Close() -> void;
    auto Suspend() -> void;
    auto SaveDrafts(bool force) -> void;
    auto RestoreDrafts() -> void;
    static auto DraftFile(EditTarget draft) -> std::filesystem::path;

    auto BeginText(std::string id, std::string initial, std::function<void(Editor&, const std::string&)> commit) -> void;
    auto BeginCapture(bool shortcut, std::function<void(Editor&, const std::vector<VirtualKey>&)> done) -> void;
    auto OnKey(VirtualKey key, std::string_view text) -> void;
    auto CommitText() -> void;
    auto OpenColor(ColorDialog dialog) -> void;
    auto OpenActions(ButtonTrigger trigger) -> void;
    auto OpenKeyPicker() -> void;
    auto OpenTimezones(size_t element) -> void;
    auto CloseDialog() -> void;
    auto Touch() -> void;
    auto Notify(std::string text) -> void;
    auto EditWatch(const std::function<void(ModularUiConfig&)>& change) -> void;
    auto EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void;
    auto EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void;
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
    [[nodiscard]] auto WatchProfile() const -> const std::string& { return m_watch_profile; }

  private:
    auto BuildToolbar(PanelBuilder& builder) -> void;
    auto BuildPalette(PanelBuilder& builder) -> void;
    auto BuildProperties(PanelBuilder& builder) -> void;
    auto BuildWatchProperties(PanelBuilder& builder, size_t index) -> void;
    auto BuildFormProperties(PanelBuilder& builder) -> void;
    auto BuildExtraFormProperties(PanelBuilder& builder, size_t form) -> void;
    auto BuildKeyProperties(PanelBuilder& builder, size_t index) -> void;
    auto BuildKeyboardProperties(PanelBuilder& builder) -> void;
    auto BuildThemeProperties(PanelBuilder& builder) -> void;
    auto ApplyTheme() -> void;
    auto ArmDelete() -> bool;
    auto BuildColorDialog(PanelBuilder& builder) -> void;
    auto PickColor(const Eigen::Vector2f& point) -> void;
    auto EnsureColorImages(const ColorDialog& dialog) -> void;
    auto BuildActionsDialog(PanelBuilder& builder) -> void;
    auto BuildKeyPicker(PanelBuilder& builder) -> void;
    auto BuildTimezonePicker(PanelBuilder& builder) -> void;
    auto BuildGhost(Canvas& canvas) -> void;
    auto TextRow(
        PanelBuilder& builder,
        std::string_view label,
        const std::string& id,
        const std::string& value,
        const std::function<void(Editor&, const std::string&)>& commit
    ) -> void;
    auto ColorRow(
        PanelBuilder& builder,
        std::string_view label,
        const std::string& title,
        const std::function<std::string(const Editor&)>& get,
        const std::function<void(Editor&, const std::string&)>& set,
        const std::function<void(Editor&)>& reset
    ) -> void;
    auto ApplyWatch() -> void;
    auto SetMeasure() -> void;
    auto ApplyKeyboard() -> void;
    auto ShowKeyboard() -> void;
    auto EndText() -> void;

    Eigen::Affine3f m_anchor = Eigen::Affine3f::Identity();
    EditTarget m_target = EditTarget::Watch;
    FormDesigner m_watch_design;
    KeyboardDesigner m_keyboard_design;
    History<ThemeDefinition> m_theme_history;
    DialogKind m_dialog = DialogKind::None;
    std::optional<ColorDialog> m_color;
    ColorDrag m_color_drag = ColorDrag::None;
    bool m_amend = false;
    CanvasRect m_wheel_rect;
    CanvasRect m_brightness_rect;
    std::shared_ptr<Image> m_wheel_image;
    float m_wheel_value = -1.0f;
    std::shared_ptr<Image> m_brightness_image;
    Eigen::Vector2f m_brightness_key = Eigen::Vector2f::Constant(-1.0f);
    ButtonTrigger m_trigger = ButtonTrigger::LeftClick;
    size_t m_key_category = 0;
    size_t m_timezone_element = 0;
    std::string m_timezone_query;
    std::optional<PaletteItem> m_carrying;
    size_t m_carry_pointer = 1;
    std::optional<size_t> m_owner;
    std::optional<TextFocus> m_focus;
    std::optional<KeyCapture> m_capture;
    std::shared_ptr<TextInput> m_input;
    std::deque<std::string> m_recent_colors;
    std::optional<std::chrono::steady_clock::time_point> m_revert_armed;
    bool m_palette_on_hand = false;
    bool m_exploded = false;
    bool m_hidden = false;
    bool m_closing_prompt = false;
    bool m_closing = false;
    bool m_finished = false;
    bool m_watch_apply = false;
    bool m_restore_saved_watch = false;
    bool m_restyle_keyboard = false;
    uint64_t m_theme_drafted = 0;
    std::optional<std::chrono::steady_clock::time_point> m_delete_armed;
    std::chrono::steady_clock::time_point m_last_restyle;
    uint64_t m_watch_drafted = 0;
    uint64_t m_keyboard_drafted = 0;
    std::chrono::steady_clock::time_point m_last_draft = std::chrono::steady_clock::now();

    std::optional<OverlayId> m_preview_id;
    std::array<std::optional<OverlayId>, FormDesigner::kLayers> m_layer_ids;
    std::optional<OverlayId> m_palette_id;
    Eigen::Vector2f m_last_point = Eigen::Vector2f::Zero();
    std::string m_keyboard_file_name;
    std::string m_watch_profile;
    uint64_t m_revision = 1;
    uint64_t m_preview_revision = 1;
};
