# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# noqa: E501

import os
from subprocess import run, PIPE

test_dir = os.path.dirname(__file__)
example_dir = os.path.realpath(os.path.join(test_dir, '..', 'example'))

if 'PYTHONPATH' in os.environ:
    os.environ['PYTHONPATH'] = os.environ['PYTHONPATH'] + ':' + example_dir
else:
    os.environ['PYTHONPATH'] = example_dir


def fixture(file):
    return os.path.join(test_dir, 'fixtures', file)


def example(file):
    return os.path.join(example_dir, file)


def test_idec():
    status = run(['xlat', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}',
                  example('dhrystone.xlat'), example('decoder.sv.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 0
    assert status.stdout.decode() == open(fixture('idec.out')).read()


def test_idec_translation_expansion():
    status = run(['xlat', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}', '--fmt', 'case',
                  fixture('add_64_addsub_shift.xlat'), fixture('expansion_test.sv.xlat.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 0
    assert status.stdout.decode() == open(fixture('expansion_test.out')).read()


def test_idec_fusion():
    status = run(['xlat', '--enforce-fusion-requirements', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}',
                  example('integer.xlat'), example('fusion.xlat'), example('fusion.sv.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 0
    assert status.stdout.decode() == open(fixture('idec_fusion.out')).read()


def test_idec_fusion_set_assertion():
    status = run(['xlat', '--enforce-fusion-requirements', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}',
                  example('integer.xlat'), example('fusion_set.xlat'), example('fusion.sv.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 255


def test_idec_variants():
    status = run(['xlat', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}',
                  example('evenodd.xlat'), example('variant.sv.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 0
    assert status.stdout.decode() == open(fixture('idec_variant.out')).read()


def test_modal_idec():
    status = run(['xlat', '-i', 'test_uop_import', f'--uops={example("ixu_ops.yaml")}',
                  example('mode.xlat'), example('modal_decoder.sv.py'), '--stdout'], stdout=PIPE)
    assert status.returncode == 0
    assert status.stdout.decode() == open(fixture('idec_modal.out')).read()
