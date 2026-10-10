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

#include "Keyboard.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>
#include <numeric>
#include <system_error>
#include <utility>

#include <clocale>
#include <cwctype>
#include <locale.h>

#include "Config.hpp"
#include "Font.hpp"
#include "Hid.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "State.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

namespace {

constexpr float kPixelsPerUnit = 80.0f;
constexpr float kButtonPadding = 7.0f;
constexpr float kKeyboardMargin = 14.0f;
constexpr float kKeyRadius = 10.0f;
constexpr int kKeyFontSize = 15;
constexpr float kMetersPerUnit = 0.04f;
constexpr float kKeyboardDrop = 0.5f;
constexpr float kKeyboardReach = 0.8f;
constexpr std::array<uint8_t, 5> kAutoReleaseModifiers{kModShift, kModCtrl, kModAlt, kModSuper, kModMeta};

constexpr auto kLatchingKeys = std::to_array<VirtualKey>({VirtualKey::Caps, VirtualKey::NumLock, VirtualKey::Scroll});
constexpr float kLatchedOutlineWidth = 3.0f;

auto ActiveColor() -> Eigen::Vector4f {
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.5f};
}

auto LatchedColor() -> Eigen::Vector4f {
    return {1.0f, 1.0f, 1.0f, 0.16f};
}

auto LatchedOutline() -> HighlightOutline {
    return {.color = {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 1.0f}, .width = kLatchedOutlineWidth};
}

constexpr char kKeychronV1Yaml[] = {
#embed "keyboards/keychron-v1-ansi.yaml"
    ,
    '\0'
};

constexpr char kFullSizeYaml[] = {
#embed "keyboards/full-size.yaml"
    ,
    '\0'
};

constexpr auto kBuiltInProfiles = std::to_array<std::string_view>({kKeychronV1Yaml, kFullSizeYaml});

constexpr unsigned char kKeyClickSound[] = {
#embed "421581.wav"
};

