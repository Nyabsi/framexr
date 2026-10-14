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

#include "panels/WatchProperties.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <string>
#include <utility>

#include "ButtonComponent.hpp"
#include "ChoiceGridComponent.hpp"
#include "ColorFieldComponent.hpp"
#include "Editor.hpp"
#include "Forms.hpp"
#include "LaserInputComponent.hpp"
#include "RowComponent.hpp"
#include "SectionComponent.hpp"
#include "StackComponent.hpp"
#include "StepperComponent.hpp"
#include "SwatchComponent.hpp"
#include "TextLineComponent.hpp"
#include "document/FormElements.hpp"
#include "panels/ActionsPanel.hpp"
#include "panels/PanelStyle.hpp"
#include "panels/TimezonePanel.hpp"

namespace
{

auto PrettyType(const FormElement& element) -> std::string_view
{
    return std::visit(
        [](const auto& item) -> std::string_view
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>)
            {
                return "Panel";
            }
            else if constexpr (std::is_same_v<Item, LabelElement>)
            {
                return "Text";
            }
            else if constexpr (std::is_same_v<Item, ClockElement>)
            {
                return "Clock";
            }
            else if constexpr (std::is_same_v<Item, TimezoneElement>)
            {
                return "Zone name";
            }
            else if constexpr (std::is_same_v<Item, TimerElement>)
            {
                return "Timer";
            }
            else if constexpr (std::is_same_v<Item, BatteryElement>)
            {
                return "Battery";
            }
            else if constexpr (std::is_same_v<Item, ExecElement>)
            {
                return "Output";
            }
            else if constexpr (std::is_same_v<Item, IpdElement>)
            {
                return "IPD";
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierElement>)
            {
                return "Drag";
            }
            else if constexpr (std::is_same_v<Item, SpriteElement>)
            {
                return "Picture";
            }
            else if constexpr (std::is_same_v<Item, ButtonElement>)
            {
                return "Button";
            }
            else if constexpr (std::is_same_v<Item, BatteryListElement>)
            {
                return "Batteries";
            }
            else
            {
                return "Screens";
            }
        },
        element
    );
}

template <typename To>
auto Convert(FormElement& element, To target) -> void
{
    std::visit(
        [&target](const auto& item) -> void
        {
            target.rect = item.rect;
            target.region = item.region;
            target.form = item.form;
            if constexpr (requires { item.centered; })
            {
                target.font_size = item.font_size;
                target.fg_color = item.fg_color;
                target.centered = item.centered;
            }
        },
        element
    );
    element = std::move(target);
}

auto ElementEdit(Editor& editor, size_t index, std::function<void(FormElement&)> change) -> std::function<void()>
{
    return [&editor, index, change = std::move(change)]() -> void
    {
        editor.EditWatch(
            [&editor, index, &change](FormDocument& document) -> void
            {
                change(document.Components().at(index));
                editor.WatchDesign().Fit(document, index);
            }
        );
    };
}

} // namespace

WatchProperties::WatchProperties(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("watch"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap))
{
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto& design = editor.WatchDesign();
    if (const auto selected = design.Selected(); selected && *selected < design.Document().Components().size())
    {
        AddElement(editor, *selected);
    }
    else if (const auto form = design.SelectedForm(); form && *form > 0 && *form < design.Document().FormCount())
    {
        AddForm(editor, *form);
    }
    else
    {
        AddOverview(editor);
    }
}

