# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import enum
import glob
import importlib
import inspect
import os
import sys

modlist = []

for path in glob.glob(os.path.join(os.path.dirname(__file__), "*.py")):
    mod, ext = os.path.splitext(os.path.basename(path))
    if mod != "__init__":
        importlib.import_module(f".{mod}", "panthera.enums")
        modlist.append(f"panthera.enums.{mod}")

enum_map = {}

for mod in modlist:
    classes = inspect.getmembers(sys.modules[mod], inspect.isclass)
    enum_classes = [x for _, x in classes if issubclass(x, enum.Enum) and hasattr(x, "event_data_name")]
    for enum_class in enum_classes:
        enum_map[enum_class.event_data_name()] = enum_class
