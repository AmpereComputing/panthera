# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import logging
import os
import sys
import traceback
import warnings
from functools import partial

from pandas import Series
from pandas.errors import PerformanceWarning

from . import common


class FormulaCache:
    def __init__(self, filenames=None):
        self._formulas = {}
        if filenames:
            for filename in filenames:
                self.read(filename)

    @property
    def formula_filenames(self):
        return list(self._formulas.keys())

    def read(self, filename, directory=None, log=True):
        if directory:
            filename = os.path.join(directory, filename)

        if filename not in self._formulas:
            self._formulas[filename] = read_formula_file(filename, log=log)
        return (filename, self._formulas[filename])

    def compute_all(self, metric, emit_warnings=True):
        compute_all_formulas(self._formulas.items(), metric, emit_warnings=emit_warnings)


def read_formula_file(filename, directory=None, log=True):
    """Read formula file and compile."""
    if log:
        logging.info("reading formulas from {}".format(filename))
    if directory:
        filename = os.path.join(directory, filename)
    with open(filename) as f:
        return compile(f.read(), filename, "exec")


def compute_formulas(filename, formula, metric, cache=None, emit_warnings=True):
    """Compute formulas into metrics."""

    def include_formulas(filename, from_path=None, **params):
        directory = os.path.dirname(os.path.abspath(from_path)) if from_path else None
        if cache is None:
            included = read_formula_file(filename, directory=directory)
        else:
            included = cache.read(filename, directory=directory)

        exec_formulas(included, filename, **params)

    def exec_formulas(code, filename, **params):
        params.update(
            metric=metric,
            include=partial(
                include_formulas,
                from_path=filename,
            ),
            rollup=common,
        )
        try:
            exec(code, params)
        except (TypeError, ValueError):
            if emit_warnings:
                exc_type, exc_value, exc_traceback = sys.exc_info()
                exc_str = "".join(traceback.format_exception(exc_type, exc_value, exc_traceback))
                logging.info(f"metric failed to compute:\n{exc_str}")

    exec_formulas(formula, filename)


def compute_all_formulas(formulas, metric, cache=None, emit_warnings=True):
    """Compute all formulas into metrics."""
    for formula_file, rollup_formula in formulas:
        compute_formulas(
            formula_file,
            rollup_formula,
            metric,
            cache=cache,
            emit_warnings=emit_warnings,
        )


class DataFrameContainer:
    def __init__(self, df):
        self.__df = df

    def get_df(self):
        return self.__df

    def get_zeros(self, dtype=None):
        return Series(0, dtype=dtype, index=self.__df.index)

    def __contains__(self, item):
        return item in self.__df

    def __getitem__(self, item):
        if item in self.__df:
            return self.__df[item]
        raise KeyError(f"{item} cannot be found on DataFrameContainer")

    def __setitem__(self, key, value):
        with warnings.catch_warnings():
            warnings.filterwarnings("error", category=PerformanceWarning)
            try:
                self.__df[key] = value
            except PerformanceWarning:
                self.__df = self.__df.copy()
                self.__df[key] = value


class MetricFormulaHelper:
    __non_formula_names = frozenset(
        [
            "_MetricFormulaHelper__df",
            "_MetricFormulaHelper__name",
            "_MetricFormulaHelper__emit_warnings",
        ]
    )

    def __init__(self, df, name, emit_warnings=True):
        self.__df = df
        self.__name = name
        self.__emit_warnings = emit_warnings

    @property
    def df(self):
        return self.__df.get_df()

    def __repr__(self):
        return f"MetricFormulaHelper<{self.__name}>"

    def __contains__(self, item):
        name = self.__name + "." + item
        return name in self.__df

    def __getattr__(self, item):
        if item.startswith("__array_") or item.startswith("__pandas_"):
            raise AttributeError(item)

        name = self.__name + "." + item
        if name in self.__df:
            return self.__df[name]
        else:
            return MetricFormulaHelper(self.__df, name, emit_warnings=self.__emit_warnings)

    def __setattr__(self, key, value):
        if key == "_MetricFormulaHelper__non_formula_names" or key in self.__non_formula_names:
            super().__setattr__(key, value)
        elif isinstance(value, MetricFormulaHelper):
            self._warn_zeros()
            self.__df[self.__name + "." + key] = 0
        else:
            self.__df[self.__name + "." + key] = value

    def _warn_zeros(self):
        if self.__emit_warnings:
            logging.warning(f"{self.__name} not in dataframe, using 0 for all rows")

    def _warn_div_zero(self):
        if self.__emit_warnings:
            logging.warning(f"{self.__name} had divide-by-zero, using 0 for all rows")

    def __array__(self, dtype=None):
        self._warn_zeros()
        return self.__df.get_zeros(dtype=dtype).to_numpy()

    def __add__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else 0 + other

    def __radd__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else other + 0

    def __sub__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else 0 - other

    def __rsub__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else other - 0

    def __mul__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else 0 * other

    def __rmul__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else other * 0

    def __lshift__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else 0 << other

    def __rshift__(self, other):
        self._warn_zeros()
        return 0 if isinstance(other, MetricFormulaHelper) else 0 >> other

    def __truediv__(self, other):
        self._warn_zeros()
        if isinstance(other, MetricFormulaHelper):
            other._warn_div_zero()
            return 0
        return 0 / other

    def __floordiv__(self, other):
        self._warn_zeros()
        if isinstance(other, MetricFormulaHelper):
            other._warn_div_zero()
            return 0
        return 0 // other

    def __mod__(self, other):
        self._warn_zeros()
        if isinstance(other, MetricFormulaHelper):
            other._warn_div_zero()
            return 0
        return 0 % other


class MetricFormula:
    def __init__(self, df, emit_warnings=True):
        self.__non_formula_names = {
            "_MetricFormula__df",
            "_MetricFormula__emit_warnings",
        }
        self.__df = DataFrameContainer(df)
        self.__emit_warnings = emit_warnings

    @property
    def df(self):
        return self.__df.get_df()

    def __contains__(self, item):
        return item in self.__df

    def __getattr__(self, name):
        if name in self.__df:
            return self.__df[name]
        else:
            return MetricFormulaHelper(self.__df, name, emit_warnings=self.__emit_warnings)

    def __setattr__(self, key, value):
        if key == "_MetricFormula__non_formula_names" or key in self.__non_formula_names:
            super().__setattr__(key, value)
        else:
            self.__df[key] = value