auto WatchProperties::AddOverview(Editor& editor) -> void
{
    const auto& document = editor.WatchDesign().Document();
    const auto& profile = editor.WatchProfile();
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto red = FormTheme::Rgba("@red");

    m_stack.AddComponent<TextLineComponent>("title", "Watch", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);

    m_stack.AddComponent<SectionComponent>("layouts", "Watch layouts");
    const auto profiles = Forms::WatchProfiles();
    const auto current = std::ranges::find(profiles, profile);
    const auto selected = current == profiles.end() ? std::nullopt : std::optional(static_cast<size_t>(current - profiles.begin()));
    auto& list = m_stack.AddComponent<ChoiceGridComponent>("list", profiles, selected, 2, PanelStyle::kListRow);
    list.SetOnChoose([&editor, profiles](size_t index) -> void { editor.SelectWatch(profiles.at(index)); });

    auto& actions = m_stack.AddComponent<RowComponent>("actions", PanelStyle::kRowHeight, PanelStyle::kGap);
    auto& create = actions.AddComponent<ButtonComponent>("new", "New layout");
    create.SetFontSize(PanelStyle::kTextFont);
    create.SetFgColor(text);
    create.SetBgColor(surface);
    create.SetEllipsize(true);
    create.SetOnRelease([&editor]() -> void { editor.NewWatch(); });
    if (!Forms::IsBuiltInWatch(profile))
    {
        auto& remove = actions.AddComponent<ButtonComponent>("delete", editor.DeleteArmed() ? "Sure?" : "Delete");
        remove.SetFontSize(PanelStyle::kTextFont);
        remove.SetFgColor(on_accent);
        remove.SetBgColor(red);
        remove.SetEllipsize(true);
        remove.SetOnRelease([&editor]() -> void { editor.DeleteWatch(); });
        m_stack.AddComponent<LaserInputComponent>(
            "watch.name",
            "Name",
            editor.Text(),
            profile,
            [&editor](const std::string& typed) -> void { editor.RenameWatch(typed); }
        );
    }

    m_stack.AddComponent<SectionComponent>("size", "Size");
    auto& width = m_stack.AddComponent<StepperComponent>("width", "Width", std::format("{:.0f}", document.Form().width));
    width.Minus().SetOnRelease(
        [&editor]() -> void
        {
            editor.EditWatch(
                [&editor](FormDocument& edited) -> void
                { edited.Form().width = std::max(edited.Form().width - 10.0f, editor.WatchDesign().MinimumForm(edited).x()); }
            );
        }
    );
    width.Plus().SetOnRelease(
        [&editor]() -> void { editor.EditWatch([](FormDocument& edited) -> void { edited.Form().width += 10.0f; }); }
    );
    auto& height = m_stack.AddComponent<StepperComponent>("height", "Height", std::format("{:.0f}", document.Form().height));
    height.Minus().SetOnRelease(
        [&editor]() -> void
        {
            editor.EditWatch(
                [&editor](FormDocument& edited) -> void
                { edited.Form().height = std::max(edited.Form().height - 10.0f, editor.WatchDesign().MinimumForm(edited).y()); }
            );
        }
    );
    height.Plus().SetOnRelease(
        [&editor]() -> void { editor.EditWatch([](FormDocument& edited) -> void { edited.Form().height += 10.0f; }); }
    );
    auto& wrist = m_stack.AddComponent<StepperComponent>(
        "wrist",
        "On the wrist",
        std::format("{:.0f} mm", document.Width() * 1000.0f)
    );
    wrist.Minus().SetOnRelease(
        [&editor]() -> void
        { editor.EditWatch([](FormDocument& edited) -> void { edited.SetWidth(std::max(edited.Width() - 0.005f, 0.03f)); }); }
    );
    wrist.Plus().SetOnRelease(
        [&editor]() -> void { editor.EditWatch([](FormDocument& edited) -> void { edited.SetWidth(edited.Width() + 0.005f); }); }
    );
    m_stack.AddComponent<FormComponent>("spacer");

    if (const auto& toolbar = document.Form().toolbar)
    {
        m_stack.AddComponent<SectionComponent>("toolbar", "Toolbar");
        auto& bar = m_stack.AddComponent<StepperComponent>("toolbar-height", "Height", std::format("{:.0f}", toolbar->height));
        bar.Minus().SetOnRelease(
            [&editor]() -> void
            {
                editor.EditWatch(
                    [&editor](FormDocument& edited) -> void
                    {
                        const float minimum = editor.WatchDesign().MinimumToolbarHeight(edited);
                        edited.Form().toolbar->height = std::max(edited.Form().toolbar->height - 2.0f, minimum);
                    }
                );
            }
        );
        bar.Plus().SetOnRelease(
            [&editor]() -> void
            {
                editor.EditWatch(
                    [](FormDocument& edited) -> void
                    { edited.Form().toolbar->height = std::min(edited.Form().toolbar->height + 2.0f, 160.0f); }
                );
            }
        );

        const bool above = toolbar->position == ToolbarPosition::Above;
        auto& row = m_stack.AddComponent<RowComponent>("position", PanelStyle::kRowHeight, PanelStyle::kGap);

        auto& to_above = row.AddComponent<ButtonComponent>("above", "Above");
        to_above.SetFontSize(PanelStyle::kTextFont);
        to_above.SetFgColor(above ? on_accent : text);
        to_above.SetBgColor(above ? accent : surface);
        to_above.SetEllipsize(true);
        to_above.SetOnRelease(
            [&editor]() -> void
            { editor.EditWatch([](FormDocument& edited) -> void { edited.Form().toolbar->position = ToolbarPosition::Above; }); }
        );

        auto& to_below = row.AddComponent<ButtonComponent>("below", "Below");
        to_below.SetFontSize(PanelStyle::kTextFont);
        to_below.SetFgColor(above ? text : on_accent);
        to_below.SetBgColor(above ? surface : accent);
        to_below.SetEllipsize(true);
        to_below.SetOnRelease(
            [&editor]() -> void
            { editor.EditWatch([](FormDocument& edited) -> void { edited.Form().toolbar->position = ToolbarPosition::Below; }); }
        );

        auto& remove = row.AddComponent<ButtonComponent>("remove", "Remove");
        remove.SetFontSize(PanelStyle::kTextFont);
        remove.SetFgColor(text);
        remove.SetBgColor(surface);
        remove.SetEllipsize(true);
        remove.SetOnRelease(
            [&editor]() -> void
            {
                editor.EditWatch(
                    [](FormDocument& edited) -> void
                    {
                        std::erase_if(
                            edited.Components(),
                            [](const FormElement& element) -> bool { return FormElements::RegionOf(element) == Region::Toolbar; }
                        );
                        edited.Form().toolbar.reset();
                    }
                );
                editor.WatchDesign().Selected().reset();
            }
        );
    }
    else
    {
        auto& add = m_stack.AddComponent<ButtonComponent>("add-toolbar", "Add a toolbar");
        add.SetSize({0.0f, PanelStyle::kRowHeight});
        add.SetFontSize(PanelStyle::kTextFont);
        add.SetFgColor(text);
        add.SetBgColor(surface);
        add.SetEllipsize(true);
        add.SetOnRelease(
            [&editor]() -> void { editor.EditWatch([](FormDocument& edited) -> void { edited.Form().toolbar = ToolbarLayout{}; }); }
        );
    }
    m_stack.AddComponent<FormComponent>("spacer");
    m_stack.AddComponent<FormComponent>("spacer");
    m_stack.AddComponent<TextLineComponent>(
        "hint",
        "Pick something on the preview to edit it",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );
}

