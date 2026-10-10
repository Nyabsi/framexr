// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "Screen.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <numbers>
#include <ranges>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Hid.hpp"
#include "Image.hpp"
#include "Logger.hpp"
#include "Pipeline.hpp"
#include "Shaders.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "Notifications.hpp"
#include "PipewireCapture.hpp"
#include "WaylandClient.hpp"
#include "WlrDmabufCapture.hpp"
#include "WlrScreencopyCapture.hpp"
#endif

#if FRAMEXR_FEATURE_X11
#include "XshmCapture.hpp"
#endif

namespace {

constexpr float kCursorSize = 16.0f / 1440.0f;
constexpr float kScreenDistance = 1.0f;
constexpr float kScreenGap = 0.05f;
constexpr float kScreenTilt = 0.1745f;
constexpr uint32_t kMaxScreenHeight = 2560;

const auto kStart = std::chrono::steady_clock::now();
std::atomic<int64_t> next_move_ms{0};

auto ElapsedMs() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count();
}

auto CanMove() -> bool {
    return ElapsedMs() > next_move_ms.load(std::memory_order_relaxed);
}

auto SetNextMove(uint32_t millis_from_now) -> void {
    next_move_ms.store(ElapsedMs() + millis_from_now, std::memory_order_relaxed);
}

auto MakeAffine2(const Eigen::Vector2f& x_axis, const Eigen::Vector2f& y_axis, const Eigen::Vector2f& translation)
    -> Eigen::Affine2f {
    Eigen::Affine2f result = Eigen::Affine2f::Identity();
    result.linear().col(0) = x_axis;
    result.linear().col(1) = y_axis;
    result.translation() = translation;
    return result;
}

auto IsRotated90(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated90 || transform == FrameTransform::Flipped90;
}

auto IsRotated180(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated180 || transform == FrameTransform::Flipped180;
}

auto IsRotated270(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated270 || transform == FrameTransform::Flipped270;
}

auto ExtentFromFormat(const FrameFormat& format, const Config& config) -> VkExtent2D {
    const uint32_t height_limit = config.screen_render_down ? std::min<uint32_t>(config.screen_max_height, kMaxScreenHeight)
                                                            : kMaxScreenHeight;
    const uint32_t height = std::min(format.height, height_limit);
    const auto width = static_cast<uint32_t>(
        static_cast<float>(format.width) / static_cast<float>(format.height) * static_cast<float>(height)
    );
    return {width, height};
}

auto AffineFromFormat(const FrameFormat& format) -> Eigen::Affine3f {
    const auto rotation = [](float angle) -> Eigen::Affine3f {
        return Eigen::Affine3f(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitZ()));
    };
    Eigen::Affine3f flip_x(Eigen::Scaling(-1.0f, 1.0f, 1.0f));
    constexpr float kPi = std::numbers::pi_v<float>;

    switch (format.transform) {
    case FrameTransform::Rotated90: return rotation(-kPi / 2.0f);
    case FrameTransform::Rotated180: return rotation(kPi);
    case FrameTransform::Rotated270: return rotation(kPi / 2.0f);
    case FrameTransform::Flipped: return flip_x;
    case FrameTransform::Flipped90: return flip_x * rotation(-kPi / 2.0f);
    case FrameTransform::Flipped180: return flip_x * rotation(kPi);
    case FrameTransform::Flipped270: return flip_x * rotation(kPi / 2.0f);
    case FrameTransform::Undefined:
    case FrameTransform::Normal: break;
    }
    return Eigen::Affine3f::Identity();
}

auto AllowsDmabuf(const std::string& method) -> bool {
    return method != "pw_fallback" && method != "screencopy";
}

