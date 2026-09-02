project('wayoled', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'b_pie=true',
  ],
)

cc = meson.get_compiler('c')

wayland_client   = dependency('wayland-client')
wayland_protos   = dependency('wayland-protocols')
wayland_scanner  = dependency('wayland-scanner', native: true)
libudev          = dependency('libudev')
libm             = cc.find_library('m', required: true)

wayland_scanner_prog = find_program(
  wayland_scanner.get_variable(pkgconfig: 'wayland_scanner'),
  native: true,
)

wl_protocol_dir = wayland_protos.get_variable(pkgconfig: 'pkgdatadir')

protocols = [
  { 'name': 'xdg-shell',                       'src': wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml' },
  { 'name': 'wlr-layer-shell-unstable-v1',      'src': meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml' },
  { 'name': 'wlr-gamma-control-unstable-v1',    'src': meson.project_source_root() / 'protocols/wlr-gamma-control-unstable-v1.xml' },
  { 'name': 'wlr-screencopy-unstable-v1',       'src': meson.project_source_root() / 'protocols/wlr-screencopy-unstable-v1.xml' },
  { 'name': 'ext-idle-notify-v1',               'src': meson.project_source_root() / 'protocols/ext-idle-notify-v1.xml' },
]

wl_protocol_sources = []
wl_protocol_headers = []

foreach p : protocols
  wl_protocol_headers += custom_target(p['name'] + '-client-header',
    input: p['src'],
    output: p['name'] + '-client-protocol.h',
    command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  wl_protocol_sources += custom_target(p['name'] + '-client-source',
    input: p['src'],
    output: p['name'] + '-client-protocol.c',
    command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

install_brightness_helper_setuid = get_option('install-setuid-helper')

if install_brightness_helper_setuid
  helper_install_mode = ['rwsr-xr-x', 'root', 'root']
else
  helper_install_mode = ['rwxr-xr-x', 'root', 'root']
endif

subdir('src')
subdir('oledctl')

brightness_helper = executable('wayoled-brightness-helper',
  'contrib/wayoled-brightness-helper.c',
  install: true,
  install_dir: get_option('bindir'),
  install_mode: helper_install_mode,
)

install_data('contrib/udev/90-wayoled-backlight.rules',
  install_dir: get_option('prefix') / 'lib/udev/rules.d')

install_data('contrib/schedule.conf.example',
  install_dir: get_option('sysconfdir') / 'wayoled',
  rename: 'schedule.conf.example')

install_data(['profiles/default.conf', 'profiles/movie.conf'],
  install_dir: get_option('datadir') / 'wayoled/profiles')

if get_option('install-systemd-unit')
  systemd_userdir = get_option('systemd-user-unit-dir')
  if systemd_userdir == ''
    systemd_dep = dependency('systemd', required: false)
    if systemd_dep.found()
      systemd_userdir = systemd_dep.get_variable(pkgconfig: 'systemduserunitdir')
    else
      systemd_userdir = get_option('prefix') / 'lib/systemd/user'
    endif
  endif
  install_data('contrib/systemd/wayoled.service', install_dir: systemd_userdir)
endif