auto Color(std::string_view hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto StartsWithLetter(std::string_view text) -> bool {
    const auto codepoints = FontCache::DecodeUtf8(text);
    if (codepoints.empty()) {
        return false;
    }
    const uint32_t first = codepoints.front();
    if (first < 0x80) {
        return std::isalpha(static_cast<int>(first)) != 0;
    }
    static const locale_t utf8_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
    return utf8_locale != nullptr && iswalpha_l(static_cast<wint_t>(first), utf8_locale) != 0;
}

auto ParseMacroVerb(std::string_view verb) -> std::optional<std::pair<std::string_view, std::optional<bool>>> {
    const auto name_end = std::ranges::find_if_not(verb, [](char c) -> bool {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
    });
    const auto name_length = static_cast<size_t>(name_end - verb.begin());
    if (name_length == 0) {
        return std::nullopt;
    }
    const auto name = verb.substr(0, name_length);
    auto rest = verb.substr(name_length);
    if (rest.empty()) {
        return std::pair{name, std::optional<bool>()};
    }
    const auto state_begin = rest.find_first_not_of(' ');
    if (state_begin == 0 || state_begin == std::string_view::npos) {
        return std::nullopt;
    }
    rest = rest.substr(state_begin);
    if (rest == "UP") {
        return std::pair{name, std::optional<bool>(false)};
    }
    if (rest == "DOWN") {
        return std::pair{name, std::optional<bool>(true)};
    }
    return std::nullopt;
}

auto ToLower(std::string_view text) -> std::string {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

auto from_json(const nlohmann::json& json, AltModifier& value) -> void {
    constexpr auto kNames = std::to_array<std::pair<AltModifier, std::string_view>>({
        {AltModifier::None, "None"},
        {AltModifier::Shift, "Shift"},
        {AltModifier::Ctrl, "Ctrl"},
        {AltModifier::Alt, "Alt"},
        {AltModifier::Super, "Super"},
        {AltModifier::Meta, "Meta"},
    });
    const auto name = Json::VariantName(json);
    for (const auto& [modifier, modifier_name] : kNames) {
        if (modifier_name == name) {
            value = modifier;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for AltModifier", name), &json);
}

auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void {
    value.name = json.at("name").get<std::string>();
    value.row_size = json.at("row_size").get<float>();
    value.key_sizes = json.at("key_sizes").get<std::vector<std::vector<float>>>();
    value.main_layout.clear();
    for (const auto& row : json.at("main_layout")) {
        auto& keys = value.main_layout.emplace_back();
        for (const auto& key : row) {
            keys.push_back(key.is_null() ? std::nullopt : std::optional(key.get<std::string>()));
        }
    }
    value.alt_modifier = json.at("alt_modifier").get<AltModifier>();
    value.exec_commands = json.at("exec_commands").get<std::map<std::string, std::vector<std::string>, std::less<>>>();
    value.macros = json.at("macros").get<std::map<std::string, std::vector<std::string>, std::less<>>>();
    value.labels = json.at("labels").get<std::map<std::string, std::vector<std::string>, std::less<>>>();
    if (const auto it = json.find("auto_labels"); it != json.end() && !it->is_null()) {
        value.auto_labels = it->get<bool>();
    }
    if (const auto it = json.find("region"); it != json.end() && !it->is_null()) {
        value.region = it->get<std::string>();
    }
}

auto KeyboardLayout::Parse(std::string_view text) -> Result<KeyboardLayout> {
    ASSIGN_OR_RETURN(const auto parsed, Yaml::Parse(text));
    KeyboardLayout layout;
    try {
        layout = parsed.get<KeyboardLayout>();
    } catch (const nlohmann::json::exception& error) {
        return Unexpected(error.what());
    }
    RETURN_IF_ERROR(layout.Validate());
    return layout;
}

auto KeyboardLayout::Validate() const -> Result<> {
    for (size_t i = 0; i < key_sizes.size(); ++i) {
        const float width = std::accumulate(key_sizes[i].begin(), key_sizes[i].end(), 0.0f);
        if (std::abs(width - row_size) >= 0.001f) {
            return Unexpected(std::format("Row {} has a width of {}, but the row size is {}", i, width, row_size));
        }
    }
    for (size_t i = 0; i < main_layout.size(); ++i) {
        if (i >= key_sizes.size() || main_layout[i].size() != key_sizes[i].size()) {
            return Unexpected(
                std::format(
                    "Row {} has {} keys, needs to have {} according to key_sizes",
                    i,
                    main_layout[i].size(),
                    i < key_sizes.size() ? key_sizes[i].size() : 0
                )
            );
        }
    }
    return {};
}

auto KeyboardProfiles::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
    -> std::vector<KeyboardLayout> {
    std::vector<KeyboardLayout> profiles;
    const auto add = [&profiles](KeyboardLayout layout) -> void {
        const auto existing = std::ranges::find(profiles, layout.name, &KeyboardLayout::name);
        if (existing != profiles.end()) {
            *existing = std::move(layout);
        } else {
            profiles.push_back(std::move(layout));
        }
    };

    for (const auto text : built_in) {
        auto layout = KeyboardLayout::Parse(text);
        if (!layout) {
            FatalError(std::format("Invalid built-in keyboard profile: {}", layout.error().Message()));
        }
        add(std::move(*layout));
    }

    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(user_directory, error)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file(error) && (extension == ".yaml" || extension == ".yml")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto text = Config::ReadFile(file);
        if (!text) {
            LOG_ERROR("Could not read keyboard profile {}", file.string());
            continue;
        }
        auto layout = KeyboardLayout::Parse(*text);
        if (!layout) {
            LOG_ERROR("Skipping keyboard profile {}: {}", file.string(), layout.error().Message());
            continue;
        }
        LOG_INFO("Loaded keyboard profile {} from {}", layout->name, file.string());
        add(std::move(*layout));
    }
    return profiles;
}

auto KeyboardProfiles::All() -> const std::vector<KeyboardLayout>& {
    static const auto profiles = Load(kBuiltInProfiles, Config::Root() / "keyboards");
    return profiles;
}

auto KeyboardProfiles::Find(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::name);
    if (found == profiles.end()) {
        LOG_WARN("Unknown keyboard profile {}, using {}", name, profiles.front().name);
        return profiles.front();
    }
    return *found;
}

auto KeyboardProfiles::After(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::name);
    if (found == profiles.end() || std::next(found) == profiles.end()) {
        return profiles.front();
    }
    return *std::next(found);
}

auto KeyboardLayout::LabelForKey(std::string_view key) const -> std::vector<std::string> {
    if (const auto it = labels.find(key); it != labels.end()) {
        return it->second;
    }
    if (key.empty()) {
        return {};
    }
    if (key.size() == 1) {
        return {ToLower(key)};
    }
    if (key.starts_with("KP_")) {
        key.remove_prefix(3);
    }
    if (const auto underscore = key.find('_'); underscore != std::string_view::npos) {
        key = key.substr(0, underscore);
    }
    if (key.empty()) {
        FatalError("keyboard.yaml: Key must not start or end with '_'!");
    }
    return {std::format("{}{}", static_cast<char>(std::toupper(static_cast<unsigned char>(key.front()))), ToLower(key.substr(1)))};
}

auto KeyboardLayout::AltModifierMask() const -> uint8_t {
    switch (alt_modifier) {
    case AltModifier::Shift: return kModShift;
    case AltModifier::Ctrl: return kModCtrl;
    case AltModifier::Alt: return kModAlt;
    case AltModifier::Super: return kModSuper;
    case AltModifier::Meta: return kModMeta;
    case AltModifier::None: break;
    }
    return 0;
}

auto KeyboardState::ReapProcesses() -> void {
    std::erase_if(processes, [](Process& process) -> bool {
        const auto status = process.TryWait();
        return status && status->has_value();
    });
}

auto KeyboardState::RefreshLetterCase() -> void {
    const bool upper = caps_lock != ((modifiers & kModShift) != 0);
    for (const auto& letter : letters) {
        letter.label->SetText(upper ? letter.upper : letter.lower);
    }
}

KeyControl::KeyControl(std::shared_ptr<KeyboardState> state, VirtualKey key)
    : state_(std::move(state)),
      key_(key),
      sent_(key),
      latches_(Keyboard::IsLatchingKey(key)) {
}

auto KeyControl::OnPress(AppState& app, PointerMode mode) -> void {
    Keyboard::Click(app);
    if (mode == PointerMode::Right) {
        state_->modifiers |= kModShift;
    } else if (mode == PointerMode::Middle) {
        state_->modifiers |= state_->alt_modifier;
    }
    sent_ = key_;
    translated_ = false;
    uint8_t modifiers = state_->modifiers;
    if (state_->translator && !latches_) {
        if (const auto target = state_->translator->Translate(key_, state_->modifiers, state_->caps_lock)) {
            sent_ = target->key;
            translated_ = true;
            modifiers = static_cast<uint8_t>((state_->modifiers & ~(kModShift | kModMeta)) | target->modifiers);
        }
    }
    app.hid->SetModifiers(modifiers);
    Keyboard::SendKey(app, sent_, true);
    if (latches_) {
        latched_ = !latched_;
        if (key_ == VirtualKey::Caps) {
            state_->caps_lock = latched_;
        }
    }
}

auto KeyControl::OnRelease(AppState& app) -> void {
    Keyboard::SendKey(app, sent_, false);
    if (translated_) {
        Keyboard::SetModifiers(app, state_->modifiers);
    }
    for (const uint8_t modifier : kAutoReleaseModifiers) {
        if ((state_->modifiers & modifier) != 0) {
            state_->modifiers &= static_cast<uint8_t>(~modifier);
            Keyboard::SetModifiers(app, state_->modifiers);
        }
    }
    state_->RefreshLetterCase();
}

auto KeyControl::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return latched_ ? std::optional(LatchedColor()) : std::nullopt;
}

