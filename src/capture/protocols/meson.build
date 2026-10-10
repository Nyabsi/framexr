wayland_scanner = find_program(
  dependency('wayland-scanner', native: true).get_variable(pkgconfig: 'wayland_scanner')
)
wayland_protocols_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

capture_protocol_headers = []
capture_protocol_sources = []
foreach protocol : [
  wayland_protocols_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  meson.project_source_root() / 'protocols/wlr-export-dmabuf-unstable-v1.xml',
  meson.project_source_root() / 'protocols/wlr-screencopy-unstable-v1.xml',
]
  base = fs.stem(protocol)
  capture_protocol_headers += custom_target(
    base + '-client-protocol.h',
    input: protocol,
    output: base + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  capture_protocol_sources += custom_target(
    base + '-protocol.c',
    input: protocol,
    output: base + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

capture_generated += capture_protocol_headers + capture_protocol_sources
capture_protocols_dep = declare_dependency(
  include_directories: include_directories('.', is_system: true),
  sources: capture_protocol_headers,
)
