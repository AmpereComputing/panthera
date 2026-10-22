## Instruction Decode

This section is WIP. The text below is just a dump from an email I sent about xlat files.

### XLAT Files

An `.xlat` file contains a series of instruction translations that look like the following:

```python
class ADC_32_addsub_carry(Translation):
    """Add with Carry"""
    pattern = '0  0  0 11010000 ----- 000000 ----- -----'
    pfields = 'sf op S           Rm           Rn    Rd  '
    
    asmtemplate = 'ADC  <Wd>, <Wn>, <Wm>'
    
    def decode(self):
        return NotImplemented
```

A little about the anatomy of this translation. It has a name matching its Arm encoding.

```python
class ADC_32_addsub_carry(Translation):
```

This is a breadcrumb so that I can find out if all encodings in the spec have been covered and if their patterns match,
and so forth. It seemed to be the one string that was unique enough for this purpose. Eventually, it shouldn't be
necessary to have a 1:1 mapping from ARM encodings to Translations, e.g., I can easily imagine something like this
going forward:

```python
class ADC(Translation):
    """Add with Carry"""
    encodings = {'ADC_32_addsub_carry', 'ADC_64_addsub_carry', 'ADCS_32_addsub_carry', ...}
```

but it's not there today.

`pattern` and `pfields` are the instruction pattern and fields to be extracted from the pattern (I guess the "p" in
`pfields` stands for "pattern"; really I just wanted two words the same length). How they line up visually is how they
get extracted. It's also possible to specify fields "manually" with the dictionary attribute `fields`:

    fields = { 'sf': (31,31), 'Rd': (4,0), ...}

I just thought it was more human friendly the first way, but you may disagree.

`asmtemplate` is mainly just an example of an instruction level attribute. No one is using it yet, though I suspect the
perf model will eventually want it for disassembly. More attributes will need to be added to generate instruction level
signals.

Now for the fun part. The `decode` function is where all the action is. For many translations it will be pretty
trivial, as it is in this example:

    def decode(self):
        adc_nzcv(Rd, Rn, Rm)

The opcode name comes directly from `uops.yaml` (or whatever replaces it) with the "."s converted into "_"s (can we
just make this change, actually?). The register names might look like fields names from the instruction, but they
aren't exactly&mdash;they are function names defined in the xlat package which turn around and set register signals
based on the values of those self-same fields. I'm still working on how exactly the field names should be exposed to
the decode function. Right now you can get at them with `self.fields[<field name>]`.

The decode function is side effectful. It will produce uops as the function executes in the order that they are
encountered. That means that these two examples produce one uop and eight uops, respectively:

    def decode(self):
        if self.fields['S']:
            some_uop(Rd, Rm, Rn)
        else:
            some_other_uop(Rd, Rm, Rn)

    def decode(self):
        [repeat_me(Rd)] * 8