auto WatchProperties::AddForm(Editor& editor, size_t form) -> void
{
    const auto& extra = editor.WatchDesign().Document().Forms().at(form - 1);
    const auto subtle = FormTheme::Rgba("@subtle");

    m_stack.AddComponent<TextLineComponent>(
        "title",
        std::format("Form {}", form + 1),
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );

    auto& width = m_stack.AddComponent<StepperComponent>("width", "Width", std::format("{:.0f}", extra.width));
    width.Minus().SetOnRelease(
        [&editor, form]() -> void
        {
            editor.EditWatch(
                [&editor, form](FormDocument& edited) -> void
                {
                    auto& changed = edited.Forms().at(form - 1);
                    changed.width = std::max(changed.width - 10.0f, editor.WatchDesign().MinimumForm(edited, form).x());
                }
            );
        }
    );
    width.Plus().SetOnRelease(
        [&editor, form]() -> void
        { editor.EditWatch([form](FormDocument& edited) -> void { edited.Forms().at(form - 1).width += 10.0f; }); }
    );

    auto& height = m_stack.AddComponent<StepperComponent>("height", "Height", std::format("{:.0f}", extra.height));
    height.Minus().SetOnRelease(
        [&editor, form]() -> void
        {
            editor.EditWatch(
                [&editor, form](FormDocument& edited) -> void
                {
                    auto& changed = edited.Forms().at(form - 1);
                    changed.height = std::max(changed.height - 10.0f, editor.WatchDesign().MinimumForm(edited, form).y());
                }
            );
        }
    );
    height.Plus().SetOnRelease(
        [&editor, form]() -> void
        { editor.EditWatch([form](FormDocument& edited) -> void { edited.Forms().at(form - 1).height += 10.0f; }); }
    );

    auto& colour = m_stack.AddComponent<ColorFieldComponent>("colour", "Colour", extra.bg_color.value_or("#00000000"));
    colour.Swatch().SetOnRelease(
        [&editor, form]() -> void
        {
            editor.OpenColor({
                .title = "Form colour",
                .get = [&editor, form]() -> std::string
                { return editor.WatchDesign().Document().Forms().at(form - 1).bg_color.value_or("#00000000"); },
                .set = [&editor, form](const std::string& color) -> void
                {
                    editor.EditWatch([form, &color](FormDocument& edited) -> void { edited.Forms().at(form - 1).bg_color = color; });
                },
                .reset = [&editor, form]() -> void
                { editor.EditWatch([form](FormDocument& edited) -> void { edited.Forms().at(form - 1).bg_color.reset(); }); },
            });
        }
    );

    m_stack.AddComponent<TextLineComponent>(
        "hint",
        "Drag it by an empty spot to move it",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );

    auto& remove = m_stack.AddComponent<ButtonComponent>("delete", "Delete form");
    remove.SetSize({0.0f, PanelStyle::kRowHeight});
    remove.SetFontSize(PanelStyle::kTextFont);
    remove.SetFgColor(FormTheme::Rgba("@on-accent"));
    remove.SetBgColor(FormTheme::Rgba("@red"));
    remove.SetEllipsize(true);
    remove.SetOnRelease(
        [&editor, form]() -> void
        {
            editor.WatchDesign().RemoveForm(form);
            editor.CloseDialog();
            editor.WatchChanged();
        }
    );
}

