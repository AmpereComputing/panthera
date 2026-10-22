# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
import re
import subprocess

test_dir = os.path.dirname(__file__)

if 'PYTHONPATH' in os.environ:
    os.environ['PYTHONPATH'] = os.environ['PYTHONPATH'] + ':' + os.path.join(test_dir, 'fixtures')
else:
    os.environ['PYTHONPATH'] = os.path.join(test_dir, 'fixtures')


def fixture(file):
    return os.path.join(test_dir, 'fixtures', file)


class udec:
    PROG = 'xlat'
    UOP_IMPORT = 'uops'

    @staticmethod
    def run(*args, **kwargs):
        return subprocess.run(udec.command(args), **kwargs)

    @staticmethod
    def call(*args, **kwargs):
        return subprocess.call(udec.command(args), **kwargs)

    @staticmethod
    def check_call(*args, **kwargs):
        return subprocess.check_call(udec.command(args), **kwargs)

    @staticmethod
    def check_output(*args, **kwargs):
        return subprocess.check_output(udec.command(args), **kwargs)

    @staticmethod
    def command(args):
        return [udec.PROG, '-i', udec.UOP_IMPORT] + list(args)


def test_udec_logic():
    output = udec.check_output(fixture('testpred.sv.py'), '--stdout')
    assert output.decode() == '''
package testpred;

function automatic logic isPipeA(input logic [7:0] uopcode);
  isPipeA = 1'b1;
endfunction


function automatic logic test_predicate(input logic [7:0] uopcode);
  test_predicate = ~uopcode[0] & ~uopcode[1] |
                   ~uopcode[2];
endfunction

endpackage
'''.lstrip()


def test_udec_failure():
    status = udec.run(fixture('fail.pred.py'), '--stdout', stdout=subprocess.PIPE,
                      stderr=subprocess.STDOUT)
    assert status.returncode != 0
    assert re.search("name 'bogus' is not defined", status.stdout.decode())


def test_udec_generates_default_output_alongisde_input():
    try:
        udec.call(fixture('testpred.sv.py'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert os.path.exists(fixture('testpred.sv'))
    finally:
        if os.path.exists(fixture('testpred.sv')):
            os.remove(fixture('testpred.sv'))


def test_udec_generates_specific_output():
    try:
        udec.check_call(fixture('testpred.sv.py'), '-o', 'foo.svh',
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        assert os.path.exists('foo.svh')
    finally:
        if os.path.exists('foo.svh'):
            os.remove('foo.svh')


def test_udec_detects_conflicting_output_options():
    status = udec.run(fixture('testpred.sv.py'), '-o', 'foo.svh', '--stdout',
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert status.returncode != 0
    assert status.stdout == b'ERROR: cannot specify both -o and --stdout\n'


def test_udec_reports_missing_input_file():
    status = udec.run(fixture('does.not.exist.py'), '--stdout',
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert status.returncode != 0
    assert re.match("ERROR: No such file or directory: .*does\\.not\\.exist\\.py", status.stdout.decode())


def test_udec_for_default_false():
    status = subprocess.run(['xlat', '-i', 'default_false_uops', fixture('default_false.sv.py'), '--stdout'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert status.stdout.decode() == '''
package default_false;

function automatic logic isValidIxuUop(input logic [3:0] uopcode);
  isValidIxuUop = uopcode[0] & ~uopcode[1] & ~uopcode[2] & ~uopcode[3] |
                  ~uopcode[0] & uopcode[1] & ~uopcode[2] & ~uopcode[3];
endfunction

endpackage
'''.lstrip()


def test_udec_reports_espresso_error():
    status = subprocess.run(['xlat', '-i', 'conflict_uops', fixture('conflict.sv.py'), '--stdout'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert status.returncode != 0
    assert re.search('ERROR: minimization failed: espresso: ON-set and OFF-set are not orthogonal',
                     status.stdout.decode())


def test_udec_reports_predicate_which_matches_nothing():
    status = udec.run(fixture('testnomatch.sv.py'), '--stdout',
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert status.returncode != 0
    assert re.search('ERROR: decode function "nomatch" did not match anything', status.stdout.decode())


def test_udec_logic_with_scope_check():
    output = udec.check_output(fixture('testpred2.sv.py'), '--stdout', '--scope-check', 'DV')
    assert output.decode() == '''
package testpred2;

function automatic logic IXUVld(input logic [7:0] uopcode);
  IXUVld = ~uopcode[0] & uopcode[1] & ~uopcode[2] & ~uopcode[3] & uopcode[4] & uopcode[5] & ~uopcode[6] & ~uopcode[7] |
           ~uopcode[1] & uopcode[2] & ~uopcode[3] & ~uopcode[4] & uopcode[5] & uopcode[6] & ~uopcode[7] |
           ~uopcode[0] & uopcode[1] & uopcode[2] & ~uopcode[3] & uopcode[4] & uopcode[5] & uopcode[6] & ~uopcode[7];
endfunction


function automatic logic isPipeA(input logic [7:0] uopcode);
  isPipeA = 1'b1;

`ifdef DV
  if (IXUVld(uopcode) !== 1) isPipeA = 'x;
`endif
endfunction

endpackage
'''.lstrip()
