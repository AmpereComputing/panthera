# Tracing Microbenchmarks

A simple microbenchmark tracing script exists for turnkey collection of a trace from a compiled and statically linked Linux ELF with tracing start and stop hints included. In general the usage is:

```
./install/bin/panthera tracing.trace_micro --kernel <path to kernel image> --execute <'path to microbenchmark binary and arguments'> [--environment ENV_VAR1=VALUE1 ENV_VAR2=VALUE2 ...]
```

This script will download an Alpine Linux rootfs, add the binary, construct an init which invokes the binary with the supplied arguments, builds an initrd-compatible ramdisk (obviating the need for root access) and collects a trace of the hint-delinieated regions of the microbenchmark execution.

## End-to-End Simple Microbenchmark Tracing Example

Using the example of `bm_mem_lat` produced by every Panthera build we can demonstrate tracing, packaging, simple verification, and even modification and collection of a new microbenchmark with a step-by-step recipe (Note: commands in this section assume bash-style shell features. If you use a different shell you may need to adapt some of the command lines accordingly):

### If running on an AArch64 machine, use the local machine to inspect the binary

1. Ensure the binary is statically linked. One option is to use `ldd` to inspect the binary for any dynamic linkage. Alternatively `file` will report some information about the file format:

```sh
$ cd [Panthera build output directory]
$ ldd ./src/ampere/benchmarks/bm_mem_lat
        not a dynamic executable
$ file ./src/ampere/benchmarks/bm_mem_lat
./src/ampere/benchmarks/bm_mem_lat: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=2d2d0ba0471c4253f61a68fb1b215e8ecea3a678, for GNU/Linux 3.7.0, with debug_info, not stripped
```

Now that we've confirmed that the binary is suitable we can use `perf stat` to characerize the behavior of the benchmark and tune it to a size range suitable for the workload and microbenchmarking environment. Our chosen benchmark `bm_mem_lat` uses environment variables to determine the offline and online warmup length as well as the benchmark and cooldown iteration counts. We can use these knobs to determine an appropriate number of iterations for microbenchmark tracing. Since the benchmark is written within the gtest framework we can use the command line argument `--gtest_filter` to seelct individual paramters of the test (Note: `perf stat` may not be available on some machines, please try a different machine if you encounter an error regarding access permissions to perf counters):

2. First run with all iterations counts = 0 in order to get a baseline count for untraced instructions:
```
$ perf stat -e instructions:u env BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=0 BENCHMARK_ITERATIONS=0 BENCHMARK_COOLDOWN_ITERATIONS=0 ./src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0
        ...
           1269398      instructions:u  
        ...
```

3. Then determine the incremental instruction counts per iteration by increasing the iteration counts. For example:
```
$ perf stat -e instructions:u env BENCHMARK_ITERATIONS=1000 BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=0 BENCHMARK_COOLDOWN_ITERATIONS=0 ./src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0
        ...
           1380732      instructions:u  
        ...
```
${{1380732-1269398}\over {1000}} = 111 {{instructions} \over {iteration}}$

For a different version of this benchmark we might see a different count:
```
$ perf stat -e instructions:u env BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=0 BENCHMARK_ITERATIONS=0 BENCHMARK_COOLDOWN_ITERATIONS=0 ./src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/18
                ...
         206909622      instructions:u  
                ...
$ perf stat -e instructions:u env BENCHMARK_ITERATIONS=10 BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=0 BENCHMARK_COOLDOWN_ITERATIONS=0 ./src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/18
                ...
         228301309      instructions:u  
                ...
```
${{228301309-206909622} \over {10}} = 2139169 {{instructions} \over {iteration}}$
This version of the test is not a great candidate for microbenchmarking since a single iteration is much larger than our ideal max instruction count

4. Select appropriate iteration counts to achieve reasonable instruction counts for tracing. For microbenchmarking we generally want this to be at least 10000 instructions per region and no more than 100000 total. A script exists in the repo to simplify this process for these env-var based benchmarks. It automatically iterates over each variable independently to achieve target instruction counts and minimum iteration counts for each region:
```
$ ../src/ampere/benchmarks/deduce_environment.sh ./src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0
BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=94 BENCHMARK_ITERATIONS=90 BENCHMARK_COOLDOWN_ITERATIONS=92
```
this script can be used directly with the microbenchmark tracing infrastructure to define appropriate variables for the tracing run automatically

### Trace the benchmark with the microbenchmark tracing infrastructure

