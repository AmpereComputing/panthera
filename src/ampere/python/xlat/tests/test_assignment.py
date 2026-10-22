# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pytest

from ampere.xlat.assignment import Assignment
from ampere.xlat import Field, Signal, XlatError


def test_assignment_with_no_terms_produces_nothing():
    a = Assignment('foo')
    pla, mux = a.as_table()
    assert not pla and not mux


def test_assignment_with_one_signal_produces_one_entry_table():
    a = Assignment('foo')
    a.add_term(Signal(True), {'a': Signal(False)})
    pla, mux = a.as_table()
    assert pla == {'foo': [Signal(True)], 'a': [Signal(False)]}
    assert not mux


def test_assignment_with_two_signals_produces_two_entry_table():
    a = Assignment('foo')
    a.add_term(Signal(True), {'a': Signal(False)})
    a.add_term(Signal(False), {'a': Signal(True)})
    pla, mux = a.as_table()
    assert pla == {'foo': [Signal(True), Signal(False)], 'a': [Signal(False), Signal(True)]}
    assert not mux


def test_assignment_with_mismatched_input_widths_fails():
    a = Assignment('foo')
    a.add_term(Signal(True), {'a': Signal('0')})
    with pytest.raises(XlatError):
        a.add_term(Signal(False), {'a': Signal('10')})


def test_assignment_with_fields_generates_mux():
    a = Assignment('foo')
    a.add_term(Field('x', 'Instruction', '----', (1, 0)), {'a': Signal(False)})
    a.add_term(Field('y', 'Instruction', '----', (3, 2)), {'a': Signal(True)})
    pla, mux = a.as_table()
    assert pla == {'fooMuxSel': [Signal('0'), Signal('1')], 'a': [Signal(False), Signal(True)]}
    expected_case = '''
unique casez (fooMuxSel)
  1'b0:
    foo = Instruction[1:0];
  1'b1:
    foo = Instruction[3:2];
  default:
    foo = 'x;
endcase
'''
    assert mux.to_case() == expected_case


def test_assignment_with_default_output_and_fields_generates_mux_with_default_output():
    a = Assignment('foo', default_output=Signal(2))
    a.add_term(Field('x', 'Instruction', '----', (1, 0)), {'a': Signal(False)})
    a.add_term(Field('y', 'Instruction', '----', (3, 2)), {'a': Signal(True)})
    pla, mux = a.as_table()
    assert pla == {'fooMuxSel': [Signal('0'), Signal('1')], 'a': [Signal(False), Signal(True)]}
    expected_case = '''
unique casez (fooMuxSel)
  1'b0:
    foo = Instruction[1:0];
  1'b1:
    foo = Instruction[3:2];
  default:
    foo = 2'b00;
endcase
'''
    assert mux.to_case() == expected_case
    assert mux._default_output == "2'b00"
    assert a.default_output is None
