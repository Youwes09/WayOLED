Name:           wayoled
Version:        0.1.0
Release:        1%{?dist}
Summary:        Display protection daemon for wlroots-based Wayland compositors

License:        MIT
URL:            https://github.com/Youwes09/WayOLED
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  wayland-protocols-devel
BuildRequires:  systemd-rpm-macros

%description
WayOLED reduces OLED burn-in risk and manages output brightness, gamma, and
color temperature on wlroots-based Wayland compositors. It provides non-PWM
dimming, time-of-day color temperature, a per-channel gamma curve, static
content plus idle detection with automatic dimming, an on-demand pixel-refresh
sweep, and backlight smoothing. The oledctl client controls a running daemon
over a Unix socket.

%prep
%autosetup -n WayOLED-%{version}

%build
%meson
%meson_build

%install
%meson_install

%files
%license LICENSE
%doc README.md
%{_bindir}/wayoled
%{_bindir}/oledctl
%{_bindir}/wayoled-brightness-helper
%{_userunitdir}/wayoled.service
%{_udevrulesdir}/90-wayoled-backlight.rules
%dir %{_datadir}/wayoled
%dir %{_datadir}/wayoled/profiles
%{_datadir}/wayoled/profiles/default.conf
%{_datadir}/wayoled/profiles/movie.conf
%dir %{_sysconfdir}/wayoled
%config(noreplace) %{_sysconfdir}/wayoled/schedule.conf.example

%changelog
* Tue Sep 02 2026 Youwes09 <yuvi.shen@outlook.com> - 0.1.0-1
- Initial package.
