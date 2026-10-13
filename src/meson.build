framexr_inc = include_directories(
  '.',
  '..',
  'capture',
  'capture/pipewire',
  'capture/wayland',
  'capture/win32',
  'capture/x11',
  'config',
  'core',
  'desktop',
  'editor',
  'form',
  'form/components',
  'form/shared',
  'renderer',
  'hid',
  'input',
  'net/shared',
  'overlay',
  'overlay/views',
  'platform',
  'runtime',
  'runtime/openvr',
  'runtime/openxr',
  'runtime/shared',
  'util',
)

core_sources = files(
  'core/Error.cpp',
  'core/Logger.cpp',
  'core/TaskQueue.cpp',
)
if is_windows
  core_sources += files(
    'platform/win32/ExitStatus.cpp',
    'platform/win32/Platform.cpp',
    'platform/win32/Process.cpp',
  )
else
  core_sources += files(
    'platform/posix/ExitStatus.cpp',
    'platform/posix/Platform.cpp',
    'platform/posix/Process.cpp',
  )
endif

app_sources = files(
  'AppState.cpp',
)

config_sources = files(
  'config/Config.cpp',
  'config/ConfigStore.cpp',
  'config/Forms.cpp',
  'config/Json.cpp',
  'config/KeyboardLayout.cpp',
  'config/Yaml.cpp',
)

input_sources = files(
  'input/Bindings.cpp',
  'input/Devices.cpp',
  'input/Interaction.cpp',
  'input/TextEntry.cpp',
  'input/methods/KeyboardInput.cpp',
  'input/methods/LaserPointer.cpp',
)

net_sources = files(
  'net/Notifications.cpp',
  'net/Osc.cpp',
  'net/ToastIngest.cpp',
)

if is_windows
  net_sources += files(
    'net/win32/Notifications.cpp',
    'net/win32/UdpSocket.cpp',
  )
else
  net_sources += files(
    'net/linux/Dbus.cpp',
    'net/linux/DbusNotificationSender.cpp',
    'net/linux/Notifications.cpp',
    'net/linux/ToastIngest.cpp',
    'net/linux/UdpSocket.cpp',
  )
endif

runtime_sources = files(
  'runtime/Runtime.cpp',
)

util_sources = files(
  'util/AudioPlayback.cpp',
  'util/ByteReader.cpp',
  'util/ByteWriter.cpp',
  'util/CommandLine.cpp',
  'util/OneEuroFilter.cpp',
  'util/PoseFilter.cpp',
  'util/Raycast.cpp',
  'util/RotationFilter.cpp',
  'util/TextSearch.cpp',
)

desktop_sources = files(
  'desktop/CapturedFrame.cpp',
  'desktop/FrameUploader.cpp',
  'desktop/PendingFrame.cpp',
  'desktop/ScreenInteraction.cpp',
  'desktop/ScreenPipeline.cpp',
  'desktop/ScreenRenderer.cpp',
)

if get_option('wayland').allowed()
  desktop_sources += files('desktop/Mirror.cpp')
endif

overlay_sources = files(
  'overlay/ActionBinding.cpp',
  'overlay/ActionRunner.cpp',
  'overlay/Compose.cpp',
  'overlay/Container.cpp',
  'overlay/View.cpp',
  'overlay/views/Desktop.cpp',
  'overlay/views/EditorPanel.cpp',
  'overlay/views/Keyboard.cpp',
  'overlay/views/Settings.cpp',
  'overlay/views/Toast.cpp',
  'overlay/views/Watch.cpp',
)

form_sources = files(
  'form/Form.cpp',
  'form/FormComponent.cpp',
  'form/FormTheme.cpp',
  'form/components/BatteryComponent.cpp',
  'form/components/BatteryListComponent.cpp',
  'form/components/ButtonComponent.cpp',
  'form/components/ChoiceGridComponent.cpp',
  'form/components/ClockComponent.cpp',
  'form/components/ColorFieldComponent.cpp',
  'form/components/ColorPickerComponent.cpp',
  'form/components/DragMultiplierComponent.cpp',
  'form/components/ExecComponent.cpp',
  'form/components/IpdComponent.cpp',
  'form/components/KeyComponent.cpp',
  'form/components/LabelComponent.cpp',
  'form/components/LaserInputComponent.cpp',
  'form/components/OutlineComponent.cpp',
  'form/components/OverlayListComponent.cpp',
  'form/components/PanelComponent.cpp',
  'form/components/RowComponent.cpp',
  'form/components/SectionComponent.cpp',
  'form/components/SpriteComponent.cpp',
  'form/components/StackComponent.cpp',
  'form/components/StepperComponent.cpp',
  'form/components/SwatchComponent.cpp',
  'form/components/TextLineComponent.cpp',
  'form/components/TimerComponent.cpp',
  'form/components/TimezoneComponent.cpp',
  'form/document/Actions.cpp',
  'form/document/FormDocument.cpp',
  'form/document/FormElements.cpp',
  'form/document/FormParser.cpp',
)
if is_windows
  form_sources += files('form/win32/FontLookup.cpp')
else
  form_sources += files('form/linux/FontLookup.cpp')
endif

editor_sources = files(
  'editor/ColorDialog.cpp',
  'editor/Drafts.cpp',
  'editor/Editor.cpp',
  'editor/FormDesigner.cpp',
  'editor/KeyboardDesigner.cpp',
  'editor/PanelPlacement.cpp',
  'editor/panels/ActionsPanel.cpp',
  'editor/panels/CapturePanel.cpp',
  'editor/panels/ColorPanel.cpp',
  'editor/panels/FontPanel.cpp',
  'editor/panels/GhostPanel.cpp',
  'editor/panels/KeyPickerPanel.cpp',
  'editor/panels/KeyboardProperties.cpp',
  'editor/panels/PalettePanel.cpp',
  'editor/panels/ThemeProperties.cpp',
  'editor/panels/TimezonePanel.cpp',
  'editor/panels/ToolbarPanel.cpp',
  'editor/panels/WatchProperties.cpp',
)

hid_sources = files(
  'hid/HidProvider.cpp',
  'hid/KeyTranslator.cpp',
  'hid/NullHidProvider.cpp',
  'hid/VirtualKeys.cpp',
)
if is_windows
  hid_sources += files(
    'hid/win32/HidProvider.cpp',
    'hid/win32/Keymap.cpp',
    'hid/win32/Scancodes.cpp',
    'hid/win32/SendInputProvider.cpp',
  )
else
  hid_sources += files(
    'hid/linux/HidProvider.cpp',
    'hid/linux/Keymap.cpp',
    'hid/linux/UInputProvider.cpp',
  )
endif

subdir('capture')
subdir('runtime/openvr')
subdir('runtime/openxr')
subdir('renderer/shaders')

renderer_sources = files(
  'renderer/Buffer.cpp',
  'renderer/CommandBatch.cpp',
  'renderer/CommandBuffer.cpp',
  'renderer/DdsImage.cpp',
  'renderer/DescriptorSet.cpp',
  'renderer/Device.cpp',
  'renderer/FontCache.cpp',
  'renderer/Image.cpp',
  'renderer/Pipeline.cpp',
  'renderer/Queue.cpp',
  'renderer/Shaders.cpp',
)

framexr_all_sources = (app_sources + config_sources + core_sources + desktop_sources + editor_sources + form_sources + hid_sources
  + input_sources + net_sources + overlay_sources + runtime_sources + util_sources + capture_sources + renderer_sources + openvr_sources
  + openxr_sources)

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
