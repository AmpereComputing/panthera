# Copyright (c) 2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ gcc11Stdenv, lib, cmake, fetchurl }:
gcc11Stdenv.mkDerivation rec {
  name = "env";
  pname = "systemc";
  version = "2.3.3";

  nativeBuildInputs = [ cmake ];

  src = fetchurl {
    url = "https://www.accellera.org/images/downloads/standards/systemc/${pname}-${version}.tar.gz";
    sha256 = "5781b9a351e5afedabc37d145e5f7edec08f3fd5de00ffeb8fa1f3086b1f7b3f";
  };

  cmakeFlags = [
    "-D CMAKE_CXX_STANDARD=11"
  ];

  meta = with lib; {
    description = "The language for System-level design, modeling and verification";
    homepage    = "https://systemc.org/";
    license     = licenses.asl20;
    maintainers = with maintainers; [ victormignot ];
  };
}
