# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from argparse import ArgumentParser
from collections.abc import Sequence as _Sequence
from copy import deepcopy
from typing import Any, Dict, Iterable, List, Union


class Cmd:
    """Single command with attached metadata for substitution."""

    MAX_RENDER_PASSES = 100
    NON_META = frozenset(
        {
            "render",
            "rendered_meta",
            "parser_add_args",
            "validation_cmd",
            "only_if",
        }
    )

    def __init__(self, **kwargs):
        """Initialize ourselves with whatever keyword args we were given.

        Most are arbitrary, user-chosen settings, but "cmd" is special, since that's what
        we're going to use as our command line."""
        self.__dict__.update(kwargs)
        if not getattr(self, "cmd"):
            raise RuntimeError("no command")

    def __str__(self):
        if getattr(self, "only_if") and not self.only_if(self):
            return ""
        return str(self.render())

    def _render(self, s: str) -> str:
        for _passes in range(self.MAX_RENDER_PASSES):
            try:
                new_s = s.format_map(self)
            except ValueError as exc:
                raise RuntimeError(f"{exc} while formatting {s}")

            if s == new_s:
                return new_s
            s = new_s

        raise RuntimeError(f"recursion limit ({self.MAX_RENDER_PASSES}) reached while expanding command")

    def _render_for_meta(self, s: Any, key, depth: int = 0) -> Any:
        if depth > self.MAX_RENDER_PASSES:
            raise RuntimeError(f"recursion limit ({self.MAX_RENDER_PASSES}) reached while expanding command")

        if isinstance(s, str):
            return self._render(s)
        elif isinstance(s, list):
            return [self._render_for_meta(i, key) for i in s]
        elif callable(s) and not self._is_class(s, key=key):
            return self._render_for_meta(self._eval_func(s), key, depth=depth + 1)
        return s

    def render(self, value: Any = None) -> str:
        """Iteratively format a string until it stops changing."""
        value = self.cmd if value is None else value
        if not isinstance(value, str):
            value = str(self._process(value))
        return self._render(value)

    def rendered_meta(self, exclude_none: bool = True) -> Dict[str, Any]:
        def valid_meta(name):
            # Avoid attempting to process/return an metadata beginning with
            # underscores, or that of this method or the argument parser
            # routine, as they will cause infinite recursion and errors
            return not name.startswith("_") and name not in self.NON_META

        def meta_from(obj):
            return {
                k: self._render_for_meta(getattr(self, k), k)
                for k in vars(obj)
                if valid_meta(k)
                and (not exclude_none or getattr(self, k) is not None)
                and not self._is_class(getattr(self, k), key=k)
            }

        meta_dict = {}

        # First, gather and render any class attributes along the reversed
        # method resolution order for this object (in other words, we allow
        # sub-classes' attribute values to override those of their parents and
        # ancestors)
        for cls in reversed(self.__class__.__mro__):
            if cls not in [object, Cmd]:
                meta_dict.update(meta_from(cls))

        # Finally, layer attributes of this object itself on top of all class
        # attributes before returning the resulting dictionary
        meta_dict.update(meta_from(self))

        return meta_dict

    def __contains__(self, key: str) -> bool:
        return key not in self.NON_META and key in self.__dict__

    def __getitem__(self, key: str):
        """The format_map method expects a dictionary-like object, so by implementing this
        we can re-direct its accesses to our own attributes."""
        return self._process(getattr(self, key), key=key)

    def __getattr__(self, name: str):
        """If we reach this method, it's only because we *don't* have this attribute. So
        just return an empty string (unless it's a private attribute, in which case we fall
        back to the normal route)."""
        if name.startswith("_"):
            return super().__getattr__(name)
        return ""

    def _eval_func(self, value: callable) -> Any:
        # Call normal method functions of the Cmd objects (or derived objects)
        #
        # The check of "__self__" here is to test for a bound method; if the
        # method is bound, presumably we don't need to pass the command object.
        #
        return value() if hasattr(value, "__self__") else value(self)

    def _is_class(self, value: Any, key: str | None = None) -> bool:
        return isinstance(value, type) and (key is None or key == value.__name__)

    def _process(self, value: Any, key: str | None = None, depth: int = 0) -> Any:
        """A couple of attribute types deserve special treatment:

        1. Lists get expanded into space separated strings
        2. Callables get called with this object, so they can make decisions.
        """
        if depth > self.MAX_RENDER_PASSES:
            raise RuntimeError(f"recursion limit ({self.MAX_RENDER_PASSES}) reached while expanding command")

        if isinstance(value, list):
            return " ".join([str(self._process(elem, depth=depth + 1)) for elem in value])
        elif callable(value) and not self._is_class(value, key=key):
            return self._process(self._eval_func(value), depth=depth + 1)
        else:
            return value

    def copy(self, **kwargs) -> "Cmd":
        """Make it easy to create new version of this command with overridden settings."""
        new_me = deepcopy(self)
        new_me.__dict__.update(kwargs)
        return new_me

    def apply(self, iterable: Iterable) -> "CmdPool":
        return CmdPool([self.copy(**item) for item in iterable])

    def append(self, other: Union["Cmd", "CmdSeq", "CmdPool"]) -> Union["CmdSeq", "CmdPool"]:
        if isinstance(other, CmdSeq) or isinstance(other, CmdPool):
            return other.prepend(self)
        return CmdSeq([self, other])

    def prepend(self, other: Union["Cmd", "CmdSeq", "CmdPool"]) -> Union["CmdSeq", "CmdPool"]:
        if isinstance(other, CmdSeq) or isinstance(other, CmdPool):
            return other.append(self)
        return CmdSeq([other, self])

    def __add__(self, other):
        return self.append(other)

    def update(self, vals: Dict[str, Any]):
        self.__dict__.update(vals)

    @classmethod
    def parser_add_args(cls, parser: ArgumentParser, **kwargs):
        """Add arguments for Cmd to parser."""

    class has:
        """Has all variables."""

        def __init__(self, *names):
            self.names = names

        def __call__(self, cmd):
            return all(name in cmd for name in self.names)

    class has_any:
        """Has any variable."""

        def __init__(self, *names):
            self.names = names

        def __call__(self, cmd) -> str:
            return any(name in cmd for name in self.names)

    class if_set:
        """Return then_value if all supplied variables are set, else else_value."""

        def __init__(self, var_names: str | List[str], then_value: Any, else_value: Any = ""):
            self.var_names = [var_names] if isinstance(var_names, str) else var_names
            self.then_value = then_value
            self.else_value = else_value

        def __repr__(self) -> str:
            return f"if_set({self.var_names}, then={self.then_value}, else={self.else_value})"

        def __call__(self, cmd):
            return cmd.render(self.then_value if all(getattr(cmd, v) for v in self.var_names) else self.else_value)

    class pick_first:
        """Pick value from first available variable (or fallback to default if none exist)."""

        def __init__(self, var_value_priority: Dict[str, Any], default_value: Any = ""):
            self.var_value_priority = var_value_priority
            self.default_value = default_value

        def __repr__(self) -> str:
            return f"pick({self.var_value_priority}, default={self.default_value})"

        def __call__(self, cmd):
            available = [k for k in self.var_value_priority if getattr(cmd, k)]
            value = self.var_value_priority[available[0]] if available else self.default_value
            return cmd.render(value)

    class join_paths:
        """Join paths with optional prefix in beginning."""

        def __init__(self, *paths, prefix=""):
            self.paths = paths
            self.prefix = prefix

        def __repr__(self) -> str:
            return f"{self.prefix}join({', '.join(self.paths)})"

        def __call__(self, cmd) -> str:
            return self.prefix + os.path.join(*[cmd.render(p) for p in self.paths])

    @classmethod
    def as_numeric(cls, value: Any) -> int | float:
        """Convert string to numeric value."""
        if isinstance(value, (int, float)):
            return value

        for number_type in (int, float):
            try:
                return number_type(value)
            except ValueError:
                continue

        raise ValueError(f"Cannot cast '{value}' to number")

    class _reduce_numeric_value:
        def __init__(self, reducer, *var_names: str, default_value=None):
            self.reducer = reducer
            self.var_names = var_names
            self.default_value = default_value

        def __repr__(self) -> str:
            name = getattr(self.reducer, "__name__", str(self.reducer))
            return f"{name}({', '.join(self.var_names)}, default={self.default_value})"

        def __call__(self, cmd):
            if not any(getattr(cmd, v) for v in self.var_names):
                if self.default_value is None:
                    raise ValueError(f"All {self.var_names} are missing, cannot reduce")
                return self.default_value

            return str(
                self.reducer(Cmd.as_numeric(cmd.render(getattr(cmd, v))) for v in self.var_names if getattr(cmd, v))
            )

    class min_value(_reduce_numeric_value):
        """Return minimum value for variables (if present)."""

        def __init__(self, *var_names: str, default_value=None):
            super().__init__(min, *var_names, default_value=default_value)

    class max_value(_reduce_numeric_value):
        """Return maximum value for variables (if present)."""

        def __init__(self, *var_names: str, default_value=None):
            super().__init__(max, *var_names, default_value=default_value)