auto KeyControl::Outline(AppState&) -> std::optional<HighlightOutline> {
    return latched_ ? std::optional(LatchedOutline()) : std::nullopt;
}

ModifierControl::ModifierControl(std::shared_ptr<KeyboardState> state, uint8_t modifier)
    : state_(std::move(state)),
      modifier_(modifier) {
}

auto ModifierControl::OnPress(AppState& app, PointerMode) -> void {
    sticky_ = (state_->modifiers & modifier_) == 0;
    state_->modifiers |= modifier_;
    Keyboard::Click(app);
    Keyboard::SetModifiers(app, state_->modifiers);
    state_->RefreshLetterCase();
}

auto ModifierControl::OnRelease(AppState& app) -> void {
    if (!sticky_) {
        state_->modifiers &= static_cast<uint8_t>(~modifier_);
        Keyboard::SetModifiers(app, state_->modifiers);
        state_->RefreshLetterCase();
    }
}

auto ModifierControl::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return (state_->modifiers & modifier_) != 0 ? std::optional(ActiveColor()) : std::nullopt;
}

ProfileControl::ProfileControl(std::shared_ptr<const Keymap> keymap, std::string current)
    : keymap_(std::move(keymap)),
      current_(std::move(current)) {
}

auto ProfileControl::OnPress(AppState& app, PointerMode) -> void {
    Keyboard::Click(app);
    Keyboard::SwitchProfile(app, keymap_, KeyboardProfiles::After(current_).name);
}

