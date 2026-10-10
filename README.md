# FrameXR

> [!NOTE]
> This project is a C++ rewrite of [wlx-overlay-s](https://github.com/galister/wlx-overlay-s) (now [WayVR](https://github.com/wayvr-org/wayvr)), originally written in Rust. It was branched off release [v25.4.2](https://github.com/wayvr-org/wayvr/releases/tag/v25.4.2).

A lightweight OpenXR/OpenVR overlay for Wayland and X11 desktops.

### Building from source

Requirements: a C++23 compiler (GCC 14+ or Clang 19+), Meson, Ninja, `glslangValidator`, and the development packages for Vulkan, Wayland, PipeWire, D-Bus, xkbcommon, FreeType, Fontconfig, OpenXR, X11/Xext/Xrandr, EGL/GLES and libdrm/gbm.

On Fedora:

```sh
sudo dnf install meson ninja-build gcc-c++ glslang vulkan-loader-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel libxkbcommon-x11-devel dbus-devel freetype-devel fontconfig-devel pipewire-devel \
    openxr-devel mesa-libgbm-devel libdrm-devel mesa-libEGL-devel libX11-devel libXext-devel libXrandr-devel
```

Then:

```sh
git submodule update --init --recursive
meson setup build
ninja -C build
./build/src/framexr
```

Features can be toggled with `-D<feature>=disabled`: `openvr`, `openxr`, `osc`, `x11`, `wayland`.

Tests use googletest (vendored in `external/googletest`) and live in `tests/`:

- `meson test -C build --no-suite integration` runs the unit tests (`framexr-tests`) and the clang-format check.
- `meson test -C build --suite integration` runs `framexr-integration-tests`, which needs a GPU and a desktop session: it renders the watch, keyboard, settings and anchor panels to PNGs, drives pointer interaction (hover, click, grab, scroll, curvature) through the real input code, and captures the X display through XShm. Set `FRAMEXR_TEST_PW_TOKEN` to a ScreenCast restore token to also exercise PipeWire capture.

Log verbosity is controlled by the `FRAMEXR_LOG` environment variable (e.g. `FRAMEXR_LOG=debug` or `FRAMEXR_LOG=info,overlays/Screen=trace`).

## First Start

**When the screen share pop-up appears, check the terminal and select the screens in the order it requests.**

In case screens were selected in the wrong order:

- `rm ~/.config/framexr/conf.d/pw_tokens.yaml` then restart

**SteamVR users**: FrameXR will register itself for auto-start, so there is no need to start it every time.

**Please continue reading the guide below.**

## Getting Started

### Working Set

The working set consists of all currently selected overlays; screens, mirrors, keyboard, etc.

The working set appears in front of the headset when shown, and can be re-centered by hiding and showing again.

Show and hide the working set using:

- Non-vive controller: double-tap B or Y on the left controller.
- Vive controller: double-tap the menu button on the left controller (for SteamVR, the `showhide` binding must be bound)

### Pointer Modes AKA Laser Colors

Much of the functionality in FrameXR depends on what color of laser is used to interact with a UI element. \
Using the default settings, there are 3 modes:

- Regular Mode: Blue laser
- Right-click Mode: Orange laser
- Middle-click Mode: Purple laser

Please see the bindings section below on how to activate these modes.

The guide here uses the colors for ease of getting started.

### The Watch

Check your left wrist for the watch. The watch is the primary tool for controlling the app.

### The Screens

Hovering a pointer over a screen will move the mouse. If there are more than one pointers hovering a screen, the pointer that was last used to click will take precedence.

The click depends on the laser color:

- Blue laser: Left click
- Orange laser: Right click
- Purple laser: Middle click
- Stick up/down: Scroll wheel

To **curve a screen**, grab it with one hand. Then, using the other hand, hover the laser over the screen and use the scroll action.

See the [bindings](#default-bindings) section on how to grab, move and resize screens.

### The keyboard

Typing

- Use the BLUE laser when typing regularly.
- While using ORANGE laser, all keystrokes will have SHIFT applied.
- Purple laser has no effect as of now.

**Modifier Keys** are sticky. They will remain pressed until a non-modifier key is pressed, the modifier gets toggled off, or the keyboard gets hidden.

## Troubleshooting

When an error is detected, we often print tips for fixing into the log file.

Logs will be at `/tmp/framexr.log` for most distros.

Check [here](https://github.com/galister/wlx-overlay-s/wiki/Troubleshooting) for tips.

## Known Issues

### Crashes, blank screens

There are some driver-desktop combinations that don't play nice with DMA-buf capture. 

Disabling DMA-buf capture is a good first step to try when encountering an app crash or gpu driver reset.

```bash
echo 'capture_method: pw_fallback' > ~/.config/framexr/conf.d/pw_fallback.yaml
```

Without DMA-buf capture, capturing screens takes CPU power, so let's try and not show too many screens at the same time.

### Space-drag crashes SteamVR

This has been idenfitied as an issue with SteamVR versions 2.5.5 and above (latest tested 2.7.2). One way to avoid the crash is by switching to the `temp-v1.27.5` branch of SteamVR (via beta selection) and selecting [Steam-Play-None](https://github.com/Scrumplex/Steam-Play-None) under the compatibility tab.

### Modifiers get stuck in weird ways

This is a rare issue that can make KDE Plasma not react to click or keys due to what seems to be a race condition with modifiers. Restarting the overlay fixes this.

### X11 limitations

- X11 capture can generally seem slow. This is because zero-copy GPU capture is not supported on the general X11 desktop. Consider trying Wayland or Picom.
- DPI scaling is not supported and may cause the mouse to not follow the laser properly.
- Upright screens are not supported and can cause the mouse to act weirdly.
- Screen changes (connecting / disconnecting a display, resolution changes, etc) are not handled at runtime. Restart the overlay for these to take effect.

## Credits

- [galister](https://github.com/galister), thank you for your amazing work on wlx-overlay-s.

