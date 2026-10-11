googletest_root = meson.project_source_root() / 'external/googletest/googletest'

gtest_lib = static_library(
  'gtest',
  googletest_root / 'src/gtest-all.cc',
  include_directories: include_directories('../external/googletest/googletest/include', '../external/googletest/googletest'),
  cpp_args: ['-w'],
  override_options: ['werror=false', 'warning_level=0'],
  dependencies: threads_dep,
)

gtest_main_lib = static_library(
  'gtest_main',
  googletest_root / 'src/gtest_main.cc',
  include_directories: include_directories('../external/googletest/googletest/include'),
  cpp_args: ['-w'],
  override_options: ['werror=false', 'warning_level=0'],
)

gtest_dep = declare_dependency(
  link_with: gtest_lib,
  include_directories: include_directories('../external/googletest/googletest/include', is_system: true),
  dependencies: threads_dep,
)

test_support_inc = include_directories('support')
test_env = {'XDG_CONFIG_HOME': meson.current_build_dir() / 'config-home', 'FRAMEXR_LOG': 'warn'}

unit_tests = executable(
  'framexr-tests',
  'ConfigTest.cpp',
  'DesignerTest.cpp',
  'KeyboardTest.cpp',
  'KeyTranslatorTest.cpp',
  'LabelTest.cpp',
  'ModularTest.cpp',
  'OscTest.cpp',
  'RaycastTest.cpp',
  'ResourceTest.cpp',
  'ToastIngestTest.cpp',
  'YamlTest.cpp',
  link_with: gtest_main_lib,
  dependencies: [framexr_dep, gtest_dep],
  cpp_args: ['--embed-dir=' + meson.project_source_root() / 'res'],
)

test('unit', unit_tests, env: test_env, protocol: 'gtest')

integration_tests = executable(
  'framexr-integration-tests',
  'ActionTest.cpp',
  'EditorTest.cpp',
  'InteractionTest.cpp',
  'KeyStateTest.cpp',
  'OverlayRenderTest.cpp',
  'ScreenCaptureTest.cpp',
  'support/FakeCapture.cpp',
  'support/GpuReadback.cpp',
  'support/IntegrationMain.cpp',
  'support/Png.cpp',
  'support/RecordingBackend.cpp',
  'support/RecordingHid.cpp',
  'support/TestApp.cpp',
  include_directories: test_support_inc,
  dependencies: [framexr_dep, gtest_dep],
)

test(
  'integration',
  integration_tests,
  env: test_env + {'FRAMEXR_TEST_OUTPUT': meson.current_build_dir() / 'integration-output'},
  protocol: 'gtest',
  suite: 'integration',
  timeout: 180,
  is_parallel: false,
)
