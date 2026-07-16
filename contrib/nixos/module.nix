flake:
{ config, lib, pkgs, ... }:

with lib;

let
  cfg = config.services.wayoled;
  pkg = flake.packages.${pkgs.system}.default;
in
{
  options.services.wayoled = {
    enable = mkEnableOption "WayOLED, an OLED-care daemon for wlroots-based Wayland compositors";

    package = mkOption {
      type = types.package;
      default = pkg;
      defaultText = literalExpression "wayoled.packages.<system>.default";
      description = "The wayoled package to use.";
    };

    useSetuidHelper = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Install `wayoled-brightness-helper` as a setuid-root wrapper via
        `security.wrappers`. Only needed as a fallback for backlight
        devices not covered by the shipped udev rule (Intel, AMD/amdgpu,
        and ACPI-generic backlights already get group-writable brightness
        files and never need this). Leave disabled unless `wayoled`
        logs "backlight is read-only for this user" for your hardware.
      '';
    };

    extraGroups = mkOption {
      type = types.listOf types.str;
      default = [ ];
      description = ''
        Extra groups (beyond `video`, added automatically) that should
        be granted the udev-based backlight write access.
      '';
    };

    users = mkOption {
      type = types.listOf types.str;
      default = [ ];
      example = [ "shozikan" ];
      description = ''
        Usernames to add to the `video` group so they get udev-granted
        backlight write access. NixOS user group membership is
        deliberately not modified implicitly by this module beyond
        what you list here.
      '';
    };

    settings = mkOption {
      type = types.submodule {
        options = {
          dimFactor = mkOption {
            type = types.float;
            default = 0.7;
            description = "Gamma multiplier applied while dimmed (0.0-1.0).";
          };
          staticThresholdPolls = mkOption {
            type = types.int;
            default = 20;
            description = "Consecutive static-content polls before risk-dimming triggers.";
          };
          minSafeBrightness = mkOption {
            type = types.int;
            default = 2;
            description = "Backlight percentage floor; wayoled will never target below this.";
          };
          riskMonitorEnabled = mkOption {
            type = types.bool;
            default = true;
            description = "Enable static-content + idle burn-in risk detection.";
          };
          colortemp = mkOption {
            type = types.bool;
            default = true;
            description = "Enable time-of-day color temperature shifting.";
          };
          dayTemp = mkOption {
            type = types.int;
            default = 6500;
            description = "Color temperature (Kelvin) during the day.";
          };
          nightTemp = mkOption {
            type = types.int;
            default = 3400;
            description = "Color temperature (Kelvin) in the evening/night.";
          };
        };
      };
      default = { };
      description = ''
        Values written to the generated `default` profile at
        /etc/wayoled/profiles/default.conf. To ship additional named
        profiles (e.g. "movie"), use `environment.etc` directly with
        the same key=value format this generates.
      '';
    };
  };

  config = mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    environment.etc."wayoled/profiles/default.conf".text = ''
      dim_factor=${toString cfg.settings.dimFactor}
      static_threshold_polls=${toString cfg.settings.staticThresholdPolls}
      min_safe_brightness=${toString cfg.settings.minSafeBrightness}
      risk_monitor_enabled=${if cfg.settings.riskMonitorEnabled then "1" else "0"}
      colortemp_enabled=${if cfg.settings.colortemp then "1" else "0"}
      day_temp=${toString cfg.settings.dayTemp}
      night_temp=${toString cfg.settings.nightTemp}
    '';

    users.users = genAttrs cfg.users (_: { extraGroups = [ "video" ] ++ cfg.extraGroups; });

    services.udev.extraRules = ''
      SUBSYSTEM=="backlight", KERNEL=="amdgpu_bl*", RUN+="${pkgs.coreutils}/bin/chgrp video $sys$devpath/brightness", RUN+="${pkgs.coreutils}/bin/chmod g+w $sys$devpath/brightness"
      SUBSYSTEM=="backlight", KERNEL=="intel_backlight", RUN+="${pkgs.coreutils}/bin/chgrp video $sys$devpath/brightness", RUN+="${pkgs.coreutils}/bin/chmod g+w $sys$devpath/brightness"
      SUBSYSTEM=="backlight", KERNEL=="acpi_video*", RUN+="${pkgs.coreutils}/bin/chgrp video $sys$devpath/brightness", RUN+="${pkgs.coreutils}/bin/chmod g+w $sys$devpath/brightness"
    '';

    users.groups.video = { };

    security.wrappers = mkIf cfg.useSetuidHelper {
      wayoled-brightness-helper = {
        source = "${cfg.package}/bin/wayoled-brightness-helper";
        owner = "root";
        group = "root";
        setuid = true;
        capabilities = null;
      };
    };

    systemd.user.services.wayoled = {
      description = "WayOLED display protection daemon";
      wantedBy = [ "default.target" ];
      unitConfig = {
        StartLimitIntervalSec = 60;
        StartLimitBurst = 5;
      };
      serviceConfig = {
        Type = "simple";
        ExecStart = "${cfg.package}/bin/wayoled";
        Restart = "on-failure";
        RestartSec = 2;
      };
    };
  };
}
