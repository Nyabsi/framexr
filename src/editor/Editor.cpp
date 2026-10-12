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
#include <utility>

#include "Config.hpp"
#include "Device.hpp"
#include "EditorColors.hpp"
#include "EditorPanel.hpp"
#include "EditorTextInput.hpp"
#include "Font.hpp"
#include "Input.hpp"
#include "Keyboard.hpp"
#include "KeyboardProfiles.hpp"
#include "Label.hpp"
#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "Task.hpp"
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
    return {.pose = Pointer::LinePose(Eigen::Affine3f(Eigen::Translation3f(from)), to), .length = (to - from).norm()};
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
    const auto text = ConfigStore::ReadFile(path);
    if (!text) {
        return {};
    }
    const auto layout = KeyboardLayout::Parse(*text);
    return layout ? layout->Name() : std::string{};
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
    return !layer || m_watch_design.Accepts(m_watch_design.ToDocument(point), *layer);
}

auto Editor::Claim(size_t pointer) -> void {
    m_owner = pointer;
}

auto Editor::Unclaim(size_t pointer) -> void {
    if (m_owner == pointer && !m_carrying) {
        m_owner.reset();
    }
}

auto Editor::SetExploded(bool exploded) -> void {
    m_exploded = exploded;
    m_watch_design.Hovered().reset();
    Touch();
}

auto Editor::Create() -> std::shared_ptr<Editor> {
    auto editor = std::make_shared<Editor>(
        Watch::LoadConfig(g_config->watch_profile),
        g_keyboard_profiles->Find(g_config->keyboard_profile),
        g_config->watch_profile
    );
    editor->m_input = std::make_shared<EditorTextInput>(editor);
    editor->SetMeasure();
    editor->RestoreDrafts();
    return editor;
}

Editor::Editor(ModularUiConfig watch_config, KeyboardLayout keyboard_layout, std::string watch_profile)
    : m_watch_design(std::move(watch_config)),
      m_keyboard_design(std::move(keyboard_layout)),
      m_theme_history(g_themes->Active()),
      m_keyboard_file_name(m_keyboard_design.Document().Name()),
      m_watch_profile(std::move(watch_profile)) {
}

auto Editor::DraftFile(EditTarget draft) -> std::filesystem::path {
    switch (draft) {
    case EditTarget::Watch: return ConfigStore::Root() / "drafts" / "watch.yaml";
    case EditTarget::Keyboard: return ConfigStore::Root() / "drafts" / "keyboard.yaml";
    case EditTarget::Theme: return ConfigStore::Root() / "drafts" / "theme.yaml";
    }
    return ConfigStore::Root() / "drafts" / "watch.yaml";
}

auto Editor::SaveDrafts(bool force) -> void {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - m_last_draft < kDraftInterval) {
        return;
    }
    m_last_draft = now;
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
    keep(
        EditTarget::Watch,
        m_watch_design.Revisions().Dirty(),
        m_watch_design.Revisions().Version(),
        m_watch_drafted,
        m_watch_design.Document()
    );
    keep(
        EditTarget::Keyboard,
        m_keyboard_design.Revisions().Dirty(),
        m_keyboard_design.Revisions().Version(),
        m_keyboard_drafted,
        m_keyboard_design.Document()
    );
    keep(EditTarget::Theme, m_theme_history.Dirty(), m_theme_history.Version(), m_theme_drafted, m_theme_history.Current());
}

auto Editor::RestoreDrafts() -> void {
    bool restored = false;
    if (const auto text = ConfigStore::ReadFile(DraftFile(EditTarget::Watch))) {
        if (const auto parsed = Yaml::Parse(*text)) {
            try {
                auto draft = parsed->get<ModularUiConfig>();
                m_watch_design.Revisions().Edit([&draft](ModularUiConfig& config) -> void { config = std::move(draft); });
                m_watch_drafted = m_watch_design.Revisions().Version();
                m_watch_apply = true;
                restored = true;
            } catch (const nlohmann::json::exception& error) {
                LOG_WARN("Ignoring broken watch draft: {}", error.what());
            }
        }
    }
    if (const auto text = ConfigStore::ReadFile(DraftFile(EditTarget::Keyboard))) {
        if (auto draft = KeyboardLayout::Parse(*text)) {
            const auto& profiles = g_keyboard_profiles->All();
            if (const auto saved = std::ranges::find(profiles, draft->Name(), &KeyboardLayout::Name); saved != profiles.end()) {
                m_keyboard_design.Revisions().Reset(*saved);
                draft->SetSource(saved->Source());
            }
            m_keyboard_file_name = draft->Name();
            m_keyboard_design.Revisions().Edit([&draft](KeyboardLayout& layout) -> void { layout = std::move(*draft); });
            m_keyboard_drafted = m_keyboard_design.Revisions().Version();
            restored = true;
        } else {
            LOG_WARN("Ignoring broken keyboard draft: {}", draft.error().Message());
        }
    }
    if (const auto text = ConfigStore::ReadFile(DraftFile(EditTarget::Theme))) {
        if (auto draft = Theme::Parse(*text); draft && draft->name == m_theme_history.Current().name) {
            draft->source = m_theme_history.Current().source;
            m_theme_history.Edit([&draft](ThemeDefinition& edited) -> void { edited = std::move(*draft); });
            m_theme_drafted = m_theme_history.Version();
            ApplyTheme();
            restored = true;
        }
    }
    if (restored) {
        Notify("Picked up your unsaved edits where you left off");
    }
}

