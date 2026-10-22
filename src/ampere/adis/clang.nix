# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

let
  nixpkgs = fetchTarball "https://github.com/NixOS/nixpkgs/tarball/nixos-24.11"; # python3 3.12.8
  pkgs = import nixpkgs { config = {}; overlays = []; };
in

pkgs.mkShell {
  packages = with pkgs; [
  glib.dev
  glibc.out
  glibc.static
  llvm_18
  clang_18
  python3
  python3Packages.pyyaml
  python3Packages.sympy
  python3Packages.tqdm
  ninja
  cmake
  fmt
  ];
  NIX_SHELL_PRESERVE_PROMPT=1;
}
