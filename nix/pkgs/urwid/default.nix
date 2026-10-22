# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

{ lib
, buildPythonPackage
, exceptiongroup
, fetchFromGitHub
, fetchPypi
, glibcLocales
, pygobject3
, pyserial
, pytestCheckHook
, pythonOlder
, pyzmq
, setuptools
, setuptools-scm
, tornado
, trio
, twisted
, typing-extensions
, wcwidth
}:

buildPythonPackage rec {
  pname = "urwid";
  version = "2.6.16";
  pyproject = true;

  disabled = pythonOlder "3.7";

  src = fetchPypi {
    inherit pname version;
    hash = "sha256-k60jmTnkTDheZKoAAnh4ueXEhtWehV7Iq1seGtzbMqI=";
  };

  postPatch = ''
    sed -i '/addopts =/d' pyproject.toml
  '';

  nativeBuildInputs = [
    setuptools
    setuptools-scm
  ];

  propagatedBuildInputs = [
    typing-extensions
    wcwidth
  ];

  passthru.optional-dependencies = {
    glib = [
      pygobject3
    ];
    tornado = [
      tornado
    ];
    trio = [
      exceptiongroup
      trio
    ];
    twisted = [
      twisted
    ];
    zmq = [
      pyzmq
    ];
    serial = [
      pyserial
    ];
    lcd = [
      pyserial
    ];
  };

  nativeCheckInputs = [
    glibcLocales
    pytestCheckHook
  ] ++ lib.flatten (builtins.attrValues passthru.optional-dependencies);

  env.LC_ALL = "en_US.UTF8";

  pytestFlagsArray = [
    "tests"
  ];

  disabledTestPaths = [
    # expect call hangs
    "tests/test_vterm.py"
  ];

  pythonImportsCheck = [
    "urwid"
  ];

  meta = with lib; {
    description = "A full-featured console (xterm et al.) user interface library";
    changelog = "https://github.com/urwid/urwid/releases/tag/${version}";
    downloadPage = "https://github.com/urwid/urwid";
    homepage = "https://urwid.org/";
    license = licenses.lgpl21Plus;
    maintainers = with maintainers; [ ];
  };
}