auto WatchProperties::AddElement(Editor& editor, size_t index) -> void
{
    const auto& element = editor.WatchDesign().Document().Components().at(index);
    const auto subtle = FormTheme::Rgba("@subtle");

    m_stack.AddComponent<TextLineComponent>(
        "title",
        PrettyType(element),
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );
    if (!std::holds_alternative<PanelElement>(element) && !std::holds_alternative<OverlayListElement>(element))
    {
        m_stack.AddComponent<SectionComponent>("content", "Content");
    }

    if (const auto* button = std::get_if<ButtonElement>(&element))
    {
        m_stack.AddComponent<LaserInputComponent>(
            "button.text",
            "Text",
            editor.Text(),
            button->text,
            [&editor, index](const std::string& typed) -> void
            {
                editor.EditWatch(
                    [&editor, index, &typed](FormDocument& document) -> void
                    {
                        std::get<ButtonElement>(document.Components().at(index)).text = typed;
                        editor.WatchDesign().Fit(document, index);
                    }
                );
            }
        );
    }
    else if (FormElements::IsText(element))
    {
        AddText(editor, index);
    }
    else if (const auto* battery = std::get_if<BatteryListElement>(&element))
    {
        auto& devices = m_stack.AddComponent<StepperComponent>("devices", "Devices", std::format("{}", battery->num_devices));
        devices.Minus().SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            {
                auto& item = std::get<BatteryListElement>(changed);
                item.num_devices = std::max<size_t>(item.num_devices, 2) - 1;
            }
        ));
        devices.Plus().SetOnRelease(
            ElementEdit(editor, index, [](FormElement& changed) -> void { ++std::get<BatteryListElement>(changed).num_devices; })
        );
    }
    else if (const auto* sprite = std::get_if<SpriteElement>(&element))
    {
        m_stack.AddComponent<TextLineComponent>(
            "sprite",
            sprite->sprite,
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
    }

    AddLook(editor, index);
    if (std::holds_alternative<ButtonElement>(element))
    {
        AddButtonActions(editor);
    }
    AddArrange(editor, index);
}

