framexr_inc = include_directories(
  '.',
  '..',
  'backend',
  'capture',
  'capture/pipewire',
  'capture/wayland',
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
)

core_sources = files(
  'core/Error.cpp',
  'core/Logger.cpp',
  'core/Process.cpp',
)

config_sources = files(
  'config/Config.cpp',
  'config/Json.cpp',
  'config/Yaml.cpp',
)

state_sources = files(
  'CommandLine.cpp',
  'State.cpp',
)

backend_sources = files(
  'backend/Input.cpp',
  'backend/OneEuroFilter.cpp',
  'backend/Notifications.cpp',
  'backend/Osc.cpp',
  'backend/Overlay.cpp',
  'backend/OverlayContainer.cpp',
  'backend/OverlayLayout.cpp',
  'backend/Task.cpp',
)

editor_sources = files(
  'editor/Editor.cpp',
  'editor/EditorPanel.cpp',
  'editor/EditorProperties.cpp',
  'editor/FormDesigner.cpp',
  'editor/KeyboardDesigner.cpp',
  'editor/PanelBuilder.cpp',
)

gui_sources = files(
  'gui/Font.cpp',
  'gui/Theme.cpp',
  'gui/canvas/Canvas.cpp',
  'gui/canvas/Control.cpp',
  'gui/modular/Button.cpp',
  'gui/modular/Label.cpp',
  'gui/modular/Modular.cpp',
)

hid_sources = files(
  'hid/Hid.cpp',
  'hid/KeyTranslator.cpp',
  'hid/Keymap.cpp',
  'hid/VirtualKey.cpp',
)

overlay_sources = files(
  'overlays/CustomUi.cpp',
  'overlays/Keyboard.cpp',
  'overlays/KeyboardLayout.cpp',
  'overlays/Screen.cpp',
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
  'graphics/CommandBuffer.cpp',
  'graphics/DdsImage.cpp',
  'graphics/Device.cpp',
  'graphics/Image.cpp',
  'graphics/Pipeline.cpp',
  'graphics/Queue.cpp',
  'graphics/Shaders.cpp',
)

framexr_all_sources = (core_sources + config_sources + editor_sources + state_sources + backend_sources + hid_sources + gui_sources + overlay_sources
  + capture_sources + graphics_sources + openvr_sources + openxr_sources)

hid_deps = [xkbcommon_dep]
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
  dependencies: [threads_dep, dl_dep, vulkan_dep, vma_dep, eigen_dep, json_dep, yaml_cpp_dep, miniaudio_dep, freetype_dep, fontconfig_dep, dbus_dep] + capture_deps + hid_deps + openvr_deps + openxr_deps,
)

framexr_dep = declare_dependency(
  link_with: framexr_lib,
  include_directories: framexr_inc,
  dependencies: [threads_dep, dl_dep, vulkan_dep, vma_dep, eigen_dep, json_dep, yaml_cpp_dep, miniaudio_dep, freetype_dep, fontconfig_dep, dbus_dep] + capture_deps + hid_deps + openvr_deps + openxr_deps,
  sources: [version_h, shader_spirv],
)

executable(
  meson.project_name(),
  'main.cpp',
  dependencies: framexr_dep,
  install: true,
)
