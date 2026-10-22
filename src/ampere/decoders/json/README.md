# AArch64 JSON metadata base

`a64.xlat` is generated from Arm's BSD-licensed AARCHMRS A-profile
`Instructions.json`. It contains only Arm-derived instruction metadata. Model
overlays subclass its `_JSON_*` classes to provide Ampere's uop and decoder
behaviour.

Do not edit `a64.xlat` by hand. To regenerate it from a matching AARCHMRS
release, run from the repository root:

```shell
PYTHONPATH=src/ampere/python/xlat/src \
python3 src/ampere/python/xlat/bin/json2xlat \
  --metadata-only --class-prefix _JSON_ \
  --output src/ampere/decoders/json <AARCHMRS-directory>
```

New encodings remain explicitly unimplemented until a model overlay supplies a
decoder.
