# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ pkgs }:
let
  version = "39724d94fbdba2200df959955fd49aec2b9e730b";

  # Keep the checkout as a fixed-output derivation so Nix can share it between
  # concurrent Slurm jobs rather than fetching QEMU and its submodules per job.
  src = pkgs.stable.fetchgit {
    url = "https://github.com/AmpereComputing/tracing-qemu.git";
    rev = version;
    hash = "sha256-HjdoA8Hf44ZKcQbJvmv2cZ4CM/K/6TS5MEppIXZwET0=";
    fetchSubmodules = true;
  };
in {
  qemu = pkgs.stable.qemu.overrideAttrs (old: {
    inherit src;

    # Only for readability, should be kept in sync with the verison that
    # is used for the "src"
    version = "8.0.0";

    # Omit all patches since they're for a newer version (9.1.3) and
    # do not apply cleanly
    patches = [];

    configureFlags = old.configureFlags ++ [
      "--target-list=aarch64-softmmu,aarch64-linux-user"
      "--enable-slirp"
      "--enable-plugins"
      "--disable-docs"
    ];
  });
}