auto WatchProperties::AddText(Editor& editor, size_t index) -> void
{
    const auto& element = editor.WatchDesign().Document().Components().at(index);
    const auto* fixed = std::get_if<LabelElement>(&element);
    const auto* clock = std::get_if<ClockElement>(&element);
    const auto* zone_name = std::get_if<TimezoneElement>(&element);
    const auto& design = editor.WatchDesign();
    const auto shown = design.label_text ? design.label_text(element) : std::string("Text");
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");

    if (fixed != nullptr || clock != nullptr || zone_name != nullptr)
    {
        m_stack.AddComponent<TextLineComponent>("shows", "Shows", PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
        auto& kinds = m_stack.AddComponent<RowComponent>("kinds", PanelStyle::kRowHeight, PanelStyle::kGap);
        kinds.SetWeights({1.0f, 1.0f, 1.4f});

        auto& as_text = kinds.AddComponent<ButtonComponent>("text", "Text");
        as_text.SetFontSize(PanelStyle::kTextFont);
        as_text.SetFgColor(fixed != nullptr ? on_accent : text);
        as_text.SetBgColor(fixed != nullptr ? accent : surface);
        as_text.SetEllipsize(true);
        as_text.SetOnRelease(ElementEdit(
            editor,
            index,
            [shown](FormElement& changed) -> void
            {
                if (!std::holds_alternative<LabelElement>(changed))
                {
                    Convert(changed, LabelElement{.text = shown});
                }
            }
        ));

        auto& as_clock = kinds.AddComponent<ButtonComponent>("clock", "Clock");
        as_clock.SetFontSize(PanelStyle::kTextFont);
        as_clock.SetFgColor(clock != nullptr ? on_accent : text);
        as_clock.SetBgColor(clock != nullptr ? accent : surface);
        as_clock.SetEllipsize(true);
        as_clock.SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            {
                if (const auto* name = std::get_if<TimezoneElement>(&changed))
                {
                    Convert(changed, ClockElement{.format = "%H:%M", .timezone = name->timezone});
                }
                else if (!std::holds_alternative<ClockElement>(changed))
                {
                    Convert(changed, ClockElement{.format = "%H:%M"});
                }
            }
        ));

        auto& as_zone = kinds.AddComponent<ButtonComponent>("zone", "Zone name");
        as_zone.SetFontSize(PanelStyle::kTextFont);
        as_zone.SetFgColor(zone_name != nullptr ? on_accent : text);
        as_zone.SetBgColor(zone_name != nullptr ? accent : surface);
        as_zone.SetEllipsize(true);
        as_zone.SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            {
                if (const auto* time = std::get_if<ClockElement>(&changed); time != nullptr && time->timezone)
                {
                    Convert(changed, TimezoneElement{.timezone = *time->timezone});
                }
                else if (!std::holds_alternative<TimezoneElement>(changed))
                {
                    Convert(changed, TimezoneElement{.timezone = TimezonePanel::LocalZone()});
                }
            }
        ));
    }

    const bool is_centered = FormElements::Centered(element);
    auto& centered = m_stack.AddComponent<ButtonComponent>("centered", "Centered");
    centered.SetSize({0.0f, PanelStyle::kRowHeight});
    centered.SetFontSize(PanelStyle::kTextFont);
    centered.SetFgColor(is_centered ? on_accent : text);
    centered.SetBgColor(is_centered ? accent : surface);
    centered.SetEllipsize(true);
    centered.SetOnRelease(ElementEdit(editor, index, [](FormElement& changed) -> void { FormElements::ToggleCentered(changed); }));

    if (zone_name != nullptr)
    {
        AddTimezone(editor, index, TimezonePanel::ZoneName(zone_name->timezone));
    }

    if (fixed != nullptr)
    {
        m_stack.AddComponent<LaserInputComponent>(
            "label.text",
            "Text",
            editor.Text(),
            fixed->text,
            [&editor, index](const std::string& typed) -> void
            {
                editor.EditWatch(
                    [&editor, index, &typed](FormDocument& document) -> void
                    {
                        std::get<LabelElement>(document.Components().at(index)).text = typed;
                        editor.WatchDesign().Fit(document, index);
                    }
                );
            }
        );
        return;
    }
    if (clock == nullptr)
    {
        return;
    }

    m_stack.AddComponent<LaserInputComponent>(
        "label.format",
        "Format",
        editor.Text(),
        clock->format,
        [&editor, index](const std::string& typed) -> void
        {
            editor.EditWatch(
                [index, &typed](FormDocument& document) -> void
                {
                    if (auto* time = std::get_if<ClockElement>(&document.Components().at(index)))
                    {
                        time->format = typed;
                    }
                }
            );
        }
    );

    auto& presets = m_stack.AddComponent<RowComponent>("presets", PanelStyle::kRowHeight, PanelStyle::kGap);
    for (const auto& [name, preset] : {std::pair{"24h", "%H:%M"}, {"12h", "%I:%M %p"}, {"Date", "%x"}, {"Day", "%A"}})
    {
        const bool chosen = clock->format == preset;
        auto& button = presets.AddComponent<ButtonComponent>(name, name);
        button.SetFontSize(PanelStyle::kTextFont);
        button.SetFgColor(chosen ? on_accent : text);
        button.SetBgColor(chosen ? accent : surface);
        button.SetEllipsize(true);
        button.SetOnRelease(ElementEdit(
            editor,
            index,
            [preset](FormElement& changed) -> void
            {
                if (auto* time = std::get_if<ClockElement>(&changed))
                {
                    time->format = preset;
                }
            }
        ));
    }

    AddTimezone(editor, index, TimezonePanel::ZoneName(clock->timezone));

    const bool has_city = clock->city_size.has_value();
    auto& city = m_stack.AddComponent<ButtonComponent>("city", "City name above");
    city.SetSize({0.0f, PanelStyle::kRowHeight});
    city.SetFontSize(PanelStyle::kTextFont);
    city.SetFgColor(has_city ? on_accent : text);
    city.SetBgColor(has_city ? accent : surface);
    city.SetEllipsize(true);
    city.SetOnRelease(ElementEdit(
        editor,
        index,
        [](FormElement& changed) -> void
        {
            if (auto* time = std::get_if<ClockElement>(&changed))
            {
                time->city_size = time->city_size ? std::nullopt : std::optional(14);
            }
        }
    ));

    if (has_city)
    {
        auto& city_size = m_stack.AddComponent<StepperComponent>("city-size", "City size", std::format("{}", *clock->city_size));
        city_size.Minus().SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            {
                if (auto* time = std::get_if<ClockElement>(&changed); time != nullptr && time->city_size)
                {
                    time->city_size = std::clamp(*time->city_size - 1, 6, 120);
                }
            }
        ));
        city_size.Plus().SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            {
                if (auto* time = std::get_if<ClockElement>(&changed); time != nullptr && time->city_size)
                {
                    time->city_size = std::clamp(*time->city_size + 1, 6, 120);
                }
            }
        ));
    }
}

