# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from panthera.encoder import CreateEncoderClass, create_encoder

parser = argparse.ArgumentParser(description="Build an encoder for a given CPU")
parser.add_argument("--cpu", help="CPU name", required=True)
parser.add_argument("inputs", nargs="+", help="XLAT input files")
args = parser.parse_args()

xlat_globals = {}

for input_file in args.inputs:
    exec(compile(open(input_file).read(), input_file, mode="exec"), xlat_globals)

create_encoder_class = CreateEncoderClass(args.cpu)
create_encoder(create_encoder_class)
create_encoder_class.generate_outputs()
