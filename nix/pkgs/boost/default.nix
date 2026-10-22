# Copyright (c) 2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ pkgs }: {
  boost = pkgs.stable.boost.override {
    python = pkgs.stable.python3;
    enablePython = true;
    enableIcu = false;
  };
}