MacroControl::MacroControl(std::vector<std::pair<VirtualKey, bool>> events) : events_(std::move(events)) {
}

auto MacroControl::OnPress(AppState& app, PointerMode) -> void {
    Keyboard::Click(app);
    for (const auto& [key, down] : events_) {
        Keyboard::SendKey(app, key, down);
    }
}

ExecControl::ExecControl(std::shared_ptr<KeyboardState> state, std::vector<std::string> press, std::vector<std::string> release)
    : state_(std::move(state)),
      press_(std::move(press)),
      release_(std::move(release)) {
}

auto ExecControl::Spawn(const std::vector<std::string>& command) -> void {
    if (auto process = Process::Spawn(command)) {
        state_->processes.push_back(std::move(*process));
    }
}

auto ExecControl::OnPress(AppState& app, PointerMode) -> void {
    state_->ReapProcesses();
    Keyboard::Click(app);
    Spawn(press_);
}

auto ExecControl::OnRelease(AppState&) -> void {
    state_->ReapProcesses();
    if (!release_.empty()) {
        Spawn(release_);
    }
}

KeyboardBackend::KeyboardBackend(std::unique_ptr<Canvas> canvas, std::shared_ptr<KeyboardState> state)
    : canvas_(std::move(canvas)),
      state_(std::move(state)) {
}

auto KeyboardBackend::SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void {
    canvas_->SetRenderer(std::move(renderer));
}

auto KeyboardBackend::SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void {
    canvas_->SetInteraction(std::move(interaction));
}

auto KeyboardBackend::Init(AppState& app) -> Result<> {
    return canvas_->Init(app);
}

auto KeyboardBackend::Pause(AppState& app) -> Result<> {
    state_->modifiers = 0;
    state_->RefreshLetterCase();
    Keyboard::SetModifiers(app, 0);
    return canvas_->Pause(app);
}

auto KeyboardBackend::Resume(AppState& app) -> Result<> {
    return canvas_->Resume(app);
}

auto KeyboardBackend::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    return canvas_->ShouldRender(app);
}

auto KeyboardBackend::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha)
    -> Result<bool> {
    return canvas_->Render(app, target, batch, alpha);
}

auto KeyboardBackend::Meta() -> std::optional<FrameMeta> {
    return canvas_->Meta();
}

auto KeyboardBackend::OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> {
    return canvas_->OnHover(app, hit);
}

auto KeyboardBackend::OnLeft(AppState& app, size_t pointer) -> void {
    canvas_->OnLeft(app, pointer);
}

auto KeyboardBackend::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    canvas_->OnPointer(app, hit, pressed);
}

auto KeyboardBackend::OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void {
    canvas_->OnScroll(app, hit, delta_y, delta_x);
}

auto Keyboard::IsLatchingKey(VirtualKey key) -> bool {
    return std::ranges::contains(kLatchingKeys, key);
}

auto Keyboard::SendKey(AppState& app, VirtualKey key, bool down) -> void {
    app.hid->SendKey(key, down);
}

auto Keyboard::SetModifiers(AppState& app, uint8_t modifiers) -> void {
    app.hid->SetModifiers(modifiers);
}

auto Keyboard::Click(AppState& app) -> void {
    if (app.config.keyboard_sound_enabled) {
        app.audio.Play(std::as_bytes(std::span(kKeyClickSound)));
    }
}

auto Keyboard::MacroEvents(const std::vector<std::string>& verbs) -> std::vector<std::pair<VirtualKey, bool>> {
    std::vector<std::pair<VirtualKey, bool>> events;
    for (const auto& verb : verbs) {
        const auto parsed = ParseMacroVerb(verb);
        if (!parsed) {
            continue;
        }
        const auto& [name, state] = *parsed;
        const auto key = VirtualKeys::Parse(name);
        if (!key) {
            LOG_ERROR("Unknown virtual key: {}", name);
            return {};
        }
        if (state) {
            events.emplace_back(*key, *state);
        } else {
            events.emplace_back(*key, true);
            events.emplace_back(*key, false);
        }
    }
    return events;
}

