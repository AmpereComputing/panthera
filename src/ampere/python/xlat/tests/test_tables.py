# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pytest

from ampere.xlat import Signal, Table, XlatError


def test_create_table():
    t = Table({'in': ('00', '01', '10'), 'out': ('1', '0', '1')}, inputs=['in'], outputs=['out'])
    assert str(t) == '''
00 1
01 0
10 1
    '''.strip()


def test_table_with_no_listed_inputs_or_outputs_raises():
    with pytest.raises(XlatError) as excinfo:
        Table({'in': ('00', '01', '10'), 'out': ('1', '0', '1')})
    assert "no inputs or outputs" in str(excinfo.value)


def test_table_with_spurious_input_raises():
    with pytest.raises(XlatError) as excinfo:
        Table({'in': ('00', '01', '10'), 'out': ('1', '0', '1')}, inputs=['in', 'in2'])
    assert "Table inputs {'in2'} have no corresponding data" == str(excinfo.value)


def test_table_with_spurious_output_raises():
    with pytest.raises(XlatError) as excinfo:
        Table({'in': ('00', '01', '10'), 'out': ('1', '0', '1')}, outputs=['out', 'out2'])
    assert "Table outputs {'out2'} have no corresponding data" == str(excinfo.value)


def test_table_with_noncovered_column_raises():
    with pytest.raises(XlatError) as excinfo:
        Table({'a': ('0', '1'), 'b': ('1', '0'), 'c': ('-', '0')}, inputs=['a'], outputs=['b'])
    assert "Table column c is neither an input nor output" == str(excinfo.value)


def test_table_with_duplicate_column_raises():
    with pytest.raises(XlatError) as excinfo:
        Table({'a': ('0', '1'), 'b': ('1', '0'), 'c': ('-', '0')}, inputs=['a', 'b'], outputs=['b', 'c'])
    assert "Table column b cannot be both an input and output" == str(excinfo.value)


def test_auto_fill_outputs():
    t = Table({'a': ['0'], 'b': ['1']}, inputs=['a'])
    assert t._inputs == ['a']
    assert t._outputs == ['b']


def test_auto_fill_inputs():
    t = Table({'a': ['0'], 'b': ['1']}, outputs=['b'])
    assert t._inputs == ['a']
    assert t._outputs == ['b']


def test_table_format():
    t = Table({'a': ('00', '01', '10'), 'z': ('1', '0', '1')}, inputs=['a'], outputs=['z'])
    assert f'{t:logic}' == 'z = ~a[0] & ~a[1] |\n    ~a[0] & a[1];'
    assert f'{t:espresso}' == '''
# inputs=a
# outputs=z
.i 2
.o 1
.type fr
00 1
01 0
10 1
    '''.strip()
    assert f'{t:case}' == '''
unique casez (a)
    2'b00:
        z = 1'b1;
    2'b01:
        z = 1'b0;
    2'b10:
        z = 1'b1;
endcase
    '''.strip() + '\n'


def test_minimize():
    t = Table({'a': ('00', '01', '10', '11'),
               'b': ( '1',  '1',  '0',  '0')}, outputs=['b'])  # noqa: E201
    t.minimize()
    assert f'{t:logic}' == 'b = ~a[1];'


def test_minimize_using_default_output():
    t = Table({'a': ('00', '01'),
               'b': ( '1',  '1')}, outputs=['b'], default_output=False)  # noqa: E201
    t.minimize()
    assert f'{t:logic}' == 'b = ~a[1];'


def test_minimize_empty_table():
    t = Table({'a': ('0'), 'b': ('-')}, outputs=['b'])
    t.minimize()
    assert f'{t:logic}' == "b = 1'b0;"
    assert f'{t:function}' == '''
function automatic logic b(input logic a);
  b = 1'b0;
endfunction
'''


def test_conflicting_minterm_is_reported():
    t = Table({'a': ('00', '00'), 'z': ('1', '0')}, inputs=['a'], outputs=['z'])
    with pytest.raises(XlatError) as excinfo:
        t.minimize()
    assert "minimization failed: espresso: ON-set and OFF-set are not orthogonal" == str(excinfo.value)


def test_unconditional_output():
    t = Table({'a': ('--------',), 'b': ('1010',)}, inputs=['a'], outputs=['b'])
    t.minimize()
    assert f'{t:logic}' == """
b[0] = 1'b0;
b[1] = 1'b1;
b[2] = 1'b0;
b[3] = 1'b1;
""".strip()


def test_unconditional_output_with_dc():
    t = Table({'a': ('--------',), 'b': ('1-1-',)}, inputs=['a'], outputs=['b'])
    t.minimize()
    assert f'{t:logic}' == """
b[0] = 1'b0;
b[1] = 1'b1;
b[2] = 1'b0;
b[3] = 1'b1;
""".strip()


def test_output_with_no_on_set():
    t = Table({'a': ('0011', '0100', '1000'), 'b': ('0', '0', '0'),
               'c': ('0', '1', '0')}, inputs=['a'], outputs=['b', 'c'])
    t.minimize()
    assert f'{t:logic}' == "b = 1'b0;\nc = a[2];"


def test_multibit_default_output():
    t = Table({'a': ('00', '01'), 'b': ('010', '011')}, outputs=['b'], default_output=False)
    t.minimize()
    assert f'{t:logic}' == """
b[0] = a[0] & ~a[1];
b[1] = ~a[1];
b[2] = 1'b0;
""".strip()


def test_on_set_and_dc_set():
    t = Table({'a': ('00', '01', '11'), 'b': ('1', '-', '-')}, outputs=['b'], default_output=False)
    t.minimize()
    assert f'{t:logic}' == """
b = ~a[1];
""".strip()


def test_table_sort():
    t1 = Table({'a': ('00', '01', '10'), 'z': ('1', '0', '1')}, inputs=['a'], outputs=['z'])
    t2 = Table({'a': ('01', '00', '10'), 'z': ('0', '1', '1')}, inputs=['a'], outputs=['z'])
    t3 = Table({'a': ('10', '00', '01'), 'z': ('1', '1', '0')}, inputs=['a'], outputs=['z'])

    t1_espresso = f'{t1:espresso}'
    t2_espresso = f'{t2:espresso}'
    t3_espresso = f'{t3:espresso}'

    assert t1_espresso == t2_espresso
    assert t2_espresso == t3_espresso