class CmdSeq:
    """Commands which are in a serial sequence."""

    def __init__(self, cmds: List[Cmd] | None):
        if isinstance(cmds, list):
            self.cmds = cmds
        elif cmds is None:
            self.cmds = []
        else:
            self.cmds = list(cmds)

    def copy(self, **kwargs) -> "CmdSeq":
        """Make it easy to create new version of this command sequence with
        overridden settings."""
        return CmdSeq([c.copy(**kwargs) for c in self.cmds])

    def apply(self, iterable: Iterable) -> "CmdPool":
        return CmdPool([self.copy(**item) for item in iterable])

    def append(self, other: Union["CmdSeq", Cmd]) -> "CmdSeq":
        if isinstance(other, CmdSeq):
            return CmdSeq(self.cmds + other.cmds)
        else:
            assert isinstance(other, Cmd)
            return CmdSeq(self.cmds + [other])

    def prepend(self, other: Union["CmdSeq", Cmd]) -> "CmdSeq":
        if isinstance(other, CmdSeq):
            return CmdSeq(other.cmds + self.cmds)
        else:
            assert isinstance(other, Cmd)
            return CmdSeq([other] + self.cmds)

    def trim(self) -> "CmdSeq":
        return CmdSeq([cmd for cmd in self.cmds if not getattr(cmd, "only_if") or cmd.only_if(cmd)])

    def __add__(self, other):
        return self.append(other)

    def __radd__(self, other):
        return self.prepend(other)

    def __iter__(self):
        return self.cmds.__iter__()

    def __reversed__(self):
        return self.cmds.__reversed__()

    def __len__(self):
        return len(self.cmds)

    def __getitem__(self, index):
        return self.cmds[index]