auto TokenPath() -> std::filesystem::path {
    return Config::ConfD() / "pw_tokens.yaml";
}

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE
auto BestMatch(const PipewireStream& stream, const std::vector<std::shared_ptr<XshmScreen>>& screens)
    -> std::shared_ptr<XshmScreen> {
    if (screens.empty()) {
        return nullptr;
    }
    auto best = screens.front();
    LOG_DEBUG("stream: {}", stream.position ? std::format("({}, {})", stream.position->x(), stream.position->y()) : "None");
    if (!stream.position) {
        return best;
    }
    const auto distance = [&](const XshmScreen& screen) -> int32_t {
        return std::abs(screen.x - stream.position->x()) + std::abs(screen.y - stream.position->y());
    };
    int32_t best_distance = distance(*best);
    for (const auto& screen : screens | std::views::drop(1)) {
        LOG_DEBUG("checking: {} {}x{}+{}+{}", screen->name, screen->width, screen->height, screen->x, screen->y);
        if (const int32_t current = distance(*screen); current < best_distance) {
            best = screen;
            best_distance = current;
        }
    }
    LOG_DEBUG("best: {}", best->name);
    return best;
}
#endif

} // namespace

CapturedFrame::CapturedFrame(std::shared_ptr<Image> captured_image, FrameFormat frame_format, std::optional<MouseMeta> mouse_meta)
    : image(std::move(captured_image)),
      format(frame_format),
      mouse(mouse_meta) {
}

PendingFrame::PendingFrame(Frame&& source) : frame(std::move(source)) {
    if (auto* dmabuf = std::get_if<DmabufFrame>(&frame)) {
        for (auto& plane : dmabuf->planes) {
            plane.fd = plane.fd >= 0 ? dup(plane.fd) : -1;
        }
    } else if (auto* memfd = std::get_if<MemFdFrame>(&frame)) {
        memfd->plane.fd = memfd->plane.fd >= 0 ? dup(memfd->plane.fd) : -1;
    } else if (auto* memptr = std::get_if<MemPtrFrame>(&frame); memptr != nullptr && memptr->data != nullptr) {
        bytes.assign(memptr->data, memptr->data + memptr->size);
        memptr->data = bytes.data();
    }
}

PendingFrame::~PendingFrame() {
    if (const auto* dmabuf = std::get_if<DmabufFrame>(&frame)) {
        for (const auto& plane : dmabuf->planes) {
            if (plane.fd >= 0) {
                close(plane.fd);
            }
        }
    } else if (const auto* memfd = std::get_if<MemFdFrame>(&frame); memfd != nullptr && memfd->plane.fd >= 0) {
        close(memfd->plane.fd);
    }
}

FrameUploader::FrameUploader(std::string name, std::shared_ptr<Device> device, Queue& queue)
    : name_(std::move(name)),
      device_(std::move(device)),
      queue_(&queue) {
}

auto FrameUploader::Upload(const FrameFormat& format, std::span<const std::byte> data, int32_t stride) const
    -> std::shared_ptr<Image> {
    const auto vk_format = Device::FourccToFormat(format.fourcc);
    if (!vk_format) {
        LOG_ERROR("{}: {}", name_, vk_format.error().Message());
        return nullptr;
    }
    auto command_buffer = device_->CreateCommandBuffer(*queue_);
    if (!command_buffer) {
        LOG_ERROR("{}: Could not create vkCommandBuffer: {}", name_, command_buffer.error().Message());
        return nullptr;
    }
    const uint32_t row_length = stride > 0 ? static_cast<uint32_t>(stride) / 4 : 0;
    auto image = command_buffer->UploadImage(format.width, format.height, *vk_format, data, row_length);
    if (!image) {
        LOG_ERROR("{}: Could not create vkImage: {}", name_, image.error().Message());
        return nullptr;
    }
    if (const auto result = command_buffer->SubmitUpload(); !result) {
        LOG_ERROR("{}: Could not execute upload: {}", name_, result.error().Message());
        return nullptr;
    }
    return std::move(*image);
}

