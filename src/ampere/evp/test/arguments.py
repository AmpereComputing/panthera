# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import argparse

parser = argparse.ArgumentParser()
parser.add_argument("location")
parser.add_argument("--time")
parser.add_argument("--hello", action="store_true")
args = parser.parse_args(evp_args)

greeting = "Goodbye"
if args.hello:
    greeting = "Hello"

print(f"{greeting}, {args.location}! It is {args.time}")
