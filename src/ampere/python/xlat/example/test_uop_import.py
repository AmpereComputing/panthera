# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import pytest
import uop
import ampere.xlat as xlat

from argparse import Namespace


FIXTURE_DIRECTORY = os.path.abspath(os.path.join(os.path.dirname(__file__), 'fixtures'))


def fixture(path):
    return os.path.join(FIXTURE_DIRECTORY, path)


def test_uop_import_reports_missing_yaml_file():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=['no_findie.yaml']))
    assert exc.match('No such file or directory: no_findie\\.yaml')


def test_uop_import_detects_duplicate_uops():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('test_ops.yaml'), fixture('dups.yaml')]))
    assert exc.match('duplicate keys in uop definitions \\(described below\\)')
    assert exc.match("uops\\.sxtnd from .*/dups\\.yaml; first defined in .*/test_ops\\.yaml")
    assert exc.match("uops\\.xtnd from .*/dups\\.yaml; first defined in .*/test_ops\\.yaml")


def test_uop_import_detects_duplicate_unit_defaults():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('test_ops.yaml'), fixture('dup_units.yaml')]))
    assert exc.match('duplicate keys in uop definitions \\(described below\\)')
    assert exc.match("unit_defaults\\.ixu from .*/dup_units\\.yaml; first defined in .*/test_ops\\.yaml")


def test_uop_import_detects_duplicates_alternate_case():
    """This one is specifically to test the reporting mechanism, which used to have a bug in that it
    assumed every YAML file had the top level keys (uops, unit_defaults)"""
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('test_ops.yaml'), fixture('more_uops.yaml'), fixture('dup_units.yaml')]))
    assert exc.match('duplicate keys in uop definitions \\(described below\\)')
    assert exc.match("unit_defaults\\.ixu from .*/dup_units\\.yaml; first defined in .*/test_ops\\.yaml")


def test_uop_import_reports_unrecognized_key():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('unknown_key.yaml')]))
    assert exc.match("unexpected key\\(s\\) something_else in .*/unknown_key\\.yaml")


def test_uop_import_reports_multiple_unrecognized_keys():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('unknown_keys.yaml')]))
    assert exc.match("unexpected key\\(s\\) and_more, something_else in .*/unknown_keys\\.yaml")


def test_uop_import_complains_about_no_uops():
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('unit_defaults.yaml')]))
    assert exc.match('no uops defined \\(use --uops <YAML file>\\)')


def test_uop_import_reports_missing_opcode_width_field():
    """This test may be temporary, but for now we check that 'opcode_width' is defined per uop"""
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('no_opcode_width.yaml')]))
    assert exc.match('uop "add_no_opcode_width" is missing required field "opcode_width"\n' +
                     '...while importing uop add_no_opcode_width')


def test_uop_import_reports_missing_opcode_field():
    """This test may be temporary, but for now we check that 'opcode' is defined per uop"""
    with pytest.raises(xlat.XlatError) as exc:
        uop.load_uops(Namespace(uops=[fixture('no_opcode.yaml')]))
    assert exc.match('uop "add_no_opcode" is missing required field "opcode"\n' +
                     '...while importing uop add_no_opcode')


def test_uop_import_assigns_uop_names():
    uopcodes = uop.load_uops(Namespace(uops=[fixture('test_ops.yaml')]))
    assert uopcodes['add'].name == 'add'
