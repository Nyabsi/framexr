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

#include "panels/GhostPanel.hpp"

#include <string>
#include <type_traits>

#include "Editor.hpp"
#include "LabelComponent.hpp"
#include "PanelComponent.hpp"

GhostPanel::GhostPanel(const Editor& editor, const Eigen::Vector2f& size) : FormComponent("ghost") {
    SetSize(size);
    auto& panel = AddComponent<PanelComponent>("background");
    panel.SetSize(size);
    panel.SetCornerRadius(FormTheme::Radius(g_themes->Active().rounding, Shape::Control) * 2.0f);
    panel.SetBgColor(FormTheme::Rgba("@accent"));
    std::string name;
    if (editor.Carrying()) {
        name = std::visit(
            [](const auto& item) -> std::string {
                if constexpr (std::is_same_v<std::decay_t<decltype(item)>, FormPart>) {
                    return std::string(FormDesigner::PartName(item));
                } else {
                    return std::string(KeyboardDesigner::TemplateName(item));
                }
            },
            *editor.Carrying()
        );
    }
    auto& label = AddComponent<LabelComponent>("name", name);
    label.SetSize(size);
    label.SetFontSize(20);
    label.SetFgColor(FormTheme::Rgba("@on-accent"));
    label.SetCentered(true);
}
