# Micro Benchmarks

The [src/ampere/benchmarks](/src/ampere/benchmarks) directory contains micro
benchmarks, very small programs that exercise specific parts of a CPU micro
architecture.

The micro benchmark programs use the [Google test](https://github.com/google/googletest)
framework for running and instrumenting tests.

These can be written in either assembly (`*.s` or `*.S`), C, or C++.

## Examples

### Vector instruction test

The `vector_instructions `executable contains bunch of micro benchmarks
that are intended to measure basic vector instruction performance.

To run all of the test just run the program with no arguments.

    ./vector_instructions

This will generate a CSV file named **benchmark.csv** which contains
run times and other information for every benchmark.

All of the tests implement a simple operation on fixed length
vectors. The full set of tests is the cross-product of three
dimensions: operation, vector length, and data type.

The set of operations are:

* Add
* Multiply
* Multiply and Add

The set of vector lengths are:

* 1024
* 10,240
* 102,400
* 1,024,000
* 10,240,000

The set of types are:

* `char`
* `int`
* `long long`
* `float`
* `double`

The benchmarks are implemented using the Google Test
infrastructure. One of the good things about this infrastructure is
that it provides a method for selecting which tests to run via the
command line.

For example to run only the test with vector length of 10,240 run the
program like this:

    ./vector_instructions --gtest_filter=VectorTest10K*

## Creating an instruction stream snippet

Instructions on how to create a small instruction stream of hand-coded
assembly, which is useful to validate changes:

All of the stuff is in the panthera repo under `src/ampere/benchmarks`. I would follow the example for `independent_fmuls`. 

* `independent_fmuls.s` - assembly code for the micro
* `test_alu.cc` - googletest file that is used for instantiating and running the tests, add an extern definition here and create a new test that calls your assembly routine
* `CMakeLists.txt` - add any new assembly file to the `add_executable()` line here

Just build panthera as usual, this will generate: `cmake-build-release/src/ampere/benchmarks/bm_alu`
 
You can use this to run your test, you can use `--gtest_filter=LatALU.DependentFmuls` to run a single test.

For tracing, use a supported AArch64 host with `sudo` access and enter
Panthera's Nix development environment (or another environment that provides
the required tools). Here is how to trace:

    ./install/bin/panthera tracing.create_image cmake-build-release/src/ampere/benchmarks/bm_alu
    ./install/bin/panthera tracing.trace -o dependent_fmuls -- 'bm_alu BENCHMARK_ITERATIONS=10' --gtest_filter=LatALU.DependentFmuls