The `tracing.trace_micro` python script accepts a path to a binary, arguments for that binary, and environment vars relevant to a given run and produces a trace and optionally an ELF automatically based on that run. The trace requires a suitable kernel image. As determined above we need about 100 iterations of the LatMemRdTests/RandomStrided.Reads/0 test to achieve 10000 iterations, so:

```
$ mkdir bm_mem_lat_trace && cd bm_mem_lat_trace
$ ../../install/bin/panthera tracing.trace_micro --kernel [path to kernel images]/Image-64k-v5.10 --trace-elf --cooldown=3000 --execute "../src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0" --environment BENCHMARK_OFFLINE_WARMUP_ITERATIONS=0 BENCHMARK_ONLINE_WARMUP_ITERATIONS=100 BENCHMARK_ITERATIONS=100 BENCHMARK_COOLDOWN_ITERATIONS=100 --output bm_mem_lat_0
```
Alternatively, we can provide the invocation to the `deduce_environment.sh` script and allow it to automatically set environment variables based on running the binary and counting instructions with `perf`:
```
$ pwd
[...]/panthera/build/bm_mem_lat_trace
$ ../../install/bin/panthera tracing.trace_micro --kernel [path to kernel images]/Image-64k-v5.10 --trace-elf --cooldown=3000 --execute "../src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0" --environment $(../../src/ampere/benchmarks/deduce_environment.sh ../src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/0) --output bm_mem_lat_0_auto
```
Utilizing this script allows us to easily collect a range of traces each with their own automatically determined iteration counts. For example:
```
$ cat ./trace_bm_mem_lat.sh
#!/bin/bash

for t in $(seq 1 17)
do
    BENCHMARK_INVOCATION="../src/ampere/benchmarks/bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/${t}"
    TRACE_NAME=bm_mem_lat_$(printf "%02d" ${t})
    ../../install/bin/panthera tracing.trace_micro --kernel [path to kernel images]/Image-64k-v5.10 --trace-elf --cooldown=3000 --execute "${BENCHMARK_INVOCATION}" --environment $(../../src/ampere/benchmarks/deduce_environment.sh ${BENCHMARK_INVOCATION}) --output ${TRACE_NAME}
done
```
Please note, in the above script relative paths assume the script is run one level below the build directory and some paths are one level up because they reference output within the build directory following a directory structure similar to the repo and others are two levels up because they are referencing the repo itself (`bm_mem_lat` vs `deduce_environment.sh` for example). Running this script in our example output directory yields:
```
$ ./trace_bm_mem_lat.sh
[stdout from several qemu instances tracing the various runs]
$ ls
bm_mem_lat_01_0.bs.xz  bm_mem_lat_04_0.is.xz  bm_mem_lat_08_0.bs.xz  bm_mem_lat_11_0.is.xz  bm_mem_lat_15_0.bs.xz
bm_mem_lat_01_0.elf    bm_mem_lat_04_0.ms.xz  bm_mem_lat_08_0.elf    bm_mem_lat_11_0.ms.xz  bm_mem_lat_15_0.elf
bm_mem_lat_01_0.is.xz  bm_mem_lat_05_0.bs.xz  bm_mem_lat_08_0.is.xz  bm_mem_lat_12_0.bs.xz  bm_mem_lat_15_0.is.xz
bm_mem_lat_01_0.ms.xz  bm_mem_lat_05_0.elf    bm_mem_lat_08_0.ms.xz  bm_mem_lat_12_0.elf    bm_mem_lat_15_0.ms.xz
bm_mem_lat_02_0.bs.xz  bm_mem_lat_05_0.is.xz  bm_mem_lat_09_0.bs.xz  bm_mem_lat_12_0.is.xz  bm_mem_lat_16_0.bs.xz
bm_mem_lat_02_0.elf    bm_mem_lat_05_0.ms.xz  bm_mem_lat_09_0.elf    bm_mem_lat_12_0.ms.xz  bm_mem_lat_16_0.is.xz
bm_mem_lat_02_0.is.xz  bm_mem_lat_06_0.bs.xz  bm_mem_lat_09_0.is.xz  bm_mem_lat_13_0.bs.xz  bm_mem_lat_16_0.ms.xz
bm_mem_lat_02_0.ms.xz  bm_mem_lat_06_0.elf    bm_mem_lat_09_0.ms.xz  bm_mem_lat_13_0.elf    bm_mem_lat_17_0.bs.xz
bm_mem_lat_03_0.bs.xz  bm_mem_lat_06_0.is.xz  bm_mem_lat_10_0.bs.xz  bm_mem_lat_13_0.is.xz  bm_mem_lat_17_0.elf
bm_mem_lat_03_0.elf    bm_mem_lat_06_0.ms.xz  bm_mem_lat_10_0.elf    bm_mem_lat_13_0.ms.xz  bm_mem_lat_17_0.is.xz
bm_mem_lat_03_0.is.xz  bm_mem_lat_07_0.bs.xz  bm_mem_lat_10_0.is.xz  bm_mem_lat_14_0.bs.xz  bm_mem_lat_17_0.ms.xz
bm_mem_lat_03_0.ms.xz  bm_mem_lat_07_0.elf    bm_mem_lat_10_0.ms.xz  bm_mem_lat_14_0.elf    trace_bm_mem_lat.sh
bm_mem_lat_04_0.bs.xz  bm_mem_lat_07_0.is.xz  bm_mem_lat_11_0.bs.xz  bm_mem_lat_14_0.is.xz
bm_mem_lat_04_0.elf    bm_mem_lat_07_0.ms.xz  bm_mem_lat_11_0.elf    bm_mem_lat_14_0.ms.xz
```

