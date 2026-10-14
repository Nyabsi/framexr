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
#include <filesystem>
#include <format>
#include <span>
#include <system_error>
#include <utility>

#include "ClockComponent.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "FontCache.hpp"
#include "Form.hpp"
#include "Forms.hpp"
#include "Interaction.hpp"
#include "KeyboardLayout.hpp"
#include "Logger.hpp"
#include "Toast.hpp"
#include "Watch.hpp"
#include "views/Keyboard.hpp"

namespace
{

constexpr auto kArmWindow = std::chrono::seconds(4);
constexpr auto kRestyleInterval = std::chrono::milliseconds(300);

auto SampleText(const FormElement& element) -> std::string
{
    if (std::holds_alternative<ExecElement>(element))
    {
        return "Output";
    }
    Form scratch;
    Form::Instantiate(scratch, std::span(&element, 1), {.theme = g_themes->Active()});
    scratch.UpdateAll();
    for (const auto& item : scratch.Build())
    {
        if (const auto* text = std::get_if<TextItem>(&item); text != nullptr && !text->text.empty())
        {
            return text->text;
        }
    }
    return std::holds_alternative<BatteryElement>(element) ? std::string("R99") : std::string("100");
}

} // namespace

auto Editor::Create() -> std::shared_ptr<Editor>
{
    auto editor = std::make_shared<Editor>(
        Forms::Watch(g_config->watch_profile),
        KeyboardLayout::Find(g_config->keyboard_profile),
        g_config->watch_profile
    );
    if (editor->m_drafts.Restore(*editor))
    {
        Notify("Picked up your unsaved edits where you left off");
    }
    return editor;
}

Editor::Editor(FormDocument watch, KeyboardLayout keyboard, std::string watch_profile)
    : m_watch_design(std::move(watch)),
      m_keyboard_design(std::move(keyboard)),
      m_theme_history(g_themes->Active()),
      m_text(std::make_shared<TextEntry>()),
      m_keyboard_file(m_keyboard_design.Document().Name()),
      m_watch_profile(std::move(watch_profile))
{
    m_text->SetOnChange([this]() -> void { Touch(); });
    m_watch_design.measure = [](std::string_view text, int font_size) -> float { return FontCache::Width(text, font_size); };
    m_watch_design.city_text = [](const ClockElement& clock) -> std::string { return ClockComponent::City(clock.timezone); };
    m_watch_design.label_text = SampleText;
}

auto Editor::Dirty() const -> bool
{
    return m_watch_design.Revisions().Dirty() || m_keyboard_design.Revisions().Dirty() || m_theme_history.Dirty();
}

auto Editor::Revision(PanelKind kind) const -> uint64_t
{
    return kind == PanelKind::Preview || PanelPlacement::LayerOf(kind) ? m_preview_revision : m_revision;
}

auto Editor::PanelSize(PanelKind kind) const -> Eigen::Vector2f
{
    switch (kind)
    {
    case PanelKind::Preview:
        return m_target == EditTarget::Keyboard ? m_keyboard_design.PreviewSize() : m_watch_design.PreviewSize();
    case PanelKind::Toolbar: return {1040.0f, 58.0f};
    case PanelKind::Palette: return {240.0f, 420.0f};
    case PanelKind::Properties: return {360.0f, 820.0f};
    case PanelKind::Dialog: return {380.0f, 820.0f};
    case PanelKind::Ghost: return {220.0f, 52.0f};
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: break;
    }
    return m_watch_design.PreviewSize();
}

auto Editor::Accepts(PanelKind kind, const Eigen::Vector2f& point) const -> bool
{
    const auto layer = PanelPlacement::LayerOf(kind);
    return !layer || m_watch_design.Accepts(m_watch_design.ToDocument(point), *layer);
}

auto Editor::Lines() const -> std::vector<EditorLine>
{
    return m_placement.Lines(*this);
}

