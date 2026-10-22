# Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

class Set:
    """A kind of lazily created set.

    Each member is a pair: an element, and a "value", where the value is typically truthy (though
    it doesn't have to be). This allows us to represent sets which are ternary; a thing can be
    outside a set, in the set but False or in the set but True. In that manner we can model
    ON, OFF, and don't-care sets.
    """
    def __init__(self, of, such_that=lambda x: True):
        super().__init__()
        self._scope = of
        self._function = such_that
        self._set = None

    def _generate(self):
        if self._set is None:
            # This used to be an actual set(), but it turns out order matters if you want to keep the generated
            # logic as stable as possible.
            #
            self._set = []
            for elem, value in self._scope:
                if value:
                    my_value = self._function(elem)
                    if my_value is not None:
                        self._set.append((elem, my_value))

    def __bool__(self):
        self._generate()
        return bool(self._set)

    def __iter__(self):
        self._generate()
        return self._set.__iter__()

    def __repr__(self):
        return f'Set({self._set})'