### Verifying and Scoring a Trace
Several tools exist within the Panthera repo for inspecting traces:

1. `is_validate` ensures that the trace is well-formed by ensuring all instructions decode, inspecting load and store addresses, i-side and d-side translations, and cache and tlb maintenance instruction contents. Running `is_validate` on a trace without issues will result in no output and a `true` return value to the shell:

        $ ../../install/bin/is_validate ./bm_mem_lat_01_0.is.xz && echo "Validated OK" || echo "Validation Issue Detected"
        Validated OK

2. `is_info` prints summary info that's useful for confirming we've captured the appropriate amount of instructions for our desired test length

        $ ../../install/bin/is_info ./bm_mem_lat_01_0.is.xz
        stream version = 5
        total instruction count = 32993
        (12 warmup, 29981 measurement, and 3000 cooldown)
        ARM SVE instruction count = 0
        memop count = 31905
        memory count = 653
        sysreg count = 34
        context count = 1
        control count = 2
        total event count = 33683
        $ ../../install/bin/is_info ./bm_mem_lat_03_0.is.xz 
        stream version = 5
        total instruction count = 36659
        (12 warmup, 33647 measurement, and 3000 cooldown)
        ARM SVE instruction count = 0
        memop count = 35839
        memory count = 16491
        sysreg count = 34
        context count = 1
        control count = 2
        total event count = 53187

3. `is_footprint` reports statistics based on total unique memory accesses for the duration of the trace:

        $ ../install/bin/is_footprint ./bm_mem_lat_01_0.is.xz
        iside footprint in kB: 1.3125
        dside footprint in kB: 33.125
        dside footprint in kB (Physical): 33.125
        synonyms: 0
        iside 4k pages touched: 6
        dside 4k pages touched: 19
        $ ../../install/bin/is_footprint ./bm_mem_lat_03_0.is.xz 
        iside footprint in kB: 1.3125
        dside footprint in kB: 1025.125
        dside footprint in kB (Physical): 1025.125
        synonyms: 0
        iside 4k pages touched: 6
        dside 4k pages touched: 267

