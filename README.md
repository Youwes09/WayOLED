# WayOLED

Display protection daemon for wlroots-based Wayland compositors. Reduces OLED
burn-in risk and manages output brightness, gamma, and color temperature.

## Features

- Non-PWM dimming through the gamma ramp, or an ordered-dither pixel mask
- Time-of-day color temperature
- Per-channel gamma curve
- Static-content plus idle detection, with automatic dimming
- Pixel-refresh sweep (full-screen color cycle) on request
- Backlight smoothing, with tracking of external brightness changes
- Time-based profile scheduling
- Per-output control, including hotplug
- Runtime feature gating based on the protocols the compositor exposes

## Requirements

A wlroots-based compositor: Sway, Hyprland, river, Wayfire, labwc, and others.
Individual features depend on Wayland protocols:

| Feature | Protocol |
| --- | --- |
| Dimming, color temperature, gamma curve | wlr-gamma-control-unstable-v1 |
| Static-content detection | wlr-screencopy-unstable-v1 |
| Pixel-refresh sweep | wlr-layer-shell-unstable-v1 |
| Idle detection | ext-idle-notify-v1 |

A missing protocol disables the matching feature. `oledctl capabilities`
reports what is active. Backlight control uses `/sys/class/backlight` and does
not depend on the compositor.

Build dependencies: `meson`, `ninja`, `wayland`, `wayland-protocols`,
`wayland-scanner`, `libudev`, a C compiler.

## Build

    meson setup build
    ninja -C build

Binaries land in `build/src/wayoled` and `build/oledctl/oledctl`.

## Install

    meson setup build --buildtype=release
    ninja -C build
    sudo ninja -C build install

Installed files:

- `wayoled`, `oledctl`, `wayoled-brightness-helper` in bindir
- `wayoled.service`, a systemd user unit
- `90-wayoled-backlight.rules` in the udev rules directory
- example profiles in `/usr/share/wayoled/profiles`
- `schedule.conf.example` in `/etc/wayoled`

Build options:

| Option | Default | Effect |
| --- | --- | --- |
| `install-systemd-unit` | true | install the systemd user unit |
| `install-setuid-helper` | false | install the brightness helper setuid root |
| `systemd-user-unit-dir` | auto | override the unit install path |

### Nix

The flake exposes `packages.default` and `nixosModules.default`.

    nix run github:Youwes09/WayOLED

On NixOS, add the flake as an input and import the module:

    {
      inputs.wayoled.url = "github:Youwes09/WayOLED";

      # in your configuration
      imports = [ inputs.wayoled.nixosModules.default ];
      services.wayoled.enable = true;
      services.wayoled.users = [ "yourname" ];
    }

## Backlight access

`wayoled` writes `/sys/class/backlight/<device>/brightness`. Grant write access
one of two ways:

- The installed udev rule adds group `video` write access for common devices.
  Add your user to the `video` group, then reboot or replug.
- Build with `-Dinstall-setuid-helper=true`. `wayoled` then runs
  `wayoled-brightness-helper` as root for the write.

Without either, brightness control is disabled and the rest of the daemon runs
normally.

## Running

As a systemd user service:

    systemctl --user enable --now wayoled

Or from the compositor autostart:

    wayoled

## Configuration

### Profiles

`~/.config/wayoled/profiles/<name>.conf` overrides
`/etc/wayoled/profiles/<name>.conf`. The `default` profile is applied to every
output at startup.

| Key | Meaning |
| --- | --- |
| `dim_factor` | gamma multiplier when dimmed, 0.0 to 1.0 |
| `static_threshold_polls` | static 30-second polls before dimming |
| `min_safe_brightness` | lower bound for `brightness set`, percent |
| `risk_monitor_enabled` | 1 to allow automatic dimming |
| `colortemp_enabled` | 1 to apply time-of-day color temperature |
| `day_temp`, `night_temp` | Kelvin for day and night |
| `gamma` | `V` or `R:G:B`, per-channel gamma, 0.1 to 10.0 |
| `dim_mode` | `gamma` (default) or `mask` |
| `mask_density` | fraction of pixels turned off in mask mode, 0.0 to 1.0 |
| `mask_area` | `x:y:w:h` in output-local pixels, or `full` (default) |
| `mask_shift_interval_s` | seconds between phase shifts of the mask, 0 disables, default 3600 |
| `monitor` | comma-separated output names, or `all` |

`dim_mode=mask` dims by covering an ordered-dither fraction of pixels with an
opaque overlay instead of scaling the gamma ramp. Those pixels go fully off on
an OLED panel rather than merely darker, and it works without
`wlr-gamma-control`, needing only `wlr-layer-shell-v1`. It trades a visible
dither pattern for that independence, so `gamma` is still the default.

`mask_area` restricts the mask to part of one output, such as a bar, instead
of the whole screen. `mask_shift_interval_s` periodically rotates which
pixels in the pattern are off, so a masked static region does not stress the
same subpixels forever.

`oledctl mask-region` runs `slurp` to drag-select a rectangle and prints the
`monitor=` and `mask_area=` lines for the profile. `oledctl mask-region
--write NAME` writes those two lines into
`~/.config/wayoled/profiles/NAME.conf` instead, creating the file or
directory if needed and replacing only those two lines if the file already
exists. Neither form requires a running daemon.

Color temperature holds `night_temp` from 21:00 to 06:00, `day_temp` from 08:00
to 19:00, and interpolates across 06:00 to 08:00 and 19:00 to 21:00.

### Schedule

`/etc/wayoled/schedule.conf`, one entry per line:

    HH:MM profile [output]

An entry with no output field applies to every output. See
`schedule.conf.example`. Unpinned outputs follow the schedule. `oledctl profile`
pins an output to a profile; `oledctl auto` unpins it.

## oledctl

    oledctl <command> [args] [--monitor NAME]

| Command | Action |
| --- | --- |
| `status` | daemon and per-output state |
| `dim`, `restore` | force dimming on or off, gamma or mask per profile |
| `pause`, `resume` | suspend or resume automatic dimming |
| `brightness get\|set <pct>\|step <+-pct>` | backlight |
| `refresh [stop]` | run or cancel the pixel-refresh sweep |
| `profile [name]` | show, or switch and pin |
| `profiles` | list profile names |
| `auto` | unpin, return control to the schedule |
| `colortemp get\|on\|off` | time-of-day color temperature |
| `gamma get\|<v>\|<r:g:b>\|reset` | per-channel gamma curve |
| `monitors` | detected output names |
| `capabilities` | features the compositor supports |
| `mask-region [--write NAME]` | drag-select with `slurp`, print or write profile lines |

`--monitor NAME` targets one output. Without it, a command applies to all
outputs, or to the single output when there is only one. `mask-region` is a
client-side helper and ignores `--monitor`; it works without a running daemon.

## Packaging

| Target | Location |
| --- | --- |
| Arch | `contrib/packaging/PKGBUILD` |
| Alpine | `contrib/packaging/APKBUILD` |
| Fedora, openSUSE | `contrib/packaging/wayoled.spec` |
| Debian, Ubuntu | `contrib/packaging/debian/`, copy to `./debian/` then `dpkg-buildpackage -b` |
| NixOS | `flake.nix` and `contrib/nixos/module.nix` |

The Arch and RPM specs fetch the `v$version` release tarball, so a matching git
tag must exist.

## License

MIT. See `LICENSE`.