auto WatchProperties::AddTimezone(Editor& editor, size_t index, const std::string& zone) -> void
{
    const bool picking = editor.Dialog() == DialogKind::Timezone;
    auto& row = m_stack.AddComponent<RowComponent>("timezone", PanelStyle::kRowHeight, PanelStyle::kGap);
    row.SetWeights({1.0f, 2.0f});
    row.AddComponent<TextLineComponent>("caption", "Timezone", PanelStyle::kTextFont, FormTheme::Rgba("@subtle"));
    auto& pick = row.AddComponent<ButtonComponent>("pick", zone);
    pick.SetFontSize(PanelStyle::kTextFont);
    pick.SetFgColor(FormTheme::Rgba(picking ? "@on-accent" : "@text"));
    pick.SetBgColor(FormTheme::Rgba(picking ? "@accent" : "@surface"));
    pick.SetEllipsize(true);
    pick.SetOnRelease([&editor, index]() -> void { editor.OpenTimezones(index); });
}

auto WatchProperties::AddLook(Editor& editor, size_t index) -> void
{
    const auto& element = editor.WatchDesign().Document().Components().at(index);

    m_stack.AddComponent<SectionComponent>("look", "Look");

    if (const auto font = FormElements::FontSize(element))
    {
        auto& size = m_stack.AddComponent<StepperComponent>("font", "Font size", std::format("{}", *font));
        size.Minus().SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            { FormElements::SetFontSize(changed, FormElements::FontSize(changed).value_or(12) - 1); }
        ));
        size.Plus().SetOnRelease(ElementEdit(
            editor,
            index,
            [](FormElement& changed) -> void
            { FormElements::SetFontSize(changed, FormElements::FontSize(changed).value_or(12) + 1); }
        ));
    }

    if (const auto background = FormElements::Color(element, true))
    {
        auto& colour = m_stack.AddComponent<ColorFieldComponent>("colour", "Colour", *background);
        colour.Swatch().SetOnRelease(
            [&editor, index]() -> void
            {
                editor.OpenColor({
                    .title = "Colour",
                    .get = [&editor, index]() -> std::string
                    {
                        return FormElements::Color(editor.WatchDesign().Document().Components().at(index), true).value_or("#ffffff");
                    },
                    .set = [&editor, index](const std::string& color) -> void
                    {
                        editor.EditWatch(
                            [index, &color](FormDocument& document) -> void
                            { FormElements::SetColor(document.Components().at(index), true, color); }
                        );
                    },
                });
            }
        );
    }

    if (const auto foreground = FormElements::Color(element, false))
    {
        auto& colour = m_stack.AddComponent<ColorFieldComponent>("text-colour", "Text colour", *foreground);
        colour.Swatch().SetOnRelease(
            [&editor, index]() -> void
            {
                editor.OpenColor({
                    .title = "Text colour",
                    .get = [&editor, index]() -> std::string
                    {
                        return FormElements::Color(editor.WatchDesign().Document().Components().at(index), false)
                            .value_or("#ffffff");
                    },
                    .set = [&editor, index](const std::string& color) -> void
                    {
                        editor.EditWatch(
                            [index, &color](FormDocument& document) -> void
                            { FormElements::SetColor(document.Components().at(index), false, color); }
                        );
                    },
                });
            }
        );
    }

    if (const auto radius = FormElements::Radius(element))
    {
        const auto shape = std::holds_alternative<PanelElement>(element) ? Shape::Panel : Shape::Control;
        const float shown = FormTheme::Resolve(*radius, g_themes->Active().rounding, shape);
        const auto value = radius->has_value() ? std::format("{:.0f}", shown) : std::format("{:.0f} theme", shown);
        auto& corners = m_stack.AddComponent<StepperComponent>("corners", "Corners", value);
        corners.Minus().SetOnRelease(ElementEdit(
            editor,
            index,
            [shown](FormElement& changed) -> void { FormElements::SetRadius(changed, std::max(shown - 1.0f, 0.0f)); }
        ));
        corners.Plus().SetOnRelease(
            ElementEdit(editor, index, [shown](FormElement& changed) -> void { FormElements::SetRadius(changed, shown + 1.0f); })
        );
        if (radius->has_value())
        {
            auto& follow = m_stack.AddComponent<ButtonComponent>("follow-theme", "Follow the theme");
            follow.SetSize({0.0f, PanelStyle::kRowHeight});
            follow.SetFontSize(PanelStyle::kTextFont);
            follow.SetFgColor(FormTheme::Rgba("@text"));
            follow.SetBgColor(FormTheme::Rgba("@surface"));
            follow.SetEllipsize(true);
            follow.SetOnRelease(
                ElementEdit(editor, index, [](FormElement& changed) -> void { FormElements::SetRadius(changed, std::nullopt); })
            );
        }
    }

    if (FormElements::RegionOf(element) == Region::Toolbar || !FormElements::IsText(element) || FormElements::Centered(element))
    {
        auto& width = m_stack
                          .AddComponent<StepperComponent>("width", "Width", std::format("{:.0f}", FormElements::Rect(element)[2]));
        width.Minus().SetOnRelease(
            ElementEdit(editor, index, [](FormElement& changed) -> void { FormElements::Rect(changed)[2] -= 2.0f; })
        );
        width.Plus().SetOnRelease(
            ElementEdit(editor, index, [](FormElement& changed) -> void { FormElements::Rect(changed)[2] += 2.0f; })
        );
    }
}