4. `is_working_set` reports the missrate for the main loop of the workload indicating the effective size of the data utilized by the trace:

        $ ../../install/bin/is_working_set --ignore-compulsory --ms ./bm_mem_lat_01_0.ms.xz --is ./bm_mem_lat_01_0.is.xz
        Bytes, I Lookups, I Misses, D Lookups, D Misses, Non-Compulsory D Missrate
        0, 29981, 20, 28999, 12, 0, 
        1024, 29981, 2042, 28999, 28883, 0.9959982, 
        2048, 29981, 2036, 28999, 28875, 0.99572223, 
        4096, 29981, 2036, 28999, 28875, 0.99572223, 
        8192, 29981, 27, 28999, 24739, 0.8530376, 
        16384, 29981, 27, 28999, 24651, 0.85000175, 
        32768, 29981, 27, 28999, 24651, 0.85000175, 
        65536, 29981, 20, 28999, 12, 0 
        131072, 29981, 20, 28999, 12, 0, 
        262144, 29981, 20, 28999, 12, 0, 
        524288, 29981, 20, 28999, 12, 0, 
        1048576, 29981, 20, 28999, 12, 0, 
        2097152, 29981, 20, 28999, 12, 0, 
        4194304, 29981, 20, 28999, 12, 0, 
        8388608, 29981, 20, 28999, 12, 0, 
        16777216, 29981, 20, 28999, 12, 0, 
        33554432, 29981, 20, 28999, 12, 0,
        $ ../../install/bin/is_working_set --ignore-compulsory --ms ./bm_mem_lat_03_0.ms.xz --is ./bm_mem_lat_03_0.is.xz
        Bytes, I Lookups, I Misses, D Lookups, D Misses, Non-Compulsory D Missrate
        0, 33647, 20, 32907, 12, 0, 
        1024, 33647, 2322, 32907, 32837, 0.997872, 
        2048, 33647, 2316, 32907, 32813, 0.99714243, 
        4096, 33647, 2316, 32907, 32797, 0.996656, 
        8192, 33647, 27, 32907, 32797, 0.996656, 
        16384, 33647, 27, 32907, 32797, 0.996656, 
        32768, 33647, 27, 32907, 32797, 0.996656, 
        65536, 33647, 27, 32907, 32797, 0.996656, 
        131072, 33647, 27, 32907, 32797, 0.996656, 
        262144, 33647, 27, 32907, 32797, 0.996656, 
        524288, 33647, 27, 32907, 32797, 0.996656, 
        1048576, 33647, 27, 32907, 31720, 0.96391547, 
        2097152, 33647, 20, 32907, 12, 0,
        4194304, 33647, 20, 32907, 12, 0, 
        8388608, 33647, 20, 32907, 12, 0, 
        16777216, 33647, 20, 32907, 12, 0, 
        33554432, 33647, 20, 32907, 12, 0,

  Pulling out the relevant sections from each:

        32768, 29981, 27, 28999, 24651, 0.85000175, 
        65536, 29981, 20, 28999, 12, 0 

        1048576, 33647, 27, 32907, 31720, 0.96391547, 
        2097152, 33647, 20, 32907, 12, 0,

  we can see that as expected, the larger memory latency tests require larger caches and demonstrate a relatively high missrate until sufficient cache size is achieved at which time the missrate drops off precipitously.

Additionally, a Panthera core model run may be useful to confirm latency-based microbenchmarks
report accurate results. Let's consider the `LatMemRdTests/RandomStrided.Reads/1` which based on our above analysis and expectation of the purported behavior of the benchmark should be an L1-hit load-use latency test for most uarchs:
```
$../../install/bin/panthera sim ../../install/bin/amperesys -cfg ../../install/config/model/sys/siryn.cfg -sys.soc_0.core_0.l2c.warmup_ms ./bm_mem_lat_01_0.ms.xz -sys.soc_0.warmup_ms ./bm_mem_lat_01_0.ms.xz -sys.soc_0.core_0.instruction_stream_name ./bm_mem_lat_01_0.is.xz -sys.soc_0.core_0.gpc.max_instructions 10000000 -sys.soc_0.core_0.gpc.heartbeat 5000

        SystemC 2.3.2-Accellera --- Nov 11 2022 04:01:25
        Copyright (c) 1996-2017 by all Contributors,
        ALL RIGHTS RESERVED
(1ce26ceeadb841cb33d34c5d6bce4f364b11c07c, refs/heads/user/assembly_based_micro_wrapper, DIRTY)
Warmstart: L2C with file ./bm_mem_lat_01_0.ms.xz
Warmstart: L2C completed 1056 transactions
Warmstart: SLC with file ./bm_mem_lat_01_0.ms.xz
Warmstart: SLC completed 1056 transactions
Info: /ampere/panthera: Start performance measurement after 14 instructions @ 4005 cycles, 1,333,998 ps
Heartbeat: Simulate       30,344 cycles,       5,000 instructions,       5,029 uops, IPC = 0.19
Heartbeat: Simulate       49,704 cycles,      10,000 instructions,      10,029 uops, IPC = 0.26
Heartbeat: Simulate       69,024 cycles,      15,000 instructions,      15,029 uops, IPC = 0.26
Heartbeat: Simulate       88,384 cycles,      20,000 instructions,      20,029 uops, IPC = 0.26
Heartbeat: Simulate      107,744 cycles,      25,000 instructions,      25,029 uops, IPC = 0.26
Info: /ampere/panthera: Stop performance measurement after 29934 instructions @ 42,257,034 ps
Info: /OSCI/SystemC: Simulation stopped by user.

Panthera completed : 122893 cycles, 29935 instructions, IPC = 0.244, in 2 sec
 Simulator warmup  :  12.59 kHz,   0.04 kips, 144.71 MB virtmem
 Simulator perfsim :  71.95 kHz,  17.53 kips, 144.84 MB virtmem
```
In order to understand the meaning of the IPC we must know a few things about the test in question:
- the `RandomStrided` versions of the `bm_mem_lat` test traverse a pseudo-randomly shuffled linked-list of entries each taking up one cacheline and of a number of list entries sufficient to achieve a desired total number of cachelines accessed per traversal
- being a linked-list traversal the address of each load is dependent on the value of the previous load in the stream
- the pseudo-random shuffling attempts to confuse any prefetcher by ensuring that no obvious pattern between the addresses of the current entry and the next exist
With this information we can make some assumptions about the performance behavior of the workload that help us evaluate the load-use latency of the system under test, to wit:
- performance is dominated by dependent loads and any other accessory instructions parallel to the dependent load stream have no impact on performance
- the dependent load stream is fully serialized and the time between each individual instruction issuing is the load-use penalty
In this case we can approximate the load-use penalty as:

