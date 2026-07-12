wayoled_sources = files(
  'core/config.c',
  'core/main.c',
  'core/scheduler.c',
  'backlight/backlight.c',
  'backlight/inotify_watch.c',
  'colortemp/colortemp.c',
  'wayland/wl_client.c',
  'wayland/idle_notify.c',
  'wayland/screencopy.c',
  'wayland/layer_shell_overlay.c',
  'drm/kms_pan.c',
  'drm/gamma_lut.c',
  'ipc/ipc_server.c',
)

wayoled_deps = [
  wayland_client,
  libdrm,
  libudev,
]

executable('wayoled',
  wayoled_sources,
  wl_protocol_sources,
  wl_protocol_headers,
  dependencies: wayoled_deps,
  include_directories: include_directories('.'),
  install: true)
