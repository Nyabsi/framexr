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
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Eigen/Geometry>

#include "Canvas.hpp"
#include "Input.hpp"
#include "KeyboardDesigner.hpp"
#include "Overlay.hpp"
#include "OverlayContainer.hpp"
#include "PanelBuilder.hpp"
#include "TextInput.hpp"
#include "Theme.hpp"
#include "FormDesigner.hpp"

class AppState;
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
    std::function<void(Editor&, AppState&, const std::string&)> commit;
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

    static auto Create(AppState& app) -> std::shared_ptr<Editor>;
    static auto PanelName(PanelKind kind) -> std::string_view;
    static auto LayerOf(PanelKind kind) -> std::optional<int>;
    static auto SplitCommand(std::string_view text) -> std::vector<std::string>;
    static auto JoinCommand(const std::vector<std::string>& command) -> std::string;
    static auto Describe(const ButtonAction& action) -> std::string;

    Editor(ModularUiConfig watch_config, KeyboardLayout keyboard_layout, std::string watch_profile);

    EditTarget target = EditTarget::Watch;
    FormDesigner watch;
    KeyboardDesigner keyboard;
    History<ThemeDefinition> theme;

    auto Open(AppState& app) -> void;
    auto Recenter(const AppState& app) -> void;
    auto SetHidden(bool hidden) -> void;
    [[nodiscard]] auto Hidden() const -> bool { return hidden_; }
    auto Update(AppState& app, OverlayContainer& overlays) -> void;
    [[nodiscard]] auto Lines(OverlayContainer& overlays) const -> std::vector<EditorLine>;
    [[nodiscard]] auto Finished() const -> bool { return finished_; }
    [[nodiscard]] auto Closing() const -> bool { return closing_; }

    [[nodiscard]] auto PanelSize(PanelKind kind) const -> Eigen::Vector2f;
    [[nodiscard]] auto Revision(PanelKind kind) const -> uint64_t;
    [[nodiscard]] auto LocalTransform(PanelKind kind) const -> Eigen::Affine3f;
    [[nodiscard]] auto Dialog() const -> DialogKind { return dialog_; }
    [[nodiscard]] auto Carrying() const -> std::optional<PaletteItem> { return carrying_; }
    [[nodiscard]] auto Focused() const -> const std::optional<TextFocus>& { return focus_; }
    [[nodiscard]] auto Capturing() const -> bool { return capture_.has_value(); }
    [[nodiscard]] auto Dirty() const -> bool;
    [[nodiscard]] auto Exploded() const -> bool { return exploded_ && target == EditTarget::Watch; }
    [[nodiscard]] auto Accepts(PanelKind kind, const Eigen::Vector2f& point) const -> bool;
    [[nodiscard]] auto Allows(size_t pointer) const -> bool { return !owner_ || *owner_ == pointer; }
    [[nodiscard]] auto Owner() const -> std::optional<size_t> { return owner_; }
    auto Claim(size_t pointer) -> void;
    auto Unclaim(size_t pointer) -> void;

    auto BuildPanel(PanelKind kind, Canvas& canvas, AppState& app) -> void;
    auto PreviewHover(AppState& app, const Eigen::Vector2f& point) -> std::optional<Haptics>;
    auto PreviewLeft(AppState& app) -> void;
    auto PreviewPointer(AppState& app, PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed) -> void;
    auto SetExploded(bool exploded) -> void;
    auto PreviewScroll(AppState& app, const Eigen::Vector2f& point, float delta) -> void;
    auto PanelReleased(AppState& app, PanelKind from, const PointerHit& hit) -> bool;
    auto DialogPointer(AppState& app, const Eigen::Vector2f& point, bool pressed) -> bool;
    auto DialogHover(AppState& app, const Eigen::Vector2f& point) -> bool;
    [[nodiscard]] auto WheelRect() const -> const CanvasRect& { return wheel_rect_; }
    [[nodiscard]] auto BrightnessRect() const -> const CanvasRect& { return brightness_rect_; }
    [[nodiscard]] auto Color() const -> const std::optional<ColorDialog>& { return color_; }

    static auto HsvColor(float hue, float saturation, float value) -> std::string;

    auto SwitchTarget(EditTarget next) -> void;
    auto StartCarry(AppState& app, PaletteItem item) -> void;
    auto Drop(AppState& app, std::optional<Eigen::Vector2f> point) -> void;
    auto Undo(AppState& app) -> void;
    auto Redo(AppState& app) -> void;
    auto Save(AppState& app) -> void;
    auto SaveAsNew(AppState& app) -> void;
    auto Revert(AppState& app) -> void;
    auto Done(AppState& app) -> void;
    auto Discard(AppState& app) -> void;
    auto Close(AppState& app) -> void;
    auto Suspend(AppState& app) -> void;
    auto SaveDrafts(bool force) -> void;
    auto RestoreDrafts(AppState& app) -> void;
    static auto DraftFile(EditTarget draft) -> std::filesystem::path;

    auto BeginText(
        AppState& app,
        std::string id,
        std::string initial,
        std::function<void(Editor&, AppState&, const std::string&)> commit
    ) -> void;
    auto BeginCapture(AppState& app, bool shortcut, std::function<void(Editor&, const std::vector<VirtualKey>&)> done) -> void;
    auto OnKey(AppState& app, VirtualKey key, std::string_view text) -> void;
    auto CommitText(AppState& app) -> void;
    auto OpenColor(ColorDialog dialog) -> void;
    auto OpenActions(ButtonTrigger trigger) -> void;
    auto OpenKeyPicker() -> void;
    auto OpenTimezones(size_t element) -> void;
    auto CloseDialog() -> void;
    auto Touch() -> void;
    auto Notify(AppState& app, std::string text) -> void;
    auto EditWatch(const std::function<void(ModularUiConfig&)>& change) -> void;
    auto EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void;
    auto EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void;
    auto SelectWatch(AppState& app, const std::string& name) -> void;
    auto NewWatch(AppState& app) -> void;
    auto RenameWatch(AppState& app, const std::string& name) -> void;
    auto DeleteWatch(AppState& app) -> void;
    auto SelectKeyboard(AppState& app, const std::string& name) -> void;
    auto DeleteKeyboard(AppState& app) -> void;
    auto SelectTheme(AppState& app, const std::string& name) -> void;
    auto NewTheme(AppState& app) -> void;
    auto RenameTheme(AppState& app, const std::string& name) -> void;
    auto DeleteTheme(AppState& app) -> void;
    [[nodiscard]] auto WatchProfile() const -> const std::string& { return watch_profile_; }

  private:
    auto BuildToolbar(PanelBuilder& builder, AppState& app) -> void;
    auto BuildPalette(PanelBuilder& builder, AppState& app) -> void;
    auto BuildProperties(PanelBuilder& builder, AppState& app) -> void;
    auto BuildWatchProperties(PanelBuilder& builder, AppState& app, size_t index) -> void;
    auto BuildFormProperties(PanelBuilder& builder, AppState& app) -> void;
    auto BuildExtraFormProperties(PanelBuilder& builder, size_t form) -> void;
    auto BuildKeyProperties(PanelBuilder& builder, AppState& app, size_t index) -> void;
    auto BuildKeyboardProperties(PanelBuilder& builder, AppState& app) -> void;
    auto BuildThemeProperties(PanelBuilder& builder, AppState& app) -> void;
    auto ApplyTheme() -> void;
    auto ArmDelete() -> bool;
    auto BuildColorDialog(PanelBuilder& builder, AppState& app) -> void;
    auto PickColor(const Eigen::Vector2f& point) -> void;
    auto EnsureColorImages(AppState& app, const ColorDialog& dialog) -> void;
    auto BuildActionsDialog(PanelBuilder& builder, AppState& app) -> void;
    auto BuildKeyPicker(PanelBuilder& builder, AppState& app) -> void;
    auto BuildTimezonePicker(PanelBuilder& builder, AppState& app) -> void;
    auto BuildGhost(Canvas& canvas, AppState& app) -> void;
    auto TextRow(
        PanelBuilder& builder,
        AppState& app,
        std::string_view label,
        const std::string& id,
        const std::string& value,
        const std::function<void(Editor&, AppState&, const std::string&)>& commit
    ) -> void;
    auto ColorRow(
        PanelBuilder& builder,
        std::string_view label,
        const std::string& title,
        const std::function<std::string(const Editor&)>& get,
        const std::function<void(Editor&, const std::string&)>& set,
        const std::function<void(Editor&)>& reset
    ) -> void;
    auto ApplyWatch(AppState& app, OverlayContainer& overlays) -> void;
    auto SetMeasure(AppState& app) -> void;
    auto ApplyKeyboard(AppState& app) -> void;
    auto ShowKeyboard(AppState& app) -> void;
    auto EndText(AppState& app) -> void;

    Eigen::Affine3f anchor_ = Eigen::Affine3f::Identity();
    DialogKind dialog_ = DialogKind::None;
    std::optional<ColorDialog> color_;
    ColorDrag color_drag_ = ColorDrag::None;
    bool amend_ = false;
    CanvasRect wheel_rect_;
    CanvasRect brightness_rect_;
    std::shared_ptr<Image> wheel_image_;
    float wheel_value_ = -1.0f;
    std::shared_ptr<Image> brightness_image_;
    Eigen::Vector2f brightness_key_ = Eigen::Vector2f::Constant(-1.0f);
    ButtonTrigger trigger_ = ButtonTrigger::LeftClick;
    size_t key_category_ = 0;
    size_t timezone_element_ = 0;
    std::string timezone_query_;
    std::optional<PaletteItem> carrying_;
    size_t carry_pointer_ = 1;
    std::optional<size_t> owner_;
    std::optional<TextFocus> focus_;
    std::optional<KeyCapture> capture_;
    std::shared_ptr<TextInput> input_;
    std::deque<std::string> recent_colors_;
    std::optional<std::chrono::steady_clock::time_point> revert_armed_;
    bool palette_on_hand_ = false;
    bool exploded_ = false;
    bool hidden_ = false;
    bool closing_prompt_ = false;
    bool closing_ = false;
    bool finished_ = false;
    bool watch_apply_ = false;
    bool restore_saved_watch_ = false;
    bool restyle_keyboard_ = false;
    uint64_t theme_drafted_ = 0;
    std::optional<std::chrono::steady_clock::time_point> delete_armed_;
    std::chrono::steady_clock::time_point last_restyle_;
    uint64_t watch_drafted_ = 0;
    uint64_t keyboard_drafted_ = 0;
    std::chrono::steady_clock::time_point last_draft_ = std::chrono::steady_clock::now();

    std::optional<OverlayId> preview_id_;
    std::array<std::optional<OverlayId>, FormDesigner::kLayers> layer_ids_;
    std::optional<OverlayId> palette_id_;
    Eigen::Vector2f last_point_ = Eigen::Vector2f::Zero();
    std::string keyboard_file_name_;
    std::string watch_profile_;
    uint64_t revision_ = 1;
    uint64_t preview_revision_ = 1;
};