auto Editor::Open() -> void
{
    m_placement.Open(shared_from_this());
}

auto Editor::Update() -> void
{
    if (m_watch_apply)
    {
        ApplyWatch();
    }
    if (m_restore_saved_watch)
    {
        m_restore_saved_watch = false;
        if (auto* live = dynamic_cast<Watch*>(g_container->ByName(kWatchName)))
        {
            if (const auto applied = live->Apply(Forms::Watch(m_watch_profile)); !applied)
            {
                LOG_ERROR("Could not restore the saved watch: {}", applied.error().Message());
            }
        }
    }
    if (!m_closing)
    {
        m_drafts.Save(*this, false);
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_restyle_keyboard && !m_colors.Dragging() && now - m_last_restyle > kRestyleInterval)
    {
        m_restyle_keyboard = false;
        m_last_restyle = now;
        Keyboard::SwitchProfile(g_interaction->TypingKeymap(), g_config->keyboard_profile);
    }
    if (m_closing)
    {
        if (!m_finished)
        {
            m_placement.Close();
            m_text->End();
            m_finished = true;
        }
        return;
    }
    if (m_owner && !m_carrying && *m_owner < g_interaction->Pointers().size()
        && !g_interaction->Pointers().at(*m_owner).Now().click)
    {
        m_owner.reset();
    }
    if (m_revert_armed && now - *m_revert_armed > kArmWindow)
    {
        m_revert_armed.reset();
        Touch();
    }
    m_placement.Place(*this);
}

auto Editor::SetHidden(bool hidden) -> void
{
    m_hidden = hidden;
    if (hidden)
    {
        m_carrying.reset();
        m_owner.reset();
    }
}

auto Editor::Claim(size_t pointer) -> void
{
    m_owner = pointer;
}

auto Editor::Unclaim(size_t pointer) -> void
{
    if (m_owner == pointer && !m_carrying)
    {
        m_owner.reset();
    }
}

auto Editor::PreviewHover(const Eigen::Vector2f& point) -> std::optional<Haptics>
{
    m_last_point = point;
    if (m_target == EditTarget::Theme)
    {
        return std::nullopt;
    }
    if (m_target == EditTarget::Watch)
    {
        const auto before = m_watch_design.Hovered();
        if (m_carrying)
        {
            if (const auto* part = std::get_if<FormPart>(&*m_carrying))
            {
                m_watch_design.SetCarrying(std::pair{*part, m_watch_design.ToDocument(point)});
            }
        }
        const auto guides_before = m_watch_design.Guides().size();
        const bool engaged = m_watch_design.Engaged();
        m_watch_design.Hover(m_watch_design.ToDocument(point));
        if (!engaged && m_watch_design.Engaged())
        {
            ++m_preview_revision;
            return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
        }
        if (m_watch_design.Dragging() || m_watch_design.Carrying() || m_watch_design.Hovered() != before)
        {
            ++m_preview_revision;
        }
        if (m_watch_design.Dragging() && m_watch_design.Guides().size() > guides_before)
        {
            return Haptics{.intensity = 0.15f, .duration = 0.01f, .frequency = 5.0f};
        }
        return std::nullopt;
    }
    const auto before = m_keyboard_design.Hovered();
    if (m_carrying)
    {
        if (const auto* key_template = std::get_if<KeyTemplate>(&*m_carrying))
        {
            m_keyboard_design.SetCarrying(std::pair{*key_template, point});
        }
    }
    const bool engaged = m_keyboard_design.Engaged();
    m_keyboard_design.Hover(point);
    if (m_keyboard_design.Carrying() || m_keyboard_design.Hovered() != before || m_keyboard_design.Engaged())
    {
        ++m_preview_revision;
    }
    if (!engaged && m_keyboard_design.Engaged())
    {
        return Haptics{.intensity = 0.4f, .duration = 0.04f, .frequency = 120.0f};
    }
    return std::nullopt;
}

