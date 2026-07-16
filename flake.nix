{
  description = "WayOLED - OLED care daemon for wlroots-based Wayland compositors";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "wayoled";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libdrm
            systemd # provides libudev
          ];

          preConfigure = ''
            rm -rf build
          '';

          mesonFlags = [
            # Never setuid inside the store; the NixOS module uses
            # security.wrappers for that instead (see contrib/nixos/module.nix).
            (pkgs.lib.mesonBool "install-setuid-helper" false)
            # The unit shipped here is for non-NixOS use
            # (contrib/systemd/wayoled.service); the NixOS module defines
            # its own systemd.user.services.wayoled instead.
            (pkgs.lib.mesonBool "install-systemd-unit" false)
          ];

          meta = with pkgs.lib; {
            description = "OLED care daemon (pixel shift, burn-in mitigation, smooth backlight) for Wayland";
            license = licenses.mit; # adjust as you like
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
            gdb
            clang-tools # clangd for LSP, clang-format
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libdrm
            systemd
          ];
        };
      }) // {
        nixosModules.default = import ./contrib/nixos/module.nix self;

        # Full-stack integration test: boots a real NixOS VM, enables the
        # module, and drives it through systemd/udev/IPC end-to-end.
        # Run with: nix flake check   (or: nix build .#checks.x86_64-linux.vmTest -L)
        #
        # LIMITS: this uses sway with WLR_BACKEND=headless, so wl_display_connect
        # succeeds and the wlr protocols (screencopy/gamma-control/layer-shell)
        # exist and can be exercised programmatically -- but nothing is actually
        # rendered to a GPU/display, so this cannot verify real pixel dimming
        # or color-temperature changes are visually correct. It verifies wiring:
        # service starts, udev grants backlight access, generated profile
        # values are read, and IPC round-trips.
        checks = nixpkgs.lib.genAttrs
          (builtins.filter (s: nixpkgs.lib.hasSuffix "linux" s) flake-utils.lib.defaultSystems)
          (system:
            let pkgs = import nixpkgs { inherit system; }; in
            {
              vmTest = pkgs.testers.runNixOSTest {
                name = "wayoled-integration";

                nodes.machine = { config, pkgs, ... }: {
                  imports = [ self.nixosModules.default ];

                  services.wayoled = {
                    enable = true;
                    users = [ "alice" ];
                    settings = {
                      dayTemp = 5000;
                      nightTemp = 3000;
                      minSafeBrightness = 5;
                    };
                  };

                  users.users.alice = {
                    isNormalUser = true;
                    password = "";
                  };

                  # Headless wlroots compositor so wl_display_connect has
                  # something real to talk to inside the VM.
                  programs.sway.enable = true;
                  services.getty.autologinUser = "alice";
                  programs.bash.loginShellInit = ''
                    if [ -z "$WAYLAND_DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
                      WLR_BACKEND=headless WLR_LIBINPUT_NO_DEVICES=1 exec sway
                    fi
                  '';

                  # NOTE: VMs have no real /sys/class/backlight/* device, and
                  # unlike userspace paths you cannot fake one with tmpfiles.d
                  # (/sys is a kernel-managed virtual filesystem). So this test
                  # exercises the path where backlight_detect() finds nothing
                  # and wayoled logs "continuing without backlight control" --
                  # that's a real, already-handled code path, just not the one
                  # you actually care about for brightness control. Verifying
                  # real backlight writes needs a physical machine or a kernel
                  # test harness that can register a fake backlight class
                  # device (out of scope here).

                  virtualisation.memorySize = 1024;
                };

                testScript = ''
                  machine.start()
                  machine.wait_for_unit("multi-user.target")

                  with subtest("video group membership from services.wayoled.users"):
                      machine.succeed("id -nG alice | grep -qw video")

                  with subtest("generated profile reflects settings.* values"):
                      machine.succeed("test -f /etc/wayoled/profiles/default.conf")
                      machine.succeed("grep -q 'day_temp=5000' /etc/wayoled/profiles/default.conf")
                      machine.succeed("grep -q 'night_temp=3000' /etc/wayoled/profiles/default.conf")
                      machine.succeed("grep -q 'min_safe_brightness=5' /etc/wayoled/profiles/default.conf")

                  with subtest("udev rule installed"):
                      machine.succeed(
                          "find /run/current-system/sw/lib/udev/rules.d /etc/udev/rules.d "
                          "-name '90-wayoled-backlight.rules' | grep -q ."
                      )

                  with subtest("sway (headless) comes up for alice and wayoled follows it"):
                      machine.wait_until_succeeds(
                          "test -S /run/user/1000/wayland-1", timeout=60
                      )
                      machine.wait_until_succeeds(
                          "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 systemctl --user is-active wayoled'",
                          timeout=60,
                      )

                  with subtest("oledctl round-trips over the real IPC socket"):
                      status = machine.succeed(
                          "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 "
                          "/run/current-system/sw/bin/oledctl status'"
                      )
                      assert "profile=default" in status, status
                '';
              };
            });
      };
}
