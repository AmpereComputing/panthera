# Debugging Tips and Tricks

The following are an assortment of strategies, tools, or features available to help debug the panthera model.

## Enabling Debug Messages

Debug messages are only enabled in the `DEBUG` and `ReleaseWithDebMessages`
built types. If using the top-level Makefile you can use either `make debug` or
`make debug-messages`.

Each module will have a `debug` parameter which allows for categories of debug
to be enabled (or `*` for all). When a category is enabled at some level of
hierarchy, it's also enabled for the levels *below* as well.

Enabling these messages corresponds to allowing `AMPERE_DEBUG(<category>)`
and/or `AMPERE_UNCATEGORIZED_DEBUG()` to emit messages to stdout (similar to
INFO/etc).

## Getting/viewing an event stream

[Event streams](events-tracing.md) are typically the best first step to get a better idea of the interactions between different modules within the processor.

### How it works internally

It can be helpful to understand how `panthera sim` works at a basic level to understand how the options provided to it may interact. `panthera sim` is a wrapper around two other processes: `amperesys` and `evp`. `amperesys` is the model which generates an event stream and `evp` processes the event stream from `amperesys` to generate summarized CSV or protobuf file representations of (a portion of) it. This is important to understand because options ultimately passed to `evp` by `panthera sim` are only aware of the portion of the event stream `amperesys` provides to it. This should make more sense later.

### Collecting the interesting part

The `evp` command has several command-line options you can pass it to extract an event stream for the portion of the simulation interesting to you.

* `+capture <file>`: specifies the location to save the event stream file
* `--start`: optionally specifies when to begin recording the event stream (based on either time or event counts)
* `--stop`: optionally specifies when to stop recording the event stream in the same way as `--start`
* `--exit`: can be used to exit immediately upon stopping recording the event stream

Additional help output for these options can be found by running `panthera sim --help`.

Here is an example of a command-line to collect 6000 instructions worth of an event stream, beginning at the 5000th instruction after warmup is complete, and exiting the model when done capturing the event stream. Replace the instruction-stream path with a stream for the workload you are debugging:
```
./install/bin/panthera sim \
    ./install/bin/amperesys \
        -sys.soc_0.core_0.instruction_stream_name /path/to/workload.is.xz \
        -cfg ./install/config/model/sys/siryn.cfg \
        -sys.soc_0.core_0.gpc.max_instructions 10000000
    -- -i 10us @base.cfg \
        --start sys.soc_0.core_0.gpc.instruction_commit=5000 \
        --stop sys.soc_0.core_0.gpc.instruction_commit=11000 \
        --exit +capture workload.es.xz
```

### How much should I collect?

Collecting 4,000-6,000 instructions and/or cycles is a reasonable first guess
for investigating an error or issue. For example, if a model fails at
cycle 2385964, use `--start=sys.soc_0.core_0.gpc.cycle=2380000`. Collecting a
much larger event stream can make it harder to work with, including when
generating JSON for `chrome://tracing`.

#### Warmup can be tricky

Note that the arguments passed to `--start` and `--stop` typically begin counting when the instruction containing the START_MEASUREMENT marker in the instruction stream is committed. In other words, if your instruction stream has 1000 instructions of warmup, `--start=sys.soc_0.core_0.gpc.instruction_commit=500` will begin capturing the event stream when the 1500th instruction is executed, because that is the 500th of the 'measurement' section of the instruction stream. To modify this behavior, `-event_stream_always true` can be passed on the `amperesys` command-line to cause events to always be emitted (even for the portion of the simulation prior to the START_MEASUREMENT marker). This means events for all instructions (even warmup) will be passed to the event stream processor which results in the event counts considered by the implementations of `--start` and `--stop` to begin counting from the beginning of time. In other words, the following commands are equivalent assuming there are 1000 instructions of warmup:

```
./install/bin/panthera sim [...] -- --start=sys.soc_0.core_0.gpc.instruction_commit=500
./install/bin/panthera sim [...] -event_stream_always true -- --start=sys.soc_0.core_0.gpc.instruction_commit=1500
```

A caveat to this caveat is that some pieces of information remain 'absolute', even when the event stream is truncated at the START_MEASUREMENT marker. Time is an obvious example, so if you extract the time from a Panthera error message and use it (minus a few thousand nanoseconds) for `--start`, then you needn't adjust for warmup. Certain event data is also absolute. For example, the 'sys.soc_0.core_0.gpc.inum' field on the 'sys.soc_0.core_0.gpc.instruction_commit' event will always be the instruction count beginning with the START_MEASUREMENT marker (i.e. it should be 0 for warmup instructions committed).

Here is an example of a command-line to collect almost 6000 cycles of an event stream for a simulation which I know dies at cycle 2385964 in the warmup section.
```
./install/bin/panthera sim \
    ./install/bin/amperesys \
        -sys.soc_0.core_0.instruction_stream_name /path/to/workload.is.xz \
        -sys.soc_0.core_0.l2c.warmup_ms /path/to/workload.ms.xz \
        -sys.soc_0.warmup_ms /path/to/workload.ms.xz \
        -cfg ./install/config/model/sys/siryn.cfg \
        -sys.soc_0.core_0.gpc.max_instructions 10000000 \
        -event_stream_always true
    -- -i 10us @base.cfg \
        --start sys.soc_0.core_0.gpc.cycle=2380000 \
        +capture workload.es.xz
```

