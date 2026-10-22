# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/tarball/nixos-25.05"; # python3 3.12
  pkgs = import nixpkgs { config = {}; overlays = []; };
in

pkgs.mkShell {
  hardeningDisable = [ "all" ];
  packages = with pkgs; [
  python3
  python3Packages.pyyaml
  python3Packages.sympy
  python3Packages.tqdm
  ninja
  cmake
  fmt_11
  gcc13
  ];
  NIX_SHELL_PRESERVE_PROMPT=1;
}
