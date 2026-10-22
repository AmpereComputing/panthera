# xlat

xlat is a Python package and command line tool set relating to the *translation* and *decode* of instructions and
uops. At a high level, xlat will read a set of input files (which are themselves Python) and
generate outputs in an alternate language (e.g. SystemVerilog) for decoding instructions and uops.

## The `xlat` program

The primary user interface for the xlat package is the `xlat` command line tool. `xlat` is invoked as follows:

```shell
xlat [<options>...] [<xlat files>...] [<input files>...]
```

#### Options

<table>
<tr>
<td>-h | --help</td><td>Print a usage message</td>
</tr>
<tr>
<td>-o | --output &lt;FILE&gt;</td><td>Send all output to a file</td>
</tr>
<tr>
<td>--stdout</td><td>Send all output to standard out</td>
</tr>
<tr>
<td>-i | --import &lt;MODULE&gt;</td><td>Auxiliary module to import</td>
<tr>
<td>--debug</td><td>Run in debug mode, producing more verbose errors</td>
</tr>
</tr>
</table>

#### File Arguments

File arguments passed to `xlat` are compiled and executed as Python. Instruction translation files
typically end in the extension ".xlat" but are in fact ordinary Python.
 
For any input file that contains generator functions (see [Uop Decode](udec/index.html)), and if not given either
of `--stdout` or `--output`, `xlat` will produce an output file named the same as input file with the `.py` suffix
removed (if it exists), or with an `.sv` suffix added (if the `.py` suffix does not exist on the input).

For example: 

```shell
xlat integer.xlat float.xlat integer_decoder.sv.py float_decoder.sv.py
```

will produce `integer_decoder.sv` and `float_decoder.sv`, assuming those files contain generator functions.

It is not necessary for an input file to produce output. For example, in the case of a checker:

```shell
xlat integer.xlat float.xlat validate_translations.py
```

#### Auxiliary Modules

Auxiliary modules, specified with `-i`, must be valid Python module names. They will be _imported_ in a manner
similar to normal Python modules, and therefore must be discoverable from the current `PYTHONPATH`. They also have
the option of registering an initialization command and adding new command line arguments.
