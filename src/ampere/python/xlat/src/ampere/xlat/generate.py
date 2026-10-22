# Copyright (c) 2019-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import assignment, table, Cat, Field, Signal, XlatContext, XlatError
from .signal import NotABoolean


class Generator:
    all_generators = []

    def __init__(self, source_set, inputs, output, default_output, do_not_generate=False, is_variant=False,
                 allow_empty=False):
        self.source_set = source_set
        self.inputs = inputs
        self.output = output
        self.default_output = default_output
        self.do_not_generate = do_not_generate
        self.is_variant = is_variant
        self.allow_empty = allow_empty
        self.all_generators.append(self)

    def generate(self, minimize=True):
        if self.do_not_generate or not self.inputs:
            return None, None

        if not self._source_set_is_not_empty() and not self.is_variant and not self.allow_empty:
            raise XlatError(f'decode function "{self.output}" did not match anything')

        assign = assignment.Assignment(self.output, default_output=self.default_output)

        for elem, value in self.source_set:
            assign.add_term(self._canonize_value(value), _get_input_signals(elem, self.inputs))

        with XlatContext(f'generating PLA for {assign.name}'):
            data, mux = assign.as_table()
            if data:
                pla = table.Table(data, inputs=list(assign._inputs.keys()),
                                  default_output=_truth_value(assign.default_output))
                if minimize:
                    pla.minimize()
            else:
                pla = None

        return pla, mux

    def _canonize_value(self, value):
        if isinstance(value, Field) or isinstance(value, Signal) or isinstance(value, Cat):
            return value
        elif type(value) is tuple or type(value) is list:
            return Cat.build(*[self._canonize_value(v) for v in value])
        elif type(value) is bool or type(value) is str:
            return Signal(value)
        else:
            raise XlatError(f'illegal type "{type(value).__name__}" (value={value})' +
                            f' returned from function "{self.output}"')

    def _source_set_is_not_empty(self):
        return any([value is not None and value is not False for _, value in self.source_set])

    def __iter__(self):
        return self.source_set.__iter__()


def _get_input_signals(elem, inputs):
    if callable(inputs):
        return inputs(elem)
    elif type(inputs) is str:
        return {inputs: getattr(elem, inputs)}
    else:
        return dict([(i, getattr(elem, i)) for i in inputs])


def _truth_value(default_output):
    if default_output is None:
        return None

    try:
        return bool(default_output)
    except NotABoolean:
        if default_output.value == 0:
            return False
        else:
            raise
