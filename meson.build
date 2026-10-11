project(
  'framexr',
  ['c', 'cpp'],
  version: '25.4.2',
  license: 'GPL-3.0-only',
  meson_version: '>=1.3.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'werror=true',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

cpp = meson.get_compiler('cpp')
fs = import('fs')
is_windows = host_machine.system() == 'windows'

add_project_arguments(
  cpp.get_supported_arguments(
    '-Wshadow',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wold-style-cast',
    '-Wnon-virtual-dtor',
    '-Woverloaded-virtual',
    '-Wcast-align',
    '-Wnull-dereference',
    '-Wdouble-promotion',
    '-Wformat=2',
    '-Wimplicit-fallthrough',
    '-Wuseless-cast',
    '-Wduplicated-cond',
    '-Wduplicated-branches',
    '-Wlogical-op',
    '-Wmisleading-indentation',
    '-Wno-c++26-extensions',
    '-Wno-missing-field-initializers',
  ),
  language: 'cpp',
)

if is_windows
  add_project_link_arguments(
    '-static-libgcc',
    '-static-libstdc++',
    '-Wl,-Bstatic,--whole-archive', '-lwinpthread', '-Wl,--no-whole-archive,-Bdynamic',
    language: ['c', 'cpp'],
  )
endif

# ---------------------------------------------------------------------------
# Vendored libraries (git submodules in external/)
# ---------------------------------------------------------------------------

foreach sub : ['eigen', 'json', 'yaml-cpp', 'miniaudio', 'VulkanMemoryAllocator',
               'openvr', 'OpenXR-SDK']
  if not fs.is_dir('external' / sub / '.git') and not fs.is_file('external' / sub / '.git')
    error('Submodule external/@0@ is missing; run: git submodule update --init'.format(sub))
  endif
endforeach

eigen_dep = declare_dependency(
  include_directories: include_directories('external/eigen', is_system: true),
)

json_dep = declare_dependency(
  include_directories: include_directories('external/json/include', is_system: true),
)

yaml_cpp_lib = static_library(
  'yaml-cpp',
  files(
    'external/yaml-cpp/src/binary.cpp',
    'external/yaml-cpp/src/convert.cpp',
    'external/yaml-cpp/src/depthguard.cpp',
    'external/yaml-cpp/src/directives.cpp',
    'external/yaml-cpp/src/emit.cpp',
    'external/yaml-cpp/src/emitfromevents.cpp',
    'external/yaml-cpp/src/emitter.cpp',
    'external/yaml-cpp/src/emitterstate.cpp',
    'external/yaml-cpp/src/emitterutils.cpp',
    'external/yaml-cpp/src/exceptions.cpp',
    'external/yaml-cpp/src/exp.cpp',
    'external/yaml-cpp/src/fptostring.cpp',
    'external/yaml-cpp/src/memory.cpp',
    'external/yaml-cpp/src/node.cpp',
    'external/yaml-cpp/src/node_data.cpp',
    'external/yaml-cpp/src/nodebuilder.cpp',
    'external/yaml-cpp/src/nodeevents.cpp',
    'external/yaml-cpp/src/null.cpp',
    'external/yaml-cpp/src/ostream_wrapper.cpp',
    'external/yaml-cpp/src/parse.cpp',
    'external/yaml-cpp/src/parser.cpp',
    'external/yaml-cpp/src/regex_yaml.cpp',
    'external/yaml-cpp/src/scanner.cpp',
    'external/yaml-cpp/src/scanscalar.cpp',
    'external/yaml-cpp/src/scantag.cpp',
    'external/yaml-cpp/src/scantoken.cpp',
    'external/yaml-cpp/src/simplekey.cpp',
    'external/yaml-cpp/src/singledocparser.cpp',
    'external/yaml-cpp/src/stream.cpp',
    'external/yaml-cpp/src/tag.cpp',
  ),
  include_directories: include_directories('external/yaml-cpp/include', 'external/yaml-cpp/src'),
  cpp_args: ['-DYAML_CPP_STATIC_DEFINE', '-w'],
  override_options: ['werror=false', 'warning_level=0'],
)
yaml_cpp_dep = declare_dependency(
  link_with: yaml_cpp_lib,
  include_directories: include_directories('external/yaml-cpp/include', is_system: true),
  compile_args: ['-DYAML_CPP_STATIC_DEFINE'],
)

miniaudio_dep = declare_dependency(
  include_directories: include_directories('external/miniaudio', is_system: true),
)

vma_dep = declare_dependency(
  include_directories: include_directories('external/VulkanMemoryAllocator/include', is_system: true),
)

# ---------------------------------------------------------------------------
# System libraries (C APIs used directly)
# ---------------------------------------------------------------------------

threads_dep = dependency('threads')
dl_dep = cpp.find_library('dl', required: false)
vulkan_dep = dependency('vulkan', include_type: 'system')
freetype_dep = dependency('freetype2', include_type: 'system')

if is_windows
  platform_deps = [
    cpp.find_library('ws2_32'),
    cpp.find_library('d3d11'),
    cpp.find_library('dxgi'),
    cpp.find_library('shell32'),
    cpp.find_library('ole32'),
  ]
else
  platform_deps = [
    dependency('fontconfig', include_type: 'system'),
    dependency('dbus-1', include_type: 'system'),
    dependency('xkbcommon', include_type: 'system'),
  ]
endif

feat_openvr = get_option('openvr')
feat_openxr = get_option('openxr')
feat_osc = get_option('osc')
feat_x11 = get_option('x11')
feat_wayland = get_option('wayland')

if feat_openvr.disabled() and feat_openxr.disabled()
  error('No VR support! Enable either openvr or openxr features!')
endif
if is_windows
  if feat_openxr.allowed() or feat_x11.allowed() or feat_wayland.allowed()
    error('Windows builds are OpenVR only, configure with --cross-file cross/windows.ini')
  endif
elif feat_wayland.disabled() and feat_x11.disabled()
  error('No desktop support! Enable either wayland or x11 features!')
endif

config = configuration_data()
config.set10('FRAMEXR_FEATURE_OPENVR', feat_openvr.allowed())
config.set10('FRAMEXR_FEATURE_OPENXR', feat_openxr.allowed())
config.set10('FRAMEXR_FEATURE_OSC', feat_osc.allowed())
config.set10('FRAMEXR_FEATURE_X11', feat_x11.allowed())
config.set10('FRAMEXR_FEATURE_WAYLAND', feat_wayland.allowed())
config.set10('FRAMEXR_FEATURE_PIPEWIRE', feat_wayland.allowed())
config.set_quoted('FRAMEXR_PKG_NAME', meson.project_name())
config.set_quoted('FRAMEXR_PKG_VERSION', meson.project_version())

job = run_command('sh', '-c', 'echo "$GITHUB_JOB"', check: false).stdout().strip()
build_suffix = ''
if job == 'make_release'
  build_suffix = ' (Release)'
elif job == 'build_appimage'
  build_suffix = ' (AppImage)'
endif
config.set_quoted('FRAMEXR_BUILD_SUFFIX', build_suffix)

configure_file(output: 'framexr_config.h', configuration: config)

# Version string from `git describe`, normalized at runtime.
version_h = vcs_tag(
  command: ['git', 'describe', '--tags', '--abbrev=7', '--dirty', '--always'],
  input: 'src/version.h.in',
  output: 'version.h',
  fallback: meson.project_version() + '-unknown',
)

subdir('src')
subdir('tests')

# ---------------------------------------------------------------------------
# Style enforcement
# ---------------------------------------------------------------------------
# `ninja clang-format`, `ninja clang-format-check` and `ninja clang-tidy` are
# generated automatically by meson from .clang-format / .clang-tidy.
clang_format = find_program('clang-format', required: false)
if clang_format.found()
  test(
    'clang-format',
    clang_format,
    args: ['--dry-run', '--Werror'] + framexr_all_sources,
    workdir: meson.project_source_root(),
    suite: 'style',
  )
endif
