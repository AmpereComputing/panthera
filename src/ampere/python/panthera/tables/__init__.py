# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


class Table:
    def __init__(self, columns):
        self.name = columns.pop(0)
        if columns and columns[0].lower() == "value":
            self._has_value = True
            columns.pop(0)
        else:
            self._has_value = False
        if columns and not columns[-1]:
            columns.pop()
        self.columns = columns
        self.enums = []
        self._enum_value = 0
        self.data = dict((c, []) for c in columns)
        self.vars = {}

    @property
    def has_values(self):
        return self._has_value

    def add_row(self, row):
        if self._has_value:
            self.enums.append((row[0], int(row[1], 0)))
            row = row[2:]
        else:
            self.enums.append((row[0], self._enum_value))
            self._enum_value += 1
            row = row[1:]

        for i, c in enumerate(self.columns):
            self.data[c].append(row[i])


class TabFile:
    def __init__(self, tabfile):
        self.tables = []
        self.global_vars = {}
        self._parse_tabfile(tabfile)

    def _parse_tabfile(self, tabfile):
        current_table = None

        for line in open(tabfile).readlines():
            if not line:
                break
            elif line == "\n":
                current_table = None
            elif line[0] == "@":
                var, value = line.split(maxsplit=1)
                var = var[1:]
                value = eval(value)
                if current_table:
                    current_table.vars[var] = value
                else:
                    self.global_vars[var] = value
            elif line[0] not in {"-", "=", "#"}:
                cols = [c.strip() for c in line.split("|")]
                if not current_table:
                    current_table = Table(cols)
                    self.tables.append(current_table)
                else:
                    current_table.add_row(cols)
