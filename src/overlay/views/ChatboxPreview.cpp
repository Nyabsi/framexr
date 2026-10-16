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

#include "views/ChatboxPreview.hpp"

#include <format>
#include <utility>

#include "Chatbox.hpp"
#include "Compose.hpp"
#include "Container.hpp"
#include "FontCache.hpp"
#include "Form.hpp"
#include "FormTheme.hpp"
#include "LabelComponent.hpp"
#include "Logger.hpp"
#include "PanelComponent.hpp"
#include "TaskQueue.hpp"
#include "views/Keyboard.hpp"

namespace
{

constexpr float kWidth = 800.0f;
constexpr float kPadding = 20.0f;
constexpr float kGap = 16.0f;
constexpr float kCornerRadius = 16.0f;
constexpr int kHeaderFont = 15;
constexpr int kTextFont = 20;
constexpr float kHeaderBaseline = kPadding + static_cast<float>(kHeaderFont);
constexpr float kTextBaseline = kHeaderBaseline + 14.0f + static_cast<float>(kTextFont);
constexpr float kBottom = 8.0f + kPadding;

} // namespace

auto ChatboxPreview::Open(std::weak_ptr<const Chatbox> chatbox) -> void
{
    g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kChatboxPreviewName)});
    g_tasks->Enqueue(
        CreateOverlayTask{
            .selector = std::string(kChatboxPreviewName),
            .create = [chatbox = std::move(chatbox)]() -> std::unique_ptr<View>
            {
                const auto locked = chatbox.lock();
                auto* keyboard = locked ? g_container->ByName(kKeyboardName) : nullptr;
                if (keyboard == nullptr)
                {
                    return nullptr;
                }
                auto preview = std::unique_ptr<ChatboxPreview>(new ChatboxPreview(chatbox));
                if (const auto shown = preview->Show(*locked); !shown)
                {
                    LOG_ERROR("Could not create chatbox preview: {}", shown.error().Message());
                    return nullptr;
                }
                preview->Follow(*keyboard);
                return preview;
            },
        }
    );
}

ChatboxPreview::ChatboxPreview(std::weak_ptr<const Chatbox> chatbox) : m_chatbox(std::move(chatbox))
{
    SetName(std::string(kChatboxPreviewName));
    SetWantVisible(true);
    SetAttachedTo(RelativeTo::Stage);
    SetZOrder(kZOrderToast);
}

auto ChatboxPreview::Update() -> void
{
    const auto chatbox = m_chatbox.lock();
    const auto* keyboard = g_container->ByName(kKeyboardName);
    if (!chatbox || !chatbox->Active() || keyboard == nullptr || !keyboard->WantVisible())
    {
        SetWantVisible(false);
        g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kChatboxPreviewName)});
        return;
    }
    if (chatbox->Display() != m_text)
    {
        if (const auto shown = Show(*chatbox); !shown)
        {
            LOG_ERROR("Could not update chatbox preview: {}", shown.error().Message());
        }
    }
    Follow(*keyboard);
}

auto ChatboxPreview::Show(const Chatbox& chatbox) -> Result<>
{
    m_text = chatbox.Display();
    const bool empty = m_text.empty();
    const auto text = empty ? std::string("Type message to start") : FontCache::Wrap(m_text, kTextFont, kWidth - (2.0f * kPadding));
    ASSIGN_OR_RETURN(const auto measured, g_fonts->MeasureText(text, kTextFont));
    const Eigen::Vector2f size(kWidth, kTextBaseline + measured.height - static_cast<float>(kTextFont) + kBottom);

    auto form = std::make_unique<Form>(std::string(kChatboxPreviewName));
    form->SetSize(size);

    auto& background = form->AddComponent<PanelComponent>("background");
    background.SetRect({0.0f, 0.0f, size.x(), size.y()});
    background.SetCornerRadius(kCornerRadius);
    background.SetBgColor(FormTheme::Rgba("@panel"));

    const auto count = FontCache::DecodeUtf8(chatbox.Draft()).size();
    auto& header = form->AddComponent<LabelComponent>("header", std::format("VRChat chatbox  {}/{}", count, Chatbox::kMaxLength));
    header.SetRect({kPadding, kHeaderBaseline, size.x() - (2.0f * kPadding), static_cast<float>(kHeaderFont)});
    header.SetFontSize(kHeaderFont);
    header.SetFgColor(FormTheme::Rgba(count > Chatbox::kMaxLength ? "@red" : "@soft-text"));

    auto& message = form->AddComponent<LabelComponent>("message", text);
    message.SetRect({kPadding, kTextBaseline, size.x() - (2.0f * kPadding), measured.height});
    message.SetFontSize(kTextFont);
    message.SetFgColor(FormTheme::Rgba(empty ? "@soft-text" : "@text"));

    if (Composer() == nullptr)
    {
        ASSIGN_OR_RETURN(auto created, Compose::Create(std::move(form)));
        SetComposer(std::move(created));
    }
    else
    {
        RETURN_IF_ERROR(Composer()->SetForm(std::move(form)));
    }
    SetInteractionTransform(UiTransform({static_cast<uint32_t>(size.x()), static_cast<uint32_t>(size.y())}));
    return {};
}

auto ChatboxPreview::Follow(const View& keyboard) -> void
{
    const auto* board = keyboard.Composer() != nullptr ? keyboard.Composer()->Root() : nullptr;
    if (board == nullptr || Composer() == nullptr)
    {
        return;
    }
    const Eigen::Vector2f board_size = board->Size();
    const Eigen::Vector2f box_size = Composer()->Root()->Size();
    const float lift = ((0.5f * board_size.y()) + kGap + (0.5f * box_size.y())) / board_size.x();
    const float ratio = box_size.x() / board_size.x();
    const Eigen::Affine3f pose = keyboard.Transform() * Eigen::Translation3f(0.0f, lift, 0.0f) * Eigen::Scaling(ratio);
    const auto curvature = keyboard.Curvature().transform([ratio](float value) -> float { return value * ratio; });
    if (!pose.isApprox(Transform()) || curvature != Curvature())
    {
        SetTransform(pose);
        SetCurvature(curvature);
        SetDirty(true);
    }
}
