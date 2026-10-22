#
# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

from copy import copy
from functools import cached_property

import panthera.streams.event_stream_pb2 as es_proto
from panthera.streams.protobuf_stream import ProtobufStreamReader

protobuf_es_magic = 0x53454250  # 0x50(P) 0x42(B) 0x45(E) 0x53(S)
protobuf_es_version = 4  # synchronize with event_stream_proto.h


TXID_NAME = "txid"


class Event:
    """Python-only definition of Event (converted from protobuf objects)"""

    def __init__(self, event, reader):
        self.definition_id = event.definition_id
        self.id = event.id
        self.time = event.time
        self.name = reader.definition_names[self.definition_id]

        def as_data_value(value):
            data_value = getattr(value, value.WhichOneof("values"))
            return reader.convert_enumeration_value(value.definition_id, data_value)

        self.data = {reader.definition_names[value.definition_id]: as_data_value(value) for value in event.values}

    def __repr__(self):
        s = f"id={self.id} time={self.time} name={self.name}"
        for k, v in self.data.items():
            s += f" {k}={v}"
        return s


class Transaction:
    START_NAME = "start_transaction"
    END_NAME = "end_transaction"
    PARENT_NAME = "parent"

    def __init__(self, txid):
        """Create a Transaction with the specified txid."""
        self.events = []
        self.values = {}
        self.children = []
        self.txid = txid
        self.parent = None

    def add_child(self, transaction):
        self.children.append(transaction)
        transaction.parent = self

    def add_event(self, event, value_fields=None):
        self.events.append(event)

        if value_fields:
            for field in value_fields:
                if field in event.data:
                    self.values[field] = event.data[field]

    def all_events(self, include_children=False):
        """Generate events from this and all child transactions."""
        for event in self.events:
            yield event

        if include_children:
            for transaction in self.children:
                for event in transaction.all_events(include_children):
                    yield event


