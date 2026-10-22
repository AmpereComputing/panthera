# Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import pytest
import sys

import ampere.xlat.fields as fields


def test_single_bit_field():
    assert fields.parse_fields('0', 'x') == {'x': (0, 0)}


def test_multi_bit_field():
    assert fields.parse_fields('01', 'x') == {'x': (1, 0)}
    assert fields.parse_fields('010101', 'y') == {'y': (5, 0)}


def test_positioning():
    pstr =  ' 1-1 '   # noqa: E222
    fstr = ['x    ',  # should fail
            ' x   ',
            '  x  ',
            '   x ',
            '    x']  # should fail

    with pytest.raises(RuntimeError):
        fields.parse_fields(pstr, fstr[0])

    for f in fstr[1:3]:
        assert fields.parse_fields(pstr, f) == {'x': (2, 0)}

    with pytest.raises(RuntimeError):
        fields.parse_fields(pstr, fstr[4])


def test_starts_in_field_but_ends_outside():
    pstr = ' 1-1    '
    fstr = '  afield'

    assert fields.parse_fields(pstr, fstr) == {'afield': (2, 0)}


def test_starts_in_one_field_ends_in_another():
    pstr = '11  00'
    fstr = ' abcd '

    assert fields.parse_fields(pstr, fstr) == {'abcd': (3, 0)}


def test_skipping_around():
    #       1 11 111 1111
    #       9 87 654 3210 9876 543 21 0
    pstr = '0 11 000 1111 0000 111 00 1'
    fstr = 'a    bbb      cccc     dd  '

    assert fields.parse_fields(pstr, fstr) == {'a': (19, 19), 'bbb': (16, 14), 'cccc': (9, 6), 'dd': (2, 1)}


def test_or_widths_must_match():
    with pytest.raises(RuntimeError):
        fields.PatternParser('0|01')
    pp = fields.PatternParser('00|01')
    assert pp.expr.width == 2


def test_excl_width_must_match():
    with pytest.raises(RuntimeError):
        fields.PatternParser('---!01')
    pp = fields.PatternParser('---!001')
    assert pp.expr.width == 3


def test_parse_pattern():
    pp = fields.PatternParser('000(0|1)111')
    assert pp.fields() == [(0, 2, 3), (3, 7, 1), (8, 10, 3)]


def test_parens_enclose_a_single_field():
    pstr = '(0 1)'
    fstr = '  x  '

    assert fields.parse_fields(pstr, fstr) == {'x': (1, 0)}


def test_gen_patterns():
    assert fields.PatternParser('0').patterns() == ['0']
    assert fields.PatternParser('1').patterns() == ['1']
    assert fields.PatternParser('0 1').patterns() == ['01']
    assert fields.PatternParser('0|1').patterns() == ['0', '1']
    assert fields.PatternParser('(0)').patterns() == ['0']
    assert fields.PatternParser('-!0').patterns() == ['1']


def test_gen_exclusion():
    # Not sure I really want to test this using equality, because it depends on the simplification
    # abilities of the Pattern library. However, I do want to test the ! operator...so for now I'm
    # going with the straightforward technique.
    assert fields.PatternParser('---!000').patterns() == ['--1', '-1-', '1--']


def test_gen_exclusion2():
    assert fields.PatternParser('---!000!010').patterns() == ['--1', '1--']


def test_field_value_conversion():
    assert fields.Field('a', 'sig', '00000000000000000000000000000011', (1, 0)).value == 3
    assert fields.Field('a', 'sig', '00000000000000000000000000000011', (2, 1)).value == 1
    assert fields.Field('a', 'sig', '00000000000000000000000000000011', (2, 2)).value == 0
    with pytest.raises(fields.NeedDefiniteValue):
        assert fields.Field('a', 'sig', '00000000000000000000000000000-01', (2, 0)).value == -1
