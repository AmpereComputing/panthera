# Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

from panthera.decoder import CreateDecoderClass, create_decoder

parser = argparse.ArgumentParser(description="Build a decoder for a given CPU")
parser.add_argument("--cpu", help="CPU name", required=True)
parser.add_argument("--fusion", action="store_true", help="Generate fusion patterns/decode", default=False)
parser.add_argument("--split-decoder", type=int, help="Number of decoder files to generate", default=1)
parser.add_argument("inputs", nargs="+", help="XLAT input files")
args = parser.parse_args()

xlat_globals = {}
for input_file in args.inputs:
    exec(compile(open(input_file, encoding="utf-8").read(), input_file, mode="exec"), xlat_globals)

create_decoder_class = CreateDecoderClass(args.cpu, args.fusion, args.split_decoder)
create_decoder(create_decoder_class)
create_decoder_class.generate_outputs()
