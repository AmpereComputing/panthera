# Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from .generate import Generator
from .set import Set
from . import XlatError


class scope:
    """Decorator class for decode generator functions.

    The scope of a decode generator function defines the set of objects which will be
    in the ON- and OFF-set of the resulting logic, while objects which are outside the
    scope are in the don't-care set.

    The required argument to 'scope' is called the 'parent', and it can be a class or
    any callable. If it is a class, then the object passed to the generator is considered
    in the ON- or OFF-set if it is either a subclass of that class, or an instance of
    it. For a callable, the callable must simply return something truthy.
    """
    def __init__(self, parent, default_output=None, inputs=None, do_not_generate=False, variants=None,
                 allow_empty=False):
        self._parent = parent
        self._default_output = default_output
        self._do_not_generate = do_not_generate
        self._allow_empty = allow_empty
        if type(parent) is list:
            self._inputs = getattr(parent[0], 'inputs', None) if inputs is None else inputs
        else:
            self._inputs = getattr(parent, 'inputs', None) if inputs is None else inputs
        self._variants = variants

    def __call__(self, func):
        if type(self._parent) is list:
            if self._variants:
                raise XlatError(f'{func.__name__} cannot have variants since it is scoped with variants')
            generators = []
            for p in self._parent:
                suffix = p.output.split('_')[-1]
                generators.append(Generator(Set(p, func), inputs=self._inputs, output=f'{func.__name__}_{suffix}',
                                            default_output=self._default_output, do_not_generate=self._do_not_generate,
                                            is_variant=True, allow_empty=self._allow_empty))
            return generators
        elif self._variants:
            generators = []
            for v in self._variants:
                compound_set = Set(Set(self._parent, v), func)
                generators.append(Generator(compound_set, inputs=self._inputs, output=f'{func.__name__}_{v.__name__}',
                                            default_output=self._default_output, do_not_generate=self._do_not_generate,
                                            is_variant=True, allow_empty=self._allow_empty))
            return generators
        else:
            return Generator(Set(self._parent, func), inputs=self._inputs, output=func.__name__,
                             default_output=self._default_output, do_not_generate=self._do_not_generate,
                             allow_empty=self._allow_empty)
