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

#include "DesktopInputMethod.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <csignal>
#include <chrono>
#include <filesystem>
#include <format>
#include <system_error>
#include <thread>
#include <optional>
#include <string>
#include <tuple>
#include <string_view>
#include <utility>
#include <vector>

#include "ConfigStore.hpp"
#include "Logger.hpp"
#include "Platform.hpp"
#include "Process.hpp"

namespace
{

struct InputMethodFramework
{
    std::string_view program;
    std::string_view query;
    std::string_view set;
    std::array<std::array<std::string_view, 3>, 4> engines;
};

constexpr auto kFrameworks = std::to_array<InputMethodFramework>({
    {
        .program = "fcitx5-remote",
        .query = "-n",
        .set = "-s",
        .engines = {{{}, {"hangul", "", ""}, {"mozc", "anthy", "kkc"}, {"cangjie", "cangjie5", ""}}},
    },
    {
        .program = "ibus",
        .query = "engine",
        .set = "engine",
        .engines = {
            {{}, {"hangul", "", ""}, {"mozc-jp", "anthy", "kkc"}, {"table:cangjie5", "table:cangjie3", "table:cangjie-big"}}
        },
    },
});

constexpr int kStartAttempts = 40;
constexpr auto kStartWait = std::chrono::milliseconds(250);

auto Marker() -> std::filesystem::path
{
    return Platform::RuntimeDirectory() / "framexr-input-method";
}

auto Query(const InputMethodFramework& framework) -> std::optional<std::string>
{
    return Process::Output({std::string(framework.program), std::string(framework.query)});
}

auto Detect() -> std::optional<std::pair<size_t, std::string>>
{
    for (size_t index = 0; index < kFrameworks.size(); ++index)
    {
        if (auto current = Query(kFrameworks.at(index)))
        {
            return std::pair(index, std::move(*current));
        }
    }
    return std::nullopt;
}

auto Set(const InputMethodFramework& framework, std::string_view engine) -> bool
{
    Process::Output({std::string(framework.program), std::string(framework.set), std::string(engine)});
    return Query(framework) == engine;
}

auto Advise(const InputMethodFramework& framework, std::string_view engine) -> void
{
    if (framework.program != "ibus")
    {
        return;
    }
    if (engine == "hangul"
        && Process::Output({"gsettings", "get", "org.freedesktop.ibus.engine.hangul", "initial-input-mode"}) == "'latin'")
    {
        LOG_WARN("IBus Hangul starts in Latin, run `gsettings set org.freedesktop.ibus.engine.hangul initial-input-mode hangul`");
    }
    else if (
        engine == "anthy"
        && Process::Output({"gsettings", "get", "org.freedesktop.ibus.engine.anthy.common", "typing-method"}) == "0"
    )
    {
        LOG_WARN(
            "Anthy types romaji, run `gsettings set org.freedesktop.ibus.engine.anthy.common typing-method 1` to type the printed "
            "kana"
        );
    }
}

auto Remember(size_t framework, const std::string& previous) -> void
{
    if (const auto written = ConfigStore::WriteFile(Marker(), std::format("{}\n{}", framework, previous)); !written)
    {
        LOG_WARN("Could not remember the input method to restore: {}", written.error().Message());
    }
}

auto Forget() -> void
{
    std::error_code error;
    std::filesystem::remove(Marker(), error);
}

auto Recover() -> void
{
    const auto saved = ConfigStore::ReadFile(Marker());
    if (!saved)
    {
        return;
    }
    const auto split = saved->find('\n');
    size_t framework = kFrameworks.size();
    if (split != std::string::npos)
    {
        std::from_chars(saved->c_str(), saved->c_str() + split, framework);
    }
    if (framework < kFrameworks.size())
    {
        const auto previous = saved->substr(split + 1);
        LOG_INFO("Restoring input method {} left behind by a previous FrameXR", previous);
        Set(kFrameworks.at(framework), previous);
    }
    Forget();
}

struct DesktopFramework
{
    std::string_view desktop_file;
    std::array<std::string_view, 2> command;
};

constexpr auto kDesktopFrameworks = std::to_array<DesktopFramework>({
    {.desktop_file = "/usr/share/applications/org.fcitx.Fcitx5.desktop", .command = {"fcitx5", ""}},
    {.desktop_file = "/usr/share/applications/org.freedesktop.IBus.Panel.Wayland.Gtk3.desktop",
     .command = {"ibus-daemon", "--xim"}},
});

auto KwinMarker() -> std::filesystem::path
{
    return Platform::RuntimeDirectory() / "framexr-kwin-input-method";
}

auto Installed() -> const DesktopFramework*
{
    for (const auto& input_method : kDesktopFrameworks)
    {
        std::error_code error;
        if (std::filesystem::exists(input_method.desktop_file, error))
        {
            return &input_method;
        }
    }
    return nullptr;
}

auto OnDesktop(std::string_view name) -> bool
{
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    return desktop != nullptr && std::string_view(desktop).contains(name);
}

auto KwinSetting(std::string_view program, std::string_view argument = {}) -> std::optional<std::string>
{
    std::vector<std::string> command{std::string(program), "--file", "kwinrc", "--group", "Wayland", "--key", "InputMethod"};
    if (!argument.empty())
    {
        command.emplace_back(argument);
    }
    return Process::Output(command);
}

auto ReconfigureKwin() -> void
{
    Process::Output({"dbus-send", "--session", "--dest=org.kde.KWin", "--type=method_call", "/KWin", "org.kde.KWin.reconfigure"});
}

auto RecoverKwin() -> void
{
    const auto configured = ConfigStore::ReadFile(KwinMarker());
    if (!configured)
    {
        return;
    }
    if (KwinSetting("kreadconfig6") == *configured)
    {
        KwinSetting("kwriteconfig6", "--delete");
        ReconfigureKwin();
        LOG_INFO("Removed FrameXR's virtual keyboard setting from KDE");
    }
    std::error_code error;
    std::filesystem::remove(KwinMarker(), error);
}

} // namespace

