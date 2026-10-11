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

#include "Editor.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <ranges>
#include <utility>

#include "Config.hpp"
#include "Font.hpp"
#include "EditorPanel.hpp"
#include "Keyboard.hpp"
#include "Label.hpp"
#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "State.hpp"
#include "Theme.hpp"
#include "Toast.hpp"
#include "Watch.hpp"
#include "Yaml.hpp"

namespace {

constexpr float kPanelGap = 0.04f;
constexpr float kPreviewHeight = -0.08f;
constexpr float kGhostReach = 0.3f;
constexpr float kGhostWidth = 0.12f;
constexpr float kHandPaletteWidth = 0.2f;
constexpr auto kRevertWindow = std::chrono::seconds(4);
constexpr auto kDraftInterval = std::chrono::seconds(1);
constexpr auto kRestyleInterval = std::chrono::milliseconds(300);

constexpr std::array<std::string_view, 9> kPanelNames{
    "editor-preview",
    "editor-toolbar",
    "editor-palette",
    "editor-properties",
    "editor-dialog",
    "editor-ghost",
    "editor-layer-back",
    "editor-layer-middle",
    "editor-layer-front",
};

class EditorTextInput final : public TextInput {
  public:
    explicit EditorTextInput(std::weak_ptr<Editor> editor) : editor_(std::move(editor)) {}

    [[nodiscard]] auto WantsRawKeys() const -> bool override {
        const auto editor = editor_.lock();
        return editor && editor->Capturing();
    }

    auto Key(AppState& app, VirtualKey key, uint8_t, std::string_view text) -> void override {
        if (const auto editor = editor_.lock()) {
            editor->OnKey(app, key, text);
        }
    }

  private:
    std::weak_ptr<Editor> editor_;
};

auto WritePanelText(const std::filesystem::path& path, std::string_view text) -> Result<> {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Could not write {}", path.string()));
    }
    stream << text;
    return {};
}

auto PointOn(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Vector2f& uv) -> Eigen::Vector3f {
    return corners[0] + (corners[1] - corners[0]) * uv.x() + (corners[3] - corners[0]) * (1.0f - uv.y());
}

auto LineBetween(const Eigen::Vector3f& from, const Eigen::Vector3f& to) -> EditorLine {
    const InteractionResult aim{.target = to};
    return {.pose = aim.LinePose(Eigen::Affine3f(Eigen::Translation3f(from))), .length = (to - from).norm()};
}

auto PopCodepoint(std::string& text) -> void {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
}

auto FileProfileName(const std::filesystem::path& path) -> std::string {
    if (path.empty()) {
        return {};
    }
    const auto text = Config::ReadFile(path);
    if (!text) {
        return {};
    }
    const auto layout = KeyboardLayout::Parse(*text);
    return layout ? layout->name : std::string{};
}

} // namespace

auto Editor::PanelName(PanelKind kind) -> std::string_view {
    return kPanelNames.at(static_cast<size_t>(kind));
}

auto Editor::LayerOf(PanelKind kind) -> std::optional<int> {
    switch (kind) {
    case PanelKind::BackLayer: return 0;
    case PanelKind::MiddleLayer: return 1;
    case PanelKind::FrontLayer: return 2;
    case PanelKind::Preview:
    case PanelKind::Toolbar:
    case PanelKind::Palette:
    case PanelKind::Properties:
    case PanelKind::Dialog:
    case PanelKind::Ghost: break;
    }
    return std::nullopt;
}

auto Editor::Accepts(PanelKind kind, const Eigen::Vector2f& point) const -> bool {
    const auto layer = LayerOf(kind);
    return !layer || watch.Accepts(watch.ToDocument(point), *layer);
}

auto Editor::Claim(size_t pointer) -> void {
    owner_ = pointer;
}

auto Editor::Unclaim(size_t pointer) -> void {
    if (owner_ == pointer && !carrying_) {
        owner_.reset();
    }
}

auto Editor::SetExploded(bool exploded) -> void {
    exploded_ = exploded;
    watch.hovered.reset();
    Touch();
}

auto Editor::Create(AppState& app) -> std::shared_ptr<Editor> {
    auto editor = std::make_shared<Editor>(
        Watch::LoadConfig(app.config.watch_profile),
        KeyboardProfiles::Find(app.config.keyboard_profile),
        app.config.watch_profile
    );
    editor->input_ = std::make_shared<EditorTextInput>(editor);
    editor->SetMeasure(app);
    editor->RestoreDrafts(app);
    return editor;
}

Editor::Editor(ModularUiConfig watch_config, KeyboardLayout keyboard_layout, std::string watch_profile)
    : watch(std::move(watch_config)),
      keyboard(std::move(keyboard_layout)),
      theme(Theme::Active()),
      keyboard_file_name_(keyboard.Document().name),
      watch_profile_(std::move(watch_profile)) {
}

auto Editor::DraftFile(EditTarget draft) -> std::filesystem::path {
    switch (draft) {
    case EditTarget::Watch: return Config::Root() / "drafts" / "watch.yaml";
    case EditTarget::Keyboard: return Config::Root() / "drafts" / "keyboard.yaml";
    case EditTarget::Theme: return Config::Root() / "drafts" / "theme.yaml";
    }
    return Config::Root() / "drafts" / "watch.yaml";
}

auto Editor::SaveDrafts(bool force) -> void {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_draft_ < kDraftInterval) {
        return;
    }
    last_draft_ = now;
    const auto keep = [](EditTarget draft, bool dirty, uint64_t version, uint64_t& drafted, const auto& document) -> void {
        const auto path = DraftFile(draft);
        std::error_code error;
        if (!dirty) {
            std::filesystem::remove(path, error);
            drafted = 0;
            return;
        }
        if (version == drafted) {
            return;
        }
        const auto temporary = std::filesystem::path(path).concat(".tmp");
        if (WritePanelText(temporary, Yaml::Emit(nlohmann::ordered_json(document)))) {
            std::filesystem::rename(temporary, path, error);
            drafted = version;
        }
    };
    keep(EditTarget::Watch, watch.history.Dirty(), watch.history.Version(), watch_drafted_, watch.Document());
    keep(EditTarget::Keyboard, keyboard.history.Dirty(), keyboard.history.Version(), keyboard_drafted_, keyboard.Document());
    keep(EditTarget::Theme, theme.Dirty(), theme.Version(), theme_drafted_, theme.Current());
}