auto FrameUploader::Process(Frame&& frame) const -> std::unique_ptr<CapturedFrame> {
    if (const auto* dmabuf = std::get_if<DmabufFrame>(&frame)) {
        if (!dmabuf->IsValid()) {
            LOG_ERROR("{}: Invalid frame", name_);
            return nullptr;
        }
        LOG_TRACE("{}: New DMA-buf frame", name_);
        auto image = device_->ImportDmabuf(*dmabuf);
        if (!image) {
            LOG_ERROR("{}: Failed to create DMA-buf vkImage: {}", name_, image.error().Message());
            return nullptr;
        }
        return std::make_unique<CapturedFrame>(std::move(*image), dmabuf->format, std::nullopt);
    }

    if (const auto* memfd = std::get_if<MemFdFrame>(&frame)) {
        if (memfd->plane.fd < 0) {
            LOG_ERROR("{}: No fd in MemFd frame", name_);
            return nullptr;
        }
        const size_t length = static_cast<size_t>(memfd->plane.stride) * memfd->format.height;
        void* map = mmap(nullptr, length, PROT_READ, MAP_SHARED, memfd->plane.fd, static_cast<off_t>(memfd->plane.offset));
        if (map == MAP_FAILED) {
            LOG_ERROR("{}: Could not map MemFd frame", name_);
            return nullptr;
        }
        auto image = Upload(memfd->format, std::span(static_cast<const std::byte*>(map), length), memfd->plane.stride);
        munmap(map, length);
        if (!image) {
            return nullptr;
        }
        return std::make_unique<CapturedFrame>(std::move(image), memfd->format, std::nullopt);
    }

    const auto& memptr = std::get<MemPtrFrame>(frame);
    LOG_TRACE("{}: New MemPtr frame", name_);
    auto image = Upload(memptr.format, std::span(memptr.data, memptr.size), 0);
    if (!image) {
        return nullptr;
    }
    return std::make_unique<CapturedFrame>(std::move(image), memptr.format, memptr.mouse);
}

ScreenPipeline::ScreenPipeline(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<Image> mouse, VkExtent2D extent)
    : pipeline_(std::move(pipeline)),
      mouse_(std::move(mouse)),
      extent_(extent) {
}

auto ScreenPipeline::Create(AppState& app, VkExtent2D extent) -> Result<ScreenPipeline> {
    auto& device = *app.device;
    ASSIGN_OR_RETURN(auto pipeline, device.CreatePipeline(Shaders::Screen(), device.NativeFormat(), Blend::SquareSource));

    constexpr std::array<uint8_t, 64> kMouseBytes{
        0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
    };
    ASSIGN_OR_RETURN(auto upload, device.CreateCommandBuffer(device.TransferQueue()));
    ASSIGN_OR_RETURN(auto mouse, upload.UploadImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM, std::as_bytes(std::span(kMouseBytes))));
    RETURN_IF_ERROR(upload.SubmitUpload());

    return ScreenPipeline(std::move(pipeline), std::move(mouse), extent);
}

auto ScreenPipeline::Render(
    AppState& app,
    const CapturedFrame& frame,
    const std::shared_ptr<Image>& target,
    CommandBatch& batch,
    float alpha
) -> Result<> {
    auto& device = *app.device;
    const std::array uniform{alpha};

    ASSIGN_OR_RETURN(auto texture_set, pipeline_->CreateTextureSet(0, frame.image, device.TextureFilter()));
    ASSIGN_OR_RETURN(auto alpha_set, pipeline_->CreateUniformSet(1, uniform));
    const auto pass = pipeline_->CreatePassForTarget(*target, {std::move(texture_set), std::move(alpha_set)});

    ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.GraphicsQueue()));
    command_buffer.BeginRendering(target);
    command_buffer.Draw(pass);

    if (frame.mouse) {
        const auto width = static_cast<float>(extent_.width);
        const auto height = static_cast<float>(extent_.height);
        const float size = kCursorSize * height;
        const float half_size = size * 0.5f;

        ASSIGN_OR_RETURN(
            auto vertices,
            device.UploadVertices(
                extent_,
                std::fma(frame.mouse->x, width, -half_size),
                std::fma(frame.mouse->y, height, -half_size),
                size,
                size
            )
        );
        ASSIGN_OR_RETURN(auto mouse_set, pipeline_->CreateTextureSet(0, mouse_, VK_FILTER_NEAREST));
        ASSIGN_OR_RETURN(auto mouse_alpha_set, pipeline_->CreateUniformSet(1, uniform));
        command_buffer.Draw(
            pipeline_->CreatePass(target->Extent(), std::move(vertices), {std::move(mouse_set), std::move(mouse_alpha_set)})
        );
    }

    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));
    return {};
}

