# Copyright (c) 2025-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os

from jinja2 import Environment, FileSystemLoader


class Notifier:
    def __init__(self, template=None):
        self.template = template

    @property
    def template_env(self):
        template_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")
        return Environment(loader=FileSystemLoader(template_dir))

    def render(self, template=None, **kwargs):
        template = self.template_env.get_template(template or self.template)
        return template.render(**kwargs)

    def notify(self, content):
        """Write rendered content locally."""


class PrintNotifier(Notifier):
    def notify(self, content):
        print(content)