class CmdPool(_Sequence):
    """Commands which are in parallel."""

    def __init__(self, cmds: List[CmdSeq] | List[Cmd] | None = None):
        if isinstance(cmds, list):
            self.cmds = cmds
        elif cmds is None:
            self.cmds = []
        else:
            self.cmds = list(cmds)

    @classmethod
    def merge(cls, cmds) -> "CmdPool":
        if all(isinstance(cmd, CmdPool) for cmd in cmds):
            all_cmds = []
            for pool in cmds:
                all_cmds.extend(pool.cmds)
            return cls(all_cmds)

        pool = cls()
        for cmd in cmds:
            pool = pool.concat(cmd)
        return pool

    def apply(self, iterable: Iterable) -> "CmdPool":
        return CmdPool([c.copy(**item) for c in self.cmds for item in iterable])

    def select(self, func: callable) -> "CmdPool":
        return CmdPool([c for c in self.cmds if func(c)])

    def concat(self, other) -> "CmdPool":
        if isinstance(other, CmdPool):
            return CmdPool(self.cmds + other.cmds)
        else:
            tmp = CmdPool(self.cmds)
            tmp.cmds.extend(other)
            return tmp

    def append(self, other: "CmdPool") -> "CmdPool":
        return CmdPool([c.append(other) for c in self.cmds])

    def prepend(self, other: "CmdPool") -> "CmdPool":
        return CmdPool([c.prepend(other) for c in self.cmds])

    def __add__(self, other):
        return self.append(other)

    def __radd__(self, other):
        return self.prepend(other)

    def __len__(self):
        return len(self.cmds)

    def __getitem__(self, index):
        return self.cmds[index]
