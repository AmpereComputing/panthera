# Copyright (c) 2019-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

.PHONY: test pytest coverage flake8

PYTHON ?= python3
XLAT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

export PATH := $(XLAT_ROOT)/bin:$(PATH)
export PYTHONPATH := $(XLAT_ROOT)/src$(if $(PYTHONPATH),:$(PYTHONPATH))

test: coverage flake8

pytest:
	$(PYTHON) -m pytest tests

coverage:
	$(PYTHON) -m pytest --cov=ampere.xlat tests
	coverage html

flake8:
	$(PYTHON) -m flake8 src/ampere/xlat