auto Editor::PreviewLeft() -> void
{
    m_watch_design.Hovered().reset();
    m_watch_design.Carrying().reset();
    m_keyboard_design.Hovered().reset();
    m_keyboard_design.Carrying().reset();
    ++m_preview_revision;
}

auto Editor::PreviewPointer(PanelKind kind, const PointerHit& hit, const Eigen::Vector2f& point, bool pressed) -> void
{
    const Eigen::Vector2f at = pressed || m_placement.ShowsPreview(hit.overlay) ? point : m_last_point;
    if (pressed)
    {
        m_text->Commit();
    }
    if (m_target == EditTarget::Theme)
    {
        return;
    }
    if (m_target == EditTarget::Watch)
    {
        if (pressed)
        {
            m_watch_design.Press(m_watch_design.ToDocument(at), PanelPlacement::LayerOf(kind));
        }
        else if (m_watch_design.Release(m_watch_design.ToDocument(at)))
        {
            m_watch_apply = true;
        }
    }
    else if (pressed)
    {
        m_keyboard_design.Press(at);
    }
    else
    {
        m_keyboard_design.Release(at);
    }
    Touch();
}

auto Editor::PanelReleased(PanelKind from, const PointerHit& hit) -> bool
{
    if (!m_carrying || from != PanelKind::Palette)
    {
        return false;
    }
    if (m_placement.ShowsPreview(hit.overlay))
    {
        Drop(hit.uv.cwiseProduct(PanelSize(PanelKind::Preview)));
    }
    else if (
        m_placement.ShowsPalette(hit.overlay)
        && m_placement.ShowsPalette(g_interaction->Pointers().at(hit.pointer).Interaction().hovered_id.value_or(OverlayId{}))
    )
    {
        Drop(std::nullopt);
    }
    m_carrying.reset();
    m_watch_design.Carrying().reset();
    m_keyboard_design.Carrying().reset();
    m_owner.reset();
    Touch();
    return true;
}

auto Editor::SwitchTarget(EditTarget next) -> void
{
    if (m_target == next)
    {
        return;
    }
    m_target = next;
    m_watch_design.Hovered().reset();
    m_dialog = DialogKind::None;
    m_carrying.reset();
    Touch();
}

auto Editor::SetExploded(bool exploded) -> void
{
    m_exploded = exploded;
    m_watch_design.Hovered().reset();
    Touch();
}

auto Editor::StartCarry(PaletteItem item) -> void
{
    m_carrying = item;
    if (m_owner)
    {
        m_carry_pointer = *m_owner;
    }
    else
    {
        for (const auto& pointer : g_interaction->Pointers())
        {
            if (pointer.Now().click)
            {
                m_carry_pointer = pointer.Index();
            }
        }
    }
    Touch();
}

auto Editor::Drop(std::optional<Eigen::Vector2f> point) -> void
{
    if (!m_carrying)
    {
        return;
    }
    if (const auto* part = std::get_if<FormPart>(&*m_carrying))
    {
        if (m_target == EditTarget::Keyboard)
        {
            return;
        }
        std::optional<Eigen::Vector2f> at;
        if (point)
        {
            at = m_watch_design.ToDocument(*point);
        }
        m_watch_design.Insert(*part, at);
        m_watch_apply = true;
    }
    else if (const auto* key_template = std::get_if<KeyTemplate>(&*m_carrying))
    {
        if (m_target == EditTarget::Watch)
        {
            return;
        }
        auto cell = std::pair{0.0f, m_keyboard_design.Document().Rows()};
        if (point)
        {
            cell = m_keyboard_design.DropCell(*point);
        }
        else if (const auto free = m_keyboard_design.Document().FreeCells(); !free.empty() && *key_template == KeyTemplate::Key)
        {
            cell = free.front();
        }
        if (!m_keyboard_design.Place(*key_template, cell.first, cell.second))
        {
            Notify("No room for that there");
        }
    }
    Touch();
}