ScreenRenderer::ScreenRenderer(std::string name, std::unique_ptr<Capture> capture)
    : name_(std::move(name)),
      capture_(std::move(capture)) {
}

auto ScreenRenderer::Init(AppState&) -> Result<> {
    return {};
}

auto ScreenRenderer::Start(AppState& app) -> void {
    auto& device = *app.device;
    const bool supports_dmabuf = device.SupportsDmabuf() && capture_->SupportsDmabuf();
    const auto& capture_method = app.config.capture_method;
    const bool allow_dmabuf = AllowsDmabuf(capture_method);

    std::span<const DrmFormat> dmabuf_formats;
    if (!supports_dmabuf || !allow_dmabuf) {
        if (!supports_dmabuf) {
            LOG_INFO("Capture method does not support DMA-buf");
        } else {
            LOG_INFO("Not using DMA-buf capture due to {}", capture_method);
        }
        if (device.CaptureQueue() == nullptr) {
            LOG_WARN(
                "Current GPU does not support multiple queues. Software capture will take place on the main thread. Expect "
                "degraded performance."
            );
        }
    } else {
        LOG_WARN("Using DMA-buf capture. If screens are blank for you, switch to SHM using:");
        LOG_WARN("echo 'capture_method: pw_fallback' > ~/.config/framexr/conf.d/pw_fallback.yaml");
        dmabuf_formats = device.DrmFormats();
    }

    auto* capture_queue = device.CaptureQueue();
    main_thread_ = capture_queue == nullptr;
    uploader_ = std::make_shared<FrameUploader>(name_, app.device, main_thread_ ? device.TransferQueue() : *capture_queue);

    if (main_thread_) {
        capture_->Start(dmabuf_formats, [](Frame&& frame) -> std::unique_ptr<ProcessedFrame> {
            return std::make_unique<PendingFrame>(std::move(frame));
        });
    } else {
        capture_->Start(dmabuf_formats, [uploader = uploader_](Frame&& frame) -> std::unique_ptr<ProcessedFrame> {
            return uploader->Process(std::move(frame));
        });
    }
    capture_->RequestNewFrame();
}

auto ScreenRenderer::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    if (!capture_->IsStarted()) {
        Start(app);
        return RenderReadiness::Unable;
    }

    if (auto received = capture_->Receive()) {
        if (main_thread_) {
            auto& pending = static_cast<PendingFrame&>(*received);
            if (auto processed = uploader_->Process(std::move(pending.frame))) {
                current_ = std::move(processed);
            }
        } else {
            current_.reset(static_cast<CapturedFrame*>(received.release()));
        }
    }

    if (current_ && !meta_) {
        meta_ = FrameMeta{
            .extent = ExtentFromFormat(current_->format, app.config),
            .transform = AffineFromFormat(current_->format),
            .format = current_->image->Format(),
        };
        ASSIGN_OR_RETURN(auto pipeline, ScreenPipeline::Create(app, current_->image->Extent()));
        pipeline_.emplace(std::move(pipeline));
    }

    return current_ ? RenderReadiness::Should : RenderReadiness::Unable;
}

auto ScreenRenderer::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    if (!current_) {
        return false;
    }
    const auto frame = std::move(current_);
    if (!pipeline_) {
        FatalError("Screen pipeline missing while a frame is pending");
    }
    RETURN_IF_ERROR(pipeline_->Render(app, *frame, target, batch, alpha));
    capture_->RequestNewFrame();
    return true;
}

auto ScreenRenderer::Pause(AppState&) -> Result<> {
    capture_->Pause();
    return {};
}

auto ScreenRenderer::Resume(AppState&) -> Result<> {
    capture_->Resume();
    return {};
}

auto ScreenRenderer::Meta() -> std::optional<FrameMeta> {
    return meta_;
}

