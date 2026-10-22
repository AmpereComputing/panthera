# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Tracing tools."""

__tools__ = [
    "create_image",
    "run",
    "trace",
    "trace_micro",
    "manifest_to_workloads",
    "filter_study_list",
    "annotate_study_list",
]


def arg_list_to_dict(args):
    kwargs = {}

    while args:
        k = args.pop(0)
        if k.startswith("-"):
            k = k[1:]
            if args and not args[0].startswith("-"):
                v = args.pop(0)
                if " " in v:
                    v = v.split()
                kwargs[k] = v
            else:
                kwargs[k] = True

    return kwargs


def dict_to_arg_list(kwargs):
    args = []

    for k, v in kwargs.items():
        if v is True:
            args.append(f"-{k}")
        elif v is False or v is None:
            pass
        else:
            args.append(f"-{k}")
            if isinstance(v, list):
                args.append(" ".join([str(x) for x in v]))
            else:
                args.append(str(v))

    return args
