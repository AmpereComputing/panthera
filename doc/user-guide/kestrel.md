# Kestrel Directory

This directory is consistent of the aice simulator along with the mesh model. 
The test directory right now only contains tests for the mesh.

## Panthera AICE pipeline simulator

This is a pipeline simulator that consumes compatible protobuf traces.

The trace producer and this project must use a common protobuf specification
file.

### How to run traces 

From the Panthera repository root, provide a compatible `.trc` trace file and
run:

```
./src/ampere/kestrel/aice-sim/test/RUN.AICE ./install /path/to/trace.trc
```
The script stores output files in the repository's `output/` directory.

If the script runs successfully, it echos the command to view the event stream file created. Something like the following:
```
${install}/bin/panthera catscan ${es} @${install}/config/events.tracing/aice-events.cfg
```

### Adding new instructions to the protobuf

Add the instructions to `ilist.txt` and then run `gen_inst.py ilist.txt`. 
This script writes `opcode.proto` which you can then
manually replace the opcodes in `aice_stream.proto` file in this
project and the trace producer's version of that same file.

The script also writes the `aice_opcode_enum.h` file.

Both updated files should be `git add`-ed to the repository after being updated.