auto Editor::RestoreDrafts(AppState& app) -> void {
    bool restored = false;
    if (const auto text = Config::ReadFile(DraftFile(EditTarget::Watch))) {
        if (const auto parsed = Yaml::Parse(*text)) {
            try {
                auto draft = parsed->get<ModularUiConfig>();
                watch.history.Edit([&draft](ModularUiConfig& config) -> void { config = std::move(draft); });
                watch_drafted_ = watch.history.Version();
                watch_apply_ = true;
                restored = true;
            } catch (const nlohmann::json::exception& error) {
                LOG_WARN("Ignoring broken watch draft: {}", error.what());
            }
        }
    }
    if (const auto text = Config::ReadFile(DraftFile(EditTarget::Keyboard))) {
        if (auto draft = KeyboardLayout::Parse(*text)) {
            const auto& profiles = KeyboardProfiles::All();
            if (const auto saved = std::ranges::find(profiles, draft->name, &KeyboardLayout::name); saved != profiles.end()) {
                keyboard.history.Reset(*saved);
                draft->source = saved->source;
            }
            keyboard_file_name_ = draft->name;
            keyboard.history.Edit([&draft](KeyboardLayout& layout) -> void { layout = std::move(*draft); });
            keyboard_drafted_ = keyboard.history.Version();
            restored = true;
        } else {
            LOG_WARN("Ignoring broken keyboard draft: {}", draft.error().Message());
        }
    }
    if (const auto text = Config::ReadFile(DraftFile(EditTarget::Theme))) {
        if (auto draft = ThemeDefinition::Parse(*text); draft && draft->name == theme.Current().name) {
            draft->source = theme.Current().source;
            theme.Edit([&draft](ThemeDefinition& edited) -> void { edited = std::move(*draft); });
            theme_drafted_ = theme.Version();
            ApplyTheme();
            restored = true;
        }
    }
    if (restored) {
        Notify(app, "Picked up your unsaved edits where you left off");
    }
}

auto Editor::Suspend(AppState& app) -> void {
    CommitText(app);
    restore_saved_watch_ = watch.history.Dirty();
    Close(app);
}

auto Editor::SetMeasure(AppState& app) -> void {
    auto measure = [state = &app](std::string_view text, int font_size) -> float {
        if (state->fonts && state->device) {
            if (const auto size = state->fonts->MeasureText(text, font_size, *state->device)) {
                return size->width;
            }
        }
        return 0.6f * static_cast<float>(font_size) * static_cast<float>(text.size());
    };
    watch.measure = std::move(measure);
    watch.city_text = [state = &app](const ClockLabelContent& clock) -> std::string {
        return Labels::City(clock.timezone, *state);
    };
    watch.label_text = [state = &app](const LabelContent& content) -> std::string {
        if (std::holds_alternative<ExecLabelContent>(content)) {
            return "Output";
        }
        auto setup = Labels::Create(content, *state, Eigen::Vector4f::Ones());
        if (setup.control) {
            setup.control->Update(*state);
            setup.text = setup.control->text;
        }
        if (setup.text.empty()) {
            return std::holds_alternative<BatteryLabelContent>(content) ? std::string("R99") : std::string("100");
        }
        return setup.text;
    };
}

auto Editor::Dirty() const -> bool {
    return watch.history.Dirty() || keyboard.history.Dirty() || theme.Dirty();
}

auto Editor::Touch() -> void {
    ++revision_;
    ++preview_revision_;
}

auto Editor::Revision(PanelKind kind) const -> uint64_t {
    return kind == PanelKind::Preview || LayerOf(kind) ? preview_revision_ : revision_;
}

auto Editor::PanelSize(PanelKind kind) const -> Eigen::Vector2f {
    switch (kind) {
    case PanelKind::Preview: return target == EditTarget::Keyboard ? keyboard.PreviewSize() : watch.PreviewSize();
    case PanelKind::Toolbar: return {1040.0f, 58.0f};
    case PanelKind::Palette: return {240.0f, 420.0f};
    case PanelKind::Properties: return {360.0f, 820.0f};
    case PanelKind::Dialog: return {380.0f, 760.0f};
    case PanelKind::Ghost: return {220.0f, 52.0f};
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: return watch.PreviewSize();
    }
    return {64.0f, 64.0f};
}