ScreenInteraction::ScreenInteraction(const Eigen::Vector2f& position, const Eigen::Vector2f& size, FrameTransform transform) {
    if (IsRotated90(transform)) {
        mouse_transform_ = MakeAffine2({0.0f, size.y()}, {-size.x(), 0.0f}, {position.x() + size.x(), position.y()});
    } else if (IsRotated180(transform)) {
        mouse_transform_ = MakeAffine2({-size.x(), 0.0f}, {0.0f, -size.y()}, position + size);
    } else if (IsRotated270(transform)) {
        mouse_transform_ = MakeAffine2({0.0f, -size.y()}, {size.x(), 0.0f}, {position.x(), position.y() + size.y()});
    } else {
        mouse_transform_ = MakeAffine2({size.x(), 0.0f}, {0.0f, size.y()}, position);
    }
}

auto ScreenInteraction::OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> {
    LOG_TRACE("Hover: [{}, {}]", hit.uv.x(), hit.uv.y());
    if (CanMove() && (!app.config.focus_follows_mouse_mode || app.input.pointers[hit.pointer].now.move_mouse)) {
        app.hid->MouseMove(mouse_transform_ * hit.uv);
        SetNextMove(app.config.mouse_move_interval_ms);
    }
    return std::nullopt;
}

auto ScreenInteraction::OnLeft(AppState&, size_t) -> void {
}

auto ScreenInteraction::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    uint16_t button = kMouseLeft;
    if (hit.mode == PointerMode::Right) {
        button = kMouseRight;
    } else if (hit.mode == PointerMode::Middle) {
        button = kMouseMiddle;
    }

    if (pressed) {
        SetNextMove(app.config.click_freeze_time_ms);
    }

    app.hid->SendButton(button, pressed);

    if (pressed) {
        app.hid->MouseMove(mouse_transform_ * hit.uv);
    }
}

auto ScreenInteraction::OnScroll(AppState& app, const PointerHit&, float delta_y, float delta_x) -> void {
    app.hid->Wheel(static_cast<int32_t>(delta_y * 64.0f), static_cast<int32_t>(delta_x * 64.0f));
}

auto Screens::CreateState(std::string name, const Eigen::Vector2i& resolution, FrameTransform transform, const Config& config)
    -> OverlayState {
    constexpr float kPi = std::numbers::pi_v<float>;
    float angle = 0.0f;
    if (config.upright_screen_fix) {
        if (IsRotated90(transform)) {
            angle = kPi / 2.0f;
        } else if (IsRotated180(transform)) {
            angle = kPi;
        } else if (IsRotated270(transform)) {
            angle = -kPi / 2.0f;
        }
    }

    const auto width = static_cast<float>(resolution.x());
    const auto height = static_cast<float>(resolution.y());
    const Eigen::Vector2f center(0.5f, 0.5f);
    const Eigen::Vector2f unit_x = Eigen::Vector2f::UnitX();
    const Eigen::Vector2f unit_y = Eigen::Vector2f::UnitY();

    Eigen::Affine2f interaction_transform;
    if (IsRotated90(transform)) {
        interaction_transform = MakeAffine2(-unit_y * (width / height), -unit_x, center);
    } else if (IsRotated180(transform)) {
        interaction_transform = MakeAffine2(-unit_x, -unit_y * (-width / height), center);
    } else if (IsRotated270(transform)) {
        interaction_transform = MakeAffine2(unit_y * (width / height), unit_x, center);
    } else if (resolution.y() > resolution.x()) {
        interaction_transform = MakeAffine2(unit_x * (height / width), -unit_y, center);
    } else {
        interaction_transform = MakeAffine2(unit_x, -unit_y * (width / height), center);
    }

    OverlayState state;
    state.name = std::move(name);
    state.grabbable = true;
    state.recenter = true;
    state.interactable = true;
    state.spawn_scale = 1.5f * config.desktop_view_scale;
    state.spawn_point = Eigen::Vector3f(0.0f, 0.0f, -kScreenDistance);
    state.spawn_rotation = Eigen::Quaternionf(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitZ()));
    state.interaction_transform = interaction_transform;
    return state;
}

