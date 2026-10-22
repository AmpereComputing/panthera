# AARCHMRS containing the JSON files for A-profile Architecture

## Introduction

This is the Arm Architecture Machine Readable Specification containing
JSON files representing the architecture as a machine readable format.

This package contains a subset of the information provided in the packages
with Arm proprietary licenses. Content that is not currently in a machine-readable
format, as well as all descriptive content, is omitted.

The [notice](docs/notice.html) gives details of the license terms and conditions
under which this package is provided.

## Vendoring in this repository

The vendored input in this repository is Arm AARCHMRS `2026-09_rel` (build
868). To minimize file size , `Instructions.json.gz` is generated with `gzip -n
-9` from the upstream `Instructions.json`.

- Uncompressed `Instructions.json` SHA-256:
  `ef75f3ec02df1b9b9c0612f4ea259d6710ac3de686b097d8f9c83d1d97aa800a`
- Compressed `Instructions.json.gz` SHA-256:
  `6f3f6bac1b577597dbabd4a0e398265139e324336c46d3ddf23a46ccb74ee2d5`

## Package contents

 - `Features.json` contains the architecture feature constraints.
 - `Instructions.json.gz` contains the gzip-compressed A64, A32, and T32
   Instruction Set Architecture. It decompresses to the upstream
   `Instructions.json` file. ADIS reads it directly with Python's standard
   library; builds do not create an untracked decompressed copy.
 - `Registers.json` contains the AArch32, AArch64, and MemoryMapped
   System Registers and System instructions.
 - [`schema`](schema/) contains JSON schema for the data.
 - [`docs`](docs/index.html) contains the rendered view of the schema
   as well as user guides to help understand the data above.

## Package quality

  - The architectural content contained within the data files is maintained
    to the same quality standard as the corresponding architecture releases.
  - The schema is still under development and is subject to change.
