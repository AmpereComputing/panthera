# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import enum
import math
from typing import Dict, Optional


class HistogramSequence(enum.StrEnum):
    LINEAR = enum.auto()
    EXPONENTIAL = enum.auto()


class Histogram:
    def __init__(
        self,
        minimum: Optional[int] = None,
        maximum: Optional[int] = None,
        granularity: Optional[int] = None,
        sequence: Optional[HistogramSequence] = None,
    ):
        """Histogram bucketer and associated settings.

        Parameters
        ----------
        minimum:
            Minimum bucket value
        maximum:
            Maximum bucket value
        granularity:
            Size of buckets (values rounded down via ceiling)
        sequence:
            How granularity is used, i.e. linear or exponential
        """

        self._minimum = minimum
        self._maximum = maximum
        self._granularity = granularity
        self._sequence = sequence or HistogramSequence.LINEAR

    @classmethod
    def from_spec(cls, spec_string: str) -> "Histogram":
        """Create from spec similar to +count (i.e. min:max:granularity:sequence)."""

        def convert_part(part):
            try:
                return int(part)
            except ValueError:
                for seq in HistogramSequence:
                    if part.lower() in seq.lower():
                        return seq
                return part or None

        parts = [convert_part(part) for part in spec_string.split(":")]
        if len(parts) < 4:
            parts += [None] * (4 - len(parts))

        return cls(*parts)

    def _adjust(self, value: int) -> int:
        if self._granularity is not None:
            if self._sequence == HistogramSequence.LINEAR:
                value = value // self._granularity * self._granularity
            elif self._sequence == HistogramSequence.EXPONENTIAL:
                value = 0 if value <= 0 else (self._granularity ** int(math.log(value, self._granularity)))

        if self._minimum is not None:
            value = max(value, self._minimum)
        if self._maximum is not None:
            value = min(value, self._maximum)

        return value

    def __call__(self, values: Dict[int, int]) -> Dict[int, int]:
        adjusted_values: Dict[int, int] = {}
        for bucket, count in values.items():
            adjusted = self._adjust(bucket)
            if bucket not in adjusted_values:
                adjusted_values[adjusted] = 0
            adjusted_values[adjusted] += count

        return adjusted_values
