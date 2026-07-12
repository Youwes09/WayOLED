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
      };
}