auto WatchProperties::AddButtonActions(Editor& editor) -> void
{
    auto data = std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(*editor.WatchDesign().Selected())).data;

    m_stack.AddComponent<SectionComponent>("actions", "Actions");

    std::vector<std::string> names;
    std::optional<size_t> open;
    for (const auto& slot : ActionsPanel::Triggers())
    {
        const auto& actions = data.*slot.actions;
        auto name = std::string(slot.name);
        name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
        if (editor.Dialog() == DialogKind::Actions && editor.Trigger() == slot.trigger)
        {
            open = names.size();
        }
        names.push_back(actions && !actions->empty() ? std::format("{} ({})", name, actions->size()) : name);
    }
    auto& triggers = m_stack.AddComponent<ChoiceGridComponent>("triggers", names, open, 2, PanelStyle::kCompactRow);
    triggers.SetOnChoose([&editor](size_t index) -> void { editor.OpenActions(ActionsPanel::Triggers()[index].trigger); });
}

auto WatchProperties::AddArrange(Editor& editor, size_t index) -> void
{
    const auto text = FormTheme::Rgba("@text");
    const auto surface = FormTheme::Rgba("@surface");

    m_stack.AddComponent<SectionComponent>("arrange", "Arrange");

    auto& order = m_stack.AddComponent<RowComponent>("order", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& forward = order.AddComponent<ButtonComponent>("forward", "Forward");
    forward.SetFontSize(PanelStyle::kTextFont);
    forward.SetFgColor(text);
    forward.SetBgColor(surface);
    forward.SetEllipsize(true);
    forward.SetOnRelease(
        [&editor, index]() -> void
        {
            editor.WatchDesign().Restack(index, 1);
            editor.WatchChanged();
        }
    );

    auto& back = order.AddComponent<ButtonComponent>("back", "Back");
    back.SetFontSize(PanelStyle::kTextFont);
    back.SetFgColor(text);
    back.SetBgColor(surface);
    back.SetEllipsize(true);
    back.SetOnRelease(
        [&editor, index]() -> void
        {
            editor.WatchDesign().Restack(index, -1);
            editor.WatchChanged();
        }
    );

    auto& copies = m_stack.AddComponent<RowComponent>("copies", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& duplicate = copies.AddComponent<ButtonComponent>("duplicate", "Duplicate");
    duplicate.SetFontSize(PanelStyle::kTextFont);
    duplicate.SetFgColor(text);
    duplicate.SetBgColor(surface);
    duplicate.SetEllipsize(true);
    duplicate.SetOnRelease(
        [&editor, index]() -> void
        {
            editor.WatchDesign().Duplicate(index);
            editor.WatchChanged();
        }
    );

    auto& remove = copies.AddComponent<ButtonComponent>("delete", "Delete");
    remove.SetFontSize(PanelStyle::kTextFont);
    remove.SetFgColor(FormTheme::Rgba("@on-accent"));
    remove.SetBgColor(FormTheme::Rgba("@red"));
    remove.SetEllipsize(true);
    remove.SetOnRelease(
        [&editor, index]() -> void
        {
            editor.WatchDesign().Remove(index);
            editor.CloseDialog();
            editor.WatchChanged();
        }
    );
}