auto Editor::Suspend() -> void {
    CommitText();
    m_restore_saved_watch = m_watch_design.Revisions().Dirty();
    Close();
}

auto Editor::SetMeasure() -> void {
    auto measure = [](std::string_view text, int font_size) -> float {
        if (g_fonts && g_device) {
            if (const auto size = g_fonts->MeasureText(text, font_size)) {
                return size->width;
            }
        }
        return 0.6f * static_cast<float>(font_size) * static_cast<float>(text.size());
    };
    m_watch_design.measure = std::move(measure);
    m_watch_design.city_text = [](const ClockLabelContent& clock) -> std::string { return Labels::City(clock.timezone); };
    m_watch_design.label_text = [](const LabelContent& content) -> std::string {
        if (std::holds_alternative<ExecLabelContent>(content)) {
            return "Output";
        }
        auto setup = Labels::Create(content, Eigen::Vector4f::Ones());
        if (setup.control) {
            setup.control->Update();
            setup.text = setup.control->Text();
        }
        if (setup.text.empty()) {
            return std::holds_alternative<BatteryLabelContent>(content) ? std::string("R99") : std::string("100");
        }
        return setup.text;
    };
}

auto Editor::Dirty() const -> bool {
    return m_watch_design.Revisions().Dirty() || m_keyboard_design.Revisions().Dirty() || m_theme_history.Dirty();
}

auto Editor::Touch() -> void {
    ++m_revision;
    ++m_preview_revision;
}

auto Editor::Revision(PanelKind kind) const -> uint64_t {
    return kind == PanelKind::Preview || LayerOf(kind) ? m_preview_revision : m_revision;
}

