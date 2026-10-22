# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from collections import defaultdict
from typing import Dict, Optional

import evp
from plugins.common.histogram import Histogram


class OccupancyTracker:
    def __init__(self, alloc_event, dealloc_event, collect_average=False):
        self.entries = defaultdict(lambda: 0)
        self.occupancy_time = defaultdict(lambda: 0)
        self.last_time = defaultdict(lambda: 0)
        self.earliest_time = 0
        self.latest_time = 0
        self.collect_average = collect_average

        evp.on(alloc_event, lambda e: self._allocate(e))
        evp.on(dealloc_event, lambda e: self._deallocate(e))

    def _allocate(self, event):
        self._update_occupancy(event, 1)

    def _deallocate(self, event):
        self._update_occupancy(event, -1)

    def _update_time(self, name, time):
        duration = time - self.last_time.get(name, 0)
        self.last_time[name] = time
        return duration

    def _update_occupancy(self, event, inc, name=None):
        name = name or event.name
        average = None
        duration = self._update_time(name, event.time)
        if self.collect_average:
            self.occupancy_time[name] += self.entries[name] * duration
            if self.earliest_time == 0 or event.time < self.earliest_time:
                self.earliest_time = event.time
            if event.time > self.latest_time:
                self.latest_time = event.time
            if self.latest_time != self.earliest_time:
                average = self.occupancy_time[name] / (self.latest_time - self.earliest_time)

        self.entries[name] = max(self.entries[name] + inc, 0)
        occupancy = self.entries[name]
        return occupancy, duration, average or occupancy


class Occupancy(OccupancyTracker):
    """Occupancy on each transition as CSV."""

    def __init__(
        self,
        alloc_event,
        dealloc_event,
        output,
        name_col,
        entries_col,
        rename=None,
        group_on=None,
        report_average=False,
    ):
        self.report_average = report_average
        self.group_on = group_on
        self.active_events = set()
        self.output = open(output, "w")
        self.rename = rename
        if self.report_average:
            print(f"time,{name_col},{entries_col},{entries_col} Running Average", file=self.output)
        else:
            print(f"time,{name_col},{entries_col}", file=self.output)

        super().__init__(alloc_event, dealloc_event, collect_average=report_average)
        evp.end_simulation(lambda: self._end_simulation())

    def _allocate(self, event):
        if self.group_on is not None and self.group_on in event.data:
            assert event.data[self.group_on] not in self.active_events, (
                f"Error: duplicate allocate event for id: {event.data[self.group_on]}"
            )
            self.active_events.add(event.data[self.group_on])
        super()._allocate(event)

    def _deallocate(self, event):
        if self.group_on is not None and self.group_on in event.data:
            if event.data[self.group_on] in self.active_events:
                super()._deallocate(event)
                self.active_events.discard(event.data[self.group_on])
        else:
            super()._deallocate(event)

    def _update_occupancy(self, event, inc, name=None):
        name = self._rename(name or event.name)
        occupancy, _duration, average = super()._update_occupancy(event, inc, name=name)
        self._record(event.time, name, occupancy, average)

    def _rename(self, event_name):
        return self.rename(event_name) if self.rename is not None else event_name

    def _record(self, time, name, occupancy, average):
        if self.report_average:
            print(f"{time},{name},{occupancy},{average}", file=self.output)
        else:
            print(f"{time},{name},{occupancy}", file=self.output)

    def _end_simulation(self):
        self.output.close()


class OccupancyHistogram(OccupancyTracker):
    """Occupancy histogram, emits histogram of cycles within each occupancy."""

    def __init__(
        self,
        alloc_event: str,
        dealloc_event: str,
        metric_name: str,
        factor_name: Optional[str] = None,
        histogram_settings: Optional[Histogram] = None,
    ):
        self._name = metric_name
        self._factor_name = factor_name
        self._histogram: Dict[str, Dict[int, int]] = defaultdict(lambda: defaultdict(lambda: 0))
        self._bucketer = histogram_settings

        super().__init__(alloc_event, dealloc_event)
        evp.collect(lambda t: self.metrics(t))

    def _update_occupancy(self, event, inc, name=None):
        name = name or ".".join(event.name.split(".")[:-1])
        prev_occupancy = self.entries[name]
        _occupancy, duration, _average = super()._update_occupancy(event, inc, name=name)
        self._histogram[name][prev_occupancy] += duration

    def adjust_buckets(self, values: Dict[int, int]) -> Dict[int, int]:
        if not self._bucketer:
            return values
        return self._bucketer(values)

    def bucket_name(self, name: str, occurences: int) -> str:
        if self._factor_name:
            return f"{name}.{self._name}/{self._factor_name}:{occurences}"
        return f"{name}.{self._name}.{occurences}"

    def metrics(self, time) -> Dict[str, int]:
        all_metrics: Dict[str, int] = {}
        for event, histogram in self._histogram.items():
            histogram[self.entries[event]] += self._update_time(event, time)
            all_metrics.update(
                {
                    self.bucket_name(event, occurences): count
                    for occurences, count in self.adjust_buckets(histogram).items()
                }
            )

        return all_metrics
