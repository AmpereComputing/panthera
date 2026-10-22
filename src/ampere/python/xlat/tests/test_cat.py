# Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pytest
from ampere.xlat import Cat, Field, Repl, Signal, XlatError


def test_cat_of_one_signal_is_that_signal():
    assert Cat.build(Signal(True)) is Signal(True)


def test_cat_will_raise_given_nonsignal_or_field():
    with pytest.raises(XlatError):
        Cat.build(1)


def test_cat_will_raise_given_nothing():
    with pytest.raises(XlatError):
        Cat.build()


def test_cat_will_simplify_two_signals():
    assert Cat.build(Signal(True), Signal(False)) is Signal('10')


def test_cat_will_simplify_many_signals():
    assert Cat.build(Signal('110'), Signal('-'), Signal('1--1')) is Signal('110-1--1')


def test_cat_of_two_fields_has_correct_width():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 7))
    assert Cat.build(f1, f2).width == 5


def test_cat_of_cats_will_flatten():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 7))
    cat1 = Cat.build(f1, f2)
    f3 = Field('z', 'Instruction', '--------', (5, 5))
    cat2 = Cat.build(cat1, f3)
    assert cat2.width == cat1.width + f3.width
    assert len(cat2.items) == 3
    cat3 = Cat.build(f3, cat1)
    assert cat3.width == cat1.width + f3.width
    assert len(cat3.items) == 3
    cat4 = Cat.build(cat1, cat2)
    assert cat4.width == cat1.width + cat2.width
    assert len(cat4.items) == 5


def test_cat_to_string():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 7))
    assert str(Cat.build(f1, f2)) == '{Instruction[3:0], Instruction[7:7]}'


def test_cat_to_string_with_signal():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    assert str(Cat.build(f1, Signal('010'))) == "{Instruction[3:0], 3'b010}"


def test_cat_format_same_as_string():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    cat1 = Cat.build(f1, Signal('010'))
    assert f'{cat1}' == "{Instruction[3:0], 3'b010}"
    assert f'{cat1:sv}' == "{Instruction[3:0], 3'b010}"


def test_repl_formatting():
    assert f'{Repl.build(1, Signal("110")):sv}' == "3'b110"
    assert f'{Repl.build(4, Signal("110")):sv}' == "12'b110110110110"
    assert f'{Repl.build(2, Field("x", "Instruction", "--------", (3, 0))):sv}' == "{2{Instruction[3:0]}}"


def test_cat_of_adjacent_fields_collapses():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 4))
    cat1 = Cat.build(f2, f1)
    assert type(cat1) is Field
    assert cat1.hi == 7
    assert cat1.lo == 0


def test_cat_of_adjacent_fields_collapses2():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 4))
    cat1 = Cat.build(Signal('101'), f2, f1)
    assert f'{cat1}' == "{3'b101, Instruction[7:0]}"


def test_cat_of_adjacent_fields_collapses3():
    f1 = Field('x', 'Instruction', '--------', (3, 0))
    f2 = Field('y', 'Instruction', '--------', (7, 4))
    cat1 = Cat.build(f2, f1, Signal('101'))
    assert f'{cat1}' == "{Instruction[7:0], 3'b101}"


def test_cat_will_flatten_and_collapse():
    assert Cat.build(Cat.build(Signal('1'), Signal('1')), Cat.build(Signal('0'), Signal('1'))) is Signal('1101')