auto Editor::LocalTransform(PanelKind kind) const -> Eigen::Affine3f {
    const auto meters = [this](PanelKind panel) -> Eigen::Vector2f { return PanelSize(panel) * kMetersPerPixel; };
    const Eigen::Vector2f preview = meters(PanelKind::Preview);
    const Eigen::Vector2f size = meters(kind);
    float azimuth = 0.0f;
    float height = kPreviewHeight;
    float depth = 0.0f;
    switch (kind) {
    case PanelKind::Preview:
    case PanelKind::Ghost: break;
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: depth = kLayerSpacing * static_cast<float>(LayerOf(kind).value_or(0)); break;
    case PanelKind::Toolbar: height += preview.y() / 2.0f + kPanelGap / 2.0f + size.y() / 2.0f; break;
    case PanelKind::Palette:
        azimuth = (preview.x() / 2.0f + kPanelGap + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    case PanelKind::Properties:
        azimuth = -(preview.x() / 2.0f + kPanelGap + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    case PanelKind::Dialog:
        azimuth = -(preview.x() / 2.0f + 2.0f * kPanelGap + meters(PanelKind::Properties).x() + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    }
    Eigen::Affine3f local(Eigen::AngleAxisf(azimuth, Eigen::Vector3f::UnitY()));
    local.translate(Eigen::Vector3f(0.0f, height, depth - kRadius));
    local.scale(size.maxCoeff());
    return local;
}

auto Editor::Recenter(const AppState& app) -> void {
    const Eigen::Matrix3f head = app.input.hmd.linear();
    Eigen::Vector3f forward = head * -Eigen::Vector3f::UnitZ();
    forward.y() = 0.0f;
    const float yaw = forward.norm() > 1e-4f ? std::atan2(-forward.x(), -forward.z()) : 0.0f;
    anchor_ = Eigen::Translation3f(app.input.hmd.translation()) * Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitY());
}

auto Editor::SetHidden(bool hidden) -> void {
    hidden_ = hidden;
    if (hidden) {
        carrying_.reset();
        owner_.reset();
    }
}

auto Editor::Open(AppState& app) -> void {
    Recenter(app);

    std::weak_ptr<Editor> weak = weak_from_this();
    for (const auto kind : kPanels) {
        app.tasks.Enqueue(
            CreateOverlayTask{
                .selector = std::string(PanelName(kind)),
                .create = [weak, kind](AppState&) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                    const auto editor = weak.lock();
                    if (!editor) {
                        return std::nullopt;
                    }
                    OverlayState state;
                    state.name = PanelName(kind);
                    state.want_visible = kind != PanelKind::Dialog && kind != PanelKind::Ghost && !LayerOf(kind);
                    state.interactable = kind != PanelKind::Ghost;
                    state.grabbable = kind != PanelKind::Ghost;
                    state.relative_to = RelativeTo::Stage;
                    state.z_order = kind == PanelKind::Ghost ? kZOrderWatch : kZOrderDefault;
                    state.transform = editor->anchor_ * editor->LocalTransform(kind);
                    const Eigen::Vector2f size = editor->PanelSize(kind);
                    state.interaction_transform = OverlayState::UiTransform(
                        {static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))}
                    );
                    return std::pair<OverlayState, std::unique_ptr<OverlayBackend>>{
                        std::move(state),
                        std::make_unique<EditorPanel>(weak, kind)
                    };
                },
            }
        );
    }
}

auto Editor::Update(AppState& app, OverlayContainer& overlays) -> void {
    if (watch_apply_) {
        ApplyWatch(app, overlays);
    }
    if (restore_saved_watch_) {
        restore_saved_watch_ = false;
        if (auto* live = overlays.ByName(kWatchName)) {
            if (const auto applied = Watch::Apply(app, *live, Watch::LoadConfig(watch_profile_)); !applied) {
                LOG_ERROR("Could not restore the saved watch: {}", applied.error().Message());
            }
        }
    }
    if (!closing_) {
        SaveDrafts(false);
    }
    if (restyle_keyboard_ && color_drag_ == ColorDrag::None
        && std::chrono::steady_clock::now() - last_restyle_ > kRestyleInterval) {
        restyle_keyboard_ = false;
        last_restyle_ = std::chrono::steady_clock::now();
        Keyboard::SwitchProfile(app, app.keymap, app.config.keyboard_profile);
    }
    if (closing_) {
        if (!finished_) {
            for (const auto kind : kPanels) {
                app.tasks.Enqueue(DropOverlayTask{.selector = std::string(PanelName(kind))});
            }
            EndText(app);
            finished_ = true;
        }
        return;
    }
    if (owner_ && !carrying_ && *owner_ < app.input.pointers.size() && !app.input.pointers.at(*owner_).now.click) {
        owner_.reset();
    }
    if (revert_armed_ && std::chrono::steady_clock::now() - *revert_armed_ > kRevertWindow) {
        revert_armed_.reset();
        Touch();
    }

    std::array<Overlay*, kPanels.size()> panels{};
    for (const auto kind : kPanels) {
        panels.at(static_cast<size_t>(kind)) = overlays.ByName(PanelName(kind));
    }
    const auto* preview = panels.at(static_cast<size_t>(PanelKind::Preview));
    const auto* palette = panels.at(static_cast<size_t>(PanelKind::Palette));
    preview_id_ = preview != nullptr ? std::optional(preview->state.id) : std::nullopt;
    for (const auto kind : {PanelKind::BackLayer, PanelKind::MiddleLayer, PanelKind::FrontLayer}) {
        const auto* layer = panels.at(static_cast<size_t>(kind));
        layer_ids_.at(static_cast<size_t>(LayerOf(kind).value_or(0))) = layer != nullptr ? std::optional(layer->state.id)
                                                                                         : std::nullopt;
    }
    palette_id_ = palette != nullptr ? std::optional(palette->state.id) : std::nullopt;

    std::optional<PanelKind> grabbed;
    for (const auto& pointer : app.input.pointers) {
        if (!pointer.interaction.grabbed) {
            continue;
        }
        const auto grabbed_id = pointer.interaction.grabbed->grabbed_id;
        for (const auto kind : kPanels) {
            const auto* panel = panels.at(static_cast<size_t>(kind));
            if (panel == nullptr || panel->state.id != grabbed_id) {
                continue;
            }
            if (kind == PanelKind::Ghost || (kind == PanelKind::Palette && palette_on_hand_)) {
                continue;
            }
            anchor_ = Eigen::Affine3f(panel->state.transform.matrix() * LocalTransform(kind).matrix().inverse());
            grabbed = kind;
        }
    }

    const Eigen::Matrix3f facing = anchor_.linear() / anchor_.linear().col(0).norm();
    for (const auto kind : kPanels) {
        auto* panel = panels.at(static_cast<size_t>(kind));
        if (panel == nullptr) {
            continue;
        }
        auto& state = panel->state;
        const Eigen::Vector2f size = PanelSize(kind);
        state.interaction_transform = OverlayState::UiTransform(
            {static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))}
        );
        switch (kind) {
        case PanelKind::Dialog: state.want_visible = dialog_ != DialogKind::None; break;
        case PanelKind::Ghost: state.want_visible = carrying_.has_value(); break;
        case PanelKind::Preview: state.want_visible = !Exploded(); break;
        case PanelKind::BackLayer:
        case PanelKind::MiddleLayer:
        case PanelKind::FrontLayer: state.want_visible = Exploded(); break;
        case PanelKind::Toolbar:
        case PanelKind::Palette:
        case PanelKind::Properties: state.want_visible = true; break;
        }
        if (hidden_) {
            state.want_visible = false;
        }
        if (kind == grabbed) {
            continue;
        }
        if (kind == PanelKind::Ghost) {
            const auto& pointer = app.input.pointers.at(carry_pointer_);
            const Eigen::Matrix3f hand = pointer.pose.linear();
            Eigen::Affine3f pose = Eigen::Affine3f::Identity();
            pose.translate(Eigen::Vector3f(pointer.pose.translation() + hand * -Eigen::Vector3f::UnitZ() * kGhostReach));
            pose.linear() = facing * kGhostWidth;
            state.transform = pose;
        } else if (kind == PanelKind::Palette && palette_on_hand_) {
            const size_t hand = app.config.watch_hand == Handedness::Left ? 0 : 1;
            Eigen::Affine3f pose = app.input.pointers.at(hand).tracked_pose;
            pose.translate(Eigen::Vector3f(0.0f, 0.05f, -0.06f));
            const Eigen::Matrix3f wrist = pose.linear();
            const Eigen::Matrix3f tilt = Eigen::AngleAxisf(-std::numbers::pi_v<float> / 3.0f, Eigen::Vector3f::UnitX())
                                             .toRotationMatrix();
            pose.linear() = wrist * tilt;
            pose.scale(kHandPaletteWidth);
            state.transform = pose;
        } else {
            state.transform = anchor_ * LocalTransform(kind);
        }
        state.dirty = true;
    }
}

