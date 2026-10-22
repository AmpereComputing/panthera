# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from panthera.cheetah.studies.evp import EVPCmd

from . import BaseSimTest


class EVPCmdTest(BaseSimTest):
    def render_sim(self, **kwargs):
        return self.render(EVPCmd(**self.settings, **self.trace, **kwargs))

    def assertExpected(self, expected, **kwargs):
        self.assertEqual(
            self.render_sim(**kwargs),
            self.normalize(expected),
        )

    def test_basic(self):
        self.assertExpected("./install/bin/evp --es something.es.xz -i '*.gpc.instruction_commit=30000'")

    def test_interval(self):
        self.assertExpected("./install/bin/evp --es something.es.xz -i 10us", interval="10us")

    def test_config(self):
        self.assertExpected(
            "./install/bin/evp --es something.es.xz -i '*.gpc.instruction_commit=30000' @base.cfg",
            config="base.cfg",
        )

    def test_start_stop(self):
        self.assertExpected(
            "./install/bin/evp --es something.es.xz -i '*.gpc.instruction_commit=30000' --start 100 --stop 1000",
            start=100,
            stop=1000,
        )
