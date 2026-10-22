# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

let
  pkgs = import ./nix/sources.nix {};
  panthera = import ./nix/panthera.nix {};

in pkgs.stable.mkShell.override{stdenv = pkgs.stable.gcc13Stdenv; } {
  # Nix has the toolchain with additional hardening cflags, disable as we are going to control cflags
  hardeningDisable = [ "all" ];
  name = "panthera-dev";
  version = "0.1";
  buildInputs = panthera.allPkgs;
  shellHook = panthera.allHooks;
}
