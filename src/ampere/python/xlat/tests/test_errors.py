# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import XlatContext, XlatError


def test_xlat_error_has_context():
    try:
        with XlatContext('testing'):
            raise XlatError('error')
    except XlatError as exc:
        assert exc.context == ['testing']


def test_other_exception_has_context():
    try:
        with XlatContext('testing'):
            raise RuntimeError('foo')
    except Exception as exc:
        assert exc.context == ['testing']
