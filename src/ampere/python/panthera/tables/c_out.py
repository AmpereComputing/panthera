# Copyright (c) 2021, 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from datetime import date


def print_cc_header(tabs, global_vars, output):
    include_event_h = any(["event_data" in tab.vars for tab in tabs])
    print_header(global_vars, output, "#pragma once", include_event_h)
    for tab in tabs:
        print_enum_class(global_vars, tab, output)
        for fname in tab.columns:
            print_enum_function_decl(tab, output, fname)
        print_enum_event_data_func(tab, output)
    print_footer(global_vars, output)


def print_cc_file(tabs, global_vars, output, hfile_name):
    include_event_h = any(["event_data" in tab.vars for tab in tabs])
    print_header(global_vars, output, f'#include "{hfile_name}"', include_event_h)
    for tab in tabs:
        for fname in tab.columns:
            print_enum_function_defn(tab, output, fname)
        print_enum_event_data_defn(tab, output)
    print_footer(global_vars, output)


def enter_namespace(ns, output):
    print(
        f"""
namespace {ns} {{
""",
        file=output,
    )


def exit_namespace(ns, output):
    print(
        f"""
}} // namespace {ns}
""",
        file=output,
    )


def print_header(vars, output, extra, include_event_h):
    print(
        f"""//
// Copyright (c) {date.today().year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
{extra}
#include <nlohmann/json.hpp>
#include <fmt/ostream.h>

#include "ampere/util/enum.h"
""",
        file=output,
    )

    if include_event_h:
        print('#include "ampere/sc/event.h"', file=output)

    if "namespace" in vars:
        enter_namespace(vars["namespace"], output)


def print_footer(vars, output):
    if "namespace" in vars:
        exit_namespace(vars["namespace"], output)


def print_enum_class(vars, tab, output):
    func = "make_enum("
    if "namespace" in vars:
        func = f"make_ns_enum({vars['namespace']}, "
        exit_namespace(vars["namespace"], output)

    print(f"{func}{tab.name},", file=output)
    for index, (item, item_value) in enumerate(tab.enums):
        value = f" = {int(item_value)}" if tab.has_values else ""
        print(f"    {item}{value}{',' if index + 1 != len(tab.enums) else ''}", file=output)
    print(");\n", file=output)

    if "namespace" in vars:
        enter_namespace(vars["namespace"], output)


def print_enum_function_decl(tab, output, fname):
    print(f"bool {fname}({tab.name});", file=output)


def print_enum_event_data_func(tab, output):
    if "event_data" in tab.vars:
        name = tab.vars["event_data"]["name"]
        print(f"AMPERE_EVENT_ENUM({tab.name}, {name});\n", file=output)


def print_enum_event_data_defn(tab, output):
    if "event_data" in tab.vars:
        name = tab.vars["event_data"]["name"]
        description = tab.vars["event_data"]["description"]
        print(f'AMPERE_EVENT_DATA_ENUMERATIONS_DEFN({tab.name}, {name}, "{description}");\n', file=output)


def print_enum_function_defn(tab, output, fname):
    print(f"bool {fname}({tab.name} item)\n{{", file=output)
    values = col_to_values(tab.enums, tab.data[fname])
    print("    static bool val[] = {", file=output)
    for i, val in enumerate(values):
        if val:
            print(f"        true, // {i} (0x{i:x})", file=output)
        else:
            print(f"        false, // {i} (0x{i:x})", file=output)
    print("    };\n", file=output)
    print("    return val[static_cast<int>(item)];", file=output)
    print("}\n", file=output)


def col_to_values(enums, col, default=False):
    max_value = max([v for _, v in enums])
    values = [default] * (max_value + 1)

    for i, (_, value) in enumerate(enums):
        values[value] = col[i]

    return values
