# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pytest
from ampere.xlat import Cat, Field, Mux, Signal, XlatError


def test_empty_mux_has_length_zero():
    m = Mux('foo', 'foosel')
    assert len(m) == 0


def test_mux_of_one_signal_has_length_one():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    assert len(m) == 1


def test_mux_of_two_identical_signals_has_length_one():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('1'))
    m.add_input(Signal('1'))
    assert len(m) == 1


def test_mux_of_two_different_signals_has_length_two():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    m.add_input(Signal('1'))
    assert len(m) == 2
    assert m.selector_width() == 1
    assert m.output_width() == 1


def test_mux_can_be_built_from_a_field():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    assert len(m) == 1


def test_mux_of_two_identical_fields_has_length_one():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    assert len(m) == 1


def test_mux_of_four_different_fields_has_length_four():
    m = Mux('foo', 'foosel')
    m.add_input(Field('w', 'pat', '---', (2, 0)))
    m.add_input(Field('x', 'pat', '---', (2, 1)))
    m.add_input(Field('y', 'pat', '---', (1, 0)))
    m.add_input(Field('z', 'pat', '---', (1, 1)))
    assert len(m) == 4


def test_mux_output_width_is_max_of_inputs():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    m.add_input(Field('x', 'pat2', '----', (3, 0)))
    assert m.output_width() == 4


def test_mux_output_width_is_max_of_inputs_with_mixed_fields_and_signals():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    m.add_input(Field('x', 'pat2', '----', (3, 0)))
    m.add_input(Signal('0101100'))
    assert m.output_width() == 7


def test_mux_rendered_as_case():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    m.add_input(Signal('1'))
    expected_case = '''
unique casez (foosel)
  1'b0:
    foo = 1'b0;
  1'b1:
    foo = 1'b1;
  default:
    foo = 'x;
endcase
'''
    assert m.to_case() == expected_case
    assert f'{m:case}' == expected_case


def test_mux_rendered_as_function():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    m.add_input(Signal('1'))
    expected_function = '''
function automatic logic[0:0] foo(input logic[0:0] foosel);
  unique casez (foosel)
    1'b0:
      foo = 1'b0;
    1'b1:
      foo = 1'b1;
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function
    assert f'{m:function}' == expected_function


def test_mux_unknown_format_spec_is_reported():
    m = Mux('foo', 'foosel')
    with pytest.raises(XlatError):
        f'{m:badspec}'


def test_mux_with_one_input_is_simple_assignment():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    assert f'{m:case}' == '''
foo = 1'b0;
'''
    assert f'{m:function}' == '''
function automatic logic[0:0] foo();
  foo = 1'b0;
endfunction
'''


def test_mux_with_field_as_function():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '---', (2, 0)))
    m.add_input(Field('x', 'pat2', '----', (3, 1)))
    expected_function = '''
function automatic logic[2:0] foo(input logic[0:0] foosel, input logic[2:0] pat, input logic[3:0] pat2);
  unique casez (foosel)
    1'b0:
      foo = pat[2:0];
    1'b1:
      foo = pat2[3:1];
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_with_field_as_simple_assignment():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'pat', '------------', (2, 0)))
    assert f'{m:case}' == '''
foo = pat[2:0];
'''
    assert f'{m:function}' == '''
function automatic logic[2:0] foo(input logic[11:0] pat);
  foo = pat[2:0];
endfunction
'''


def test_mux_will_uniquify_inputs():
    m = Mux('foo', 'foosel')
    m.add_input(Field('x', 'Instruction', '--------', (2, 0)))
    m.add_input(Field('y', 'Instruction', '--------', (6, 4)))
    m.add_input(Field('z', 'Instruction', '--------', (2, 0)))
    expected_function = '''
function automatic logic[2:0] foo(input logic[0:0] foosel, input logic[7:0] Instruction);
  unique casez (foosel)
    1'b0:
      foo = Instruction[2:0];
    1'b1:
      foo = Instruction[6:4];
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_will_uniquify_inputs_with_cat():
    m = Mux('foo', 'foosel')
    m.add_input(Cat([Field('x', 'Instruction', '--------', (2, 0)), Signal('00')]))
    m.add_input(Field('y', 'Instruction', '--------', (6, 4)))
    m.add_input(Cat([Field('z', 'Instruction', '--------', (2, 0)), Signal('00')]))
    expected_function = '''
