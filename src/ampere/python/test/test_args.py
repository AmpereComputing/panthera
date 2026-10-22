# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from panthera import ArgumentParser

FIXTURE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "fixtures"))
FIXTURE_CONFIG_DIR = os.path.join(FIXTURE_DIR, "configs")


class WrappedArgumentParser(ArgumentParser):
    def exit(self, status=0, message=None):
        if status != 0:
            raise Exception(message)


class TestArgumentParserBase(unittest.TestCase):
    def setUp(self, **kwargs):
        self.parser = WrappedArgumentParser(**kwargs)
        self.add_args()

    def add_args(self):
        self.parser.add_argument("--some-single-option", action="store_true")
        self.parser.add_argument("--some-option", type=int)
        self.parser.add_argument("--some-other-option", type=int)

    def parse_args(self, args):
        return self.parser.parse_args(args)

    def check_something(self, args):
        self.assertTrue(args.some_single_option)
        self.assertEqual(args.some_option, 2)
        self.assertFalse(args.some_other_option)


class TestArgumentParser(TestArgumentParserBase):
    def setUp(self):
        super().setUp(arg_file_path=[FIXTURE_CONFIG_DIR])

    def test_nothing(self):
        args = self.parse_args([])
        self.assertFalse(args.some_single_option)
        self.assertFalse(args.some_option)
        self.assertFalse(args.some_other_option)

    def test_regular(self):
        args = self.parse_args(["--some-other-option", "4"])
        self.assertEqual(args.some_other_option, 4)

    def test_absolute_config(self):
        args = self.parse_args([f"@{os.path.join(FIXTURE_CONFIG_DIR, 'something.cfg')}"])
        self.check_something(args)

    def test_relative_config(self):
        args = self.parse_args(["@something.cfg"])
        self.check_something(args)

    def test_inner_config(self):
        args = self.parse_args(["@inner_config.cfg"])
        self.assertTrue(args.some_single_option)
        self.assertEqual(args.some_option, 2)
        self.assertEqual(args.some_other_option, 9)


class TestArgumentParserDifferentPrefix(TestArgumentParserBase):
    def setUp(self):
        super().setUp(fromfile_prefix_chars="?!", arg_file_path=[FIXTURE_CONFIG_DIR])

    def test_default_throws(self):
        with self.assertRaises(Exception):
            self.parse_args(["@something.cfg"])

    def test_question(self):
        args = self.parse_args(["?something.cfg"])
        self.check_something(args)

    def test_exclamation(self):
        args = self.parse_args(["!something.cfg"])
        self.check_something(args)


class TestArgumentParserMultiplePaths(TestArgumentParserBase):
    def setUp(self):
        super().setUp(arg_file_path=[os.path.join(FIXTURE_CONFIG_DIR, "other"), FIXTURE_CONFIG_DIR])

    def test_ordering(self):
        args = self.parse_args(["@something.cfg"])
        self.assertFalse(args.some_single_option)
        self.assertEqual(args.some_option, 5)
        self.assertFalse(args.some_other_option)

    def test_missing_first_config(self):
        args = self.parse_args(["@inner_config.cfg"])
        self.assertTrue(args.some_single_option)
        self.assertEqual(args.some_option, 2)
        self.assertEqual(args.some_other_option, 9)

    def test_relative_inner_config(self):
        args = self.parse_args(["@other.cfg"])
        self.assertTrue(args.some_single_option)
        self.assertEqual(args.some_option, 2)
        self.assertEqual(args.some_other_option, 7)
