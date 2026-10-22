# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


def print_py_output(tabs, global_vars, output):
    print_py_header(output)
    for tab in tabs:
        print_py_enum(tab, output)


def print_py_header(output):
    print("from enum import IntEnum\n", file=output)


def print_py_enum(tab, output):
    print(f"\nclass {tab.name}(IntEnum):", file=output)
    for item, value in tab.enums:
        if str(item) == "None":
            item = "NONE"
        print(f"    {item} = {value}", file=output)
    print(f"\n    def __str__(self):\n        return self.name", file=output)
    if "event_data" in tab.vars:
        print(
            f"\n    @staticmethod\n    def event_data_name():\n        return '{tab.vars['event_data']['name']}'\n",
            file=output,
        )
