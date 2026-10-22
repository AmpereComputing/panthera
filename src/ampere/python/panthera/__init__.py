# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import logging
import os
import shlex
import sys

install_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
nix_shell = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "shell.nix"))
install_using_nix = os.path.exists(nix_shell)


def within_nix_env():
    return "IN_NIX_SHELL" in os.environ


def check_nix_env():
    assert install_using_nix == within_nix_env(), (
        "Detected a different environment than your build of panthera was produced with. (nix-shell vs not)"
    )


def setup_logging(debug=False):
    return logging.basicConfig(
        format="%(levelname)s: %(asctime)s %(message)s",
        datefmt="%I:%M:%S",
        level=logging.DEBUG if debug else logging.INFO,
    )


def int_arg_range(min_value, max_value):
    """
    Provides the 'type' of an integer argument, where the integer is required
    to be within a given range
    """
    assert max_value > min_value, "int_arg_range: min_value must be less than max_value"

    def int_arg_checker(arg):
        try:
            i = int(arg)
        except ValueError:
            raise argparse.ArgumentTypeError("Argument must be an integer")
        if i < min_value or i > max_value:
            raise argparse.ArgumentTypeError(f"Argument must be in range [{min_value}, {max_value}]")
        return i

    return int_arg_checker


class ArgumentParser(argparse.ArgumentParser):
    """Argument parser which supports config files via '@' prefix (by default)."""

    def __init__(
        self,
        tool=None,
        tool_aliases=None,
        comments=False,
        arg_file_path=None,
        fromfile_prefix_chars=None,
        epilog="",
        formatter_class=argparse.RawTextHelpFormatter,
        **kwargs,
    ):
        self.enable_comments = comments
        self.fromfile_prefix_chars = fromfile_prefix_chars or "@"
        self._arg_file_path = arg_file_path or ["."]
        if os.environ.get("PANTHERA_ROOT"):
            cfg_dir = os.path.join(os.environ["PANTHERA_ROOT"], "config")
            if tool:
                self._arg_file_path.append(os.path.join(cfg_dir, tool))

            tool_aliases = tool_aliases or []
            for alias in tool_aliases:
                self._arg_file_path.append(os.path.join(cfg_dir, alias))

        epilog = (
            f"""
Config file options:
    Additional arguments can be inserted from config files with `[{self.fromfile_prefix_chars}]<filename>`.

    For example:
        {os.path.basename(tool or kwargs.get("prog", "<tool>"))} {self.fromfile_prefix_chars[0]}something.cfg

    Will insert all arguments within the "something.cfg" file, which could be in any of the following:
        {", ".join('"' + f + '"' for f in self._arg_file_path)}

"""
            + epilog
        )

        super().__init__(
            epilog=epilog,
            fromfile_prefix_chars=self.fromfile_prefix_chars,
            formatter_class=formatter_class or argparse.RawTextHelpFormatter,
            **kwargs,
        )

    def _read_args_from_files(self, arg_strings, context=None):
        # expand arguments referencing files
        new_arg_strings = []
        for arg_string in arg_strings:
            # for regular arguments, just add them back into the list
            if not arg_string or arg_string[0] not in self.fromfile_prefix_chars:
                new_arg_strings.append(arg_string)
                continue

            # replace arguments referencing files with the file content
            from_path = os.path.realpath(os.path.dirname(context[-1])) if context else None
            context = context or []
            try:
                config_path = arg_string[1:]
                if from_path is None:
                    search_paths = [os.path.join(path, config_path) for path in self._arg_file_path]
                else:
                    search_paths = [os.path.join(from_path, config_path)]

                found = False
                for path in search_paths:
                    try:
                        with open(path, "r") as args_file:
                            for context_path in context:
                                if os.path.realpath(path) == os.path.realpath(context_path):
                                    raise ValueError(
                                        f"Inclusion loop, including {path} within {config_path}, previously from {context_path}"
                                    )

                            new_arg_strings.extend(
                                self._read_args_from_files(
                                    [
                                        arg
                                        for arg_line in args_file.readlines()
                                        for arg in self.convert_arg_line_to_args(arg_line)
                                    ],
                                    context=context + [path],
                                )
                            )
                        found = True
                        break
                    except FileNotFoundError:
                        pass

                if not found:
                    raise FileNotFoundError(f"No such file or directory: {config_path}")

            except OSError:
                err = sys.exc_info()[1]
                self.error(str(err))

        # return the modified argument list
        return new_arg_strings

    def convert_arg_line_to_args(self, arg_line):
        return shlex.split(arg_line, comments=self.enable_comments)
