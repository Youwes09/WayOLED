project('wayoled', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=2'])

add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols')
wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
  wayland_scanner_dep.get_variable('wayland_scanner'))

libdrm = dependency('libdrm')
libudev = dependency('libudev')

protocols_dir = meson.current_source_dir() / 'protocols'

wl_protocol_headers = []
wl_protocol_sources = []

protocol_names = [
  'xdg-shell',
  'wlr-layer-shell-unstable-v1',
  'wlr-screencopy-unstable-v1',
  'wlr-gamma-control-unstable-v1',
  'ext-idle-notify-v1',
]

foreach name : protocol_names
  xml = protocols_dir / (name + '.xml')

  header = custom_target(name + '-client-header',
    input: xml,
    output: name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

  source = custom_target(name + '-client-source',
    input: xml,
    output: name + '-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

  wl_protocol_headers += header
  wl_protocol_sources += source
endforeach

subdir('src')
subdir('oledctl')