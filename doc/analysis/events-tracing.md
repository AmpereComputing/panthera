# Event Tracing

The chrome and chromium browsers have a builtin performance analysis
tool that can be accessed in the browser by loading the
[tracing]([chrome://tracing]) URL. This system has an open standard
for providing data for visualization. We have created a tool called
**events.tracing** that can generate an output file that can be loaded
into the tracing tool.

The **events.tracing** tool has 2 different types of view that it can
generate: a resource view and a structural transaction view.

### Resource View

The default view is a resource view where the rows in the window
represent a specific event. This view provides a good way to look at
the overall utilization of the machine.

This view is selected using `--view resource`.

### Transaction View

The second view provides a transaction view based on a specific
structure of the machine. The classic example for this type is to show
uop transactions based on their ROB entry. In this view the rows in
the window display a specific ROB entry. The lifetime of a uop in an
entry is indicated by a long line and events associated with that uop
transaction appear as boxes below the uop transaction.

The Chrome tracing tool can handle a large number of events, but it
does have limits. For interactive debug it is best to limit the number
of instructions in the event stream to around 10,000. You can generate
larger traces but the viewer performance may be impacted.

This view is selected using `--view transactions`.

### Argument Configuration Files and the "@" Syntax

The **events.tracing** tool can be configured using command line
arguments.  We have gathered together useful sets of arguments into
configuration files in the `src/ampere/config/events.tracing`
directory. After "make install" installation, the config files show up
in `install/config/events.tracing`. To use the configuration prepend the
name of the file with an `@` sign on the command line. This
arguments-from-a-file is a feature of python's argparse module. For
more information see `fromfile_prefix_chars` in the [python argparse
documentation](https://docs.python.org/3/library/argparse.html).

### How to Run

Here are the steps to generate an event stream, process it with
**events.tracing** and view it in the Chrome browser.

1. Generate an event stream from the simulator called `events.es`. Replace the
   instruction-stream path with a stream for the workload being analyzed. For
   example:

        ./install/bin/panthera sim install/bin/amperesys -cfg install/config/model/sys/siryn.cfg -sys.soc_0.core_0.instruction_stream_name /path/to/workload.is.xz -sys.soc_0.core_0.gpc.max_instructions 10000 -- -i '*.gpc.instruction_commit=30000' @base.cfg +capture events.es.xz

2. Run:

        ./install/bin/panthera events.tracing events.es.xz @install/config/events.tracing/summary.cfg --compress

   This writes `events.json.gz` by default, removing the extension from `events.es.xz` and adding the json suffix.

3. For "rob-view" (i.e. transactions per uop) run:

        ./install/bin/panthera events.tracing events.es.xz --output events.rob.json @install/config/events.tracing/tx_rob.cfg --compress

4. Open a tab in chrome and navigate to [chrome://tracing]([chrome://tracing])
5. Click on the load button and select the `events.json` file created in step 2
6. Click on the load button and select the `events.rob.json` file created in step 3

### Command-line Options

As shown above, you will most commonly include events.tracing 'config' files
using the `@filename.cfg` syntax. Doing to causes all the contents of those
files to be included as if you had included them all on the command-line
yourself. The below documentation is not necessary if you are content using a
pre-made events.tracing configuration in that manner, but may be helpful should
you find yourself wanting to specify your own.

#### Basic event inclusion

Basic functionality is provided `--event <name>`, which causes an event in the
event stream with the name `<name>` to be displayed in the output.

#### Output

If you do not specify where to write the output, ".json" will be added to the
base filename of the `*.es` input file. If you with to specify an alternate
location, use `--output` to do so.

#### Posting and pulling from a transaction

Sometimes it is useful to display a data item found on one event in a
transaction on other events in the same transaction. This is possible with the
pair of arguments: `--post_to_tx` and `--pull_from_tx`. The following pair of
events will display the "pc" data item on any `sys.soc_0.core_0.idr.uop_decode`
event occurring in a transaction after a `sys.soc_0.core_0.icf.fetch event`:

```
--post_to_tx "sys.soc_0.core_0.icf.fetch:pc"
--pull_from_tx "sys.soc_0.core_0.idr.uop_decode:pc"
```

One caveat with this functionality is that an event can only receive a data
value from another event which occurred earlier than it in the event stream
because `events.tracing` streams through the event stream instead of holding
onto events until a transaction is complete. For example, trying to post data
from `*.gpc.instruction_commit` and pull it from `*.icf.fetch` will not work since
the commit event will not have been seen for this instruction yet when the fetch
event is processed.

#### Splitting events

Sometimes it is useful to break an event apart based on data items held within
it. For example, our PMU events are (mostly) posted to the same event, but with
different data values to distinguish between them. To have each instance of
the `sys.soc_0.core_0.core.pmu.raw` event show up on a different row according to
its 'event' data item in a resource view, we could do:

```
--split-all sys.soc_0.core_0.pmu.raw/event=pmu_event_{event}
```

If there are many splits and you only only want to include some events (e.g.
enumerations), you can do:
```
--split-all sys.soc_0.core_0.pmu.raw/event=sys.soc_0.core_0.pmu.{event}
--event sys.soc_0.core_0.pmu.stall_backend
--event sys.soc_0.core_0.pmu.stall_backend_rob
...
```

It is furthermore possible to rename and filter out unwanted rows produced by
`--split-all` (since they can get unwieldy quickly). Building on the above
example, if we wanted to select the cycle counter, we could do:

```
--rename pmu_event_17=pmu.cycles
--exclude "pmu_event_#"
```

This further renames the cycle counter (which was originally renamed to
`pmu_event_17` by the above `--split-all` command-line because its event data item
is 17) to `pmu.cycles` and filters out any remaining splits which match
`pmu_event\_#` after any renaming.

#### Structure occupancy

The `--occupancy` argument allows tracking structure occupancy based on credits
acquired/released. It takes a three-four item colon/comma-separated list
containing the desired name for the row in the viewer, the name of the event
emitted when a new entry in the structure/buffer is acquired, and the name of
the event emitted when entries are released, and optionally the name of the
data value that can be used for the occupancy number (by default `.occupancy`
is used). For example, to track the FEQ occupancy, you might do:

```
--occupancy FEQ:sys.soc_0.core_0.bpu.feq.acquire,sys.soc_0.core_0.bpu.feq.release
```

Or to specify a data value:

```
--occupancy FEQ:sys.soc_0.core_0.bpu.feq.acquire,sys.soc_0.core_0.bpu.feq.release,sys.soc_0.core_0.bpu.feq.used
```

#### Compression

`--compress` gzips the json output before saving it to the output file.
