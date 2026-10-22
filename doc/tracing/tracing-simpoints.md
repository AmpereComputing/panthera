# Panthera Simpoint Tracing

The tracing tools in this directory allow for the collection and validation of
simpoint-based instruction streams by running the workload in QEMU.

## Simpoint Process Overview

1. Capture basic block vectors from a single-threaded run on QEMU using a BBV
   plugin, taking occasional checkpoints throughout the run
2. Run BBVs through simpoint tool to identify intervals of interest and their
   weights
3. Restore from the nearest checkpoint preceding each identified interval, run
   until the interval is reached, then save the instruction stream for that
   interval
4. Validate that the BBV for the captured instruction stream matches (is very
   close to) that interval from the original full BBVs (and potentially other
   validation)
5. Run instruction streams through simulator, weight the captured IPC, multiply
   by total instruction count to get how long this benchmark would've taken to
   run (same weighting can be applied to other statistics, too)

## Capturing Traces

### QEMU/Workload Setup

To use these tools, you will need:

* A Linux kernel `Image` with perf-semihosting support. Apply Panthera's
  [Linux 5.10 perf-semihosting patch](perf-semihosting-5.10.patch) from the
  kernel source root with `git apply /path/to/panthera/doc/tracing/perf-semihosting-5.10.patch`.
  The patch applies cleanly to Linux commit
  `2c85ebc57b3e1817b6ce1a6b703928e113a90442`; compile with
  `CONFIG_ARM64_PERF_SEMIHOSTING=y` and
  `CONFIG_PID_IN_CONTEXTIDR=y`.
