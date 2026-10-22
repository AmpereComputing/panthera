# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{}:
let
in rec {
  stable = import (builtins.fetchTarball {
    name = "nixpkgs_24.11_2024-12-05";
    url = "https://github.com/NixOS/nixpkgs/archive/8b27c1239e5c421a2bbc2c65d52e4a6fbf2ff296.tar.gz";
    sha256 = "1gx0hihb7kcddv5h0k7dysp2xhf1ny0aalxhjbpj2lmvj7h9g80a";
  }) {};
}
