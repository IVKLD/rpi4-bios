{
  description = "UEFI ARM64 Pre-boot Diagnostics Development Environment";

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
        devShells.default = pkgs.mkShell {
          hardeningDisable = [ "all" ];

          packages = with pkgs; [
            git
            gnumake
            gcc13
            binutils
            python3
            acpica-tools
            qemu
            mtools
            dosfstools
            util-linux
            libuuid
            nasm
            bison
            flex
            pkgsCross.aarch64-multiplatform.buildPackages.gcc13
          ];

          shellHook = ''
            export ARCH=AARCH64
            export GCC5_AARCH64_PREFIX=aarch64-unknown-linux-gnu-
            echo "=== UEFI ARM64 Nix Environment Loaded (GCC 13, Hardening Disabled) ==="
            echo "Host GCC: $(gcc --version | head -n 1)"
            echo "GCC AArch64: $(which aarch64-unknown-linux-gnu-gcc 2>/dev/null || echo 'not found')"
            echo "QEMU: $(which qemu-system-aarch64 2>/dev/null || echo 'not found')"
          '';
        };
      });
}
