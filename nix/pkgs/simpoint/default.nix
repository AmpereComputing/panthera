# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ stdenv, fetchgit }:

stdenv.mkDerivation rec {
  name = "simpoint";
  enableParallelBuilding = true;

  src = fetchgit {
    url = "https://github.com/AmpereComputing/simpoint.git";
    rev = "d38b308a84fc474e0d241fa6c2ba7bbd919c388f";
    sha256 = "sha256-ABWF1IpYuvPhwBJtyo3LBTZw9CymfprJSl0G0oDx/HA=";
  };

  installPhase = ''
    mkdir -p $out/bin
    install analysiscode/simpoint $out/bin
  '';

  outputs = [ "out" ];
}
