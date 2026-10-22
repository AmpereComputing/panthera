# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
These are the commands that can be run using panthera.
"""

import importlib
import os.path
import pkgutil

from . import install_dir


def help(module_dir, prefix, doc, explicit=None):
    modules_to_skip = {"__main__", "help"}
    print(doc)

    def print_doc(name, description=None, module=None, sub_module=False):
        description = description or ""
        if module is not None and module.__doc__:
            description = module.__doc__.strip().splitlines()[0].rstrip(".")
            if sub_module:
                name = f"  .{name}"

        if description:
            print(f"    {name: <32}{description}")

    remaining_explicit = list(sorted(explicit.keys())) if explicit else []
    module_iter = pkgutil.iter_modules([module_dir])
    for module_info in module_iter:
        if module_info.name in modules_to_skip:
            continue

        while remaining_explicit and remaining_explicit[0] < module_info.name:
            print_doc(remaining_explicit[0], explicit[remaining_explicit[0]])
            del remaining_explicit[0]

        try:
            module = importlib.import_module(prefix + module_info.name)
            print_doc(module_info.name, module=module)

            if module.__tools__:
                for tool in module.__tools__:
                    sub_module = importlib.import_module(prefix + module_info.name + "." + tool)
                    print_doc(tool, module=sub_module, sub_module=True)

        except (ModuleNotFoundError, ImportError) as e:
            # Some tools are in panthera even though they're usually called elsewhere
            # and have different assumptions on the PYTHONPATH
            if "No module named 'ampere'" not in str(e):
                print(f"Failed to import {prefix}{module_info.name}")
        except:
            pass

    for name in remaining_explicit:
        print_doc(name, explicit[name])


if __name__ == "__main__":
    lib_dir = os.path.join(install_dir, "lib", "python", "panthera")
    help(lib_dir, "panthera.", __doc__)