DesktopInputMethod::DesktopInputMethod() : m_worker([this](const std::stop_token& stop) -> void { Work(stop); })
{
}

DesktopInputMethod::~DesktopInputMethod()
{
    Select(ImeScript::None);
    m_worker.request_stop();
    m_worker.join();
}

auto DesktopInputMethod::Select(ImeScript script) -> void
{
    const std::scoped_lock lock(m_mutex);
    m_pending = script;
    m_wake.notify_one();
}

auto DesktopInputMethod::Work(const std::stop_token& stop) -> void
{
    Recover();
    RecoverKwin();
    std::unique_lock lock(m_mutex);
    while (m_wake.wait(lock, stop, [this]() -> bool { return m_pending.has_value(); }))
    {
        const auto script = m_pending.value_or(ImeScript::None);
        m_pending.reset();
        lock.unlock();
        Apply(script);
        lock.lock();
    }
    lock.unlock();
    if (m_launched)
    {
        m_launched->Signal(SIGTERM);
        m_launched.reset();
    }
    RecoverKwin();
}

auto DesktopInputMethod::Start() -> std::optional<std::pair<size_t, std::string>>
{
    auto detected = Detect();
    if (detected || std::exchange(m_launch_tried, true) || !Launch())
    {
        return detected;
    }
    for (int attempt = 0; attempt < kStartAttempts && !detected; ++attempt)
    {
        std::this_thread::sleep_for(kStartWait);
        detected = Detect();
    }
    return detected;
}

auto DesktopInputMethod::Apply(ImeScript script) -> void
{
    if (script == m_active)
    {
        return;
    }
    if (m_active == ImeScript::None)
    {
        auto detected = Start();
        if (!detected)
        {
            LOG_WARN("No input method is running or could be started, install Fcitx5 or IBus to type this keyboard's script");
            return;
        }
        std::tie(m_framework, m_previous) = std::move(*detected);
        Remember(m_framework, m_previous);
    }
    m_active = script;
    const auto& framework = kFrameworks.at(m_framework);
    if (script != ImeScript::None)
    {
        for (const auto engine : framework.engines.at(static_cast<size_t>(script)))
        {
            if (!engine.empty() && Set(framework, engine))
            {
                LOG_INFO("Switched {} to input method {}", framework.program, engine);
                Advise(framework, engine);
                return;
            }
        }
        LOG_WARN("None of the {} input methods for this keyboard are installed", framework.program);
    }
    if (!Set(framework, m_previous))
    {
        LOG_WARN("Could not restore input method {}", m_previous);
    }
    Forget();
}

auto DesktopInputMethod::Launch() -> bool
{
    const auto* input_method = Installed();
    if (input_method == nullptr)
    {
        return false;
    }
    if (OnDesktop("GNOME"))
    {
        LOG_WARN("GNOME runs IBus itself, add an input source in Settings, Keyboard to type this keyboard's script");
        return false;
    }
    if (OnDesktop("KDE") && std::getenv("WAYLAND_DISPLAY") != nullptr)
    {
        if (!KwinSetting("kreadconfig6").value_or("x").empty()
            || !ConfigStore::WriteFile(KwinMarker(), std::string(input_method->desktop_file)))
        {
            return false;
        }
        KwinSetting("kwriteconfig6", input_method->desktop_file);
        ReconfigureKwin();
        LOG_INFO("Set KDE's virtual keyboard to {} until FrameXR exits", input_method->desktop_file);
        return true;
    }
    std::vector<std::string> command{"setpriv", "--pdeathsig", "TERM", "--"};
    for (const auto word : input_method->command)
    {
        if (!word.empty())
        {
            command.emplace_back(word);
        }
    }
    auto launched = Process::Spawn(command);
    if (!launched)
    {
        LOG_WARN("Could not start {}: {}", command.at(4), launched.error().Message());
        return false;
    }
    m_launched = std::move(*launched);
    LOG_INFO("Started {} until FrameXR exits", command.at(4));
    return true;
}
