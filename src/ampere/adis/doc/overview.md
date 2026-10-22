# ADIS overview

ADIS is an AArch64 decoder and disassembler. It generates a static C++
library from the vendored AARCHMRS machine-readable instruction data.

## Decoder interface

```C++
#include "adis.h"

adis::encoding_enum_t encoding = adis::decode(opcode);
```

The returned encoding identifies a 32-bit instruction and is the input to
the property, field-extraction, disassembly, and branch-target APIs. See
`src/adis.h` and `src/adis-properties.h` for the public interfaces.

## Build flow

`genadis.py` reads `data/aarchmrs-2026-09/Instructions.json.gz` and emits the
decoder, encoding-property, field-extraction, and disassembly tables in the
build directory. CMake invokes the generator automatically when the input
data or generator changes.

The generator intentionally implements a documented subset of the source
encodings. `data/aarchmrs-2026-09/implemented-encodings.json` records that
boundary; source encodings outside it remain unimplemented until they are
explicitly supported.

## Source layout

- `pysrc/` contains the generator and its JSON reader.
- `data/` contains the vendored instruction data and its license notice.
- `src/` contains the C++ runtime used by the generated library and command
  line disassembler tools.
- `pyext/` provides the optional Python extension.

## Generated files

The build directory receives the decoder state table, encoding enumeration,
static encoding properties, field extractors, operand patterns, and
disassembly tables. They are generated artifacts and are not checked in.
