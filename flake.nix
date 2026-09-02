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
            systemd
          ];

          preConfigure = ''
            rm -rf build
          '';

          mesonFlags = [
            (pkgs.lib.mesonBool "install-setuid-helper" false)
            (pkgs.lib.mesonBool "install-systemd-unit" false)
          ];

          meta = with pkgs.lib; {
            description = "OLED care daemon (pixel shift, burn-in mitigation, smooth backlight) for Wayland";
            license = licenses.mit;
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
            clang-tools
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            systemd
          ];
        };
      }) // {
        nixosModules.default = import ./contrib/nixos/module.nix self;

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

                  programs.sway.enable = true;
                  services.getty.autologinUser = "alice";
                  programs.bash.loginShellInit = ''
                    if [ -z "$WAYLAND_DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
                      WLR_BACKEND=headless WLR_LIBINPUT_NO_DEVICES=1 exec sway
                    fi
                  '';

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
