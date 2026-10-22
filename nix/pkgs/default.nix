# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ pkgs ? import ../sources.nix {} }:

{
  # Package Overrides
  boost = (import ./boost/default.nix { inherit pkgs; }).boost;       # Add python support to boost
  qemu = (import ./qemu/default.nix { inherit pkgs; }).qemu;          # Custom qemu

  # Custom Packages
  systemc = pkgs.stable.callPackage ./systemc { };                    # Build SystemC with cmake and C++11 support
  simpoint = pkgs.stable.callPackage ./simpoint { };                  # simpoint tool
  urwid = pkgs.stable.callPackage ./urwid { inherit (pkgs.stable.python3Packages) buildPythonPackage exceptiongroup pygobject3 pyserial pytestCheckHook pythonOlder pyzmq setuptools setuptools-scm tornado trio twisted typing-extensions wcwidth; };                        # Latest version of urwid
}