### Viewing an event stream

Once collected, you can use the event stream to better understand what is happening for the bug/issue you are exploring. Event streams are primarily viewed using `chrome://tracing` after being converted to the JSON format using `panthera events.tracing`, as detailed in [the event stream documentation](analysis/events-tracing.md). You may also use the `./install/bin/es_viewer` binary to view a raw text-based version of the event stream if you need quick access to more detailed information.

You can also use the event processor standalone on a previously generated event stream. Run it like this: `./install/bin/evp [options] <event stream>`. This is useful to quickly summarize events of your choice (with `--event <name>`), print events in text form with `+print` (like `es_viewer`, but with the ability to select events with `--event <name>`), or just list events and data that are in the stream using the `-l` switch. See the help output for more information.

## Braindeaths

A simulation 'braindeath' occurs when the model has not committed any uops/instructions for an unexpectedly long period of time. That period of time is defined in terms of a cycle count by `sys.soc_0.core_0.gpc.brain_death_limit`, and the current default is 60,000 cycles.

### Printing internal state upon braindeath

One way to quickly debug braindeaths is to run the model with a `-*.dump_at_end` parameter. This parameter calls the C++ `dump()` method for the selected instance of the module. For example, to cause the ICF to dump its state when braindeath occurs, you would add the following to the `amperesys` command-line: `-sys.soc_0.core_0.icf.dump_at_end true`. Most modules support this; you can substitute 'icf' here with (at least) any of: 'bpu', 'gpc', 'icf.ifb', 'idr', 'ixu', 'lsu.pcp', 'lsu.sf_predictor', 'lsu', 'l2c', 'immu', 'dmmu', 'xu_0', or even supply several at a time. You might even want to print them all with the regexp syntax for params: `-~.*dump_at_end`. In this case the star is literally part of the name, so shell quoting is likely necessary.

### Examining braindeath in an event stream

When debugging a braindeath in an event stream, recall that braindeath occurs when uops stop committing. So a useful procedure is to find the last uop (or instruction) commit event, and do a search on the contained transaction id. This is the last successful uop (or instruction). Now if you find the very next uop (or instruction) by looking back at the uop_decode (or iq_write) event and search for *that* transaction id, you can see the life of that transaction in events.

## GDB

If the event stream does not present enough information, it is common to need to drop into `gdb` to debug further. I find it helpful to have already gained some information from looking at an event stream in order to inform where I want to start my exploration in `gdb`.

### Getting to the right place

#### Fast-forwarding to a particular cycle count

The `amperesys` binary has a command-line option `-trap_cycles` which allows you to 'trap' into GDB once the model executes a certain number of cycles. This makes it easy to get to where you want to be in the model's execution if you have a cycle count. To stop execution on cycle 2383284, modify your `panthera sim` command to look like:

```
./install/bin/panthera sim --gdb "-ex 'catch throw' -ex 'run'" ./install/bin/amperesys [...] -trap_cycles 2383284
```

Once that cycle count is reached, you will be able to set further breakpoints as you normally would in gdb.

Note, if you what to stop at a particular cores cycle, you can use `-sys.soc_*.core_*.trap_cycles`.

#### Using `if` statements

Another strategy to getting to the right point in the code is to compile an `if` statement in the code somewhere which you know will only be taken when you care about, and set a breakpoint inside the code block protected by it. This can be significantly faster than a conditional breakpoint for long runs. You can even make gdb stop automatically by sending a SIGTRAP at the right point---for example if you have a particular transaction you're looking for:

```c++
if (my_transaction->txid().value == 12345)
    kill(0, SIGTRAP); // gdb will stop here
```

You'll need to `#include <signal.h>` if you want to use `kill()`, though.

### Exploring structure state using modules' `dump()` methods

Most of the modules (i.e. ICF, L2, GPC, etc.) inside panthera contain `dump()` methods which output state helpful for debugging. When inside a stack frame belonging to one of those module classes, you may execute the following inside gdb to call those methods:
```
call dump(std::cout)
```

### Other helpers

You can use `p get_cycle()` to print the current cycle number when inside GDB.

## Memory Checkers

### Address Sanitizer

Some bugs are extremely tricky to track down. For example, if a behavior seems to be changing between a debug build and a release build, or
memory just seems to be changing out from under you. In those kinds of cases, it might be helpful to build with the "address sanitizer". This
compiler/linker option can instrument and check all memory allocations and deallocations and look for undefined behavior in your code. To use
it simply select an alternate build type with cmake: `cmake -DCMAKE_BUILD_TYPE=ASAN ...` or just `make asan`.

All of the executables that are built (`amperesys` as well as unit and integration test executables) will run with address sanitizer
automatically. This can produce extra output on the terminal when an error is detected. For information about these errors, see the online
[documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer).


### Valgrind

Similar to ASAN, `valgrind` will catch various memory-related errors such as:

* Memory leaks and incorrect frees
* Out-of-bounds reads/writes
* Conditionals from uninitialized memory

You can run unit-tests under Valgrind with: `make valgrind-unit-test`. Additionally you can run on a debug build like:

    valgrind --leak-check=full --track-origins=yes --suppressions=.valgrind.supp -- amperesys ...

The suppressions file (`.valgrind.supp`) contains known/okay leaks or other violations (usually ones which we don't have control over).
