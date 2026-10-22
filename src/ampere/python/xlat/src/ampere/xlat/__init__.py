# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""xlat Python package

This package is a set of classes and utilities for manipulating XLAT files.
XLAT files are just Python code of a certain structure. Namely, an XLAT file
is expected to contain a series of classes derived from Translation. Those
classes will typically contain class attribute settings as well as special
functions, for example, the "decode" function which specifies how to translate
an instruction into uops.

Here is an example fragment of an XLAT file, with a single translation:

    class LDR_32_ldst_immpost(Translation):
        \"\"\"Load Register (immediate)\"\"\"
        pattern = '-0   111 0 00 01  0 --------- 01 ----- -----'
        pfields = 'size     V    opc     imm9        Rn    Rt  '

        asmtemplate = 'LDR  <Wt>, [<Xn|SP>], #<simm>'

        def decode(self):
            ldrw(Rt, Rn)
            add(Rn, Rn, sextimm=imm9)

One critical attribute is "pattern" which describes the instruction bit
pattern for this translation. Along with "pattern", "pfields" can be used
to describe named fields to extract from the instruction visually.

Equivalently, the "fields" attribute can be used to describe fields using
a traditional Python dictionary. For example:

        fields = { 'size': [31,30], 'V': [26,26], 'opc': [23,22],
                   'imm9': [20,12], 'Rn': [9,5], 'Rt': [4,0] }

In this example, "asmtemplate" is just documentation, but may be used in the
future to produce disassembly strings.
"""


class XlatError(Exception):
    """Base class for all XLAT generated exceptions.

    Used to distinguish detected errors from truly unexpected exceptions.
    """
    def __init__(self, msg):
        self.msg = msg
        self.context = []

    def __str__(self):
        if self.context:
            context = '\n...' + '\n...'.join([f'while {ctx}' for ctx in self.context])
            return str(self.msg) + context
        else:
            return self.msg


class YamlError(XlatError):
    """Exception class for reporting errors in YAML (i.e., uop definitions)"""
    pass


class XlatContext:
    """Add a string context to an XlatError with a Python context manager.

    E.g.

        with XlatContext('inside some loop'):
            # stuff...

    might generate:

        ERROR: Some expception message
        ...while inside some loop

    if "stuff" raises an exception.
    """
    def __init__(self, ctx):
        self.context = ctx

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # Contexts used to only be put on XlatErrors, which were designed to contain them. However, it's far too
        # useful to users to have contexts on all exceptions, so this code was updated to do that. It is a little
        # dangerous, because it potentially clobbers a "context" attribute on whatever Exception comes in, but I
        # felt like the risk was worth the improved error reporting.
        #
        if exc_type:
            if hasattr(exc_val, 'context'):
                exc_val.context.append(self.context)
            else:
                exc_val.context = [self.context]
        return False


from .fields import Field
from .signal import Signal
from .cat import Cat, Repl
from .column import Column
from .mux import Mux
from .assignment import Assignment
from .generate import Generator
from .uop import Uop, uops
from .scope import scope
from .set import Set
from .table import Table
from .translation import Fusion, Translation, decoded_uops, translations


_initializers = []


def initialize(initialization_function, arguments=None):
    _initializers.append((initialization_function, arguments))


def add_initializer_arguments(parser):
    for init_func, init_args in _initializers:
        if init_args:
            init_args(parser)


def run_initializers(args):
    for init_func, init_args in _initializers:
        init_func(args)