auto Keyboard::Create(AppState& app, const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>> {
    const auto& layout = KeyboardProfiles::Find(app.config.keyboard_profile);
    const float width = std::fma(layout.row_size, kPixelsPerUnit, 2.0f * kKeyboardMargin);
    const float height = std::fma(static_cast<float>(layout.main_layout.size()), kPixelsPerUnit, 2.0f * kKeyboardMargin);

    auto state = std::make_shared<KeyboardState>();
    state->alt_modifier = layout.AltModifierMask();

    ASSIGN_OR_RETURN(
        auto canvas,
        Canvas::Create(app.device, static_cast<uint32_t>(width), static_cast<uint32_t>(height), app.device->NativeFormat())
    );

    canvas->bg_color = Color("#181926");
    canvas->Panel(0.0f, 0.0f, width, height, 16.0f);
    canvas->font_size = kKeyFontSize;
    canvas->fg_color = Color("#cad3f5");
    canvas->bg_color = Color("#1e2030");

    std::shared_ptr<const Keymap> region_keymap;
    if (const auto region_name = layout.region.value_or(app.config.keyboard_region); !region_name.empty()) {
        const auto region = KeyboardRegion::Parse(region_name);
        if (auto loaded = Keymap::FromNames(region.layout, region.variant)) {
            region_keymap = std::make_shared<const Keymap>(std::move(*loaded));
            if (keymap) {
                state->translator = std::make_shared<const KeyTranslator>(KeyTranslator::Create(region_keymap, keymap));
            }
        } else {
            LOG_ERROR("Keyboard region {}: {}", region_name, loaded.error().Message());
        }
    }
    const Keymap* typing_keymap = region_keymap ? region_keymap.get() : keymap.get();
    const bool has_altgr = typing_keymap != nullptr && typing_keymap->HasAltGr();
    const Keymap* label_keymap = layout.auto_labels.value_or(true) ? typing_keymap : nullptr;

    const float unit_size = kPixelsPerUnit;
    const float key_height = std::fma(-2.0f, kButtonPadding, unit_size);

    for (size_t row = 0; row < layout.key_sizes.size(); ++row) {
        const float y = std::fma(unit_size, static_cast<float>(row), kKeyboardMargin + kButtonPadding);
        float sum_size = 0.0f;

        for (size_t col = 0; col < layout.key_sizes[row].size(); ++col) {
            const float key_size = layout.key_sizes[row][col];
            const float x = std::fma(unit_size, sum_size, kKeyboardMargin + kButtonPadding);
            const float key_width = std::fma(unit_size, key_size, -(2.0f * kButtonPadding));
            sum_size += key_size;

            const auto& key = layout.main_layout[row][col];
            if (!key) {
                continue;
            }

            std::vector<std::string> labels;
            std::unique_ptr<Control> control;
            auto cap_type = KeyCapType::Regular;
            std::optional<std::string> upper_case;

            if (*key == kProfileSwitchKey) {
                control = std::make_unique<ProfileControl>(keymap, layout.name);
            } else if (const auto virtual_key = VirtualKeys::Parse(*key)) {
                if (label_keymap != nullptr) {
                    switch (VirtualKeys::Type(*virtual_key)) {
                    case KeyType::Symbol: {
                        auto label0 = label_keymap->LabelForKey(*virtual_key, 0);
                        auto label1 = label_keymap->LabelForKey(*virtual_key, kModShift);
                        if (StartsWithLetter(label0)) {
                            labels.push_back(std::move(label0));
                            upper_case = std::move(label1);
                            if (has_altgr) {
                                cap_type = KeyCapType::RegularAltGr;
                                labels.push_back(label_keymap->LabelForKey(*virtual_key, kModMeta));
                            }
                        } else {
                            labels.push_back(std::move(label0));
                            labels.push_back(std::move(label1));
                            if (has_altgr) {
                                labels.push_back(label_keymap->LabelForKey(*virtual_key, kModMeta));
                                cap_type = KeyCapType::ReversedAltGr;
                            } else {
                                cap_type = KeyCapType::Reversed;
                            }
                        }
                        break;
                    }
                    case KeyType::NumPad: labels.push_back(label_keymap->LabelForKey(*virtual_key, kModNumLock)); break;
                    case KeyType::Other: break;
                    }
                }

                if (const auto modifier = VirtualKeys::ModifierOf(*virtual_key); modifier && !IsLatchingKey(*virtual_key)) {
                    control = std::make_unique<ModifierControl>(state, *modifier);
                } else {
                    control = std::make_unique<KeyControl>(state, *virtual_key);
                }
            } else if (const auto macro = layout.macros.find(*key); macro != layout.macros.end()) {
                control = std::make_unique<MacroControl>(MacroEvents(macro->second));
            } else if (const auto exec = layout.exec_commands.find(*key); exec != layout.exec_commands.end()) {
                const auto& args = exec->second;
                if (args.empty()) {
                    LOG_ERROR("Keyboard: EXEC args empty for {}", *key);
                    continue;
                }
                const auto separator = std::ranges::find(args.begin() + 1, args.end(), "null");
                std::vector<std::string> press(args.begin(), separator);
                std::vector<std::string> release;
                if (separator != args.end()) {
                    release.assign(separator + 1, args.end());
                }
                control = std::make_unique<ExecControl>(state, std::move(press), std::move(release));
            } else {
                LOG_ERROR("Unknown key: {}", *key);
                continue;
            }

            if (labels.empty()) {
                labels = layout.LabelForKey(*key);
                if (labels.size() == 1 && labels.front().size() == 1 && StartsWithLetter(labels.front())) {
                    upper_case = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(labels.front().front()))));
                }
            }
            const size_t first_label = canvas->ControlCount() + 1;
            canvas->KeyButton(x, y, key_width, key_height, kKeyRadius, cap_type, labels, std::move(control));
            if (upper_case && first_label < canvas->ControlCount()) {
                state->letters.push_back(
                    {.label = &canvas->ControlAt(first_label), .lower = labels.front(), .upper = std::move(*upper_case)}
                );
            }
        }
    }

    OverlayState overlay;
    overlay.name = kKeyboardName;
    overlay.grabbable = true;
    overlay.recenter = true;
    overlay.interactable = true;
    overlay.spawn_scale = width / kPixelsPerUnit * kMetersPerUnit * app.config.keyboard_scale;
    overlay.spawn_point = Eigen::Vector3f(0.0f, -kKeyboardDrop, -kKeyboardReach);
    overlay.spawn_rotation = Eigen::Quaternionf(
        Eigen::AngleAxisf(-std::atan2(kKeyboardDrop, kKeyboardReach), Eigen::Vector3f::UnitX())
    );
    overlay.interaction_transform = Eigen::Translation2f(0.5f, 0.5f) * Eigen::Scaling(1.0f, -width / height);

    return std::make_unique<Overlay>(std::move(overlay), std::make_unique<KeyboardBackend>(std::move(canvas), std::move(state)));
}