auto Editor::Lines(OverlayContainer& overlays) const -> std::vector<EditorLine> {
    std::vector<EditorLine> lines;
    if (closing_) {
        return lines;
    }
    auto preview_kind = PanelKind::Preview;
    if (Exploded() && watch.selected && *watch.selected < watch.Document().elements.size()) {
        constexpr std::array kLayerPanels{PanelKind::BackLayer, PanelKind::MiddleLayer, PanelKind::FrontLayer};
        preview_kind = kLayerPanels.at(static_cast<size_t>(FormDesigner::LayerOf(watch.Document().elements[*watch.selected])));
    }
    auto* preview = overlays.ByName(PanelName(preview_kind));
    auto* properties = overlays.ByName(PanelName(PanelKind::Properties));
    if (preview == nullptr || properties == nullptr) {
        return lines;
    }
    const auto preview_corners = OverlayLayout::Corners(*preview);
    const auto property_corners = OverlayLayout::Corners(*properties);
    if (!preview_corners || !property_corners) {
        return lines;
    }
    std::optional<Eigen::Vector2f> anchor;
    if (target == EditTarget::Watch && watch.selected && *watch.selected < watch.Document().elements.size()) {
        const auto rect = watch.VisualRect(*watch.selected);
        anchor = watch.ToPreview({rect.x + rect.w, rect.y + rect.h / 2.0f});
    } else if (target == EditTarget::Keyboard && keyboard.selected && *keyboard.selected < keyboard.Document().keys.size()) {
        anchor = keyboard.KeyPreviewCenter(*keyboard.selected);
    }
    if (anchor) {
        const Eigen::Vector2f size = PanelSize(PanelKind::Preview);
        const Eigen::Vector2f uv = anchor->cwiseQuotient(size).cwiseMax(0.0f).cwiseMin(1.0f);
        lines.push_back(LineBetween(PointOn(*preview_corners, uv), PointOn(*property_corners, {0.0f, 0.5f})));
    }
    if (dialog_ != DialogKind::None) {
        if (auto* dialog = overlays.ByName(PanelName(PanelKind::Dialog))) {
            if (const auto dialog_corners = OverlayLayout::Corners(*dialog)) {
                lines.push_back(LineBetween(PointOn(*property_corners, {1.0f, 0.5f}), PointOn(*dialog_corners, {0.0f, 0.5f})));
            }
        }
    }
    return lines;
}

auto Editor::BuildPanel(PanelKind kind, Canvas& canvas, AppState& app) -> void {
    switch (kind) {
    case PanelKind::Preview:
        if (target == EditTarget::Keyboard) {
            keyboard.Draw(canvas, app, app.keymap);
        } else {
            watch.Draw(canvas, app);
        }
        return;
    case PanelKind::Ghost: BuildGhost(canvas, app); return;
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: watch.Draw(canvas, app, LayerOf(kind)); return;
    case PanelKind::Toolbar:
    case PanelKind::Palette:
    case PanelKind::Properties:
    case PanelKind::Dialog: break;
    }
    PanelBuilder builder(canvas, app);
    switch (kind) {
    case PanelKind::Toolbar: BuildToolbar(builder, app); break;
    case PanelKind::Palette: BuildPalette(builder, app); break;
    case PanelKind::Properties: BuildProperties(builder, app); break;
    case PanelKind::Dialog:
        switch (dialog_) {
        case DialogKind::Color: BuildColorDialog(builder, app); break;
        case DialogKind::Actions: BuildActionsDialog(builder, app); break;
        case DialogKind::KeyPicker: BuildKeyPicker(builder, app); break;
        case DialogKind::Timezone: BuildTimezonePicker(builder, app); break;
        case DialogKind::None: break;
        }
        break;
    case PanelKind::Preview:
    case PanelKind::Ghost:
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: break;
    }
}