$latency={{cycles}\over {loads}}$

To determine the total number of dependent load instructions in any one period of execution the `is_viewer` tool is helpful. Considering the sequence of instructions starting at 15000 we see:
```
$ ../../install/bin/is_viewer ./bm_mem_lat_01_0.is.xz | grep "^Instruction: " | head -n 15010 | tail -n 10
Instruction: inum: 15014 pc: 0x4036cc op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a50740 pa[0]: 0x13e540740 bytes=8 
Instruction: inum: 15015 pc: 0x4036d0 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a55ec0 pa[0]: 0x13e545ec0 bytes=8 
Instruction: inum: 15016 pc: 0x4036d4 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a54a00 pa[0]: 0x13e544a00 bytes=8 
Instruction: inum: 15017 pc: 0x4036d8 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a50780 pa[0]: 0x13e540780 bytes=8 
Instruction: inum: 15018 pc: 0x4036dc op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a52e80 pa[0]: 0x13e542e80 bytes=8 
Instruction: inum: 15019 pc: 0x4036e0 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a53400 pa[0]: 0x13e543400 bytes=8 
Instruction: inum: 15020 pc: 0x4036e4 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a57ec0 pa[0]: 0x13e547ec0 bytes=8 
Instruction: inum: 15021 pc: 0x4036e8 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a56b00 pa[0]: 0x13e546b00 bytes=8 
Instruction: inum: 15022 pc: 0x4036ec op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a51340 pa[0]: 0x13e541340 bytes=8 
Instruction: inum: 15023 pc: 0x4036f0 op: 0xf9400042 dis: `ldr   x2, [x2]` va[0]: 0x34a50a40 pa[0]: 0x13e540a40 bytes=8
```
Let's assume that all dependent loads within the region starting at 15,000 instructions and continuing to 20,000 instructions have the same encoding: `op: 0xf9400042`, then:

```
$ ../../install/bin/is_viewer ./bm_mem_lat_01_0.is.xz | grep "^Instruction: " | head -n 20000 | tail -n 5000 | grep -c "op: 0xf9400042"
4840
```

From the heartbeat stats we see that this region occupies $88384-69024=19360$ cycles. Using our equation above we expect an L1-hit load-use penalty of $19360/4840 = 4$ cycles on this uarch.

## Modify an Existing Benchmark to Create a New Microbenchmark for Tracing

If we wish to add a new benchmark that is similar to an existing benchmark it may be possible to leverage the existing code to simplify the process. For example, starting with lat_mem_rd we can easily create an equivalent equivalent which utilizes vector load instructions instead of standard gpr loads:

1. Copy the relevent code to a new file within the repo: `cp src/ampere/benchmarks/test_mem_lat.cc src/ampere/benchmarks/test_mem_lat_vec.cc`
2. We are unlikely to use the "Strided" versions of this test so remove that section and replace the remaining classes with appropriate names referencing Vector loads
3. Modify the relevant line within the existing test that performs the dependent load to utilize a specific instruction sequence using inline assembly:
```
BOOST_PP_REPEAT(100, one_load, p = (char**) *p);
```
becomes
```
BOOST_PP_REPEAT(100, one_load, asm ("ld1 {v0.1D}, [%[ptr]]\nmov %[ptr], v0.D[0]" : [ptr] "+r" (p)  : : "v0"));
```
4. Edit `src/ampere/benchmarks/CMakeLists.txt` to add an entry for the new benchmark; follow the existing executable target for `bm_mem_lat` as an example but place it in the AArch64-specific portion of the build rules since we are now dependeing upon a particular instruction
5. Build, test, and trace the new benchmark following the above procedure

## Creating a New Microbenchmark from Scratch

For a new assembly-based benchmark there is a new gtest-based micro wrapper class defined in `src/ampere/benchmarks/micro.h`. Include this and instantiate a test after the example in `src/ampere/benchmarks/test_stld_lat.cc` or others that include micro.h.