auto Screens::ArrangeInFrontOfHead(std::vector<ScreenOverlay>& screens) -> void {
    std::vector<ScreenOverlay*> ordered;
    ordered.reserve(screens.size());
    for (auto& screen : screens) {
        ordered.push_back(&screen);
    }
    std::ranges::sort(ordered, [](const ScreenOverlay* a, const ScreenOverlay* b) -> bool {
        const auto& left = a->desktop_position;
        const auto& right = b->desktop_position;
        return left.x() != right.x() ? left.x() < right.x() : left.y() < right.y();
    });

    const float middle = (static_cast<float>(ordered.size()) - 1.0f) / 2.0f;
    for (size_t i = 0; i < ordered.size(); ++i) {
        auto& state = ordered[i]->state;
        const float step = 2.0f * std::atan((state.spawn_scale + kScreenGap) / (2.0f * kScreenDistance));
        const float angle = (static_cast<float>(i) - middle) * step;
        state.spawn_point = kScreenDistance
                          * Eigen::Vector3f(
                                std::sin(angle) * std::cos(kScreenTilt),
                                -std::sin(kScreenTilt),
                                -std::cos(angle) * std::cos(kScreenTilt)
                          );
        state.spawn_rotation = Eigen::Quaternionf(Eigen::AngleAxisf(-angle, Eigen::Vector3f::UnitY()))
                             * Eigen::Quaternionf(Eigen::AngleAxisf(-kScreenTilt, Eigen::Vector3f::UnitX())) * state.spawn_rotation;
    }
}

auto Screens::LoadTokens() -> Result<std::map<std::string, std::string>> {
    ASSIGN_OR_RETURN(const auto parsed, Config::ParseFile(TokenPath()));
    std::map<std::string, std::string> tokens;
    if (const auto it = parsed.find("pw_tokens"); it != parsed.end() && it->is_object()) {
        for (const auto& [display, token] : it->items()) {
            if (token.is_string()) {
                tokens.emplace(display, token.get<std::string>());
            }
        }
    }
    return tokens;
}

auto Screens::SaveTokens(const std::map<std::string, std::string>& tokens) -> Result<> {
    const nlohmann::json document{{"pw_tokens", tokens}};
    std::ofstream stream(TokenPath(), std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Failed to open {}", TokenPath().string()));
    }
    stream << Yaml::Emit(document);
    if (!stream) {
        return Unexpected(std::format("Failed to write {}", TokenPath().string()));
    }
    return {};
}

#if FRAMEXR_FEATURE_WAYLAND

auto Screens::SelectScreen(std::string_view instructions, const ScreenCastOptions& options) -> Result<ScreenSelection> {
    auto future = ScreenCastPortal::SelectScreen(options);
    if (future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
        return future.get();
    }

    LOG_INFO("{}", instructions);
    std::unique_ptr<DbusNotificationSender> sender;
    std::optional<uint32_t> notification;
    if (auto created = DbusNotificationSender::Create()) {
        sender = std::move(*created);
        if (auto id = sender->NotifySend(instructions, "", 2, 0, 0, true)) {
            notification = *id;
        }
    }

    auto result = future.get();
    if (sender && notification) {
        if (const auto closed = sender->NotifyClose(*notification); !closed) {
            LOG_DEBUG("Failed to close notification: {}", closed.error().Message());
        }
    }
    return result;
}

