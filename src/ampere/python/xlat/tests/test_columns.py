# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import pytest
import sys

from ampere.xlat import Column, Signal, XlatError


def test_one_bit_column():
    c = Column([(1, 1)])
    assert f'{c!r}' == 'Column(values=[Signal(width=1, value=1, mask=1, signed=False)])'
    assert f'{c}' == '1'


def test_one_bit_column_of_three_values():
    c = Column([(1, 0), (1, 1), (1, 0, 0)])
    assert str(c) == '0\n1\n-'


def test_build_column_from_signals():
    c = Column()
    c.append(Signal('00--'))
    c.append(Signal('1-1-'))
    c.append(Signal('1100'))
    assert str(c) == '''
00--
1-1-
1100
    '''.strip()


def test_build_column_from_strings():
    c = Column(('00--', '1-1-', '1100'))
    assert str(c) == '''
00--
1-1-
1100
    '''.strip()


def test_slice_column():
    c = Column(('00--', '1-1-', '1100'))
    assert str(c[0]) == '''
-
-
0
    '''.strip()
    assert str(c[3]) == '''
0
1
1
    '''.strip()


def test_column_of_unequal_widths_fails():
    with pytest.raises(XlatError) as excinfo:
        Column(['0', '10'])
    assert "equal widths" in str(excinfo.value)
