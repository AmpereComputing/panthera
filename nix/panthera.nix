# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ pkgs ? import ./sources.nix {} }:
let in rec {
  # packages *required* by panthera code
  static_gtest = pkgs.stable.gtest.override {
      static = true;
  };
  codePkgs = with pkgs; [
    ampere.boost
    ampere.systemc
    stable.capstone_4
    stable.fmt_11
    stable.glib
    stable.glibc.out            # There is an outstanding nix bug where glibc.static must be after glibc.out
    stable.glibc.static         # https://github.com/NixOS/nixpkgs/issues/59267
    stable.glibcLocales
    static_gtest
    stable.immer
    stable.keystone
    stable.nlohmann_json
    stable.protobuf_21
    stable.protobufc
  ];
  # packages *required* by panthera build
  buildPkgs = with pkgs; [
    stable.cmake
    stable.espresso
    stable.git
    stable.git-lfs
    stable.ninja
    stable.pkg-config
  ];
  # packages used for tracing
  tracingPkgs = with pkgs; [
    ampere.qemu
    ampere.simpoint
    stable.cpio
  ];
  # packages as part of development
  developmentPkgs = with pkgs; [
    stable.ccache
    stable.check-jsonschema
    stable.curl
    stable.gdb
    stable.lcov
    unstable.llvmPackages_21.clang-unwrapped
    stable.openssh
    stable.parallel
    stable.pcre2
    stable.ruby
    latest.ruff
    stable.valgrind
    stable.texliveFull # for compiler guide
  ];

  # packages required to build/run panthera
  pantheraPkgs = codePkgs ++ buildPkgs ++ tracingPkgs ++ developmentPkgs ++ [pythonEnv];

  # python build packages
  pythonBuildPkgs = ps: with ps; [
    boost
    capstone_4
    openpyxl
    paramiko
    protobuf
    pyyaml
    scikit-learn
  ];
  # python development packages
  pythonDevelopmentPkgs = ps: with ps; [
    coverage
    flake8
    git-filter-repo
    lcov_cobertura
    pytest
    pytest-cov
    setuptools
    unittest-xml-reporting
  ];
  # python rollup/report/visualization packages
  pythonRollupPkgs = ps: with ps; [
    bokeh
    gitpython
    holoviews
    hvplot
    jinja2
    lxml
    matplotlib
    mongoengine
    natsort
    numpy
    openpyxl
    pandas
    panel
    pymongo
    pyslurm
    ipykernel
    markdown-it-py
    mdit-py-plugins
    linkify-it-py
    pyyaml
    requests
    scipy
    seaborn
    pkgs.ampere.urwid
    xlsxwriter
    plotly
    kaleido
  ];

  # python packages
  pythonPkgs = ps: pythonBuildPkgs(ps) ++ pythonDevelopmentPkgs(ps) ++ pythonRollupPkgs(ps) ++ usrPythonPkgs;
  pythonEnv = pkgs.stable.python312.withPackages(pythonPkgs);

  # Shell hook commands to run when entering nix-shell
  pantheraHook = ''
  '';

  # Slurm path hook
  slurmHook = if builtins.pathExists /nix/var/nix/profiles/default/bin/sbatch then ''
    export PATH="$PATH:/nix/var/nix/profiles/default/bin"
  '' else '''';

  # User settings
  usrPythonPkgs = if builtins.pathExists ~/.panthera.nix then (import ~/.panthera.nix { inherit pkgs; }).usrPythonPkgs else [];
  usrHook = if builtins.pathExists ~/.panthera.nix then (import ~/.panthera.nix { inherit pkgs; }).shellHook else '''';
  usrPkgs = if builtins.pathExists ~/.panthera.nix then (import ~/.panthera.nix { inherit pkgs; }).usrPkgs else [];

  # All
  allPkgs = pantheraPkgs ++ usrPkgs;
  allHooks = pantheraHook + usrHook + slurmHook;
}
