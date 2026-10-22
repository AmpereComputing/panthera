# Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import subprocess

from collections import defaultdict
from copy import copy

from .column import Column
from .signal import Signal

from . import XlatContext, XlatError


log = logging.getLogger('ampere')


class Table:
    """Table of input and output Columns.

    A Table is a set of input and output Columns of equal length (rows). Basically, this represents a truth table,
    or PLA, or whatever you like to call such a thing.

    A Table is constructed from a dictionary and optionally a list of inputs and outputs. For example:

        Table({ 'a': ('00', '01', '10', '11'), 'b': ('0', '1', '1', '0')}, inputs=['a'], outputs=['b'])

    corresponds to:

         a b
        -- -
        00 0
        01 1
        10 1
        11 0

    If either of "inputs" or "outputs" is omitted, then the other will be inferred from keys which do not appear
    in its counterpart. I.e., the following two invocations are equivalent to the above:

        Table({ 'a': ('00', '01', '10', '11'), 'b': ('0', '1', '1', '0')}, inputs=['a'])  # infer outputs (b)

        Table({ 'a': ('00', '01', '10', '11'), 'b': ('0', '1', '1', '0')}, outputs=['b'])  # infer inputs (a)

    It is an error for a key to appear in both inputs and outputs. Also, if both inputs and outputs are specified,
    then they should cover every key exactly. A XlatError is raised if any of these restrictions is violated.
    """
    def __init__(self, data: dict = {}, inputs=None, outputs=None, default_output=None):
        self._init_data(data)
        self._init_inputs_and_outputs(inputs, outputs)
        self._default_output = default_output

    def _init_data(self, data):
        self._data = {}
        self._length = None

        for label, column in data.items():
            with XlatContext(f'building column labeled {label} in Table'):
                column = copy(column) if type(column) is Column else Column(column)

                if self._length is None:
                    self._length = len(column)
                elif len(column) != self._length:
                    raise XlatError("Table requires Columns of equal length")

                self._data[label] = column

    def _init_inputs_and_outputs(self, inputs, outputs):
        auto_fill_inputs = False
        auto_fill_outputs = False

        if inputs is None:
            if outputs is None:
                raise XlatError('Table has no inputs or outputs')
            else:
                self._inputs = []
                auto_fill_inputs = True
        else:
            self._inputs = copy(inputs)

        if outputs is None:
            self._outputs = []
            auto_fill_outputs = True
        else:
            self._outputs = copy(outputs)

        assert not (auto_fill_inputs and auto_fill_outputs)

        input_set = set(self._inputs)
        output_set = set(self._outputs)

        for label in self._data.keys():
            is_input = label in input_set
            is_output = label in output_set

            if is_input and is_output:
                raise XlatError(f'Table column {label} cannot be both an input and output')

            if not is_input and not is_output:
                if auto_fill_outputs:
                    self._outputs.append(label)
                elif auto_fill_inputs:
                    self._inputs.append(label)
                else:
                    raise XlatError(f'Table column {label} is neither an input nor output')

            if is_input:
                input_set.remove(label)

            if is_output:
                output_set.remove(label)

        if input_set:
            raise XlatError(f'Table inputs {input_set} have no corresponding data')

        if output_set:
            raise XlatError(f'Table outputs {output_set} have no corresponding data')

    def minimize(self):
        with subprocess.Popen(['espresso', '-Dso'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as espresso:
            out, err = espresso.communicate(self.to_espresso().encode())
            if espresso.returncode != 0:
                raise XlatError(f'minimization failed: {err.decode().rstrip()}')
            new_data = defaultdict(lambda: [])
            for line in self._sorted_minterms(out.decode().split('\n')):
                columns = line.replace(' ', '').rstrip()
                start = 0
                for label in self._inputs + self._outputs:
                    width = self._data[label].width
                    new_data[label].append(columns[start:start + width])
                    start += width
            if not new_data:
                for label in self._inputs + self._outputs:
                    new_data[label] = ['0' * self._data[label].width]
            self._init_data(new_data)

    @staticmethod
    def _sorted_minterms(lines):
        pla_lines = []
        in_pla = False
        for line in lines:
            if line.startswith('.e'):
                break
            elif in_pla:
                pla_lines.append(line)
            elif line.startswith('.p'):
                in_pla = True
        return sorted(pla_lines)

    def __len__(self):
        return self._length

    def __str__(self):
        return self.to_ascii()

    def __repr__(self):
        return f'Table({self._data}, inputs={self._inputs}, outputs={self._outputs})'

    def __format__(self, format_spec):
        if format_spec == 'logic':
            return self.to_logic()
        elif format_spec == 'function':
            if len(self._outputs) == 1:
                return self.to_function(self._outputs[0])
            else:
                raise XlatError("No implicit conversion to function for multi-output Table")
        elif format_spec == 'espresso':
            return self.to_espresso()
        elif format_spec == 'case':
            return self.to_case()
        else:
            return self.to_ascii()

    def to_logic(self, outputs=None, line_indent=''):
        """Produce random logic expressions for this table. Each output bit is generated separately, so here
        we iterate through all the output columns, and then handle each bit in each column."""
        if not outputs:
            outputs = self._outputs

        nl = '\n' + line_indent

        assigns = []

        for output_name in outputs:
            if output_name not in self._data:
                defsig = Signal(False) if self._default_output is False else Signal(1, mask=0)
                assigns.append(f'{output_name} = {defsig:sv}')
                continue

            output_column = self._data[output_name]
            for bit in range(output_column.width):
                output = output_column[bit]
                or_terms, default_value = self._or_terms(output)
                if or_terms:
                    lhs = f'{self._indexed_name(output_name, bit)}'
                    continued_indent = ' ' * (len(lhs) + 3)
                    rhs = f' |{nl}{continued_indent}'.join([' & '.join(and_term) for and_term in or_terms])
                else:
                    rhs = "1'b0" if default_value is False else "1'b?"
                assigns.append(f'{self._indexed_name(output_name, bit)} = {rhs}')
        return f';{nl}'.join(assigns) + ';'

    def _or_terms(self, output_bit: Column):
        or_terms = []
        default_value = self._default_output

        orows = output_bit.rows
        for row in range(self._length):
            if orows[row] is Signal(True):
                and_terms = self._and_terms(row)
                if and_terms:
                    or_terms.append(and_terms)
                else:
                    or_terms.append([f'{orows[row]:sv}'])
            elif orows[row] is Signal(False):
                default_value = False

        return or_terms, default_value

    def _and_terms(self, row):
        and_terms = []

        for input_name in self._inputs:
            input = self._data[input_name].rows[row]
            for bit in range(input.width):
                input_bit = input[bit]
                if input_bit is Signal(True):
                    and_terms.append(self._indexed_name(input_name, bit))
                elif input_bit is Signal(False):
                    and_terms.append(f'~{self._indexed_name(input_name, bit)}')

        return and_terms

    def _indexed_name(self, name, bit):
        width = self._data[name].width
        if width == 1:
            assert bit == 0
            return name
        else:
            return f'{name}[{bit}]'

    def to_function(self, output):
        output_type = self._sv_type(output)
        inputs = ', '.join([f'input {self._sv_type(input)} {input}' for input in self._inputs])
        return f'''
function automatic {output_type} {output}({inputs});
  {self.to_logic(outputs=[output], line_indent='  ')}
endfunction
'''

    def _sv_type(self, col):
        w = self._data[col].width
        return f'logic [{w-1}:0]' if w > 1 else 'logic'

    def to_espresso(self):
        input_bits = sum([self._data[c].width for c in self._inputs])
        output_bits = sum([self._data[c].width for c in self._outputs])
        return f'# inputs={" ".join(self._inputs)}\n# outputs={" ".join(self._outputs)}\n' + \
            f'.i {input_bits}\n.o {output_bits}\n.type {self._pla_type()}\n' + self.to_ascii()

    def to_espresso_prune_dc(self):
        non_dc_inputs = [i for i in self._inputs if self._has_non_dc(self._data[i])]
        input_bits = sum([self._data[c].width for c in non_dc_inputs])
        output_bits = sum([self._data[c].width for c in self._outputs])
        return f'# inputs={" ".join(non_dc_inputs)}\n# outputs={" ".join(self._outputs)}\n' + \
            f'.i {input_bits}\n.o {output_bits}\n.type {self._pla_type()}\n' + self.to_ascii(inputs=non_dc_inputs)

    def to_case(self):
        if len(self._inputs) == 1:
            input_sig = self._inputs[0]
        else:
            input_sig = '{' + ', '.join(self._inputs) + '}'
        s = f'unique casez ({input_sig})\n'
        for row in range(len(self)):
            match = []
            for i in self._inputs:
                match.append(f'{self._data[i].rows[row]:sv}')
            if len(match) == 1:
                s += f'    {match[0]}:\n'
            else:
                s += '    {' + ', '.join(match) + '}:\n'
            for o in self._outputs:
                s += f'        {o} = {self._data[o].rows[row]:sv};\n'
        if self._default_output is False:
            s += '    default:\n'
            for o in self._outputs:
                s += f'        {o} = {Signal(self._data[o].width, value=0):sv};\n'
        s += 'endcase\n'
        return s

    def to_ascii(self, inputs=None, outputs=None):
        if inputs is None:
            inputs = self._inputs
        if outputs is None:
            outputs = self._outputs
        rows = sorted([' '.join(row)
                      for row in zip(*[str(self._data[k]).split('\n') for k in inputs + outputs])])
        return '\n'.join(rows)

    def _has_non_dc(self, col):
        return any([entry.mask for entry in col])

    def _pla_type(self):
        """Determine the espresso PLA type based on the outputs that are specified and the default output.

           Outputs Given    Default Output    Type
           ON   OFF   DC
           ---------------------------------------
           X     -     -        False (0)      f
           X     -     -        None (-)       fr
           X     -     X        False (0)      fd
           X     -     X        None (-)       fr
           X     X     -        False (0)      f
           X     X     -        None (-)       fr
           X     X     X          -            fdr
           -     -     X          -            fd    # Fishy, but I have an old test for this case (empty table)
        """
        output_types = self._output_types()

        if output_types == {True}:
            if self._default_output is False:
                return 'f'
            if self._default_output is None:
                return 'fr'
        elif output_types == {True, None}:
            if self._default_output is False:
                return 'fd'
            if self._default_output is None:
                return 'fr'
        elif output_types == {True, False}:
            if self._default_output is None:
                return 'fr'
            if self._default_output is False:
                return 'f'
        elif output_types == {True, False, None}:
            return 'fdr'
        else:
            # The remaining combinations have no ON-set, which is not really a valid table. However, sometimes we
            # legitimately hae non-matching decode functions. Real errors should be reported by the Generator, so
            # we let these slide.
            return 'fd'

    def _output_types(self):
        """Returns a set containing {None, False, True} or any subset thereof. This represents whether
        we have an ON-set, OFF-set, or DC-set in the output.
        """
        output_types = set()
        for o in self._outputs:
            for v in self._data[o]:
                if v.mask != ((1 << v.width) - 1):
                    output_types.add(None)
                if v.value:
                    output_types.add(True)
                if ~v.value & v.mask:
                    output_types.add(False)
        return output_types
