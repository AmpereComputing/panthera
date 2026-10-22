# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Classes to generate trace event files for viewing in Chrome tracing.
"""

import bisect
import json
import logging
import re
from abc import ABC
from itertools import chain
from operator import attrgetter
from typing import Callable

from panthera.streams.event_stream import Event

from . import EventSpecification, parse_event

DEFAULT_PERIOD = 333


def trace_spec(arg: str):
    return EventSpecification(arg)


def split_spec(arg: str):
    if "=" not in arg:
        raise ValueError("Invalid split, expected '='")

    event_spec, fmt = arg.split("=")
    event = EventSpecification(event_spec)
    names = tuple(zip(event.factors, event.long_factors))
    lowercase = set(m for m in re.findall(r"\{([\w_\.]+):s?ls?\}", fmt))
    fmt = re.sub(r"\{([\w_\.]+):(.*?)s?ls?(.*?)\}", r"{\1:\2\3}", fmt)

    def rename_event_from_data(ev):
        values = {
            short_name: ev.data[long_name].lower()
            if short_name in lowercase and isinstance(ev.data[long_name], str)
            else ev.data[long_name]
            for short_name, long_name in names
        }
        ev.name = fmt.format_map(values)

    return event, rename_event_from_data


class EventFilter(ABC):
    """Generic Event filter interface."""

    CacheNames = set[str]
    OptionalCacheNames = CacheNames | None

    def events(self) -> list[EventSpecification]:
        return []

    def __call__(self, event: Event) -> tuple[bool | None, OptionalCacheNames]:
        """Process/filter event, included/excluded if bool returned."""
        return None, None


class EventFilters:
    """Collection of event filters with caching."""

    CacheResult = tuple[bool, str | None]

    def __init__(self, filters: list[EventFilter] | None = None, default_inclusion: bool = False):
        self._cache: dict[
            str, tuple[EventFilter.CacheNames, dict[str, EventFilters.CacheResult]] | EventFilters.CacheResult
        ] = {}
        self._filters = filters or []
        self._default_inclusion = default_inclusion

    def events(self) -> list[EventSpecification]:
        """Get all event specifications for all filters."""
        events = []
        for event_filter in self._filters:
            events.extend(event_filter.events())

        return events

    def _apply(self, event: Event) -> tuple[bool | None, EventFilter.CacheNames]:
        included = None
        cache_data_names = set()
        for event_filter in self._filters:
            filter_includes, cache = event_filter(event)
            if isinstance(cache, set):
                cache_data_names |= cache
            elif cache is not None:
                cache_data_names.add(cache)

            if filter_includes is not None:
                if included is None:
                    included = filter_includes
                else:
                    included &= filter_includes

        return included, cache_data_names

    def _hash_event(self, event: Event, names: EventFilter.CacheNames):
        return hash(tuple(event.data[name] for name in names))

    def _apply_to(self, event: Event, included: bool | None, name_change: str | None = None):
        if name_change:
            event.name = name_change
        return self._default_inclusion if included is None else included

    def apply(self, event: Event) -> bool:
        """Apply all filters to event."""
        if not self._filters:
            return True

        event_hash = None
        if event.name in self._cache:
            names_or_included, cache_or_name = self._cache[event.name]
            if isinstance(names_or_included, set):
                event_hash = self._hash_event(event, names_or_included)
                if event_hash in cache_or_name:
                    return self._apply_to(event, *cache_or_name[event_hash])
            else:
                return self._apply_to(event, names_or_included, cache_or_name)

        original_name = event.name
        included, cache_data_names = self._apply(event)
        result_value = (included, event.name if included and original_name != event.name else None)
        if cache_data_names:
            if event_hash is None:
                event_hash = self._hash_event(event, cache_data_names)
            if original_name not in self._cache:
                self._cache[original_name] = [cache_data_names, {}]
            self._cache[original_name][1][event_hash] = result_value
        else:
            self._cache[original_name] = result_value

        return self._apply_to(event, included)


class EventIncluder(EventFilter):
    """Explicit event includer."""

    def __init__(self, includes: list[EventSpecification]):
        self._events = includes

    def events(self) -> list[EventSpecification]:
        return self._events

    def __call__(self, event: Event) -> tuple[bool | None, None]:
        for include in self._events:
            if include(event.name):
                return True, None

        return None, None


class EventExcluder(EventFilter):
    """Explicit event excluder."""

    def __init__(self, excludes: list[EventSpecification]):
        self._excludes = excludes

    def __call__(self, event: Event) -> tuple[bool | None, None]:
        for exclude in self._excludes:
            if exclude(event.name):
                return False, None

        return None, None


class EventRenamer(EventFilter):
    """Event renamer."""

    def __init__(self, renames: list[Callable[[str], str]]):
        self._renames = renames

    def __call__(self, event: Event) -> tuple[bool | None, None]:
        original_name = event.name
        for rename in self._renames:
            event.name = rename(event.name)

        return True if original_name != event.name else None, None


class EventSplitter(EventFilter):
    """Event splitter, splits factored event into discretely named event."""

    def __init__(
        self,
        splits: list[tuple[EventSpecification, Callable[[Event], None]]] | None = None,
        include: bool = False,
    ):
        self._splits = splits or []
        self._include = include

    def events(self) -> list[EventSpecification]:
        return [e for e, _ in self._splits]

    def __call__(self, event: Event) -> tuple[bool | None, EventFilter.CacheNames]:
        cache_data_names = set()
        for event_spec, gen_func in self._splits:
            if event_spec(event.name):
                gen_func(event)
                cache_data_names = set(event_spec.long_factors)
                if self._include:
                    return True, cache_data_names
                break

        return None, cache_data_names


class TraceEventJSONEncoder(json.JSONEncoder):
    def default(self, o):
        return json.JSONEncoder.default(self, o)


class Row:
    next_tid = 1

    def __init__(self, name, pid, sort_order=0):
        self.name = name
        self.pid = pid
        self.sort_order = sort_order
        self.tid = Row.next_tid
        Row.next_tid += 1


class Occupancy:
    def __init__(self, definition):
        self.name, events = definition.split(":")
        parts = events.split(",")
        self.acquire, self.release = parts[0:2]
        self.data = parts[2] if len(parts) > 2 else ".occupancy"
        self.count = 0


class TraceEventStream:
    TRANSACTION_START = "start_transaction"
    TRANSACTION_END = "end_transaction"
    TRANSACTION_EVENTS = frozenset(
        [
            TRANSACTION_START,
            TRANSACTION_END,
        ]
    )

    def __init__(self, mapper, events, period=DEFAULT_PERIOD):
        self.rows = {}
        self.tes = []
        self.period = period
        self.mapper = mapper
        self.events = events
        self.occupancy_trackers = {}
        self.processes = {}
        self.current_pid = 0
        start_event = self._start_event()
        self.tes.append(start_event)

    def new_event(self, name, row, time, args=None, duration=None):
        te = {
            "name": name,
            "cat": "none",
            "ph": "X",
            "ts": time,
            "dur": duration if duration is not None else self.period,
            "tid": row.tid,
            "pid": row.pid,
        }  # fmt: skip
        if args:
            self.mapper.modify_args(args)
            te["args"] = args
        self.tes.append(te)

    def _pid(self, process):
        if process not in self.processes:
            self.tes.append(self._create_process_name(process, self.current_pid))
            self.processes[process] = self.current_pid
            self.current_pid += 1
        return self.processes[process]

    def trace_events(self):
        """Finalize and return the list of trace events."""
        rows = sorted(self.rows.values(), key=attrgetter("name"))
        rows = sorted(rows, key=attrgetter("sort_order"))
        for i, row in enumerate(rows):
            self.tes.append(self._create_thread_name(row))
            self.tes.append(self._create_thread_sort(row, i))
        return self.tes

    def add_row(self, row, process="Events", sort_order=0):
        """Add a new row definition"""
        if row not in self.rows:
            self.rows[row] = Row(row, pid=self._pid(process), sort_order=sort_order)
        return self.rows[row]

    @staticmethod
    def _create_process_name(name, pid):
        return {
            "name": "process_name",
            "ph": "M",
            "cat": "__metadata",
            "pid": pid,
            "ts": 0,
            "args": {"name": name},
        }  # fmt: skip

    @staticmethod
    def _create_thread_name(row):
        return {
            "name": "thread_name",
            "ph": "M",
            "cat": "__metadata",
            "pid": row.pid,
            "tid": row.tid,
            "ts": 0,
            "args": {"name": row.name},
        }  # fmt: skip

    @staticmethod
    def _create_thread_sort(row, index):
        return {
            "name": "thread_sort_index",
            "ph": "M",
            "cat": "__metadata",
            "pid": row.pid,
            "tid": row.tid,
            "ts": 0,
            "args": {"sort_index": index},
        }  # fmt: skip

    def _start_event(self):
        """Create an event at time 0 so the viewer computes the correct time for events."""
        return {
            "name": "S",
            "cat": "start",
            "ph": "X",
            "ts": int(0),
            "dur": 1,
            "tid": 0,
            "pid": self._pid("Events"),
        }  # fmt: skip


class FreeRowTracker:
    def __init__(self):
        self._free_times = []
        self._free_indexes = []
        self._next_index = 0

    def next_avail(self, start_time):
        i = bisect.bisect_left(self._free_times, start_time)
        if i == 0:
            index = self._next_index
            self._next_index += 1
        else:
            index = self._free_indexes[i - 1]
            del self._free_times[i - 1]
            del self._free_indexes[i - 1]
        return index

    def free(self, index, end_time):
        i = bisect.bisect(self._free_times, end_time)
        self._free_times.insert(i, end_time)
        self._free_indexes.insert(i, index)

    def reserve(self, start_time, end_time):
        index = self.next_avail(start_time)
        self.free(index, end_time)
        return index


class OccupancyTracker:
    def __init__(self, occupancy_list) -> None:
        self.acquire = {}
        self.release = {}
        self.rows = []
        for o in occupancy_list:
            occupancy = Occupancy(o)
            self.acquire[occupancy.acquire] = occupancy
            self.release[occupancy.release] = occupancy
            self.rows.append(occupancy.name)

    @property
    def events(self):
        return chain.from_iterable([o.acquire, o.release] for o in self.acquire.values())

    def acquired(self, event):
        if occupancy := self.acquire.get(event.name):
            if count := ResourceView.value_with_suffix(event, occupancy.data):
                occupancy.count = count
            else:
                occupancy.count += 1
            return occupancy
        return None

    def released(self, event):
        if occupancy := self.release.get(event.name):
            if count := ResourceView.value_with_suffix(event, occupancy.data):
                occupancy.count = count
            elif occupancy.count > 0:
                occupancy.count -= 1
            return occupancy
        return None


class ResourceView(TraceEventStream):
    """Generate trace events for a resource view."""

    def __init__(
        self,
        mapper,
        events,
        period=DEFAULT_PERIOD,
        occupancy_list=None,
        post_to_tx=None,
        pull_from_tx=None,
        event_filters=EventFilters(),
    ):
        super().__init__(mapper, events, period)
        self.transactions = {}
        self.occupancy = OccupancyTracker(occupancy_list or [])
        for row in self.occupancy.rows:
            self.add_row(row, process="Occupancy")
        self._post_to_tx = self.setup_tx_data(post_to_tx or [])
        self._pull_from_tx = self.setup_tx_data(pull_from_tx or [])
        self._event_filters = event_filters

    def process_event_stream(self, es):
        events = set(self.events.keys())
        events.update(self.occupancy.events)

        for event in es.read_events(events):
            self.process(event)

    @staticmethod
    def value_with_suffix(event, suffix):
        for name, value in event.data.items():
            if name.endswith(suffix):
                return value

    def process(self, event):
        if event.name == self.TRANSACTION_START:
            self.start_transaction(event)
        elif event.name == self.TRANSACTION_END:
            self.end_transaction(event)
        else:
            if not self._event_filters.apply(event):
                return

            sort_order = self._get_sort_order(event)

            if occupancy := self.occupancy.acquired(event):
                process_name, row_name = self.mapper.process(occupancy.name, default_group="Occupancy")
                row = self.add_row(row_name, process_name, sort_order=sort_order)
                self.acquire_event(occupancy, row, event.time)
            elif occupancy := self.occupancy.released(event):
                process_name, row_name = self.mapper.process(occupancy.name, default_group="Occupancy")
                row = self.add_row(row_name, process_name, sort_order=sort_order)
                self.release_event(occupancy, row, event.time)
            else:
                process_name, row_name = self.mapper.process(event.name)
                row = self.add_row(row_name, process_name, sort_order=sort_order)

            self.post_tx_data(event)
            self.pull_tx_data(event)

            abbrev = self.mapper.event_name_to_abbrev(event)
            self.add_parents(event)
            self.new_event(abbrev, row, event.time, event.data)

    def acquire_event(self, occupancy, row, time):
        te = {
            "name": str(occupancy.count),
            "cat": "none",
            "ph": "B",
            "ts": time,
            "tid": row.tid,
            "pid": row.pid,
        }  # fmt: skip
        self.tes.append(te)

    def release_event(self, occupancy, row, time):
        te = {
            "cat": "none",
            "ph": "E",
            "ts": time,
            "tid": row.tid,
            "pid": row.pid,
        }  # fmt: skip
        self.tes.append(te)

    def start_transaction(self, event):
        try:
            if "parent" in event.data:
                parents = [f"txid_{event.data['parent']}."]
                if event.data["parent"] in self.transactions:
                    parents += self.transactions[event.data["parent"]]["parents"]
            else:
                parents = []
            self.transactions[event.data["txid"]] = {"start": event, "parents": parents}
        except KeyError:
            pass

    def end_transaction(self, event):
        try:
            del self.transactions[event.data["txid"]]
        except KeyError:
            pass

    def add_parents(self, event):
        if "txid" in event.data:
            txid = event.data["txid"]
            if txid in self.transactions:
                parents = self.transactions[txid]["parents"]
                if parents:
                    event.data["parents"] = "".join(parents)

    @staticmethod
    def setup_tx_data(mapping):
        tx_data = {}
        item_re = re.compile(r"([^:]+):([^=]+)(=(\S+))?")
        for item in mapping:
            m = item_re.match(item)
            if m:
                event_name, data_name, alias_name = m.group(1, 2, 4)
                if event_name not in tx_data:
                    tx_data[event_name] = {}

                alias_name = alias_name or data_name
                tx_data[event_name][data_name] = alias_name
                if "." not in data_name and "." in event_name:
                    potential_full_name = ".".join(event_name.split(".")[:-1] + [data_name])
                    tx_data[event_name][potential_full_name] = alias_name
            else:
                raise RuntimeError(f'transaction data spec did not look like <event>:<data>[=<alias>] (got "{item}")')
        return tx_data

    def post_tx_data(self, event):
        post_for_this_event = self._post_to_tx.get(event.name, None)

        if post_for_this_event:  # and 'txid' in event.data
            try:
                tx = self.transactions.get(event.data["txid"], None)
            except KeyError:
                logging.info("txid key error: " + event.name)
                return

            if tx:
                for k, v in event.data.items():
                    if k in post_for_this_event:
                        tx["start"].data[post_for_this_event[k]] = v

    def pull_tx_data(self, event):
        pull_for_this_event = self._pull_from_tx.get(event.name, None)

        if pull_for_this_event:  # and 'txid' in event.data
            try:
                tx = self.transactions.get(event.data["txid"], None)
            except KeyError:
                logging.info("txid key error: " + event.name)
                return

            pull_for_this_event = pull_for_this_event.copy()
            while tx and pull_for_this_event:
                for k, v in tx["start"].data.items():
                    if k in pull_for_this_event:
                        event.data[pull_for_this_event[k]] = v
                        del pull_for_this_event[k]
                if "parent" in tx["start"].data:
                    tx = self.transactions.get(tx["start"].data["parent"], None)
                else:
                    tx = None

    def _get_sort_order(self, event):
        if event.name in self.events:
            return self.events[event.name]

        for event_spec, order in self.events.items():
            if callable(event_spec) and event_spec(event.name):
                return order

        return 0


class TransactionBuffer(TraceEventStream):
    """Generate a transactional buffer view"""

    def __init__(
        self,
        mapper,
        events,
        size,
        field,
        name_field=None,
        start_event=None,
        end_event=None,
        period=DEFAULT_PERIOD,
        event_filters=EventFilters(),
    ):
        super().__init__(mapper, events, period)
        self.size = size
        self.field = field
        self.name_field = name_field
        self.start_event = start_event
        self.end_event = end_event
        self.transactions = {}
        for i in range(self.size):
            self.add_row(self._row_name(i))
        self._free_row_tracker = FreeRowTracker()
        self._event_filters = event_filters

    def process_event_stream(self, event_stream_reader):
        logging.debug("processing transactions")
        for transaction in event_stream_reader.read_transactions(
            set(self.events.keys()), value_fields=[self.field, self.name_field]
        ):
            self.process(transaction)

    def process(self, transaction):
        if self.field == "FREE":
            buffer_id = self._free_row_tracker.reserve(start_time, end_time)
        else:
            buffer_id = self.find_data(transaction, self.field)
        if buffer_id is None:
            return
        self.transactions[transaction.txid] = transaction
        events = list(self.find_events(transaction))

        if not events:
            return

        first_event = events[0]
        last_event = events[-1]
        start_time = first_event.time
        end_time = last_event.time

        duration = last_event.time + self.period - start_time
        args = {"txid": first_event.data["txid"]}
        if self.field != "txid":
            args[self.field] = buffer_id

        if self.size:
            row = self._row_name(buffer_id % self.size)
        else:
            row = self._row_name(buffer_id)
        row = self.add_row(row)

        if self.name_field in first_event.data:
            name = first_event.data[self.name_field]
        else:
            name = "transaction"

        self.new_event(name, row, start_time, args=args, duration=duration)

        for event in events:
            if event.name in self.TRANSACTION_EVENTS or not self._event_filters.apply(event):
                continue

            abbrev = self.mapper.event_name_to_abbrev(event)
            event.data["name"] = event.name

            self.new_event(abbrev, row, event.time, event.data)

            if event.name == self.end_event:
                break

    def find_events(self, transaction):
        found_start = self.start_event is None
        top_level_txid = transaction.txid
        all_events = sorted(transaction.all_events(include_children=True), key=lambda e: e.time)

        for event in all_events:
            if event.data.get("txid") == top_level_txid:
                if self.end_event and self.end_event == event.name:
                    yield event
                    return
                elif event.name == self.TRANSACTION_END:
                    return
                elif self.start_event and self.start_event == event.name:
                    found_start = True
            if found_start:
                yield event

    @staticmethod
    def find_data(transaction, field_name):
        if field_name is not None:
            return transaction.values.get(field_name)
        return None

    @staticmethod
    def _row_name(number):
        return f"Entry {number:03}"