* A
  [qcow2](https://qemu.weilnetz.de/doc/qemu-doc.html#disk_005fimages_005fformats)
  root filesystem containing the workload you want to run and scripts to run
  it. There's an example script for creating an [Alpine
  Linux](https://alpinelinux.org/)-based image at
  [tracing_tools/examples/build_qcow_image.sh](./examples/build_qcow_image.sh)
* Last but not least, a built and tested panthera checkout

The supported way to provide these tools is Panthera's [Nix development
environment](../UserGuide.md#nix-setup). From the repository root, enter it
with `nix-shell` (or enable it with `direnv allow`); it puts the required
plugin-enabled QEMU and SimPoint on your `$PATH`.

If you are not using the Nix environment, the following tools are also
required:

* A `qemu-system-aarch64` binary compiled from [our plugin-enabled
  QEMU](https://github.com/AmpereComputing/tracing-qemu)
  and on your `$PATH`
* A `simpoint` tool binary compiled from [the SimPoint repository on
  GitHub](https://github.com/AmpereComputing/simpoint) and on your
  `$PATH`

### Gathering Basic Block Vectors

```
kernel_dir=./linux
rootfs_qcow=./rootfs.qcow
panthera_build=./panthera/cmake-build-dir

qemu-system-aarch64 \
    -smp 1 \
    -m 4G \
    -M virt \
    -cpu cortex-a57 \
    -kernel $kernel_dir/arch/arm64/boot/Image \
    -append "console=ttyAMA0 root=/dev/vda rw perf.semihosting init=/bin/sh earlycon=smh" \
    -drive if=none,file=$rootfs_qcow,id=hd0,format=qcow2 \
    -device virtio-blk-pci,drive=hd0 \
    -nographic \
    -semihosting \
    -icount 0 \
    -plugin $panthera_build/src/ampere/tracing_tools/libbbv_generator.so,arg="--profile-comm perlbench_r_base.ampere-gcc-7.3.0.golden --bbv-interval 100000000 --checkpoint-interval 1000000000 --bbv-filename 500.perlbench-diffmail.bbv --generation-log 500.perlbench-diffmail_bbv.json" \
    -d plugin \
    -D bbv_generator_500.perlbench_diffmail.log
```

A quick breakdown of the interesting options here:
* `-append` is the kernel command-line.
  * `perf.semihosting` is required to allow the plugin to be notified of kernel
    events (when processes are forked, exec'd and exited, what their command
    names are)
  * You can modify `init=` to point to any scripts you created in your rootfs to
    non-interactively launch your workload.
  * `earlycon=smh` enables the semihosting early console, to enable debug
    messages to be printed out before the actual console (`console=ttyAMA0`) is
    enabled
* `-semihosting` enables semihosting support in QEMU, allowing the plugin to
  receive the events emitted by the kernel.
* `-icount` tells QEMU to use the guest instruction count for the guest's
  clock(s), instead of the host's clock. This makes execution (mostly?)
  deterministic, which is important since we rely upon execution to be the same
  between the original BBV-collection run and when we later collect the
  instruction streams after restoring from checkpoints. The argument is a
  scaling factor between the instruction count and clock.
* `-d plugin` specifies to log output from plugins
* `-D` specifies where to log debug and error messages (companion to `-d plugin`)
* `-plugin` specifies the path to the plugin and its arguments. The example
  command-line above will save BBVs from the first PID it finds with a
  command-name of "perlbench_r_base.ampere-gcc-7.3.0.golden", with 100M
  instruction intervals, taking checkpoints every 1B instructions to
  500.perlbench-diffmail.bbv, saving metadata from the generation run to
  500.perlbench-diffmail_bbv.json. Additional usage information from the plugin
  follows:
```
  --bbv-filename FILE
        Path to where the basic block vectors should be stored
        (default: vectors.bbv)
  --generation-log JSON_FILE
        Path to write the BBV generation metadata to
        (default: bbv_generation.json)
  --bbv-interval IVAL
        Number of instructions to include in each interval
        (default 100000000)
  --profile-el1
        Profile EL1 in addition to EL0 (default: false)
  --profile-comm COMM
        Restrict BBV profiling to the first PID found with this
        command name
  --profile-pid PID
        Restrict BBV profiling to the given PID
  --checkpoint-interval IVAL
        Number of instructions to wait between checkpoints
        (default is 0, disabling this feature)
```

Note that during this process, checkpoints are saved *inside the qcow rootfs
image* - this means that one qcow rootfs image should not be re-used for
multiple BBV-collection runs! Otherwise, it is possible (likely) that
checkpoints will be overwritten. I've been keeping a `chmod a-w` copy of my qcow
image for a particular workload, and making a copy of it for each instruction
stream-collection run.

The above example QEMU invocation will create:

| File                                     | Description |
| ---------------------------------------- | ----------- |
| 500.perlbench-diffmail.bbv               | The BBV of the EL0 instructions in the benchmark's PID |
| 500.perlbench-diffmail_bbv.json          | Metadata about the run including total instruction count, which checkpoints were taken when, and their execution context |
| bbv_generator_500.perlbench_diffmail.log | A log containing debugging output from the plugin |

### Capturing Instruction Traces

There is one script which handles the next few steps:
[tracing_tools/scripts/capture_traces.py](./scripts/capture_traces.py). This
script runs the `simpoint` tool on the .bbv file captured above to identify the
intervals of interest and their weights, invokes QEMU to restore from the
closest checkpoint, run to the beginning of the interval, and collect an
instruction trace, and finally performs some validation on that trace.

```
usage: capture_traces.py [-h] --generation-log BBV_GEN_LOG --qemu-command
                         QEMU_CMD --plugin PLUGIN --trace-base TRACE_BASE

Run the simpoint tool and capture instruction traces for identified intervals.

optional arguments:
  -h, --help            show this help message and exit
  --generation-log BBV_GEN_LOG
                        The BBV generation JSON metadata generated by the QEMU
                        plugin
  --qemu-command QEMU_CMD
                        The QEMU command-line with arguments. This will be
                        amended with the appropriate commands to capture the
                        instruction stream.
  --plugin PLUGIN       The QEMU instruction stream plugin .so file
  --trace-base TRACE_BASE
                        The base filename for the instruction traces. For
                        instance, if you want the trace for the 5th interval
                        to be saved to "/path/to/benchmark.5.is.xz", supply
                        "/path/to/benchmark" as this parameter.
  --warmup-instructions WARMUP_INSNS
                        The number of instructions to collect before the
                        beginning of the specified interval for uArch warmup.
```

Here's an example invocation of that script, using the output from the previous
QEMU bbv-collection plugin:
```
kernel_dir=./linux
rootfs_qcow=./rootfs.qcow
panthera_build=./panthera/cmake-build-dir
trace_basename=./500.perlbench-diffmail

$panthera_build/src/ampere/tracing_tools/scripts/capture_traces.py \
    --generation-log 500.perlbench-diffmail_bbv.json \
    --qemu-command "qemu-system-aarch64 -smp 1 -m 4G -M virt -cpu cortex-a57 -kernel $kernel_dir/arch/arm64/boot/Image -append \"console=ttyAMA0 root=/dev/vda rw perf.semihosting init=/bin/sh earlycon=smh\" -drive if=none,file=$rootfs_qcow,id=hd0,format=qcow2 -device virtio-blk-pci,drive=hd0 -nographic -semihosting -icount 0 -singlestep" \
    --plugin $panthera_build/src/ampere/tracing_tools/libinstruction_tracer.so \
    --trace-base $trace_basename
```

* `--generation-log` is the unmodified file from the same-named option from
  the `libbbv_generator.so` QEMU plugin above.
* `--qemu-command` is the exact full QEMU command-line from the BBV-collection
  run, with only the `--plugin` and `-D` options removed and `-singlestep`
  added. `-singlestep` ensures (among other things) that register values are
  written between every instruction. If this is not supplied, captured source
  register values may be incorrect. `./capture_traces.py` will amend the string
  passed to this option with the options needed to load the appropriate
  checkpoint from the qcow2 image and invoke the instruction-tracing QEMU
  plugin to capture the instruction streams from the appropriate locations in
  the workload's execution.
* `--trace-base` will be used as the basis for the filenames of byproducts of
  this script, most importantly the instruction streams - they will be named
  in the format ("%s.%d.is.xz", trace_base, interval_number)

Assuming two simpoints are identified, intervals 7 and 95, the above invocation
of `./capture_traces.py` will create the following files:

| File                                       | Description |
| ------------------------------------------ | ----------- |
| 500.perlbench_r_diffmail.std.bbv           | The 'standardized' BBVs (indices instead of PC VA's) fed into the simpoint tool |
| 500.perlbench_r_diffmail.std.simpoints     | List of the intervals identified as simpoints | 
| 500.perlbench_r_diffmail.std.weights       | Simpoint weights, indexed by the same IDs as the .simpoints file |
| 500.perlbench_r_diffmail.std.labels        | A list of every interval in the full benchmark, along with which simpoint represents it and the vector distance to it |
| 500.perlbench_r_diffmail.std.simpoints.log | Log from running the simpoint tool |
| 500.perlbench-diffmail.7.is.xz             | The instruction stream for simpoint interval 7 |
| 500.perlbench-diffmail.95.is.xz            | The instruction stream for simpoint interval 95 |
| 500.perlbench-diffmail.7.is.bbv            | BBV generated from 500.perlbench-diffmail.7.is.xz for validation | 
| 500.perlbench-diffmail.95.is.bbv           | BBV generated from 500.perlbench-diffmail.95.is.xz for validation |
| manifest.json                              | Contains which intervals were identified, their validation results and weights, and which instruction streams they correspond to |

### Validation

Each manifest.json file will contain one "weighted_euclidean_distance" element
containing a floating point number and one "weighted_manhattan_distance". The
best-case scenario is that these distances are 0. But this will almost certainly
never be the case since the original BBV contains only EL0 instructions from the
benchmark process, while the BBV captured from the instruction stream contains
all instructions executed during that time period on QEMU.

The maximum value for weighted_manhattan_distance is `2` and
weighted_euclidean_distance's is `sqrt(2)`.

As a rough approximation, if a workload's weighted_manhattan_distance is 0.2 and
its weighted_euclidean_distance is around 0.14, somewhere around 10% of the
instructions in an interval's collected instruction stream differ from that
interval's original BBV-collection run on QEMU. It's not entirely clear what
distance are acceptable, but for initial SPEC2017int train generation, both
weighted_manhattan_distance and weighted_euclidean_distance were rarely above
0.01 and never above 0.05. Based on my experience, these distances are low
enough to not bat an eye about, particularly when taking into account that some
of the difference is almost certainly 'normal' kernel instructions.
