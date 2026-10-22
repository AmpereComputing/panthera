# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{}:
let
in rec {
  # Unstable package release (main)
  unstable = import (builtins.fetchTarball {
    name = "unstable_2025-10-13";
    url = "https://github.com/NixOS/nixpkgs/archive/832e3b6db48508ae436c2c7bfc0cf914eac6938e.tar.gz";
    sha256 = "1xyk39afidw5qxfnzj4pnzqmx973j1ja34sb2pg40j20fh676fmr";
  }) {};
  # Latest package release (25.05)
  latest = import (builtins.fetchTarball {
    name = "nixpkgs_25.05_2025-10-13";
    url = "https://github.com/NixOS/nixpkgs/archive/7e297ddff44a3cc93673bb38d0374df8d0ad73e4.tar.gz";
    sha256 = "1rxn634v8im87na9ig2dg8nywrb5qhz7iybw4bbbvfcikq5j0673";
  }) {};
  # Stable (what most packages use) (24.11)
  stable = import (builtins.fetchTarball {
    name = "nixpkgs_24.11_2025-05-05";
    url = "https://github.com/NixOS/nixpkgs/archive/5b35d248e9206c1f3baf8de6a7683fee126364aa.tar.gz";
    sha256 = "11sih5m3cqmc9dfsjqj51pn3f26bjbhx1d0pgm7sggyh68wllfrm";
  }) {
    config.allowUnfreePredicate = pkg: (pkg.pname or "") == "espresso";
  };

  # Local packages
  ampere = import ./pkgs {};
}
