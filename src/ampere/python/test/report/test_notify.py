# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import contextlib
import io
import unittest

from panthera.report.notify import PrintNotifier


class TestPrintNotifier(unittest.TestCase):
    def setUp(self):
        self.notifier = PrintNotifier("basic.md.j2")

    def test_render(self):
        self.assertEqual(
            self.notifier.render(title="Title", body="Some message"),
            "# Title\n\nSome message",
        )

    def test_notify(self):
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            self.notifier.notify("content")

        self.assertEqual(f.getvalue(), "content\n")
