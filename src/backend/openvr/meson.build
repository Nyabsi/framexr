openvr_sources = []
openvr_deps = []
openvr_generated = []

if get_option('openvr').allowed()
  openvr_root = meson.project_source_root() / 'external/openvr'
  openvr_api_lib = static_library(
    'openvr_api',
    files(
      openvr_root / 'src/jsoncpp.cpp',
      openvr_root / 'src/openvr_api_public.cpp',
      openvr_root / 'src/vrcore/dirtools_public.cpp',
      openvr_root / 'src/vrcore/envvartools_public.cpp',
      openvr_root / 'src/vrcore/hmderrors_public.cpp',
      openvr_root / 'src/vrcore/pathtools_public.cpp',
      openvr_root / 'src/vrcore/sharedlibtools_public.cpp',
      openvr_root / 'src/vrcore/strtools_public.cpp',
      openvr_root / 'src/vrcore/vrpathregistry_public.cpp',
    ),
    include_directories: include_directories('../../../external/openvr/src', '../../../external/openvr/src/vrcore', '../../../external/openvr/headers'),
    cpp_args: ['-DVR_API_PUBLIC', '-DOPENVR_BUILD_STATIC', '-DVRCORE_NO_PLATFORM', '-w']
      + (is_windows ? ['-DWIN32', '-DWIN64'] : ['-DLINUX', '-DLINUX64', '-DPOSIX']),
    gnu_symbol_visibility: 'hidden',
    override_options: ['werror=false', 'warning_level=0'],
  )

  openvr_deps += declare_dependency(
    link_with: openvr_api_lib,
    include_directories: include_directories('../../../external/openvr/headers', is_system: true),
    compile_args: ['-DOPENVR_BUILD_STATIC'],
    dependencies: [dl_dep] + (is_windows ? [cpp.find_library('advapi32'), cpp.find_library('shell32')] : []),
  )

  openvr_sources += files(
    'OpenVrBackend.cpp',
    'OpenVrInput.cpp',
    'OpenVrLines.cpp',
    'OpenVrManifest.cpp',
    'OpenVrMath.cpp',
    'OpenVrOverlay.cpp',
    'OpenVrPlayspace.cpp',
  )
endif