auto Screens::CreateWaylandRenderer(
    const WaylandOutput& output,
    bool has_wlr_dmabuf,
    bool has_wlr_screencopy,
    std::map<std::string, std::string>& tokens,
    const AppState& app
) -> std::unique_ptr<ScreenRenderer> {
    const auto& capture_method = app.config.capture_method;
    std::unique_ptr<Capture> capture;
    if (capture_method == "wlr-dmabuf" && has_wlr_dmabuf) {
        LOG_INFO("{}: Using Wlr DMA-Buf", output.name);
        if (auto connection = WaylandClient::Connect()) {
            capture = std::make_unique<WlrDmabufCapture>(std::move(connection), output.id);
        }
    }
    if (capture_method == "screencopy" && has_wlr_screencopy) {
        LOG_INFO("{}: Using Wlr Screencopy Wl-SHM", output.name);
        if (auto connection = WaylandClient::Connect()) {
            capture = std::make_unique<WlrScreencopyCapture>(std::move(connection), output.id);
        }
    }

    if (!capture) {
        LOG_INFO("{}: Using Pipewire capture", output.name);
        std::optional<std::string> token;
        if (const auto it = tokens.find(output.name); it != tokens.end()) {
            token = it->second;
            LOG_INFO("Found existing Pipewire token for display {}: {}", output.name, it->second);
        }

        auto selection = SelectScreen(
            std::format(
                "Now select: {} {} {} @ {},{}",
                output.name,
                output.make,
                output.model,
                output.logical_position.x(),
                output.logical_position.y()
            ),
            {
                .restore_token = std::move(token),
                .embed_mouse = !app.config.double_cursor_fix,
                .screens_only = true,
                .persist = true,
                .multiple = false,
            }
        );
        if (!selection) {
            LOG_WARN("{}: Failed to create Pipewire capture: {}", output.name, selection.error().Message());
        } else if (selection->streams.empty()) {
            LOG_WARN("{}: Failed to create Pipewire capture: no streams selected", output.name);
        } else {
            capture = std::make_unique<PipewireCapture>(output.name, selection->streams.front().node_id);
            if (selection->restore_token) {
                auto& stored = tokens[output.name];
                if (stored != *selection->restore_token) {
                    stored = *selection->restore_token;
                    LOG_INFO("Adding Pipewire token {}", stored);
                }
            }
        }
    }

    if (!capture) {
        return nullptr;
    }
    return std::make_unique<ScreenRenderer>(output.name, std::move(capture));
}

