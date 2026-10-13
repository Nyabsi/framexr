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

#include "SpriteComponent.hpp"

#include <format>
#include <span>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Device.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "document/FormParser.hpp"

auto SpriteComponent::Parse(const nlohmann::json& json) -> SpriteElement {
    return {
        .sprite = json.at("sprite").get<std::string>(),
        .sprite_st = FormParser::Optional<std::array<float, 4>>(json, "sprite_st"),
    };
}

auto SpriteComponent::Serialize(nlohmann::ordered_json& json, const SpriteElement& element) -> void {
    json["sprite"] = element.sprite;
    if (element.sprite_st) {
        const auto& st = *element.sprite_st;
        json["sprite_st"] = {
            Json::ShortestFloat(st[0]),
            Json::ShortestFloat(st[1]),
            Json::ShortestFloat(st[2]),
            Json::ShortestFloat(st[3]),
        };
    }
}

auto SpriteComponent::Create(FormComponent& parent, const SpriteElement& element, const FormContext&) -> SpriteComponent* {
    auto image = Load(element.sprite);
    if (!image) {
        LOG_WARN("Could not load custom UI sprite: {}", image.error().Message());
        return nullptr;
    }
    auto& sprite = parent.AddComponent<SpriteComponent>("", std::move(*image));
    sprite.SetRect(FormParser::ToRect(element.rect));
    const auto st = element.sprite_st.value_or(std::array{1.0f, 1.0f, 0.0f, 0.0f});
    sprite.SetSpriteSt(Eigen::Vector4f(st[0], st[1], st[2], st[3]));
    return &sprite;
}

auto SpriteComponent::Load(const std::string& path) -> Result<std::shared_ptr<Image>> {
    const auto contents = ConfigStore::ReadFile(ConfigStore::Root() / path);
    if (!contents) {
        return Unexpected(std::format("Could not open custom sprite at: {}", path));
    }
    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->TransferQueue()));
    auto image = command_buffer.UploadDds(std::as_bytes(std::span(*contents)));
    if (!image) {
        return Unexpected(std::format("Could not use custom sprite at: {}\n{}", path, image.error().Message()));
    }
    RETURN_IF_ERROR(command_buffer.SubmitUpload());
    return std::move(*image);
}

SpriteComponent::SpriteComponent(std::string name, std::shared_ptr<Image> image)
    : FormComponent(std::move(name)),
      m_image(std::move(image)) {
}

auto SpriteComponent::SetSprite(std::shared_ptr<Image> image) -> void {
    m_image = std::move(image);
    MarkDirty();
}

auto SpriteComponent::SetSpriteSt(const Eigen::Vector4f& st) -> void {
    if (m_st != st) {
        m_st = st;
        MarkDirty();
    }
}

auto SpriteComponent::SetTint(const Eigen::Vector4f& tint) -> void {
    m_tint = tint;
    MarkDirty();
}

auto SpriteComponent::Emit(FormState& state, const FormFrame& frame) -> void {
    if (m_image) {
        state.push_back(SpriteItem{.rect = frame.rect, .image = m_image, .st = m_st, .tint = m_tint, .owner = this});
    }
}