auto Editor::PanelSize(PanelKind kind) const -> Eigen::Vector2f {
    switch (kind) {
    case PanelKind::Preview:
        return m_target == EditTarget::Keyboard ? m_keyboard_design.PreviewSize() : m_watch_design.PreviewSize();
    case PanelKind::Toolbar: return {1040.0f, 58.0f};
    case PanelKind::Palette: return {240.0f, 420.0f};
    case PanelKind::Properties: return {360.0f, 820.0f};
    case PanelKind::Dialog: return {380.0f, 760.0f};
    case PanelKind::Ghost: return {220.0f, 52.0f};
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: return m_watch_design.PreviewSize();
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

auto Editor::Recenter() -> void {
    const Eigen::Matrix3f head = g_input->Hmd().linear();
    Eigen::Vector3f forward = head * -Eigen::Vector3f::UnitZ();
    forward.y() = 0.0f;
    const float yaw = forward.norm() > 1e-4f ? std::atan2(-forward.x(), -forward.z()) : 0.0f;
    m_anchor = Eigen::Translation3f(g_input->Hmd().translation()) * Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitY());
}

auto Editor::SetHidden(bool hidden) -> void {
    m_hidden = hidden;
    if (hidden) {
        m_carrying.reset();
        m_owner.reset();
    }
}

auto Editor::Open() -> void {
    Recenter();

    std::weak_ptr<Editor> weak = weak_from_this();
    for (const auto kind : kPanels) {
        g_tasks->Enqueue(
            CreateOverlayTask{
                .selector = std::string(PanelName(kind)),
                .create = [weak, kind]() -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                    const auto editor = weak.lock();
                    if (!editor) {
                        return std::nullopt;
                    }
                    OverlayState state;
                    state.SetName(std::string(PanelName(kind)));
                    state.SetWantVisible(kind != PanelKind::Dialog && kind != PanelKind::Ghost && !LayerOf(kind));
                    state.SetInteractable(kind != PanelKind::Ghost);
                    state.SetGrabbable(kind != PanelKind::Ghost);
                    state.SetAttachedTo(RelativeTo::Stage);
                    state.SetZOrder(kind == PanelKind::Ghost ? kZOrderWatch : kZOrderDefault);
                    state.SetTransform(editor->m_anchor * editor->LocalTransform(kind));
                    const Eigen::Vector2f size = editor->PanelSize(kind);
                    state.SetInteractionTransform(
                        OverlayState::UiTransform(
                            {static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))}
                        )
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

auto Editor::Update() -> void {
    if (m_watch_apply) {
        ApplyWatch();
    }
    if (m_restore_saved_watch) {
        m_restore_saved_watch = false;
        if (auto* live = g_overlays->ByName(kWatchName)) {
            if (const auto applied = Watch::Apply(*live, Watch::LoadConfig(m_watch_profile)); !applied) {
                LOG_ERROR("Could not restore the saved watch: {}", applied.error().Message());
            }
        }
    }
    if (!m_closing) {
        SaveDrafts(false);
    }
    if (m_restyle_keyboard && m_color_drag == ColorDrag::None
        && std::chrono::steady_clock::now() - m_last_restyle > kRestyleInterval) {
        m_restyle_keyboard = false;
        m_last_restyle = std::chrono::steady_clock::now();
        Keyboard::SwitchProfile(g_input->TypingKeymap(), g_config->keyboard_profile);
    }
    if (m_closing) {
        if (!m_finished) {
            for (const auto kind : kPanels) {
                g_tasks->Enqueue(DropOverlayTask{.selector = std::string(PanelName(kind))});
            }
            EndText();
            m_finished = true;
        }
        return;
    }
    if (m_owner && !m_carrying && *m_owner < g_input->Pointers().size() && !g_input->Pointers().at(*m_owner).Now().click) {
        m_owner.reset();
    }
    if (m_revert_armed && std::chrono::steady_clock::now() - *m_revert_armed > kRevertWindow) {
        m_revert_armed.reset();
        Touch();
    }

    std::array<Overlay*, kPanels.size()> panels{};
    for (const auto kind : kPanels) {
        panels.at(static_cast<size_t>(kind)) = g_overlays->ByName(PanelName(kind));
    }
    const auto* preview = panels.at(static_cast<size_t>(PanelKind::Preview));
    const auto* palette = panels.at(static_cast<size_t>(PanelKind::Palette));
    m_preview_id = preview != nullptr ? std::optional(preview->State().Id()) : std::nullopt;
    for (const auto kind : {PanelKind::BackLayer, PanelKind::MiddleLayer, PanelKind::FrontLayer}) {
        const auto* layer = panels.at(static_cast<size_t>(kind));
        m_layer_ids.at(static_cast<size_t>(LayerOf(kind).value_or(0))) = layer != nullptr ? std::optional(layer->State().Id())
                                                                                          : std::nullopt;
    }
    m_palette_id = palette != nullptr ? std::optional(palette->State().Id()) : std::nullopt;

    std::optional<PanelKind> grabbed;
    for (const auto& pointer : g_input->Pointers()) {
        if (!pointer.Interaction().grabbed) {
            continue;
        }
        const auto grabbed_id = pointer.Interaction().grabbed->grabbed_id;
        for (const auto kind : kPanels) {
            const auto* panel = panels.at(static_cast<size_t>(kind));
            if (panel == nullptr || panel->State().Id() != grabbed_id) {
                continue;
            }
            if (kind == PanelKind::Ghost || (kind == PanelKind::Palette && m_palette_on_hand)) {
                continue;
            }
            m_anchor = Eigen::Affine3f(panel->State().Transform().matrix() * LocalTransform(kind).matrix().inverse());
            grabbed = kind;
        }
    }

    const Eigen::Matrix3f facing = m_anchor.linear() / m_anchor.linear().col(0).norm();
    for (const auto kind : kPanels) {
        auto* panel = panels.at(static_cast<size_t>(kind));
        if (panel == nullptr) {
            continue;
        }
        auto& state = panel->State();
        const Eigen::Vector2f size = PanelSize(kind);
        state.SetInteractionTransform(
            OverlayState::UiTransform({static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))})
        );
        switch (kind) {
        case PanelKind::Dialog: state.SetWantVisible(m_dialog != DialogKind::None); break;
        case PanelKind::Ghost: state.SetWantVisible(m_carrying.has_value()); break;
        case PanelKind::Preview: state.SetWantVisible(!Exploded()); break;
        case PanelKind::BackLayer:
        case PanelKind::MiddleLayer:
        case PanelKind::FrontLayer: state.SetWantVisible(Exploded()); break;
        case PanelKind::Toolbar:
        case PanelKind::Palette:
        case PanelKind::Properties: state.SetWantVisible(true); break;
        }
        if (m_hidden) {
            state.SetWantVisible(false);
        }
        if (kind == grabbed) {
            continue;
        }
        if (kind == PanelKind::Ghost) {
            const auto& pointer = g_input->Pointers().at(m_carry_pointer);
            const Eigen::Matrix3f hand = pointer.Pose().linear();
            Eigen::Affine3f pose = Eigen::Affine3f::Identity();
            pose.translate(Eigen::Vector3f(pointer.Pose().translation() + hand * -Eigen::Vector3f::UnitZ() * kGhostReach));
            pose.linear() = facing * kGhostWidth;
            state.SetTransform(pose);
        } else if (kind == PanelKind::Palette && m_palette_on_hand) {
            const size_t hand = g_config->watch_hand == Handedness::Left ? 0 : 1;
            Eigen::Affine3f pose = g_input->Pointers().at(hand).TrackedPose();
            pose.translate(Eigen::Vector3f(0.0f, 0.05f, -0.06f));
            const Eigen::Matrix3f wrist = pose.linear();
            const Eigen::Matrix3f tilt = Eigen::AngleAxisf(-std::numbers::pi_v<float> / 3.0f, Eigen::Vector3f::UnitX())
                                             .toRotationMatrix();
            pose.linear() = wrist * tilt;
            pose.scale(kHandPaletteWidth);
            state.SetTransform(pose);
        } else {
            state.SetTransform(m_anchor * LocalTransform(kind));
        }
        state.SetDirty(true);
    }
}

auto Editor::Lines() const -> std::vector<EditorLine> {
    std::vector<EditorLine> lines;
    if (m_closing) {
        return lines;
    }
    auto preview_kind = PanelKind::Preview;
    if (Exploded() && m_watch_design.Selected() && *m_watch_design.Selected() < m_watch_design.Document().Elements().size()) {
        constexpr std::array kLayerPanels{PanelKind::BackLayer, PanelKind::MiddleLayer, PanelKind::FrontLayer};
        preview_kind = kLayerPanels.at(
            static_cast<size_t>(FormDesigner::LayerOf(m_watch_design.Document().Elements()[*m_watch_design.Selected()]))
        );
    }
    auto* preview = g_overlays->ByName(PanelName(preview_kind));
    auto* properties = g_overlays->ByName(PanelName(PanelKind::Properties));
    if (preview == nullptr || properties == nullptr) {
        return lines;
    }
    const auto preview_corners = OverlayLayout::Corners(*preview);
    const auto property_corners = OverlayLayout::Corners(*properties);
    if (!preview_corners || !property_corners) {
        return lines;
    }
    std::optional<Eigen::Vector2f> anchor;
    if (m_target == EditTarget::Watch && m_watch_design.Selected()
        && *m_watch_design.Selected() < m_watch_design.Document().Elements().size()) {
        const auto rect = m_watch_design.VisualRect(*m_watch_design.Selected());
        anchor = m_watch_design.ToPreview({rect.x + rect.w, rect.y + rect.h / 2.0f});
    } else if (
        m_target == EditTarget::Keyboard && m_keyboard_design.Selected()
        && *m_keyboard_design.Selected() < m_keyboard_design.Document().Keys().size()
    ) {
        anchor = m_keyboard_design.KeyPreviewCenter(*m_keyboard_design.Selected());
    }
    if (anchor) {
        const Eigen::Vector2f size = PanelSize(PanelKind::Preview);
        const Eigen::Vector2f uv = anchor->cwiseQuotient(size).cwiseMax(0.0f).cwiseMin(1.0f);
        lines.push_back(LineBetween(PointOn(*preview_corners, uv), PointOn(*property_corners, {0.0f, 0.5f})));
    }
    if (m_dialog != DialogKind::None) {
        if (auto* dialog = g_overlays->ByName(PanelName(PanelKind::Dialog))) {
            if (const auto dialog_corners = OverlayLayout::Corners(*dialog)) {
                lines.push_back(LineBetween(PointOn(*property_corners, {1.0f, 0.5f}), PointOn(*dialog_corners, {0.0f, 0.5f})));
            }
        }
    }
    return lines;
}

auto Editor::BuildPanel(PanelKind kind, Canvas& canvas) -> void {
    switch (kind) {
    case PanelKind::Preview:
        if (m_target == EditTarget::Keyboard) {
            m_keyboard_design.Draw(canvas, g_input->TypingKeymap());
        } else {
            m_watch_design.Draw(canvas);
        }
        return;
    case PanelKind::Ghost: BuildGhost(canvas); return;
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: m_watch_design.Draw(canvas, LayerOf(kind)); return;
    case PanelKind::Toolbar:
    case PanelKind::Palette:
    case PanelKind::Properties:
    case PanelKind::Dialog: break;
    }
    PanelBuilder builder(canvas);
    switch (kind) {
    case PanelKind::Toolbar: BuildToolbar(builder); break;
    case PanelKind::Palette: BuildPalette(builder); break;
    case PanelKind::Properties: BuildProperties(builder); break;
    case PanelKind::Dialog:
        switch (m_dialog) {
        case DialogKind::Color: BuildColorDialog(builder); break;
        case DialogKind::Actions: BuildActionsDialog(builder); break;
        case DialogKind::KeyPicker: BuildKeyPicker(builder); break;
        case DialogKind::Timezone: BuildTimezonePicker(builder); break;
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

auto Editor::BuildGhost(Canvas& canvas) -> void {
    const Eigen::Vector2f size = PanelSize(PanelKind::Ghost);
    canvas.SetBgColor(EditorColors::Accent());
    canvas.Panel(0.0f, 0.0f, size.x(), size.y(), Theme::Radius(g_themes->Active().rounding, Shape::Control) * 2.0f);
    canvas.SetFgColor(EditorColors::OnAccent());
    canvas.SetFontSize(20);
    std::string name;
    if (m_carrying) {
        name = std::visit(
            [](const auto& item) -> std::string {
                using Item = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Item, FormPart>) {
                    return std::string(FormDesigner::PartName(item));
                } else {
                    return std::string(KeyboardDesigner::TemplateName(item));
                }
            },
            *m_carrying
        );
    }
    canvas.LabelCentered(0.0f, 0.0f, size.x(), size.y(), 0.0f, std::move(name));
}

auto Editor::PreviewHover(const Eigen::Vector2f& point) -> std::optional<Haptics> {
    m_last_point = point;
    if (m_target == EditTarget::Theme) {
        return std::nullopt;
    }
    if (m_target == EditTarget::Watch) {
        const auto before = m_watch_design.Hovered();
        if (m_carrying) {
            if (const auto* part = std::get_if<FormPart>(&*m_carrying)) {
                m_watch_design.SetCarrying(std::pair{*part, m_watch_design.ToDocument(point)});
            }
        }
        const auto guides_before = m_watch_design.Guides().size();
        const bool engaged = m_watch_design.Engaged();
        m_watch_design.Hover(m_watch_design.ToDocument(point));
        if (!engaged && m_watch_design.Engaged()) {
            ++m_preview_revision;
            return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
        }
        if (m_watch_design.Dragging() || m_watch_design.Carrying() || m_watch_design.Hovered() != before) {
            ++m_preview_revision;
        }
        if (m_watch_design.Dragging() && m_watch_design.Guides().size() > guides_before) {
            return Haptics{.intensity = 0.15f, .duration = 0.01f, .frequency = 5.0f};
        }
        return std::nullopt;
    }
    const auto before = m_keyboard_design.Hovered();
    if (m_carrying) {
        if (const auto* key_template = std::get_if<KeyTemplate>(&*m_carrying)) {
            m_keyboard_design.SetCarrying(std::pair{*key_template, point});
        }
    }
    const bool engaged = m_keyboard_design.Engaged();
    m_keyboard_design.Hover(point);
    if (m_keyboard_design.Carrying() || m_keyboard_design.Hovered() != before || m_keyboard_design.Engaged()) {
        ++m_preview_revision;
    }
    if (!engaged && m_keyboard_design.Engaged()) {
        return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
    }
    return std::nullopt;
}

auto Editor::PreviewLeft() -> void {
    m_watch_design.Hovered().reset();
    m_watch_design.Carrying().reset();
    m_keyboard_design.Hovered().reset();
    m_keyboard_design.Carrying().reset();
    ++m_preview_revision;
}

auto Editor::PreviewPointer(PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed) -> void {
    const bool on_preview = hit.overlay == m_preview_id || std::ranges::contains(m_layer_ids, std::optional(hit.overlay));
    const Eigen::Vector2f at = pressed || on_preview ? point : m_last_point;
    if (pressed) {
        CommitText();
    }
    if (m_target == EditTarget::Theme) {
        return;
    }
    if (m_target == EditTarget::Watch) {
        if (pressed) {
            m_watch_design.Press(m_watch_design.ToDocument(at), LayerOf(kind));
        } else if (m_watch_design.Release(m_watch_design.ToDocument(at))) {
            m_watch_apply = true;
        }
    } else if (pressed) {
        m_keyboard_design.Press(at);
    } else {
        m_keyboard_design.Release(at);
    }
    Touch();
}

auto Editor::PreviewScroll(const Eigen::Vector2f&, float) -> void {
}

auto Editor::PanelReleased(PanelKind from, const PointerHit& hit) -> bool {
    if (!m_carrying || from != PanelKind::Palette) {
        return false;
    }
    if (hit.overlay == m_preview_id || std::ranges::contains(m_layer_ids, std::optional(hit.overlay))) {
        const Eigen::Vector2f size = PanelSize(PanelKind::Preview);
        Drop(hit.uv.cwiseProduct(size));
    } else if (hit.overlay == m_palette_id && g_input->Pointers().at(hit.pointer).Interaction().hovered_id == m_palette_id) {
        Drop(std::nullopt);
    }
    m_carrying.reset();
    m_watch_design.Carrying().reset();
    m_keyboard_design.Carrying().reset();
    m_owner.reset();
    Touch();
    return true;
}

auto Editor::StartCarry(PaletteItem item) -> void {
    m_carrying = item;
    if (m_owner) {
        m_carry_pointer = *m_owner;
    } else {
        for (const auto& pointer : g_input->Pointers()) {
            if (pointer.Now().click) {
                m_carry_pointer = pointer.Index();
            }
        }
    }
    Touch();
}

auto Editor::Drop(std::optional<Eigen::Vector2f> point) -> void {
    if (!m_carrying) {
        return;
    }
    if (const auto* part = std::get_if<FormPart>(&*m_carrying)) {
        if (m_target == EditTarget::Keyboard) {
            return;
        }
        std::optional<Eigen::Vector2f> at;
        if (point) {
            at = m_watch_design.ToDocument(*point);
        }
        m_watch_design.Insert(*part, at);
        m_watch_apply = true;
    } else if (const auto* key_template = std::get_if<KeyTemplate>(&*m_carrying)) {
        if (m_target == EditTarget::Watch) {
            return;
        }
        auto cell = std::pair{0.0f, m_keyboard_design.Document().Rows()};
        if (point) {
            cell = m_keyboard_design.DropCell(*point);
        } else if (const auto free = m_keyboard_design.Document().FreeCells(); !free.empty() && *key_template == KeyTemplate::Key) {
            cell = free.front();
        }
        if (!m_keyboard_design.Place(*key_template, cell.first, cell.second)) {
            Notify("No room for that there");
        }
    }
    Touch();
}

auto Editor::SwitchTarget(EditTarget next) -> void {
    if (m_target == next) {
        return;
    }
    m_target = next;
    m_watch_design.Hovered().reset();
    m_dialog = DialogKind::None;
    m_carrying.reset();
    Touch();
}

auto Editor::Undo() -> void {
    if (m_target == EditTarget::Theme) {
        if (m_theme_history.Undo()) {
            ApplyTheme();
        }
    } else if (m_target == EditTarget::Watch) {
        if (m_watch_design.Revisions().Undo()) {
            m_watch_design.Selected().reset();
            m_watch_apply = true;
        }
    } else if (m_keyboard_design.Revisions().Undo()) {
        m_keyboard_design.Selected().reset();
    }
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Redo() -> void {
    if (m_target == EditTarget::Theme) {
        if (m_theme_history.Redo()) {
            ApplyTheme();
        }
    } else if (m_target == EditTarget::Watch) {
        if (m_watch_design.Revisions().Redo()) {
            m_watch_design.Selected().reset();
            m_watch_apply = true;
        }
    } else if (m_keyboard_design.Revisions().Redo()) {
        m_keyboard_design.Selected().reset();
    }
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::EditWatch(const std::function<void(ModularUiConfig&)>& change) -> void {
    if (m_amend) {
        m_watch_design.Revisions().Amend(change);
    } else {
        m_watch_design.Revisions().Edit(change);
    }
    m_watch_apply = true;
    Touch();
}

auto Editor::EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void {
    if (m_amend) {
        m_keyboard_design.Revisions().Amend(change);
    } else {
        m_keyboard_design.Revisions().Edit(change);
    }
    Touch();
}

auto Editor::ApplyWatch() -> void {
    m_watch_apply = false;
    if (auto* live = g_overlays->ByName(kWatchName)) {
        if (const auto applied = Watch::Apply(*live, m_watch_design.Document()); !applied) {
            LOG_ERROR("Could not apply watch layout: {}", applied.error().Message());
        }
    }
}

auto Editor::EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void {
    if (m_amend) {
        m_theme_history.Amend(change);
    } else {
        m_theme_history.Edit(change);
    }
    ApplyTheme();
    Touch();
}

auto Editor::ApplyTheme() -> void {
    g_themes->SetActive(m_theme_history.Current());
    m_watch_apply = true;
    m_restyle_keyboard = true;
}

auto Editor::ArmDelete() -> bool {
    const auto now = std::chrono::steady_clock::now();
    if (!m_delete_armed || now - *m_delete_armed > kRevertWindow) {
        m_delete_armed = now;
        Touch();
        return false;
    }
    m_delete_armed.reset();
    return true;
}

auto Editor::SelectWatch(const std::string& name) -> void {
    if (name == m_watch_profile) {
        return;
    }
    if (m_watch_design.Revisions().Dirty()) {
        Notify("Save or revert this watch first");
        return;
    }
    m_watch_profile = name;
    m_watch_design.Revisions().Reset(Watch::LoadConfig(name));
    m_watch_design.Selected().reset();
    m_watch_design.SelectedForm().reset();
    g_config->watch_profile = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved) {
        Notify(saved.error().Message());
    }
    m_watch_apply = true;
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::NewWatch() -> void {
    if (m_watch_design.Revisions().Dirty()) {
        Notify("Save or revert this watch first");
        return;
    }
    const auto profiles = Watch::Profiles();
    std::string name = "watch-2";
    for (int number = 3; std::ranges::contains(profiles, name); ++number) {
        name = std::format("watch-{}", number);
    }
    if (const auto written = WritePanelText(Watch::UserFile(name), Yaml::Emit(nlohmann::ordered_json(m_watch_design.Document())));
        !written) {
        Notify(written.error().Message());
        return;
    }
    SelectWatch(name);
    Notify(std::format("Created watch layout {}", name));
}

auto Editor::RenameWatch(const std::string& name) -> void {
    if (name.empty() || name == m_watch_profile || Watch::IsBuiltIn(m_watch_profile)
        || name.find_first_of("/\\.") != std::string::npos) {
        return;
    }
    if (std::ranges::contains(Watch::Profiles(), name)) {
        Notify(std::format("{} already exists", name));
        return;
    }
    if (const auto written = WritePanelText(Watch::UserFile(name), Yaml::Emit(nlohmann::ordered_json(m_watch_design.Document())));
        !written) {
        Notify(written.error().Message());
        return;
    }
    std::error_code error;
    std::filesystem::remove(Watch::UserFile(m_watch_profile), error);
    m_watch_profile = name;
    m_watch_design.Revisions().MarkSaved();
    g_config->watch_profile = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved) {
        Notify(saved.error().Message());
    }
    Touch();
}

auto Editor::DeleteWatch() -> void {
    if (Watch::IsBuiltIn(m_watch_profile) || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(Watch::UserFile(m_watch_profile), error);
    Notify(std::format("Deleted watch layout {}", m_watch_profile));
    m_watch_design.Revisions().Reset(ModularUiConfig(m_watch_design.Document()));
    SelectWatch(std::string(Watch::kDefaultProfile));
}

auto Editor::SelectKeyboard(const std::string& name) -> void {
    if (name == m_keyboard_file_name) {
        return;
    }
    if (m_keyboard_design.Revisions().Dirty()) {
        Notify("Save or revert this keyboard first");
        return;
    }
    m_keyboard_design.Revisions().Reset(g_keyboard_profiles->Find(name));
    m_keyboard_file_name = m_keyboard_design.Document().Name();
    m_keyboard_design.Selected().reset();
    g_config->keyboard_profile = m_keyboard_file_name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved) {
        Notify(saved.error().Message());
    }
    ApplyKeyboard();
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::DeleteKeyboard() -> void {
    const auto& layout = m_keyboard_design.Document();
    if (KeyboardProfiles::IsBuiltIn(layout.Name()) || layout.Source().empty() || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(layout.Source(), error);
    Notify(std::format("Deleted keyboard {}", layout.Name()));
    g_keyboard_profiles->Reload();
    m_keyboard_design.Revisions().Reset(KeyboardLayout(layout));
    m_keyboard_file_name.clear();
    SelectKeyboard(g_keyboard_profiles->All().front().Name());
}

auto Editor::SelectTheme(const std::string& name) -> void {
    if (name == m_theme_history.Current().name) {
        return;
    }
    if (m_theme_history.Dirty()) {
        Notify("Save or revert this theme first");
        return;
    }
    m_theme_history.Reset(g_themes->Find(name));
    g_config->theme = m_theme_history.Current().name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved) {
        Notify(saved.error().Message());
    }
    ApplyTheme();
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::NewTheme() -> void {
    if (m_theme_history.Dirty()) {
        Notify("Save or revert this theme first");
        return;
    }
    std::string name = "my-theme";
    for (int number = 2; std::ranges::contains(g_themes->All(), name, &ThemeDefinition::name); ++number) {
        name = std::format("my-theme-{}", number);
    }
    auto copy = m_theme_history.Current();
    copy.name = name;
    if (const auto written = WritePanelText(
            Theme::UserDirectory() / std::format("{}.yaml", name),
            Yaml::Emit(nlohmann::ordered_json(copy))
        );
        !written) {
        Notify(written.error().Message());
        return;
    }
    g_themes->Reload();
    SelectTheme(name);
    Notify(std::format("Created theme {}", name));
}

auto Editor::RenameTheme(const std::string& name) -> void {
    const auto& current = m_theme_history.Current();
    if (name.empty() || name == current.name || Theme::IsBuiltIn(current.name) || name.find_first_of("/\\.") != std::string::npos) {
        return;
    }
    if (std::ranges::contains(g_themes->All(), name, &ThemeDefinition::name)) {
        Notify(std::format("{} already exists", name));
        return;
    }
    auto renamed = current;
    renamed.name = name;
    if (const auto written = WritePanelText(
            Theme::UserDirectory() / std::format("{}.yaml", name),
            Yaml::Emit(nlohmann::ordered_json(renamed))
        );
        !written) {
        Notify(written.error().Message());
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    g_themes->Reload();
    m_theme_history.Reset(g_themes->Find(name));
    g_config->theme = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved) {
        Notify(saved.error().Message());
    }
    ApplyTheme();
    Touch();
}

auto Editor::DeleteTheme() -> void {
    const auto& current = m_theme_history.Current();
    if (Theme::IsBuiltIn(current.name) || current.source.empty() || !ArmDelete()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    Notify(std::format("Deleted theme {}", current.name));
    g_themes->Reload();
    m_theme_history.Reset(ThemeDefinition(current));
    SelectTheme(g_themes->All().front().name);
}

auto Editor::ApplyKeyboard() -> void {
    const auto& name = m_keyboard_design.Document().Name();
    Keyboard::SwitchProfile(g_input->TypingKeymap(), name);
}

auto Editor::Save() -> void {
    if (m_target == EditTarget::Theme) {
        auto saved = m_theme_history.Current();
        const auto path = Theme::UserDirectory() / std::format("{}.yaml", saved.name);
        if (const auto written = WritePanelText(path, Yaml::Emit(nlohmann::ordered_json(saved))); !written) {
            Notify(written.error().Message());
            return;
        }
        m_theme_history.MarkSaved();
        g_themes->Reload();
        SaveDrafts(true);
        Notify(std::format("Theme {} saved", saved.name));
        Touch();
        return;
    }
    if (m_target == EditTarget::Watch) {
        const auto text = Yaml::Emit(nlohmann::ordered_json(m_watch_design.Document()));
        if (const auto written = WritePanelText(Watch::UserFile(m_watch_profile), text); !written) {
            Notify(written.error().Message());
            return;
        }
        m_watch_design.Revisions().MarkSaved();
        SaveDrafts(true);
        Notify("Watch saved");
        Touch();
        return;
    }
    auto& layout = m_keyboard_design.Revisions().Live();
    if (const auto valid = layout.Validate(); !valid) {
        Notify(valid.error().Message());
        return;
    }
    std::filesystem::path path = layout.Source();
    if (path.empty() || FileProfileName(path) != layout.Name()) {
        path = KeyboardProfiles::UserDirectory() / std::format("{}.yaml", layout.Name());
    }
    if (const auto written = WritePanelText(path, Yaml::Emit(nlohmann::ordered_json(layout))); !written) {
        Notify(written.error().Message());
        return;
    }
    layout.SetSource(path);
    m_keyboard_file_name = layout.Name();
    m_keyboard_design.Revisions().MarkSaved();
    g_keyboard_profiles->Reload();
    ApplyKeyboard();
    SaveDrafts(true);
    Notify(std::format("Keyboard {} saved", layout.Name()));
    Touch();
}

auto Editor::SaveAsNew() -> void {
    const auto base = m_keyboard_design.Document().Name();
    std::string name = std::format("{}-custom", base);
    for (int number = 2; std::ranges::contains(g_keyboard_profiles->All(), name, &KeyboardLayout::Name); ++number) {
        name = std::format("{}-custom-{}", base, number);
    }
    EditKeyboard([&name](KeyboardLayout& layout) -> void {
        layout.SetName(name);
        layout.Source().clear();
    });
    Save();
}

auto Editor::Revert() -> void {
    const auto now = std::chrono::steady_clock::now();
    if (!m_revert_armed || now - *m_revert_armed > kRevertWindow) {
        m_revert_armed = now;
        Touch();
        return;
    }
    m_revert_armed.reset();
    std::error_code error;
    if (m_target == EditTarget::Theme) {
        const auto name = m_theme_history.Current().name;
        if (Theme::IsBuiltIn(name)) {
            std::filesystem::remove(Theme::UserDirectory() / std::format("{}.yaml", name), error);
        }
        g_themes->Reload();
        m_theme_history.Reset(g_themes->Find(name));
        ApplyTheme();
        Notify(std::format("Theme {} reverted", name));
    } else if (m_target == EditTarget::Watch) {
        if (Watch::IsBuiltIn(m_watch_profile)) {
            std::filesystem::remove(Watch::UserFile(m_watch_profile), error);
        }
        m_watch_design.Revisions().Reset(Watch::LoadConfig(m_watch_profile));
        m_watch_design.Selected().reset();
        m_watch_apply = true;
        Notify("Watch reverted");
    } else {
        const auto& layout = m_keyboard_design.Document();
        const std::string name = m_keyboard_file_name;
        if (!layout.Source().empty()) {
            std::filesystem::remove(layout.Source(), error);
        }
        g_keyboard_profiles->Reload();
        m_keyboard_design.Revisions().Reset(g_keyboard_profiles->Find(name));
        m_keyboard_file_name = m_keyboard_design.Document().Name();
        m_keyboard_design.Selected().reset();
        ApplyKeyboard();
        Notify(std::format("Keyboard {} reverted", m_keyboard_design.Document().Name()));
    }
    SaveDrafts(true);
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Done() -> void {
    CommitText();
    if (Dirty()) {
        m_closing_prompt = true;
        Touch();
        return;
    }
    Close();
}

auto Editor::Discard() -> void {
    m_keyboard_design.Revisions().Reset(KeyboardLayout(m_keyboard_design.Document()));
    if (m_theme_history.Dirty()) {
        m_theme_history.Reset(g_themes->Find(m_theme_history.Current().name));
        ApplyTheme();
    }
    if (m_watch_design.Revisions().Dirty()) {
        m_watch_design.Revisions().Reset(Watch::LoadConfig(m_watch_profile));
        m_watch_apply = true;
    }
    Close();
}

auto Editor::Close() -> void {
    EndText();
    SaveDrafts(true);
    m_closing_prompt = false;
    m_closing = true;
}

auto Editor::ShowKeyboard() -> void {
    g_tasks->Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [](OverlayState& overlay) -> void {
                if (!overlay.WantVisible()) {
                    overlay.SetWantVisible(true);
                    overlay.Reset(false);
                }
            },
        }
    );
}

auto Editor::BeginText(std::string id, std::string initial, std::function<void(Editor&, const std::string&)> commit) -> void {
    if (m_focus && m_focus->id != id) {
        auto previous = std::move(*m_focus);
        m_focus.reset();
        previous.commit(*this, previous.buffer);
    }
    m_focus = TextFocus{.id = std::move(id), .buffer = std::move(initial), .commit = std::move(commit)};
    m_capture.reset();
    g_input->SetFocusedTextInput(m_input);
    ShowKeyboard();
    Touch();
}

auto Editor::BeginCapture(bool shortcut, std::function<void(Editor&, const std::vector<VirtualKey>&)> done) -> void {
    m_focus.reset();
    m_capture = KeyCapture{.shortcut = shortcut, .done = std::move(done)};
    g_input->SetFocusedTextInput(m_input);
    ShowKeyboard();
    Touch();
}

auto Editor::EndText() -> void {
    m_focus.reset();
    m_capture.reset();
    if (g_input->FocusedTextInput() == m_input) {
        g_input->SetFocusedTextInput(nullptr);
    }
    Touch();
}

auto Editor::CommitText() -> void {
    if (!m_focus) {
        return;
    }
    auto focus = std::move(*m_focus);
    EndText();
    focus.commit(*this, focus.buffer);
}

auto Editor::OnKey(VirtualKey key, std::string_view text) -> void {
    if (m_capture) {
        if (m_capture->shortcut && VirtualKeys::ModifierOf(key) && !Keyboard::IsLatchingKey(key)) {
            auto& held = m_capture->held;
            if (const auto found = std::ranges::find(held, key); found != held.end()) {
                held.erase(found);
            } else {
                held.push_back(key);
            }
            Touch();
            return;
        }
        auto keys = m_capture->held;
        keys.push_back(key);
        auto done = std::move(m_capture->done);
        EndText();
        done(*this, keys);
        Touch();
        return;
    }
    if (!m_focus) {
        return;
    }
    switch (key) {
    case VirtualKey::Return:
    case VirtualKey::KpEnter: {
        auto focus = std::move(*m_focus);
        EndText();
        focus.commit(*this, focus.buffer);
        break;
    }
    case VirtualKey::Escape: EndText(); break;
    case VirtualKey::BackSpace: PopCodepoint(m_focus->buffer); break;
    default: m_focus->buffer += text; break;
    }
    Touch();
}

auto Editor::OpenColor(ColorDialog dialog) -> void {
    m_color = std::move(dialog);
    m_dialog = DialogKind::Color;
    Touch();
}

auto Editor::OpenActions(ButtonTrigger trigger) -> void {
    m_trigger = trigger;
    m_dialog = DialogKind::Actions;
    Touch();
}

auto Editor::OpenTimezones(size_t element) -> void {
    m_timezone_element = element;
    m_timezone_query.clear();
    m_dialog = DialogKind::Timezone;
    Touch();
}

auto Editor::OpenKeyPicker() -> void {
    m_dialog = DialogKind::KeyPicker;
    Touch();
}

auto Editor::CloseDialog() -> void {
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Notify(std::string text) -> void {
    ::ToastOverlay::Show(Toast{.topic = ToastTopic::System, .title = std::move(text), .timeout = 3.0f});
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