auto Editor::Undo() -> void
{
    if (m_target == EditTarget::Theme)
    {
        if (m_theme_history.Undo())
        {
            ApplyTheme();
        }
    }
    else if (m_target == EditTarget::Watch)
    {
        if (m_watch_design.Revisions().Undo())
        {
            m_watch_design.Selected().reset();
            m_watch_apply = true;
        }
    }
    else if (m_keyboard_design.Revisions().Undo())
    {
        m_keyboard_design.Selected().reset();
    }
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Redo() -> void
{
    if (m_target == EditTarget::Theme)
    {
        if (m_theme_history.Redo())
        {
            ApplyTheme();
        }
    }
    else if (m_target == EditTarget::Watch)
    {
        if (m_watch_design.Revisions().Redo())
        {
            m_watch_design.Selected().reset();
            m_watch_apply = true;
        }
    }
    else if (m_keyboard_design.Revisions().Redo())
    {
        m_keyboard_design.Selected().reset();
    }
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::EditWatch(const std::function<void(FormDocument&)>& change) -> void
{
    if (m_amend)
    {
        m_watch_design.Revisions().Amend(change);
    }
    else
    {
        m_watch_design.Revisions().Edit(change);
    }
    WatchChanged();
}

auto Editor::EditKeyboard(const std::function<void(KeyboardLayout&)>& change) -> void
{
    if (m_amend)
    {
        m_keyboard_design.Revisions().Amend(change);
    }
    else
    {
        m_keyboard_design.Revisions().Edit(change);
    }
    Touch();
}

auto Editor::EditTheme(const std::function<void(ThemeDefinition&)>& change) -> void
{
    if (m_amend)
    {
        m_theme_history.Amend(change);
    }
    else
    {
        m_theme_history.Edit(change);
    }
    ApplyTheme();
    Touch();
}

auto Editor::WatchChanged() -> void
{
    m_watch_apply = true;
    Touch();
}

auto Editor::SetKeyCategory(size_t category) -> void
{
    m_key_category = category;
    Touch();
}

auto Editor::ApplyWatch() -> void
{
    m_watch_apply = false;
    if (auto* live = dynamic_cast<Watch*>(g_container->ByName(kWatchName)))
    {
        if (const auto applied = live->Apply(m_watch_design.Document()); !applied)
        {
            LOG_ERROR("Could not apply watch layout: {}", applied.error().Message());
        }
    }
}

auto Editor::ApplyTheme() -> void
{
    g_themes->SetActive(m_theme_history.Current());
    if (g_fonts != nullptr)
    {
        g_fonts->SetFamily(m_theme_history.Current().font);
    }
    m_watch_apply = true;
    m_restyle_keyboard = true;
}

auto Editor::ApplyKeyboard() -> void
{
    Keyboard::SwitchProfile(g_interaction->TypingKeymap(), m_keyboard_design.Document().Name());
}

auto Editor::ArmDelete() -> bool
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_delete_armed || now - *m_delete_armed > kArmWindow)
    {
        m_delete_armed = now;
        Touch();
        return false;
    }
    m_delete_armed.reset();
    return true;
}

auto Editor::SelectWatch(const std::string& name) -> void
{
    if (name == m_watch_profile)
    {
        return;
    }
    if (m_watch_design.Revisions().Dirty())
    {
        Notify("Save or revert this watch first");
        return;
    }
    m_watch_profile = name;
    m_watch_design.Revisions().Reset(Forms::Watch(name));
    m_watch_design.Selected().reset();
    m_watch_design.SelectedForm().reset();
    g_config->watch_profile = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved)
    {
        Notify(saved.error().Message());
    }
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    WatchChanged();
}