auto Screens::CreateWayland(WaylandClient& client, AppState& app) -> std::vector<ScreenOverlay> {
    std::vector<ScreenOverlay> screens;
    auto tokens = LoadTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    const bool has_wlr_dmabuf = client.DmabufManager() != nullptr;
    const bool has_wlr_screencopy = client.ScreencopyManager() != nullptr;

    for (const auto& output : client.Outputs()) {
        if (std::ranges::any_of(app.screens, [&](const ScreenMeta& screen) -> bool { return screen.name == output.name; })) {
            continue;
        }

        LOG_INFO(
            "{}: Init screen of res ({}, {}), logical ({}, {}) at ({}, {})",
            output.name,
            output.size.x(),
            output.size.y(),
            output.logical_size.x(),
            output.logical_size.y(),
            output.logical_position.x(),
            output.logical_position.y()
        );

        auto renderer = CreateWaylandRenderer(output, has_wlr_dmabuf, has_wlr_screencopy, tokens, app);
        if (!renderer) {
            continue;
        }

        const auto transform = WaylandClient::ToFrameTransform(output.transform);
        auto interaction = std::make_unique<ScreenInteraction>(
            output.logical_position.cast<float>(),
            output.logical_size.cast<float>(),
            transform
        );
        auto state = CreateState(output.name, output.logical_size, transform, app.config);
        ScreenMeta meta{.name = output.name, .id = state.id, .native_handle = output.id};
        auto backend = std::make_unique<SplitOverlayBackend>(std::move(renderer), std::move(interaction));
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = output.logical_position,
        });
    }

    if (tokens != original_tokens) {
        if (const auto result = SaveTokens(tokens); !result) {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    app.hid->SetDesktopExtent(client.DesktopExtent().cast<float>());
    app.hid->SetDesktopOrigin(client.DesktopOrigin().cast<float>());
    return screens;
}

#else

auto Screens::SelectScreen(std::string_view, const ScreenCastOptions&) -> Result<ScreenSelection> {
    return Unexpected("Pipewire support not enabled");
}

auto Screens::CreateWaylandRenderer(const WaylandOutput&, bool, bool, std::map<std::string, std::string>&, const AppState&)
    -> std::unique_ptr<ScreenRenderer> {
    return nullptr;
}

auto Screens::CreateWayland(WaylandClient&, AppState&) -> std::vector<ScreenOverlay> {
    return {};
}

#endif

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE

auto Screens::CreateX11Pipewire(AppState& app) -> Result<std::vector<ScreenOverlay>> {
    auto tokens = LoadTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    std::optional<std::string> token;
    if (const auto it = tokens.find("x11"); it != tokens.end()) {
        token = it->second;
    }

    ASSIGN_OR_RETURN(
        auto selection,
        SelectScreen(
            "Select ALL screens on the screencast pop-up!",
            {
                .restore_token = std::move(token),
                .embed_mouse = !app.config.double_cursor_fix,
                .screens_only = true,
                .persist = true,
                .multiple = true,
            }
        )
    );

    if (selection.restore_token) {
        auto& stored = tokens["x11"];
        if (stored != *selection.restore_token) {
            stored = *selection.restore_token;
            LOG_INFO("Adding Pipewire token {}", stored);
        }
    }
    if (tokens != original_tokens) {
        if (const auto result = SaveTokens(tokens); !result) {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());
    LOG_INFO("Got {} monitors", monitors.size());
    LOG_INFO("Got {} streams", selection.streams.size());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<ScreenOverlay> screens;
    for (size_t i = 0; i < selection.streams.size(); ++i) {
        const auto& stream = selection.streams[i];
        const auto monitor = BestMatch(stream, monitors);
        if (!monitor) {
            FatalError("No X11 monitor available for screencast stream");
        }
        LOG_INFO("Stream {} is {}", i, monitor->name);

        const Eigen::Vector2f position(static_cast<float>(monitor->x), static_cast<float>(monitor->y));
        const Eigen::Vector2f size(static_cast<float>(monitor->width), static_cast<float>(monitor->height));
        extent = extent.cwiseMax(position + size);

        auto state = CreateState(
            monitor->name,
            {static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height)},
            FrameTransform::Normal,
            app.config
        );
        ScreenMeta meta{.name = monitor->name, .id = state.id, .native_handle = 0};
        auto backend = std::make_unique<SplitOverlayBackend>(
            std::make_unique<ScreenRenderer>(monitor->name, std::make_unique<PipewireCapture>(monitor->name, stream.node_id)),
            std::make_unique<ScreenInteraction>(position, size, FrameTransform::Normal)
        );
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = {monitor->x, monitor->y},
        });
    }

    app.hid->SetDesktopExtent(extent);
    app.hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Screens::CreateX11Pipewire(AppState&) -> Result<std::vector<ScreenOverlay>> {
    return Unexpected("Pipewire support not enabled");
}

#endif

#if FRAMEXR_FEATURE_X11

auto Screens::CreateXshm(AppState& app) -> Result<std::vector<ScreenOverlay>> {
    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<ScreenOverlay> screens;
    for (auto& monitor : monitors) {
        const Eigen::Vector2f position(static_cast<float>(monitor->x), static_cast<float>(monitor->y));
        const Eigen::Vector2f size(static_cast<float>(monitor->width), static_cast<float>(monitor->height));
        extent = extent.cwiseMax(position + size);

        LOG_INFO(
            "{}: Init X11 screen of res ({}, {}) at ({}, {})",
            monitor->name,
            monitor->width,
            monitor->height,
            monitor->x,
            monitor->y
        );

        auto state = CreateState(
            monitor->name,
            {static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height)},
            FrameTransform::Normal,
            app.config
        );
        ScreenMeta meta{.name = monitor->name, .id = state.id, .native_handle = 0};
        const auto name = monitor->name;
        const Eigen::Vector2i desktop_position(monitor->x, monitor->y);
        auto backend = std::make_unique<SplitOverlayBackend>(
            std::make_unique<ScreenRenderer>(name, std::make_unique<XshmCapture>(std::move(monitor))),
            std::make_unique<ScreenInteraction>(position, size, FrameTransform::Normal)
        );
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = desktop_position,
        });
    }

    app.hid->SetDesktopExtent(extent);
    app.hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Screens::CreateXshm(AppState&) -> Result<std::vector<ScreenOverlay>> {
    return Unexpected("X11 support not enabled");
}

#endif
