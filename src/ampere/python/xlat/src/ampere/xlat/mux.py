# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import cat, fields, signal, XlatError


class Mux:
    def __init__(self, output, selector, default_output=None):
        self.output = output
        self.selector = selector
        self._inputs = []
        self._string_inputs = []
        self._output_width = None
        self._set_default_output(default_output)
        self._frozen = False
        self._input_index_to_selector = {}

    def _set_default_output(self, default_output):
        if default_output is not None:
            if not isinstance(default_output, signal.Signal):
                default_output = signal.Signal(default_output)
            self._default_output_width = default_output.width
            self._default_output = f'{default_output:sv}'
        else:
            self._default_output = "'x"
            self._default_output_width = 0

    def add_input(self, input):
        if self._frozen:
            raise XlatError(f'adding input to frozen mux {self.output}')
        self._inputs.append(input)
        return len(self._inputs) - 1

    def input_selector(self, input_index):
        self._freeze()
        return self._input_index_to_selector[input_index]

    def selector_width(self):
        self._freeze()
        return (len(self._string_inputs) - 1).bit_length()

    def output_width(self):
        self._freeze()
        return self._output_width

    def __len__(self):
        self._freeze()
        return len(self._string_inputs)

    def _freeze(self):
        if not self._frozen:
            self._output_width = max([i.width for i in self._inputs] + [self._default_output_width])
            self._string_inputs = []
            inputs_already_seen = {}

            for idx, input in enumerate(self._inputs):
                input = _concatenation(input, self._output_width)
                if input not in inputs_already_seen:
                    self._input_index_to_selector[idx] = len(self._string_inputs)
                    inputs_already_seen[input] = len(self._string_inputs)
                    self._string_inputs.append(input)
                else:
                    self._input_index_to_selector[idx] = inputs_already_seen[input]

            self._frozen = True

    def __format__(self, format_spec):
        if format_spec == 'function':
            return self.to_function()
        elif format_spec == 'case':
            return self.to_case()
        else:
            raise XlatError('invalid format specification "{format_spec}" for Mux')

    def to_case(self):
        sel_width = self.selector_width()
        if sel_width == 0:
            return self._to_simple_assignment()
        s = f'\nunique casez ({self.selector})\n'
        for index, sig in enumerate(self._string_inputs):
            sel = signal.Signal(sel_width, index)
            s += f'  {sel:sv}:\n'
            s += f'    {self.output} = {sig};\n'
        s += '  default:\n'
        s += f"    {self.output} = {self._default_output};\n"
        s += 'endcase\n'
        return s

    def to_function(self):
        sel_width = self.selector_width()
        out_width = self.output_width()

        if sel_width == 0:
            embedded_asgn = self._to_simple_assignment().replace('\n', '\n  ').rstrip()
            return f'\nfunction automatic logic[{out_width-1}:0] {self.output}({self._input_signal_list()});' + \
                   f'{embedded_asgn}\nendfunction\n'
        else:
            embedded_case = self.to_case().replace('\n', '\n  ').rstrip()

            input_signals = self._input_signal_list()
            if input_signals:
                input_signals = ', ' + input_signals

            return f'\nfunction automatic logic[{out_width-1}:0] {self.output}(input logic[{sel_width-1}:0] ' + \
                   f'{self.selector}{input_signals});{embedded_case}\nendfunction\n'

    def _to_simple_assignment(self):
        return f'\n{self.output} = {self._string_inputs[0]};\n'

    def _input_signal_list(self):
        unique_inputs = set()
        inputs = []
        for f in self._find_inputs(self._inputs):
            if isinstance(f, fields.Field) and f.source not in unique_inputs:
                inputs.append((f.source, f.source_width))
                unique_inputs.add(f.source)
        return ', '.join([f'input logic[{f[1]-1}:0] {f[0]}' for f in inputs])

    def _find_inputs(self, inputs):
        for input in inputs:
            if isinstance(input, cat.Cat):
                yield from self._find_inputs(input.items)
            elif isinstance(input, fields.Field):
                yield input


def _concatenation(value, result_width):
    if value.width < result_width:
        if getattr(value, 'signed', False):
            msb = _most_significant_bit(value)
            assert msb is not None
            value = cat.Cat.build(cat.Repl.build(result_width - value.width, msb), value)
        else:
            value = cat.Cat.build(signal.Signal(result_width - value.width), value)
    if type(value) is signal.Signal:
        return f'{value:sv}'
    else:
        return str(value)


def _most_significant_bit(value):
    if isinstance(value, signal.Signal):
        return value[value.width - 1]
    elif isinstance(value, fields.Field):
        return fields.Field('anon', value.source, value.pattern, (value.hi, value.hi))
    elif isinstance(value, cat.Cat):
        return _most_significant_bit(value.items[0])
    elif isinstance(value, cat.Repl):
        return _most_significant_bit(value.item)
    else:
        raise XlatError(f'_most_significant_bit given unsupported type {type(value)}')
