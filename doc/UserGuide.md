# Panthera User Guide
Panthera is the performance simulator for Ampere Computing SoC products.

## How to Develop

### Downloading the Source Code
Clone the public Panthera repository using the URL provided by its source-hosting
service.

## How to Build

### Nix Setup

Panthera uses Nix to provide a consistent build environment. Enter the
repository's Nix shell before configuring if Nix is available on your system.

#### Build from the Command Line
Follow these steps to build using the command line

```bash
direnv allow    # Allow nix-direnv to run on this dir, only needed once per checkout
mkdir build     # create the build directory
cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -S .. # run cmake to create build files
ninja install   # build and install all targets
ctest -j20      # run all of the unit tests
```

Note that you may use `-D CMAKE_BUILD_TYPE=Release` instead of `Debug` to omit assertions and improve compiler optimizations (at the cost of making it more difficult to [debug](user-guide/debugging.md)).

#### Build using Top-level Makefile

```bash
make install-debug # build and install all targets
make test-debug    # run all of the tests
```

Note that you may use `make install` instead of `make install-debug` to omit assertions and improve compiler optimizations (at the cost of making it more difficult to [debug](user-guide/debugging.md)). In general, `make <target>-<type>` will run `cmake` with the specified build type (i.e. `release`, `debug, `relwithdebinfo`).

For other make targets, you can use `make help`.

### Install Directory

After completing the build above, all targets will be built and installed them into a directory named `install`.
This install directory contains all of the files needed to run simulations, rollup data and generate reports.


## How to Run Tests

Unit-tests *only* can be run with:

    make unit-test

Similarly, integration tests can be run with:

    make integration

You can run specific C++ tests by name with:

    make test TEST=<name>

Which also works for the `unit-test` and `integration` targets. Using `TEST`
will limit the compilation target to only the specified test (which is why it
only works with C++ tests).

For non-C++ tests (e.g. panthera, python tests), you can do:

    make test TESTS=<name or pattern>

In general, `TESTS` is a regex pattern which can be used to select any tests.

Additionally, if you want to see the full command used for running a particular test, you can specify:

    make test ... CTEST_ARGS=-VV


## Where to Find Traces

Panthera is a trace-driven simulator. This means the primary input of one simulation is a trace containing a time-ordered list of the instructions making up the dynamic execution of the captured portion of a workload. These traces sometimes contain a number of instructions to be executed prior to recording statistics to allow internal structures to reach a steady state, called online warmup. Some traces also have associated offline warmup materials (memory streams or branch streams), which allow the simulator to more quickly warm up its larger structures. We group traces into trace lists, which are then provided to our simulation infrastructure when running large numbers of traces. A trace list consists of a JSON array of traces, including their names, weights, which workload they belong to, the path to their instruction stream and warmup collateral, along with other metadata.

### "Perf Check 500" Traces

We publicly published some of our traces along with this model. The most accessible group of these is called the Perf Check 500, which is available for download at [https://doi.org/10.5281/zenodo.23426212](https://doi.org/10.5281/zenodo.23426212). These 500 traces were a key part of our performance verification story, as described in depth by our recent MICRO 2026 paper, [Performance Verification of the AmpereOne CPU Core](https://doi.org/10.48550/arXiv.2608.19300).

## How to Run Simulations

The Panthera install area contains a script called *panthera* (`install/bin/panthera`) that can be used to run many different tools in the Panthera system.

The following sections will describe how to use various panthera tools to run simulations, debug simulations, rollup results, and generate reports.

### Run One Simulation

The Panthera sim tool can be used to run a single simulation. The following command line will print the help message for the sim tool.

    install/bin/panthera sim --help

This tool will generally be used when debugging failure or the performance of a specific trace.

#### Run a Core Simulation

To run a core simulation you can use the `sim` command as described above and provide the required parameters including:

* Simulator executable
* Configuration file
* Instruction stream name

Here is an example command line that runs the core simulation with the Siryn
configuration using a dhrystone instruction stream:

    ./install/bin/panthera sim install/bin/amperesys -cfg install/config/model/sys/siryn.cfg -sys.soc_0.core_0.instruction_stream_name src/ampere/core/test/dhrystone.is.xz

An event stream file (i.e. `*.es`), which records important events during
simulation, is not created by default.
However, one can be generated by adding the `--es` argument to generate one.
The event stream is a binary file which can be processed by several tools.

#### Checking for Drift

If you are doing refactoring or any other change where little to no change in
IPC is expected, you can use the following to rollup drift introduced in your
change:

    make check-drift DRIFT_TLIST=/path/to/workload-manifest.json

`DRIFT_TLIST` must name a Cheetah workload manifest accessible to the machine
and any batch workers that run the simulations. This builds Panthera using the
latest common ancestor shared by your local branch and `origin/main`, then runs
both versions over that workload list. You can additionally specify other
specifications which run more or fewer configurations, like:

    make check-drift DRIFT_TLIST=/path/to/workload-manifest.json DRIFT_SPEC=all_configs

This can be a full path to a cheetah JSON specification or one of the ones
matching `src/ampere/regress/drift_specifications/*.json`.

Similarly it will do the same thing to your tree and then
compare. If you want to compare *all* metrics, you can run:

    make compare-drift-all

After the original command completes.

#### View Event Streams

An event stream can be processed and viewed using several different tools:

* `install/bin/es_viewer` - text dump of event stream contents
* `install/bin/panthera events.tracing` - convert to JSON for viewing in Chrome ([events-tracing](analysis/events-tracing.md))
* `install/bin/panthera catscan` - explore with a custom text-based (vim-like) interface ([catscan](analysis/catscan.md))

### Running Many Simulations

A performance study runs many simulations with 1 or more simulation configurations across multiple workloads.
We have developed a tool called to Cheetah to run performance simulations.
See the [Cheetah](user-guide/cheetah.md) documentation for information about how to use `panthera cheetah` to run simulations.

### Analyzing Results

#### Checking for Simulation Failures

After running the simulations you need to check to see if some of the simulations failed or did not complete.
There is a cheetah sub-command to generate a report of the failed simulations, for example:

    ./install/bin/panthera cheetah.errors --study <study_directory>/study.json --summary

Additionally, you can generate an XML report with:

    ./install/bin/panthera cheetah.xml_report study/report.xml

When running jobs with slurm an additional log file with extension `.slurm.log` is created for each trace.
These logs can have details on job status.  For instance, it will show

* When a job failed because of failures not related to panthera.  Such as NFS, compute machine failure, etc.
* When a job uses more memory than allocated.  (These can be adjusted with `--slurm-memory-*` options, use `--help` to see a description of options.)

#### Generate Simulation Rollup

If there were no simulations that failed then you can rollup the simulation results.
The `panthera rollup` tool will read the simulation results and create [Tidy Data](https://vita.had.co.nz/papers/tidy-data.pdf) CSV files.

By default three files will be generated in the study directory (see [rollup](analysis/rollup.md) for more info):

* `rollup.by_trace.csv`: trace metrics (unweighted), including formulas and metadata from study-list
* `rollup.by_workload.csv`: workload metrics, from weighted arithmetic mean of traces
* `rollup.by_suite.csv`: suites metrics, aggregated by geometric/arithmetic mean, from workload metrics

Which are generated from running the following:

    install/bin/panthera rollup study/study.json --formula install/config/rollup/rollup_formula.py

See the [rollup](analysis/rollup.md) documentation for information about using the rollup tool.

These rollups can be further analyzed with other tools, such as [Jupyter](./analysis/jupyter.md).
However, it's important to use `rollup.by_workload.csv` in **most** cases, especially when taking
an overall aggregate for each configuration. `rollup.by_suite.csv` can be used for suite-level
analysis, but not overall aggregation. Finally, `rollup.by_trace.csv` can be used for debugging or
for finding specific traces to look into.

#### Generate Reports

After the `rollup.csv` has been created you can create a variety of reports from the data rollup.
You can see what reports are available by running the command with the `--help` option.

    install/bin/panthera report --help

You can generate a report that summarizes the IPC for the study using the **summary** report.

    install/bin/panthera report --report summary  study/rollup.by_workload.csv

See the [report](analysis/report.md) documentation for information about using the report tool.

## Debugging Tips and Tricks

The [debugging documentation](user-guide/debugging.md) contains ideas and strategies for debugging the panthera model.

## Tracing with QEMU

This repository contains several QEMU plugins which can be used to collect instruction streams from workloads running inside QEMU.

1. For simple workloads, the ["simple" tracing plugin](tracing/tracing-simple.md) allows you to capture instruction traces from processes based on certain triggers (instruction count, PC, `HINT`, or semihosting).
   Features include the ability to limit profiling to a process or process tree, or capture periodic instruction traces.
   This plugin also has helper scripts for [collecting traces of microbenchmarks](micros/tracing-microbenchmarks.md).
2. For single-threaded, fixed-work applications (i.e. SPEC), we have a [simpoint-based tracing methodology](tracing/tracing-simpoints.md), which aims to capture the most representative intervals for applications which meet its assumptions.

## Writing Micro-benchmarks

The [benchmark guide](micros/benchmarks.md) has information about how to write a benchmark program for measuring the performance of specific areas of the core micro architecture.