class EventStreamReader(ProtobufStreamReader):
    def __init__(self, filename, all_events=False, convert_enumerations=False):
        """Initialize an event stream for reading"""
        super().__init__(filename, protobuf_es_magic, protobuf_es_version)
        self.simulation_started = all_events
        self.convert_enumerations = convert_enumerations
        self.definitions = {}  # keyed by definition id
        self.definition_names = {}  # keyed by definition id
        self.enumerations = {}  # keyed by enumeration id
        self.parameters = {}  # keyed by parameter name

    def _add_definition(self, definition):
        self.definitions[definition.id] = definition
        self.definition_names[definition.id] = definition.name

    def _add_enumeration(self, enumeration):
        self.enumerations[enumeration.id] = enumeration.values

    def _add_parameter(self, parameter):
        assert parameter.name not in self.parameters
        self.parameters[parameter.name] = parameter

    def find_definition_by_id(self, _id):
        return self.definitions[_id]

    def find_definition_by_name(self, name):
        for definition in self.definitions.values():
            if definition.name == name:
                return definition
        return None

    def find_parameter_by_name(self, name):
        if name not in self.parameters:
            return None
        return self.parameters[name]

    def convert_enumeration_value(self, definition_id, value):
        if isinstance(value, int) and self.convert_enumerations:
            definition = self.find_definition_by_id(definition_id)
            if definition is not None and definition.HasField("enumeration_id"):
                enumerations = self.enumerations.get(definition.enumeration_id)
                if enumerations is not None:
                    return enumerations.get(value, value)

        return value

    def read_event(self, event):
        """Return the first unread event.

        Args:
            event (es_proto.Event): event to override (if present)

        Returns:
            status: read status
        """
        record = es_proto.Record()
        status = super().read(record)
        while status:
            if record.HasField("event") and self.simulation_started:
                break
            elif record.HasField("definition"):
                self._add_definition(record.definition)
            elif record.HasField("parameter"):
                self._add_parameter(record.parameter)
            elif record.HasField("control"):
                if record.control.type == es_proto.CtrlType.Value("START_SIMULATION"):
                    self.simulation_started = True
            elif record.HasField("enumeration"):
                self._add_enumeration(record.enumeration)
            status = super().read(record)

        if status and self.simulation_started:
            event.CopyFrom(record.event)
        return status

    def _read_events_v1(self, events, constructor):
        definitions = set()
        definition_seen = set()
        event_names = {event for event in events if isinstance(event, str)}
        event_matchers = [event for event in events if not isinstance(event, str)]
        event = es_proto.Event()
        status = self.read_event(event)
        while status:
            if self.simulation_started:
                if event.definition_id not in definition_seen:
                    definition_seen.add(event.definition_id)
                    try:
                        definition = self.find_definition_by_id(event.definition_id)
                        if definition.name in event_names or any(
                            matcher(definition.name) for matcher in event_matchers
                        ):
                            definitions.add(event.definition_id)
                    except KeyError:
                        pass

                if event.definition_id in definitions:
                    yield constructor(event, self)

            status = self.read_event(event)

    def _read_until_start_simulation(self):
        assert self.version >= 2

        record = es_proto.Record()
        status = super().read(record)
        while status:
            if record.HasField("definition"):
                self._add_definition(record.definition)
            elif record.HasField("parameter"):
                self._add_parameter(record.parameter)
            elif record.HasField("control"):
                if record.control.type == es_proto.CtrlType.Value("START_SIMULATION"):
                    self.simulation_started = True
                    break
            elif record.HasField("enumeration"):
                self._add_enumeration(record.enumeration)
            else:
                assert not record.HasField("event")

            status = super().read(record)

        return status

    def _read_events_v2(self, events, constructor):
        event_names = {event for event in events if isinstance(event, str)}
        event_matchers = [event for event in events if not isinstance(event, str)]

        # Read until START_SIMULATION and stop
        self._read_until_start_simulation()

        # We have seen all the definitions at this point, because we've read
        # the first event and we're using a version 2+ stream, so let's build a
        # set of the definitions
        matching_definitions = set()
        for def_id, definition in self.definitions.items():
            if definition.name in event_names or any(matcher(definition.name) for matcher in event_matchers):
                matching_definitions.add(def_id)

        record = es_proto.Record()
        status = super().read(record)
        while status:
            if record.event.definition_id in matching_definitions:
                yield constructor(record.event, self)
            status = super().read(record)

    def read_events(self, events, constructor=Event):
        """Read all matching events.

        Args:
            events (iterable of str or callable): event matchers or strings to read

        Yields:
            Event: read event
        """
        if self.version >= 2:
            return self._read_events_v2(events, constructor)
        return self._read_events_v1(events, constructor)

    def read_transactions(
        self,
        events,
        txid_field=TXID_NAME,
        parent_field=Transaction.PARENT_NAME,
        value_fields=None,
    ):
        """Yield transactions which are events grouped by txid_field.

        Args:
            events (iterable of str or callable): event matchers or strings to read
            txid_field (str, optional): TXID field name
            parent_field (str, optional): parent field name
            value_fields (iterable, optional): value fields to include in events

        Yields:
            Transaction: transaction with attached events
        """
        events = set(events)
        value_fields = value_fields or []
        transactions_by_id = {}
        parent_transactions = {}

        for event in self.read_events(events):
            txid = event.data.get(txid_field)
            if txid is None:
                continue

            if txid not in transactions_by_id:
                transactions_by_id[txid] = Transaction(txid)

            transaction = transactions_by_id[txid]
            transaction.add_event(event, value_fields=value_fields)

            if parent_field in event.data:
                parent_id = event.data[parent_field]
                if parent_id in transactions_by_id:
                    parent = transactions_by_id[parent_id]
                    parent_transactions[txid] = parent
                    parent.add_child(transaction)

        for transaction in transactions_by_id.values():
            yield transaction
