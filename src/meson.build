framexr_inc = include_directories(
  '.',
  '..',
  'config',
  'core',
  'editor',
  'form',
  'form/components',
  'form/shared',
  'input',
  'input/hid/null',
  'input/hid/shared',
  'integration/linux',
  'integration/shared',
  'overlay',
  'overlay/views',
  'platform',
  'renderer',
  'runtime',
  'runtime/openvr',
  'runtime/openxr',
  'runtime/shared',
  'screen',
  'screen/capture',
  'screen/capture/pipewire',
  'screen/capture/wayland',
  'screen/capture/win32',
  'screen/capture/x11',
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
    'platform/win32/Utf16.cpp',
  )
else
  core_sources += files(
    'platform/posix/ExitStatus.cpp',
    'platform/posix/Platform.cpp',
    'platform/posix/Process.cpp',
  )
endif

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

integration_sources = files(
  'integration/Chatbox.cpp',
  'integration/Notifications.cpp',
  'integration/Osc.cpp',
  'integration/ToastIngest.cpp',
)

if is_windows
  integration_sources += files(
    'integration/win32/Notifications.cpp',
    'integration/win32/UdpSocket.cpp',
  )
else
  integration_sources += files(
    'integration/linux/Dbus.cpp',
    'integration/linux/DbusNotificationSender.cpp',
    'integration/linux/Notifications.cpp',
    'integration/linux/ToastIngest.cpp',
    'integration/linux/UdpSocket.cpp',
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

screen_sources = files(
  'screen/CapturedFrame.cpp',
  'screen/FrameUploader.cpp',
  'screen/PendingFrame.cpp',
  'screen/ScreenInteraction.cpp',
  'screen/ScreenPipeline.cpp',
  'screen/ScreenRenderer.cpp',
)

if get_option('wayland').allowed()
  screen_sources += files('screen/Mirror.cpp')
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
  'overlay/views/KeyboardPicker.cpp',
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
  'input/hid/HidProvider.cpp',
  'input/hid/KeyTranslator.cpp',
  'input/hid/null/NullHidProvider.cpp',
  'input/hid/VirtualKeys.cpp',
)
if is_windows
  hid_sources += files(
    'input/hid/win32/HidProvider.cpp',
    'input/hid/win32/ImeContext.cpp',
    'input/hid/win32/InputMethod.cpp',
    'input/hid/win32/Keymap.cpp',
    'input/hid/win32/LayoutInputMethod.cpp',
    'input/hid/win32/Scancodes.cpp',
    'input/hid/win32/SendInputProvider.cpp',
    'input/hid/win32/tsf/TsfCandidateSink.cpp',
    'input/hid/win32/tsf/TsfContext.cpp',
    'input/hid/win32/tsf/TsfTextStore.cpp',
  )
else
  hid_sources += files(
    'input/hid/linux/DesktopInputMethod.cpp',
    'input/hid/linux/HidProvider.cpp',
    'input/hid/linux/ImeContext.cpp',
    'input/hid/linux/InputMethod.cpp',
    'input/hid/linux/Keymap.cpp',
    'input/hid/linux/UInputProvider.cpp',
    'input/hid/linux/ibus/IbusContext.cpp',
    'input/hid/linux/ibus/IbusDaemon.cpp',
  )
endif

subdir('screen/capture')
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

framexr_all_sources = (config_sources + core_sources + editor_sources + form_sources + hid_sources
  + input_sources + integration_sources + overlay_sources + screen_sources + runtime_sources + util_sources + capture_sources + renderer_sources + openvr_sources
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
