# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from collections import defaultdict
from . import cat, fields, signal, XlatError
from .mux import Mux


class Assignment:
    def __init__(self, name, default_output=None):
        self.name = name
        self.default_output = default_output
        self._terms = []
        self._inputs = {}
        self.has_fields = False

    def add_term(self, output, inputs: dict):
        for input_name in sorted(inputs.keys()):
            input_signal = inputs[input_name]
            if input_name not in self._inputs:
                self._add_input(input_name, input_signal)
            else:
                self._verify_input(input_name, input_signal)
        if isinstance(output, fields.Field) or isinstance(output, cat.Cat):
            self.has_fields = True
        self._terms.append((output, inputs))

    def _add_input(self, input_name, input_signal):
        self._inputs[input_name] = signal.Signal(input_signal.width, value=0, mask=0)

    def _verify_input(self, input_name, input_signal):
        default_value = self._inputs[input_name]
        if default_value.width != input_signal.width:
            raise XlatError(f'Assignment requires input signals for {input_name} of equal widths')

    def as_table(self):
        """Build a data structure suitable for passing to Table's constructor.

        For that we need a dictionary with a key for each input as well as the output, each of which holds a
        list of Signals of equal length. All Signals in each list must also be of equal width, but that was
        guaranteed by add_term().
        """
        mux = None

        if self.has_fields:
            mux, output_index_to_sel = self._extract_mux()
            self.default_output = None  # Default output belongs to the mux

            if output_index_to_sel is None:
                return None, mux

            output_name = mux.selector
        else:
            output_index_to_sel = None
            output_name = self.name

        table_data = defaultdict(lambda: [])

        for output, inputs in self._terms:
            table_data[output_name].append(output_index_to_sel.pop(0) if output_index_to_sel else output)

            for input_name in sorted(self._inputs.keys()):
                input_default = self._inputs[input_name]
                table_data[input_name].append(inputs.get(input_name, input_default))

        return table_data, mux

    def _extract_mux(self):
        m = Mux(self.name, f'{self.name}MuxSel', default_output=self.default_output)
        output_index_to_sel = []

        for output, _ in self._terms:
            output_index_to_sel.append(m.add_input(output))

        mux_sel_width = m.selector_width()
        if mux_sel_width > 0:
            return m, [signal.Signal(mux_sel_width, value=m.input_selector(v)) for v in output_index_to_sel]
        else:
            return m, None