function automatic logic[4:0] foo(input logic[0:0] foosel, input logic[7:0] Instruction);
  unique casez (foosel)
    1'b0:
      foo = {Instruction[2:0], 2'b00};
    1'b1:
      foo = {2'b00, Instruction[6:4]};
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_will_uniquify_inputs_with_cat2():
    """This version has two Cats of the same size but with mixed Fields and Signals.
    See issue #31
    """
    m = Mux('foo', 'foosel')
    m.add_input(Cat([Field('x', 'Instruction', '--------', (2, 0)), Signal('00')]))
    m.add_input(Cat([Field('z', 'Instruction', '--------', (2, 0)),
                             Field('y', 'Instruction', '--------', (6, 5))]))
    expected_function = '''
function automatic logic[4:0] foo(input logic[0:0] foosel, input logic[7:0] Instruction);
  unique casez (foosel)
    1'b0:
      foo = {Instruction[2:0], 2'b00};
    1'b1:
      foo = {Instruction[2:0], Instruction[6:5]};
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_will_zero_extend_short_inputs():
    m = Mux('foo', 'foosel')
    m.add_input(Field('one', 'pat', '------', (0, 0)))
    m.add_input(Field('two', 'pat', '------', (2, 1)))
    m.add_input(Field('three', 'pat', '------', (5, 3)))
    expected_function = '''
function automatic logic[2:0] foo(input logic[1:0] foosel, input logic[5:0] pat);
  unique casez (foosel)
    2'b00:
      foo = {2'b00, pat[0:0]};
    2'b01:
      foo = {1'b0, pat[2:1]};
    2'b10:
      foo = pat[5:3];
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_with_default_output():
    m = Mux('foo', 'foosel', default_output=Signal('0'))
    m.add_input(Signal('0'))
    m.add_input(Signal('1'))
    expected_case = '''
unique casez (foosel)
  1'b0:
    foo = 1'b0;
  1'b1:
    foo = 1'b1;
  default:
    foo = 1'b0;
endcase
'''
    assert m.to_case() == expected_case


def test_mux_with_wide_default_output():
    m = Mux('foo', 'foosel', default_output=Signal('1111'))
    m.add_input(Signal('0'))
    m.add_input(Signal('1'))
    expected_case = '''
unique casez (foosel)
  1'b0:
    foo = 4'b0000;
  1'b1:
    foo = 4'b0001;
  default:
    foo = 4'b1111;
endcase
'''
    assert m.to_case() == expected_case


def test_mux_will_sign_extend():
    m = Mux('foo', 'foosel')
    f1 = Field('one', 'pat', '------', (0, 0))
    f2 = Field('two', 'pat', '------', (2, 1))
    f3 = Signal('00', signed=True)
    f4 = Signal('10', signed=True)
    f5 = Cat.build(Field('a', 'pat', '------', (1, 1)), Field('b', 'pat', '------', (2, 2)))
    f1.signed = True
    f2.signed = True
    f5.signed = True
    m.add_input(f1)
    m.add_input(f2)
    m.add_input(f3)
    m.add_input(f4)
    m.add_input(f5)
    m.add_input(Field('three', 'pat', '------', (5, 3)))
    expected_function = '''
function automatic logic[2:0] foo(input logic[2:0] foosel, input logic[5:0] pat);
  unique casez (foosel)
    3'b000:
      foo = {{2{pat[0:0]}}, pat[0:0]};
    3'b001:
      foo = {pat[2:2], pat[2:1]};
    3'b010:
      foo = 3'b000;
    3'b011:
      foo = 3'b110;
    3'b100:
      foo = {pat[1:1], pat[1:1], pat[2:2]};
    3'b101:
      foo = pat[5:3];
    default:
      foo = 'x;
  endcase
endfunction
'''
    assert m.to_function() == expected_function


def test_mux_of_two_inputs_which_extend_to_be_equal_has_length_one():
    m = Mux('foo', 'foosel')
    m.add_input(Signal('0'))
    m.add_input(Signal('00'))
    assert len(m) == 1


def test_mux_of_two_inputs_which_sign_extend_to_be_equal_has_length_one():
    m = Mux('foo', 'foosel')
    f1 = Signal('1', signed=True)
    f2 = Signal('11')
    m.add_input(f1)
    m.add_input(f2)
    assert len(m) == 1


def test_two_inputs_differently_extended_are_not_confused():
    m = Mux('foo', 'foosel')
    f1 = Signal('1', signed=True)
    f2 = Signal('1')
    f3 = Signal('000')
    m.add_input(f1)
    m.add_input(f2)
    m.add_input(f3)
    assert len(m) == 3