auto Editor::BuildGhost(Canvas& canvas, AppState&) -> void {
    const Eigen::Vector2f size = PanelSize(PanelKind::Ghost);
    canvas.bg_color = EditorColors::Accent();
    canvas.Panel(0.0f, 0.0f, size.x(), size.y(), Theme::Radius(Theme::Active().rounding, Shape::Control) * 2.0f);
    canvas.fg_color = EditorColors::OnAccent();
    canvas.font_size = 20;
    std::string name;
    if (carrying_) {
        name = std::visit(
            [](const auto& item) -> std::string {
                using Item = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Item, FormPart>) {
                    return std::string(FormDesigner::PartName(item));
                } else {
                    return std::string(KeyboardDesigner::TemplateName(item));
                }
            },
            *carrying_
        );
    }
    canvas.LabelCentered(0.0f, 0.0f, size.x(), size.y(), 0.0f, std::move(name));
}

auto Editor::PreviewHover(AppState&, const Eigen::Vector2f& point) -> std::optional<Haptics> {
    last_point_ = point;
    if (target == EditTarget::Theme) {
        return std::nullopt;
    }
    if (target == EditTarget::Watch) {
        const auto before = watch.hovered;
        if (carrying_) {
            if (const auto* part = std::get_if<FormPart>(&*carrying_)) {
                watch.carrying = std::pair{*part, watch.ToDocument(point)};
            }
        }
        const auto guides_before = watch.Guides().size();
        const bool engaged = watch.Engaged();
        watch.Hover(watch.ToDocument(point));
        if (!engaged && watch.Engaged()) {
            ++preview_revision_;
            return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
        }
        if (watch.Dragging() || watch.carrying || watch.hovered != before) {
            ++preview_revision_;
        }
        if (watch.Dragging() && watch.Guides().size() > guides_before) {
            return Haptics{.intensity = 0.15f, .duration = 0.01f, .frequency = 5.0f};
        }
        return std::nullopt;
    }
    const auto before = keyboard.hovered;
    if (carrying_) {
        if (const auto* key_template = std::get_if<KeyTemplate>(&*carrying_)) {
            keyboard.carrying = std::pair{*key_template, point};
        }
    }
    const bool engaged = keyboard.Engaged();
    keyboard.Hover(point);
    if (keyboard.carrying || keyboard.hovered != before || keyboard.Engaged()) {
        ++preview_revision_;
    }
    if (!engaged && keyboard.Engaged()) {
        return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
    }
    return std::nullopt;
}

auto Editor::PreviewLeft(AppState&) -> void {
    watch.hovered.reset();
    watch.carrying.reset();
    keyboard.hovered.reset();
    keyboard.carrying.reset();
    ++preview_revision_;
}

auto Editor::PreviewPointer(AppState& app, PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed)
    -> void {
    const bool on_preview = hit.overlay == preview_id_ || std::ranges::contains(layer_ids_, std::optional(hit.overlay));
    const Eigen::Vector2f at = pressed || on_preview ? point : last_point_;
    if (pressed) {
        CommitText(app);
    }
    if (target == EditTarget::Theme) {
        return;
    }
    if (target == EditTarget::Watch) {
        if (pressed) {
            watch.Press(watch.ToDocument(at), LayerOf(kind));
        } else if (watch.Release(watch.ToDocument(at))) {
            watch_apply_ = true;
        }
    } else if (pressed) {
        keyboard.Press(at);
    } else {
        keyboard.Release(at);
    }
    Touch();
}

auto Editor::PreviewScroll(AppState&, const Eigen::Vector2f&, float) -> void {
}

auto Editor::PanelReleased(AppState& app, PanelKind from, const PointerHit& hit) -> bool {
    if (!carrying_ || from != PanelKind::Palette) {
        return false;
    }
    if (hit.overlay == preview_id_ || std::ranges::contains(layer_ids_, std::optional(hit.overlay))) {
        const Eigen::Vector2f size = PanelSize(PanelKind::Preview);
        Drop(app, hit.uv.cwiseProduct(size));
    } else if (hit.overlay == palette_id_ && app.input.pointers.at(hit.pointer).interaction.hovered_id == palette_id_) {
        Drop(app, std::nullopt);
    }
    carrying_.reset();
    watch.carrying.reset();
    keyboard.carrying.reset();
    owner_.reset();
    Touch();
    return true;
}

auto Editor::StartCarry(AppState& app, PaletteItem item) -> void {
    carrying_ = item;
    if (owner_) {
        carry_pointer_ = *owner_;
    } else {
        for (const auto& pointer : app.input.pointers) {
            if (pointer.now.click) {
                carry_pointer_ = pointer.index;
            }
        }
    }
    Touch();
}

auto Editor::Drop(AppState& app, std::optional<Eigen::Vector2f> point) -> void {
    if (!carrying_) {
        return;
    }
    if (const auto* part = std::get_if<FormPart>(&*carrying_)) {
        if (target == EditTarget::Keyboard) {
            return;
        }
        std::optional<Eigen::Vector2f> at;
        if (point) {
            at = watch.ToDocument(*point);
        }
        watch.Insert(*part, at);
        watch_apply_ = true;
    } else if (const auto* key_template = std::get_if<KeyTemplate>(&*carrying_)) {
        if (target == EditTarget::Watch) {
            return;
        }
        auto cell = std::pair{0.0f, keyboard.Document().rows};
        if (point) {
            cell = keyboard.DropCell(*point);
        } else if (const auto free = keyboard.Document().FreeCells(); !free.empty() && *key_template == KeyTemplate::Key) {
            cell = free.front();
        }
        if (!keyboard.Place(*key_template, cell.first, cell.second)) {
            Notify(app, "No room for that there");
        }
    }
    Touch();
}

auto Editor::SwitchTarget(EditTarget next) -> void {
    if (target == next) {
        return;
    }
    target = next;
    watch.hovered.reset();
    dialog_ = DialogKind::None;
    carrying_.reset();
    Touch();
}