auto Editor::NewWatch() -> void
{
    if (m_watch_design.Revisions().Dirty())
    {
        Notify("Save or revert this watch first");
        return;
    }
    const auto profiles = Forms::WatchProfiles();
    std::string name = "watch-2";
    for (int number = 3; std::ranges::contains(profiles, name); ++number)
    {
        name = std::format("watch-{}", number);
    }
    if (const auto written = Forms::SaveWatch(name, m_watch_design.Document()); !written)
    {
        Notify(written.error().Message());
        return;
    }
    SelectWatch(name);
    Notify(std::format("Created watch layout {}", name));
}

auto Editor::RenameWatch(const std::string& name) -> void
{
    if (name.empty() || name == m_watch_profile || Forms::IsBuiltInWatch(m_watch_profile)
        || name.find_first_of("/\\.") != std::string::npos)
    {
        return;
    }
    if (std::ranges::contains(Forms::WatchProfiles(), name))
    {
        Notify(std::format("{} already exists", name));
        return;
    }
    if (const auto written = Forms::SaveWatch(name, m_watch_design.Document()); !written)
    {
        Notify(written.error().Message());
        return;
    }
    Forms::DeleteWatch(m_watch_profile);
    m_watch_profile = name;
    m_watch_design.Revisions().MarkSaved();
    g_config->watch_profile = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved)
    {
        Notify(saved.error().Message());
    }
    Touch();
}

auto Editor::DeleteWatch() -> void
{
    if (Forms::IsBuiltInWatch(m_watch_profile) || !ArmDelete())
    {
        return;
    }
    Forms::DeleteWatch(m_watch_profile);
    Notify(std::format("Deleted watch layout {}", m_watch_profile));
    m_watch_design.Revisions().Reset(FormDocument(m_watch_design.Document()));
    SelectWatch(std::string(Forms::kDefaultWatch));
}

