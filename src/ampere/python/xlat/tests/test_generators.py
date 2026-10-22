# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pytest
from ampere.xlat import Cat, Field, Generator, Signal, XlatError
from ampere.xlat.set import Set


class Opcode:
    def __init__(self, opcode):
        self.opcode = Signal(8, opcode)


def test_empty_generator_raises_error():
    with pytest.raises(XlatError):
        Generator([], 'input', '', None).generate()


def test_generator_with_no_inputs_is_empty():
    assert Generator([], '', '', None).generate() == (None, None)


def test_simple_generator():
    source = [(Opcode(1), True), (Opcode(2), False)]
    pla, mux = Generator(source, 'opcode', 'func', None).generate()
    assert mux is None
    assert pla.to_logic() == 'func = opcode[0];'


def test_generator_marked_do_not_generate_does_not_generate():
    source = [(Opcode(1), True), (Opcode(2), False)]
    pla, mux = Generator(source, 'opcode', 'func', None, do_not_generate=True).generate()
    assert mux is None
    assert pla is None


def test_generator_detects_bad_type():
    source = [(Opcode(1), True), (Opcode(2), 2)]
    with pytest.raises(XlatError) as excinfo:
        Generator(source, 'opcode', 'func', None).generate()
    assert 'illegal type "int" (value=2) returned from function "func"' in str(excinfo.value)


def test_generator_detects_bad_type_in_mux():
    source = [(Opcode(1), Field('x', 'Instruction', '----', (1, 0))), (Opcode(2), 2)]
    with pytest.raises(XlatError):
        Generator(source, 'opcode', 'func', None).generate()


def test_generator_detects_bad_type_in_cat():
    source = [(Opcode(1), Signal(2, 1)), (Opcode(2), (Signal(1, 0), 1))]
    with pytest.raises(XlatError):
        Generator(source, 'opcode', 'func', None).generate()


def test_generator_creates_mux_for_cat():
    f1 = Field('x', 'Instruction', '----', (1, 0))
    f2 = Field('y', 'Instruction', '----', (3, 3))
    source = [(Opcode(1), Cat.build(f1, f2))]
    pla, mux = Generator(source, 'opcode', 'imm', None).generate()
    assert mux is not None


def test_generate_works_with_multibit_default():
    source = [(Opcode(1), Signal('010')), (Opcode(2), Signal('100'))]
    pla, mux = Generator(source, 'opcode', 'func', Signal('000')).generate()
    assert pla.to_logic() == '''
func[0] = 1'b0;
func[1] = opcode[0] & ~opcode[1] & ~opcode[2] & ~opcode[3] & ~opcode[4] & ~opcode[5] & ~opcode[6] & ~opcode[7];
func[2] = ~opcode[0] & opcode[1] & ~opcode[2] & ~opcode[3] & ~opcode[4] & ~opcode[5] & ~opcode[6] & ~opcode[7];
'''.strip()


def test_set_which_returns_none():
    opcodes = [(Opcode(i), True) for i in range(1, 10)]

    def filter(opcode):
        if opcode.opcode.value < 5:
            return opcode.opcode

    source = Set(of=opcodes, such_that=filter)
    pla, _ = Generator(source, 'opcode', 'func', None).generate()
    assert pla.to_logic() == '''
func[0] = opcode[0];
func[1] = opcode[1];
func[2] = opcode[2];
func[3] = 1'b0;
func[4] = 1'b0;
func[5] = 1'b0;
func[6] = 1'b0;
func[7] = 1'b0;
'''.strip()
