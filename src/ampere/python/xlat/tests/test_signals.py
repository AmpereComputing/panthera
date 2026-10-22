# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import copy
import os
import pytest
import sys

from ampere.xlat.signal import Signal, IndefiniteValue, NotABoolean
from ampere.xlat import XlatError


def test_one_bit_zero():
    s = Signal(1, 0)
    assert s.width == 1
    assert s.value == 0
    assert s.mask == 1
    assert f'{s:sv}' == "1'b0"
    assert f'{s}' == '0'


def test_one_bit_one():
    s = Signal(1, 1)
    assert s.width == 1
    assert s.value == 1
    assert s.mask == 1
    assert f'{s:sv}' == "1'b1"
    assert f'{s}' == '1'


def test_one_bit_dc():
    s = Signal(1, mask=0)
    assert s.width == 1
    assert s.value == 0
    assert s.mask == 0
    assert f'{s:sv}' == "1'b?"
    assert f'{s}' == '-'


def test_false_is_one_bit_zero():
    s = Signal(False)
    assert s.width == 1
    assert s.value == 0
    assert s.mask == 1
    assert f'{s:sv}' == "1'b0"


def test_true_is_one_bit_one():
    s = Signal(True)
    assert s.width == 1
    assert s.value == 1
    assert s.mask == 1
    assert f'{s:sv}' == "1'b1"


def test_one_bit_zero_from_string():
    s = Signal('0')
    assert s.width == 1
    assert s.value == 0
    assert s.mask == 1


def test_one_bit_one_from_string():
    s = Signal('1')
    assert s.width == 1
    assert s.value == 1
    assert s.mask == 1


def test_dc_bit_from_string():
    s = Signal('-')
    assert s.width == 1
    assert s.value == 0
    assert s.mask == 0
    assert f'{s:sv}' == "1'b?"


def test_slice():
    s = Signal('01-')
    assert str(s[0]) == '-'
    assert str(s[1]) == '1'
    assert str(s[2]) == '0'
    assert str(s[0:0]) == '-'
    assert str(s[1:1]) == '1'
    assert str(s[2:2]) == '0'
    assert str(s[0:1]) == '1-'
    assert str(s[1:2]) == '01'
    assert str(s[0:2:2]) == '0-'
    with pytest.raises(IndexError):
        s[3]


def test_equivalent_signals_are_identical():
    assert Signal(1, 0, 1) is Signal(False)
    assert Signal(1, 1, 1) is Signal(True)
    assert Signal('1--01') is Signal('1--01')
    assert copy.copy(Signal(True)) is Signal(True)


def test_cant_create_signal_of_width_zero():
    with pytest.raises(XlatError):
        Signal(0, 0, 1)


def test_cant_create_signal_with_value_bits_outside_mask():
    with pytest.raises(XlatError) as excinfo:
        Signal(8, 0b10000100, 0b01111111)
    assert "signal value 0b10000100 has set bits which are masked (mask=0b1111111)" == str(excinfo.value)


def test_truthiness():
    assert Signal(True)
    assert not Signal(False)
    assert Signal('1')
    assert not Signal('0')
    with pytest.raises(NotABoolean):
        bool(Signal('10'))


def test_signal_to_integer():
    assert int(Signal('100')) == 4
    assert int(Signal('11')) == 3
    with pytest.raises(IndefiniteValue):
        int(Signal('1-1'))
    assert [1, 2, 3, 4][Signal('10')] == 3