auto Editor::SelectKeyboard(const std::string& name) -> void
{
    if (name == m_keyboard_file)
    {
        return;
    }
    if (m_keyboard_design.Revisions().Dirty())
    {
        Notify("Save or revert this keyboard first");
        return;
    }
    m_keyboard_design.Revisions().Reset(KeyboardLayout::Find(name));
    m_keyboard_file = m_keyboard_design.Document().Name();
    m_keyboard_design.Selected().reset();
    g_config->keyboard_profile = m_keyboard_file;
    if (const auto saved = ConfigStore::SaveSettings(); !saved)
    {
        Notify(saved.error().Message());
    }
    ApplyKeyboard();
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::DeleteKeyboard() -> void
{
    const auto& layout = m_keyboard_design.Document();
    if (KeyboardLayout::IsBuiltIn(layout.Name()) || layout.Source().empty() || !ArmDelete())
    {
        return;
    }
    std::error_code error;
    std::filesystem::remove(layout.Source(), error);
    Notify(std::format("Deleted keyboard {}", layout.Name()));
    KeyboardLayout::Reload();
    m_keyboard_design.Revisions().Reset(KeyboardLayout(layout));
    m_keyboard_file.clear();
    SelectKeyboard(KeyboardLayout::All().front().Name());
}

auto Editor::SelectTheme(const std::string& name) -> void
{
    if (name == m_theme_history.Current().name)
    {
        return;
    }
    if (m_theme_history.Dirty())
    {
        Notify("Save or revert this theme first");
        return;
    }
    m_theme_history.Reset(g_themes->Find(name));
    g_config->theme = m_theme_history.Current().name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved)
    {
        Notify(saved.error().Message());
    }
    ApplyTheme();
    m_delete_armed.reset();
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::NewTheme() -> void
{
    if (m_theme_history.Dirty())
    {
        Notify("Save or revert this theme first");
        return;
    }
    std::string name = "my-theme";
    for (int number = 2; std::ranges::contains(g_themes->All(), name, &ThemeDefinition::name); ++number)
    {
        name = std::format("my-theme-{}", number);
    }
    auto copy = m_theme_history.Current();
    copy.name = name;
    if (const auto written = FormTheme::Save(copy); !written)
    {
        Notify(written.error().Message());
        return;
    }
    g_themes->Reload();
    SelectTheme(name);
    Notify(std::format("Created theme {}", name));
}

auto Editor::RenameTheme(const std::string& name) -> void
{
    const auto& current = m_theme_history.Current();
    if (name.empty() || name == current.name || FormTheme::IsBuiltIn(current.name)
        || name.find_first_of("/\\.") != std::string::npos)
    {
        return;
    }
    if (std::ranges::contains(g_themes->All(), name, &ThemeDefinition::name))
    {
        Notify(std::format("{} already exists", name));
        return;
    }
    auto renamed = current;
    renamed.name = name;
    if (const auto written = FormTheme::Save(renamed); !written)
    {
        Notify(written.error().Message());
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    g_themes->Reload();
    m_theme_history.Reset(g_themes->Find(name));
    g_config->theme = name;
    if (const auto saved = ConfigStore::SaveSettings(); !saved)
    {
        Notify(saved.error().Message());
    }
    ApplyTheme();
    Touch();
}

auto Editor::DeleteTheme() -> void
{
    const auto& current = m_theme_history.Current();
    if (FormTheme::IsBuiltIn(current.name) || current.source.empty() || !ArmDelete())
    {
        return;
    }
    std::error_code error;
    std::filesystem::remove(current.source, error);
    Notify(std::format("Deleted theme {}", current.name));
    g_themes->Reload();
    m_theme_history.Reset(ThemeDefinition(current));
    SelectTheme(g_themes->All().front().name);
}

auto Editor::Save() -> void
{
    SaveTarget(m_target);
}

auto Editor::SaveAll() -> void
{
    if (m_watch_design.Revisions().Dirty())
    {
        SaveTarget(EditTarget::Watch);
    }
    if (m_keyboard_design.Revisions().Dirty())
    {
        SaveTarget(EditTarget::Keyboard);
    }
    if (m_theme_history.Dirty())
    {
        SaveTarget(EditTarget::Theme);
    }
    Close();
}

auto Editor::SaveTarget(EditTarget target) -> void
{
    if (target == EditTarget::Theme)
    {
        const auto& saved = m_theme_history.Current();
        if (const auto written = FormTheme::Save(saved); !written)
        {
            Notify(written.error().Message());
            return;
        }
        m_theme_history.MarkSaved();
        g_themes->Reload();
        m_drafts.Save(*this, true);
        Notify(std::format("Theme {} saved", m_theme_history.Current().name));
        Touch();
        return;
    }
    if (target == EditTarget::Watch)
    {
        if (const auto written = Forms::SaveWatch(m_watch_profile, m_watch_design.Document()); !written)
        {
            Notify(written.error().Message());
            return;
        }
        m_watch_design.Revisions().MarkSaved();
        m_drafts.Save(*this, true);
        Notify("Watch saved");
        Touch();
        return;
    }
    auto& layout = m_keyboard_design.Revisions().Live();
    if (const auto written = KeyboardLayout::Save(layout); !written)
    {
        Notify(written.error().Message());
        return;
    }
    m_keyboard_file = layout.Name();
    m_keyboard_design.Revisions().MarkSaved();
    ApplyKeyboard();
    m_drafts.Save(*this, true);
    Notify(std::format("Keyboard {} saved", layout.Name()));
    Touch();
}

auto Editor::SaveAsNew() -> void
{
    const auto base = m_keyboard_design.Document().Name();
    std::string name = std::format("{}-custom", base);
    for (int number = 2; std::ranges::contains(KeyboardLayout::All(), name, &KeyboardLayout::Name); ++number)
    {
        name = std::format("{}-custom-{}", base, number);
    }
    EditKeyboard(
        [&name](KeyboardLayout& layout) -> void
        {
            layout.SetName(name);
            layout.Source().clear();
        }
    );
    SaveTarget(EditTarget::Keyboard);
}

auto Editor::Revert() -> void
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_revert_armed || now - *m_revert_armed > kArmWindow)
    {
        m_revert_armed = now;
        Touch();
        return;
    }
    m_revert_armed.reset();
    std::error_code error;
    if (m_target == EditTarget::Theme)
    {
        const auto name = m_theme_history.Current().name;
        if (FormTheme::IsBuiltIn(name))
        {
            std::filesystem::remove(FormTheme::UserDirectory() / std::format("{}.yaml", name), error);
        }
        g_themes->Reload();
        m_theme_history.Reset(g_themes->Find(name));
        ApplyTheme();
        Notify(std::format("Theme {} reverted", name));
    }
    else if (m_target == EditTarget::Watch)
    {
        if (Forms::IsBuiltInWatch(m_watch_profile))
        {
            Forms::DeleteWatch(m_watch_profile);
        }
        m_watch_design.Revisions().Reset(Forms::Watch(m_watch_profile));
        m_watch_design.Selected().reset();
        m_watch_apply = true;
        Notify("Watch reverted");
    }
    else
    {
        const auto& layout = m_keyboard_design.Document();
        const std::string name = m_keyboard_file;
        if (!layout.Source().empty())
        {
            std::filesystem::remove(layout.Source(), error);
        }
        KeyboardLayout::Reload();
        m_keyboard_design.Revisions().Reset(KeyboardLayout::Find(name));
        m_keyboard_file = m_keyboard_design.Document().Name();
        m_keyboard_design.Selected().reset();
        ApplyKeyboard();
        Notify(std::format("Keyboard {} reverted", m_keyboard_design.Document().Name()));
    }
    m_drafts.Save(*this, true);
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Done() -> void
{
    m_text->Commit();
    if (Dirty())
    {
        m_closing_prompt = true;
        Touch();
        return;
    }
    Close();
}

auto Editor::KeepEditing() -> void
{
    m_closing_prompt = false;
    Touch();
}

auto Editor::Discard() -> void
{
    m_keyboard_design.Revisions().Reset(KeyboardLayout(m_keyboard_design.Document()));
    if (m_theme_history.Dirty())
    {
        m_theme_history.Reset(g_themes->Find(m_theme_history.Current().name));
        ApplyTheme();
    }
    if (m_watch_design.Revisions().Dirty())
    {
        m_watch_design.Revisions().Reset(Forms::Watch(m_watch_profile));
        m_watch_apply = true;
    }
    Close();
}

auto Editor::Suspend() -> void
{
    m_text->Commit();
    m_restore_saved_watch = m_watch_design.Revisions().Dirty();
    Close();
}

auto Editor::Close() -> void
{
    m_text->End();
    m_drafts.Save(*this, true);
    m_closing_prompt = false;
    m_closing = true;
}

auto Editor::OpenColor(ColorTarget target) -> void
{
    m_colors.Open(std::move(target));
    m_dialog = DialogKind::Color;
    Touch();
}

auto Editor::OpenActions(ButtonTrigger trigger) -> void
{
    m_trigger = trigger;
    m_dialog = DialogKind::Actions;
    Touch();
}

auto Editor::OpenKeyPicker() -> void
{
    m_dialog = DialogKind::KeyPicker;
    Touch();
}

auto Editor::OpenTimezones(size_t element) -> void
{
    m_timezone_element = element;
    m_timezone_query.clear();
    m_dialog = DialogKind::Timezone;
    Touch();
}

auto Editor::OpenFonts() -> void
{
    m_font_query.clear();
    m_dialog = DialogKind::Font;
    Touch();
}

auto Editor::CloseDialog() -> void
{
    m_dialog = DialogKind::None;
    Touch();
}

auto Editor::Touch() -> void
{
    ++m_revision;
    ++m_preview_revision;
}

auto Editor::Notify(std::string text) -> void
{
    Toast::Show(ToastMessage{.topic = ToastTopic::System, .title = std::move(text), .timeout = 3.0f});
}
