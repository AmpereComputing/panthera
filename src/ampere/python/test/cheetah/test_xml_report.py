# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import unittest

from panthera.cheetah.xml_report import XMLReport

FIXTURE_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "fixtures")


class TestXMLReport(unittest.TestCase):
    def fixture(self, name):
        return os.path.join(FIXTURE_PATH, "xml_reports", f"{name}.xml")


class TestAllPassingXMLReport(TestXMLReport):
    def setUp(self) -> None:
        self.report = XMLReport.from_xml(self.fixture("all_passed"))

    def test_summary(self):
        self.assertEqual(
            self.report.summary(),
            {
                "test": {
                    "jobs": 3,
                    "errors": 0,
                    "incompletes": 0,
                }
            },
        )

    def test_errors(self):
        self.assertEqual(self.report.errors(), {})


class TestSomeIncompleteXMLReport(TestXMLReport):
    def setUp(self) -> None:
        self.report = XMLReport.from_xml(self.fixture("some_incomplete"))

    def test_summary(self):
        self.assertEqual(
            self.report.summary(),
            {
                "test": {
                    "jobs": 3,
                    "errors": 0,
                    "incompletes": 1,
                }
            },
        )

    def test_errors(self):
        self.assertEqual(self.report.errors(), {})


class TestSomeFailingXMLReport(TestXMLReport):
    def setUp(self) -> None:
        self.report = XMLReport.from_xml(self.fixture("failures"))

    def test_summary(self):
        self.assertEqual(
            self.report.summary(),
            {
                "test": {
                    "jobs": 3,
                    "errors": 2,
                    "incompletes": 0,
                }
            },
        )

    def test_errors(self):
        self.assertEqual(
            self.report.errors(),
            {
                "test/config_0/dhrystone": "some error",
                "test/config_2/dhrystone": "some warning",
            },
        )
