framexr_inc = include_directories(
  '.',
  '..',
  'backend',
  'capture',
  'capture/pipewire',
  'capture/wayland',
  'capture/win32',
  'capture/x11',
  'config',
  'core',
  'editor',
  'graphics',
  'gui',
  'gui/canvas',
  'gui/modular',
  'hid',
  'overlays',
  'platform',
)

core_sources = files(
  'core/Error.cpp',
  'core/Logger.cpp',
)
if is_windows
  core_sources += files(
    'platform/win32/ExitStatus.cpp',
    'platform/win32/Platform.cpp',
    'platform/win32/Process.cpp',
    'platform/win32/UdpSocket.cpp',
  )
else
  core_sources += files(
    'platform/posix/ExitStatus.cpp',
    'platform/posix/Platform.cpp',
    'platform/posix/Process.cpp',
    'platform/posix/UdpSocket.cpp',
  )
endif

config_sources = files(
  'config/Config.cpp',
  'config/Json.cpp',
  'config/Yaml.cpp',
)

state_sources = files(
  'AudioOutput.cpp',
  'CommandLine.cpp',
  'State.cpp',
)

backend_sources = files(
  'backend/ByteReader.cpp',
  'backend/ByteWriter.cpp',
  'backend/Input.cpp',
  'backend/InteractionHandler.cpp',
  'backend/Notifications.cpp',
  'backend/NullInteraction.cpp',
  'backend/NullRenderer.cpp',
  'backend/OneEuroFilter.cpp',
  'backend/Osc.cpp',
  'backend/Overlay.cpp',
  'backend/OverlayContainer.cpp',
  'backend/OverlayLayout.cpp',
  'backend/OverlayRenderer.cpp',
  'backend/OverlayState.cpp',
  'backend/Pointer.cpp',
  'backend/PoseFilter.cpp',
  'backend/Raycast.cpp',
  'backend/RotationFilter.cpp',
  'backend/SplitOverlayBackend.cpp',
  'backend/Task.cpp',
  'backend/ToastIngest.cpp',
)
if is_windows
  backend_sources += files('backend/win32/Notifications.cpp')
else
  backend_sources += files(
    'backend/linux/Dbus.cpp',
    'backend/linux/DbusNotificationSender.cpp',
    'backend/linux/Notifications.cpp',
    'backend/linux/ToastIngest.cpp',
  )
endif

editor_sources = files(
  'editor/CallbackControl.cpp',
  'editor/Editor.cpp',
  'editor/EditorColors.cpp',
  'editor/EditorPanel.cpp',
  'editor/EditorProperties.cpp',
  'editor/EditorTextInput.cpp',
  'editor/FormDesigner.cpp',
  'editor/KeyboardDesigner.cpp',
  'editor/OutlineControl.cpp',
  'editor/PanelBuilder.cpp',
)

gui_sources = files(
  'gui/Font.cpp',
  is_windows ? 'gui/win32/FontLookup.cpp' : 'gui/linux/FontLookup.cpp',
  'gui/Theme.cpp',
  'gui/canvas/Canvas.cpp',
  'gui/canvas/Control.cpp',
  'gui/modular/ActionRunner.cpp',
  'gui/modular/BatteryLabel.cpp',
  'gui/modular/Button.cpp',
  'gui/modular/ClockLabel.cpp',
  'gui/modular/DragMultiplierLabel.cpp',
  'gui/modular/ExecLabel.cpp',
  'gui/modular/IpdLabel.cpp',
  'gui/modular/Label.cpp',
  'gui/modular/Modular.cpp',
  'gui/modular/ModularElements.cpp',
  'gui/modular/ModularUiConfig.cpp',
  'gui/modular/TimerLabel.cpp',
)

hid_sources = files(
  'hid/Hid.cpp',
  'hid/KeyTranslator.cpp',
  'hid/NullHidProvider.cpp',
  'hid/VirtualKey.cpp',
)
if is_windows
  hid_sources += files(
    'hid/win32/Hid.cpp',
    'hid/win32/Keymap.cpp',
    'hid/win32/Scancodes.cpp',
    'hid/win32/SendInputProvider.cpp',
  )
else
  hid_sources += files(
    'hid/linux/Hid.cpp',
    'hid/linux/Keymap.cpp',
    'hid/linux/UInputProvider.cpp',
  )
endif

overlay_sources = files(
  'overlays/ActionKeyControl.cpp',
  'overlays/CapturedFrame.cpp',
  'overlays/CustomUi.cpp',
  'overlays/FrameUploader.cpp',
  'overlays/Keyboard.cpp',
  'overlays/KeyboardBackend.cpp',
  'overlays/KeyboardLayout.cpp',
  'overlays/KeyboardProfiles.cpp',
  'overlays/KeyboardState.cpp',
  'overlays/KeyControl.cpp',
  'overlays/ModifierControl.cpp',
  'overlays/PendingFrame.cpp',
  'overlays/ProfileControl.cpp',
  'overlays/Screen.cpp',
  'overlays/ScreenInteraction.cpp',
  'overlays/ScreenPipeline.cpp',
  'overlays/ScreenRenderer.cpp',
  'overlays/Toast.cpp',
  'overlays/Watch.cpp',
)
if get_option('wayland').allowed()
  overlay_sources += files('overlays/Mirror.cpp')
endif

subdir('capture')
subdir('backend/openvr')
subdir('backend/openxr')
subdir('graphics/shaders')

graphics_sources = files(
  'graphics/Buffer.cpp',
  'graphics/CommandBatch.cpp',
  'graphics/CommandBuffer.cpp',
  'graphics/DdsImage.cpp',
  'graphics/DescriptorSet.cpp',
  'graphics/Device.cpp',
  'graphics/Image.cpp',
  'graphics/Pipeline.cpp',
  'graphics/Queue.cpp',
  'graphics/Shaders.cpp',
)

framexr_all_sources = (core_sources + config_sources + editor_sources + state_sources + backend_sources + hid_sources + gui_sources + overlay_sources
  + capture_sources + graphics_sources + openvr_sources + openxr_sources)

hid_deps = []
if get_option('wayland').allowed()
  hid_deps += dependency('wayland-client')
endif
if get_option('x11').allowed()
  hid_deps += [dependency('xkbcommon-x11'), dependency('xcb')]
endif

framexr_lib = static_library(
  'framexr',
  framexr_all_sources,
  shader_spirv,
  capture_generated,
  openvr_generated,
  openxr_generated,
  include_directories: framexr_inc,
  cpp_args: [
    '--embed-dir=' + shader_embed_dir,
    '--embed-dir=' + meson.project_source_root() / 'res',
  ],
  dependencies: [threads_dep, dl_dep, vulkan_dep, vma_dep, eigen_dep, json_dep, yaml_cpp_dep, miniaudio_dep, freetype_dep] + platform_deps + capture_deps + hid_deps + openvr_deps + openxr_deps,
)

framexr_dep = declare_dependency(
  link_with: framexr_lib,
  include_directories: framexr_inc,
  dependencies: [threads_dep, dl_dep, vulkan_dep, vma_dep, eigen_dep, json_dep, yaml_cpp_dep, miniaudio_dep, freetype_dep] + platform_deps + capture_deps + hid_deps + openvr_deps + openxr_deps,
  sources: [version_h, shader_spirv],
)

executable(
  meson.project_name(),
  'main.cpp',
  dependencies: framexr_dep,
  install: true,
)
