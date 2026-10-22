# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from typing import Any, Dict, List, Optional, Union

import evp
from plugins.common.histogram import Histogram


class EventHistogramPerKey:
    class Occurences:
        """Histogram of occurrences (for event)."""

        def __init__(self):
            self._histogram: Dict[str, int] = {}
            self._total_histogram: Dict[int, int] = {}

        @classmethod
        def _increment_histogram(cls, key: Any, values: Dict[Any, int]) -> None:
            if key not in values:
                values[key] = 1
            else:
                values[key] += 1

        @property
        def totals(self) -> Dict[int, int]:
            return self._total_histogram

        def begin(self, key) -> None:
            self.stop(key)
            self._histogram[key] = 0

        def event(self, key) -> None:
            self._increment_histogram(key, self._histogram)

        def stop(self, key) -> None:
            occurences = self._histogram.pop(key, None)
            if occurences is not None:
                self._increment_histogram(occurences, self._total_histogram)

        def stop_all(self) -> None:
            for occurences in self._histogram.values():
                self._increment_histogram(occurences, self._total_histogram)
            self._histogram.clear()

    def __init__(
        self,
        metric_name: str,
        events: Union[List[str], str],
        key_data_types: Union[List[str], str, None] = None,
        start_event: Optional[str] = None,
        stop_event: Optional[str] = None,
        histogram_settings: Optional[Histogram] = None,
    ) -> None:
        """Counts occurrences of events by some 'key' with optional start/stop events.

        Parameters
        ----------
        metric_name:
            Name for metric(s)
        events:
            Events to count occurrences (for each key)
        key_data_types:
            Data types to use as "key" on each event
        start_event:
            Start event (start looking for occurences for key)
        stop_event:
            Stop event (stop looking for occurences for key)

        Example
        -------
        See pcp.py, which emits something similar to:

            *.pcp.trigger.per_stream.2 1
            *.pcp.trigger.per_stream.3 2
            *.pcp.trigger.per_stream.5 1
            ...
        """

        self._name = metric_name
        self._events: Dict[str, EventHistogramPerKey.Occurences] = {}
        self._total_histogram: Dict[int, int] = {}
        self._key_types = key_data_types
        self._key_names = None if key_data_types else []
        self._bucketer = histogram_settings

        events = [events] if isinstance(events, str) else events
        for event in events:
            evp.on(event, lambda e: self._event(e))

        if start_event:
            evp.on(start_event, lambda e: self._start(e))

        if stop_event:
            evp.on(stop_event, lambda e: self._stop(e))

        evp.collect(lambda t: self.metrics(t))
        evp.end_simulation(lambda: self.end())

    def hash_key(self, data) -> str:
        if self._key_names is None:
            self._key_names = [name for name in data.keys() if any(k in name for k in self._key_types)]
        return hash((data[m] for m in self._key_names))

    def _occurences(self, event) -> Occurences:
        if event.name not in self._events:
            self._events[event.name] = self.Occurences()
        return self._events[event.name]

    def _event(self, event) -> None:
        key = self.hash_key(event.data)
        self._occurences(event).event(key)

    def _start(self, event) -> None:
        key = self.hash_key(event.data)
        for occurences in self._events.values():
            occurences.start(key)

    def _stop(self, event) -> None:
        key = self.hash_key(event.data)
        for occurences in self._events.values():
            occurences.stop(key)

    def adjust_buckets(self, values: Dict[int, int]) -> Dict[int, int]:
        if not self._bucketer:
            return values
        return self._bucketer(values)

    def metrics(self, time) -> Dict[str, int]:
        all_metrics: Dict[str, int] = {}
        for event, histogram in self._events.items():
            all_metrics.update(
                {
                    f"{event}.{self._name}.{occurences}": count
                    for occurences, count in self.adjust_buckets(histogram.totals).items()
                }
            )

        return all_metrics

    def end(self) -> None:
        for occurences in self._events.values():
            occurences.stop_all()