auto Keyboard::SwitchProfile(AppState& app, std::shared_ptr<const Keymap> keymap, std::string profile) -> void {
    LOG_INFO("Switching keyboard profile to {}", profile);
    app.config.keyboard_profile = std::move(profile);
    app.tasks.Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [keymap = std::move(keymap)](AppState& state, OverlayState& previous) -> void {
                state.tasks.Enqueue(DropOverlayTask{.selector = std::string(kKeyboardName)});
                state.tasks.Enqueue(
                    CreateOverlayTask{
                        .selector = std::string(kKeyboardName),
                        .create = [keymap, previous](AppState& app_state)
                            -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                            auto created = Keyboard::Create(app_state, keymap);
                            if (!created) {
                                LOG_ERROR("Could not create keyboard: {}", created.error().Message());
                                return std::nullopt;
                            }
                            auto& overlay = **created;
                            auto& next = overlay.state;
                            const float factor = previous.spawn_scale > 0.0f ? next.spawn_scale / previous.spawn_scale : 1.0f;
                            const auto rescale = [factor](Eigen::Affine3f transform) -> Eigen::Affine3f {
                                transform.linear() *= factor;
                                return transform;
                            };
                            next.want_visible = previous.want_visible;
                            next.show_hide = previous.show_hide;
                            next.recenter = previous.recenter;
                            next.grabbable = previous.grabbable;
                            next.interactable = previous.interactable;
                            next.alpha = previous.alpha;
                            next.curvature = previous.curvature;
                            next.transform = rescale(previous.transform);
                            if (previous.saved_transform) {
                                next.saved_transform = rescale(*previous.saved_transform);
                                for (auto& [name, saved] : app_state.config.transform_values) {
                                    if (name == kKeyboardName) {
                                        saved = *next.saved_transform;
                                    }
                                }
                            }
                            next.dirty = true;
                            return std::pair{std::move(next), std::move(overlay.backend)};
                        },
                    }
                );
            },
        }
    );
}
