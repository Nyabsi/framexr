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
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Error.hpp"
#include "FormComponent.hpp"
#include "Image.hpp"
#include "document/Common.hpp"

struct SpriteElement
{
    std::array<float, 4> rect{};
    std::string sprite;
    std::optional<std::array<float, 4>> sprite_st;
    Region region = Region::Form;
    size_t form = 0;
};

class SpriteComponent final : public FormComponent
{
  public:
    static constexpr std::string_view kType = "Sprite";

    static auto Parse(const nlohmann::json& json) -> SpriteElement;
    static auto Serialize(nlohmann::ordered_json& json, const SpriteElement& element) -> void;
    static auto Create(FormComponent& parent, const SpriteElement& element, const FormContext& context) -> SpriteComponent*;
    static auto Load(const std::string& path) -> Result<std::shared_ptr<Image>>;

    explicit SpriteComponent(std::string name = {}, std::shared_ptr<Image> image = nullptr);

    [[nodiscard]] auto Sprite() const -> const std::shared_ptr<Image>& { return m_image; }
    [[nodiscard]] auto SpriteSt() const -> const Eigen::Vector4f& { return m_st; }

    auto SetSprite(std::shared_ptr<Image> image) -> void;
    auto SetSpriteSt(const Eigen::Vector4f& st) -> void;
    auto SetTint(const Eigen::Vector4f& tint) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    std::shared_ptr<Image> m_image;
    Eigen::Vector4f m_st{1.0f, 1.0f, 0.0f, 0.0f};
    Eigen::Vector4f m_tint = Eigen::Vector4f::Ones();
};
