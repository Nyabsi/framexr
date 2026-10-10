capture_sources = files(
  'Frame.cpp',
)
capture_deps = []
capture_generated = []

if get_option('wayland').allowed()
  wayland_client_dep = dependency('wayland-client', include_type: 'system')
  pipewire_dep = dependency('libpipewire-0.3', include_type: 'system')
  dbus_capture_dep = dependency('dbus-1', include_type: 'system')
  subdir('protocols')

  capture_sources += files(
    'PipewireCapture.cpp',
    'ScreenCastPortal.cpp',
    'WaylandClient.cpp',
    'WlrDmabufCapture.cpp',
    'WlrScreencopyCapture.cpp',
  )
  capture_deps += [wayland_client_dep, pipewire_dep, dbus_capture_dep, capture_protocols_dep]
endif

if get_option('x11').allowed()
  capture_sources += files('XshmCapture.cpp')
  capture_deps += [
    dependency('x11', include_type: 'system'),
    dependency('xext', include_type: 'system'),
    dependency('xrandr', include_type: 'system'),
  ]
endif