auto Editor::Undo(AppState&) -> void {
    if (target == EditTarget::Theme) {
        if (theme.Undo()) {
            ApplyTheme();
        }
    } else if (target == EditTarget::Watch) {
        if (watch.history.Undo()) {
            watch.selected.reset();
            watch_apply_ = true;
        }
    } else if (keyboard.history.Undo()) {
        keyboard.selected.reset();
    }
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::Redo(AppState&) -> void {
    if (target == EditTarget::Theme) {
        if (theme.Redo()) {
            ApplyTheme();
        }
    } else if (target == EditTarget::Watch) {
        if (watch.history.Redo()) {
            watch.selected.reset();
            watch_apply_ = true;
        }
    } else if (keyboard.history.Redo()) {
        keyboard.selected.reset();
    }
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::EditWatch(const std::function<void(ModularUiConfig&)>& change) -> void {
    if (amend_) {
        watch.history.Amend(change);
    } else {
        watch.history.Edit(change);
    }
    watch_apply_ = true;
    Touch();
}

auto Editor::EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void {
    if (amend_) {
        keyboard.history.Amend(change);
    } else {
        keyboard.history.Edit(change);
    }
    Touch();
}

auto Editor::ApplyWatch(AppState& app, OverlayContainer& overlays) -> void {
    watch_apply_ = false;
    if (auto* live = overlays.ByName(kWatchName)) {
        if (const auto applied = Watch::Apply(app, *live, watch.Document()); !applied) {
            LOG_ERROR("Could not apply watch layout: {}", applied.error().Message());
        }
    }
}

auto Editor::EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void {
    if (amend_) {
        theme.Amend(change);
    } else {
        theme.Edit(change);
    }
    ApplyTheme();
    Touch();
}

auto Editor::ApplyTheme() -> void {
    Theme::SetActive(theme.Current());
    watch_apply_ = true;
    restyle_keyboard_ = true;
}

auto Editor::ArmDelete() -> bool {
    const auto now = std::chrono::steady_clock::now();
    if (!delete_armed_ || now - *delete_armed_ > kRevertWindow) {
        delete_armed_ = now;
        Touch();
        return false;
    }
    delete_armed_.reset();
    return true;
}

auto Editor::SelectWatch(AppState& app, const std::string& name) -> void {
    if (name == watch_profile_) {
        return;
    }
    if (watch.history.Dirty()) {
        Notify(app, "Save or revert this watch first");
        return;
    }
    watch_profile_ = name;
    watch.history.Reset(Watch::LoadConfig(name));
    watch.selected.reset();
    watch.selected_form.reset();
    app.config.watch_profile = name;
    if (const auto saved = app.config.SaveSettings(); !saved) {
        Notify(app, saved.error().Message());
    }
    watch_apply_ = true;
    delete_armed_.reset();
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::NewWatch(AppState& app) -> void {
    if (watch.history.Dirty()) {
        Notify(app, "Save or revert this watch first");
        return;
    }
    const auto profiles = Watch::Profiles();
    std::string name = "watch-2";
    for (int number = 3; std::ranges::contains(profiles, name); ++number) {
        name = std::format("watch-{}", number);
    }
    if (const auto written = WritePanelText(Watch::UserFile(name), Yaml::Emit(nlohmann::ordered_json(watch.Document())));
        !written) {
        Notify(app, written.error().Message());
        return;
    }
    SelectWatch(app, name);
    Notify(app, std::format("Created watch layout {}", name));
}

auto Editor::RenameWatch(AppState& app, const std::string& name) -> void {
    if (name.empty() || name == watch_profile_ || Watch::IsBuiltIn(watch_profile_)
        || name.find_first_of("/\\.") != std::string::npos) {
        return;
    }
    if (std::ranges::contains(Watch::Profiles(), name)) {
        Notify(app, std::format("{} already exists", name));
        return;
    }
    if (const auto written = WritePanelText(Watch::UserFile(name), Yaml::Emit(nlohmann::ordered_json(watch.Document())));
        !written) {
        Notify(app, written.error().Message());
        return;
    }
    std::error_code error;
    std::filesystem::remove(Watch::UserFile(watch_profile_), error);
    watch_profile_ = name;
    watch.history.MarkSaved();
    app.config.watch_profile = name;
    if (const auto saved = app.config.SaveSettings(); !saved) {
        Notify(app, saved.error().Message());
    }
    Touch();
}

auto Editor::DeleteWatch(AppState& app) -> void {
    if (Watch::IsBuiltIn(watch_profile_) || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(Watch::UserFile(watch_profile_), error);
    Notify(app, std::format("Deleted watch layout {}", watch_profile_));
    watch.history.Reset(ModularUiConfig(watch.Document()));
    SelectWatch(app, std::string(Watch::kDefaultProfile));
}

auto Editor::SelectKeyboard(AppState& app, const std::string& name) -> void {
    if (name == keyboard_file_name_) {
        return;
    }
    if (keyboard.history.Dirty()) {
        Notify(app, "Save or revert this keyboard first");
        return;
    }
    keyboard.history.Reset(KeyboardProfiles::Find(name));
    keyboard_file_name_ = keyboard.Document().name;
    keyboard.selected.reset();
    app.config.keyboard_profile = keyboard_file_name_;
    if (const auto saved = app.config.SaveSettings(); !saved) {
        Notify(app, saved.error().Message());
    }
    ApplyKeyboard(app);
    delete_armed_.reset();
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::DeleteKeyboard(AppState& app) -> void {
    const auto& layout = keyboard.Document();
    if (KeyboardProfiles::IsBuiltIn(layout.name) || layout.source.empty() || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(layout.source, error);
    Notify(app, std::format("Deleted keyboard {}", layout.name));
    KeyboardProfiles::Reload();
    keyboard.history.Reset(KeyboardLayout(layout));
    keyboard_file_name_.clear();
    SelectKeyboard(app, KeyboardProfiles::All().front().name);
}

auto Editor::SelectTheme(AppState& app, const std::string& name) -> void {
    if (name == theme.Current().name) {
        return;
    }
    if (theme.Dirty()) {
        Notify(app, "Save or revert this theme first");
        return;
    }
    theme.Reset(Theme::Find(name));
    app.config.theme = theme.Current().name;
    if (const auto saved = app.config.SaveSettings(); !saved) {
        Notify(app, saved.error().Message());
    }
    ApplyTheme();
    delete_armed_.reset();
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::NewTheme(AppState& app) -> void {
    if (theme.Dirty()) {
        Notify(app, "Save or revert this theme first");
        return;
    }
    std::string name = "my-theme";
    for (int number = 2; std::ranges::contains(Theme::All(), name, &ThemeDefinition::name); ++number) {
        name = std::format("my-theme-{}", number);
    }
    auto copy = theme.Current();
    copy.name = name;
    if (const auto written = WritePanelText(
            Theme::UserDirectory() / std::format("{}.yaml", name),
            Yaml::Emit(nlohmann::ordered_json(copy))
        );
        !written) {
        Notify(app, written.error().Message());
        return;
    }
    Theme::Reload();
    SelectTheme(app, name);
    Notify(app, std::format("Created theme {}", name));
}

auto Editor::RenameTheme(AppState& app, const std::string& name) -> void {
    const auto& current = theme.Current();
    if (name.empty() || name == current.name || Theme::IsBuiltIn(current.name) || name.find_first_of("/\\.") != std::string::npos) {
        return;
    }
    if (std::ranges::contains(Theme::All(), name, &ThemeDefinition::name)) {
        Notify(app, std::format("{} already exists", name));
        return;
    }
    auto renamed = current;
    renamed.name = name;
    if (const auto written = WritePanelText(
            Theme::UserDirectory() / std::format("{}.yaml", name),
            Yaml::Emit(nlohmann::ordered_json(renamed))
        );
        !written) {
        Notify(app, written.error().Message());
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    Theme::Reload();
    theme.Reset(Theme::Find(name));
    app.config.theme = name;
    if (const auto saved = app.config.SaveSettings(); !saved) {
        Notify(app, saved.error().Message());
    }
    ApplyTheme();
    Touch();
}

auto Editor::DeleteTheme(AppState& app) -> void {
    const auto& current = theme.Current();
    if (Theme::IsBuiltIn(current.name) || current.source.empty() || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    Notify(app, std::format("Deleted theme {}", current.name));
    Theme::Reload();
    theme.Reset(ThemeDefinition(current));
    SelectTheme(app, Theme::All().front().name);
}

auto Editor::ApplyKeyboard(AppState& app) -> void {
    const auto& name = keyboard.Document().name;
    Keyboard::SwitchProfile(app, app.keymap, name);
}

auto Editor::Save(AppState& app) -> void {
    if (target == EditTarget::Theme) {
        auto saved = theme.Current();
        const auto path = Theme::UserDirectory() / std::format("{}.yaml", saved.name);
        if (const auto written = WritePanelText(path, Yaml::Emit(nlohmann::ordered_json(saved))); !written) {
            Notify(app, written.error().Message());
            return;
        }
        theme.MarkSaved();
        Theme::Reload();
        SaveDrafts(true);
        Notify(app, std::format("Theme {} saved", saved.name));
        Touch();
        return;
    }
    if (target == EditTarget::Watch) {
        const auto text = Yaml::Emit(nlohmann::ordered_json(watch.Document()));
        if (const auto written = WritePanelText(Watch::UserFile(watch_profile_), text); !written) {
            Notify(app, written.error().Message());
            return;
        }
        watch.history.MarkSaved();
        SaveDrafts(true);
        Notify(app, "Watch saved");
        Touch();
        return;
    }
    auto& layout = keyboard.history.Live();
    if (const auto valid = layout.Validate(); !valid) {
        Notify(app, valid.error().Message());
        return;
    }
    std::filesystem::path path = layout.source;
    if (path.empty() || FileProfileName(path) != layout.name) {
        path = KeyboardProfiles::UserDirectory() / std::format("{}.yaml", layout.name);
    }
    if (const auto written = WritePanelText(path, Yaml::Emit(nlohmann::ordered_json(layout))); !written) {
        Notify(app, written.error().Message());
        return;
    }
    layout.source = path;
    keyboard_file_name_ = layout.name;
    keyboard.history.MarkSaved();
    KeyboardProfiles::Reload();
    ApplyKeyboard(app);
    SaveDrafts(true);
    Notify(app, std::format("Keyboard {} saved", layout.name));
    Touch();
}

auto Editor::SaveAsNew(AppState& app) -> void {
    const auto base = keyboard.Document().name;
    std::string name = std::format("{}-custom", base);
    for (int number = 2; std::ranges::contains(KeyboardProfiles::All(), name, &KeyboardLayout::name); ++number) {
        name = std::format("{}-custom-{}", base, number);
    }
    EditKeyboard([&name](KeyboardLayout& layout) -> void {
        layout.name = name;
        layout.source.clear();
    });
    Save(app);
}

auto Editor::Revert(AppState& app) -> void {
    const auto now = std::chrono::steady_clock::now();
    if (!revert_armed_ || now - *revert_armed_ > kRevertWindow) {
        revert_armed_ = now;
        Touch();
        return;
    }
    revert_armed_.reset();
    std::error_code error;
    if (target == EditTarget::Theme) {
        const auto name = theme.Current().name;
        if (Theme::IsBuiltIn(name)) {
            std::filesystem::remove(Theme::UserDirectory() / std::format("{}.yaml", name), error);
        }
        Theme::Reload();
        theme.Reset(Theme::Find(name));
        ApplyTheme();
        Notify(app, std::format("Theme {} reverted", name));
    } else if (target == EditTarget::Watch) {
        if (Watch::IsBuiltIn(watch_profile_)) {
            std::filesystem::remove(Watch::UserFile(watch_profile_), error);
        }
        watch.history.Reset(Watch::LoadConfig(watch_profile_));
        watch.selected.reset();
        watch_apply_ = true;
        Notify(app, "Watch reverted");
    } else {
        const auto& layout = keyboard.Document();
        const std::string name = keyboard_file_name_;
        if (!layout.source.empty()) {
            std::filesystem::remove(layout.source, error);
        }
        KeyboardProfiles::Reload();
        keyboard.history.Reset(KeyboardProfiles::Find(name));
        keyboard_file_name_ = keyboard.Document().name;
        keyboard.selected.reset();
        ApplyKeyboard(app);
        Notify(app, std::format("Keyboard {} reverted", keyboard.Document().name));
    }
    SaveDrafts(true);
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::Done(AppState& app) -> void {
    CommitText(app);
    if (Dirty()) {
        closing_prompt_ = true;
        Touch();
        return;
    }
    Close(app);
}

auto Editor::Discard(AppState& app) -> void {
    keyboard.history.Reset(KeyboardLayout(keyboard.Document()));
    if (theme.Dirty()) {
        theme.Reset(Theme::Find(theme.Current().name));
        ApplyTheme();
    }
    if (watch.history.Dirty()) {
        watch.history.Reset(Watch::LoadConfig(watch_profile_));
        watch_apply_ = true;
    }
    Close(app);
}

auto Editor::Close(AppState& app) -> void {
    EndText(app);
    SaveDrafts(true);
    closing_prompt_ = false;
    closing_ = true;
}

auto Editor::ShowKeyboard(AppState& app) -> void {
    app.tasks.Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [](AppState& state, OverlayState& overlay) -> void {
                if (!overlay.want_visible) {
                    overlay.want_visible = true;
                    overlay.Reset(state, false);
                }
            },
        }
    );
}

auto Editor::BeginText(
    AppState& app,
    std::string id,
    std::string initial,
    std::function<void(Editor&, AppState&, const std::string&)> commit
) -> void {
    if (focus_ && focus_->id != id) {
        auto previous = std::move(*focus_);
        focus_.reset();
        previous.commit(*this, app, previous.buffer);
    }
    focus_ = TextFocus{.id = std::move(id), .buffer = std::move(initial), .commit = std::move(commit)};
    capture_.reset();
    app.text_input = input_;
    ShowKeyboard(app);
    Touch();
}

auto Editor::BeginCapture(AppState& app, bool shortcut, std::function<void(Editor&, const std::vector<VirtualKey>&)> done) -> void {
    focus_.reset();
    capture_ = KeyCapture{.shortcut = shortcut, .done = std::move(done)};
    app.text_input = input_;
    ShowKeyboard(app);
    Touch();
}

auto Editor::EndText(AppState& app) -> void {
    focus_.reset();
    capture_.reset();
    if (app.text_input == input_) {
        app.text_input.reset();
    }
    Touch();
}

auto Editor::CommitText(AppState& app) -> void {
    if (!focus_) {
        return;
    }
    auto focus = std::move(*focus_);
    EndText(app);
    focus.commit(*this, app, focus.buffer);
}

auto Editor::OnKey(AppState& app, VirtualKey key, std::string_view text) -> void {
    if (capture_) {
        if (capture_->shortcut && VirtualKeys::ModifierOf(key) && !Keyboard::IsLatchingKey(key)) {
            auto& held = capture_->held;
            if (const auto found = std::ranges::find(held, key); found != held.end()) {
                held.erase(found);
            } else {
                held.push_back(key);
            }
            Touch();
            return;
        }
        auto keys = capture_->held;
        keys.push_back(key);
        auto done = std::move(capture_->done);
        EndText(app);
        done(*this, keys);
        Touch();
        return;
    }
    if (!focus_) {
        return;
    }
    switch (key) {
    case VirtualKey::Return:
    case VirtualKey::KpEnter: {
        auto focus = std::move(*focus_);
        EndText(app);
        focus.commit(*this, app, focus.buffer);
        break;
    }
    case VirtualKey::Escape: EndText(app); break;
    case VirtualKey::BackSpace: PopCodepoint(focus_->buffer); break;
    default: focus_->buffer += text; break;
    }
    Touch();
}

auto Editor::OpenColor(ColorDialog dialog) -> void {
    color_ = std::move(dialog);
    dialog_ = DialogKind::Color;
    Touch();
}

auto Editor::OpenActions(ButtonTrigger trigger) -> void {
    trigger_ = trigger;
    dialog_ = DialogKind::Actions;
    Touch();
}

auto Editor::OpenTimezones(size_t element) -> void {
    timezone_element_ = element;
    timezone_query_.clear();
    dialog_ = DialogKind::Timezone;
    Touch();
}

auto Editor::OpenKeyPicker() -> void {
    dialog_ = DialogKind::KeyPicker;
    Touch();
}

auto Editor::CloseDialog() -> void {
    dialog_ = DialogKind::None;
    Touch();
}

auto Editor::Notify(AppState& app, std::string text) -> void {
    ::Toast{.topic = ToastTopic::System, .title = std::move(text), .timeout = 3.0f}.Submit(app);
}

auto Editor::SplitCommand(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    bool active = false;
    for (const char c : text) {
        if (c == '"') {
            quoted = !quoted;
            active = true;
        } else if (c == ' ' && !quoted) {
            if (active) {
                parts.push_back(std::move(current));
                current.clear();
                active = false;
            }
        } else {
            current += c;
            active = true;
        }
    }
    if (active) {
        parts.push_back(std::move(current));
    }
    return parts;
}

auto Editor::JoinCommand(const std::vector<std::string>& command) -> std::string {
    std::string text;
    for (const auto& part : command) {
        if (!text.empty()) {
            text += ' ';
        }
        text += part.empty() || part.contains(' ') ? std::format("\"{}\"", part) : part;
    }
    return text;
}
