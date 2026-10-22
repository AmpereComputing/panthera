# Running Studies with Cheetah

Cheetah is a tool to run performance studies using the Panthera performance model.

## Overview

Cheetah does 2 main tasks:

1. Create a list of simulation jobs from a list of configurations and workloads
2. Run the list of jobs created in the first step

The configurations and workloads are provided as either JSON *or* python files
to Cheetah. JSON "specification" files are the "traditional" interface to
cheetah, but python files offer more flexibility.

## Configuring the Study

### JSON Specification Files

JSON specification files are the "traditional" interface to cheetah, which is
more core-focused. See the
[schema](../src/ampere/python/panthera/cheetah/schema/specification.json) for
what is supported in the JSON file.

#### Running A Study

> **NOTE:** if you're using Slurm make sure you've received the appropriate permissions and know the configuration needed for your cluster

Options for Cheetah can be viewed with `./install/bin/panthera cheetah --help`.
The **specification** and **workloads** parameters are required, but the others have reasonable defaults.
Use a study workload list from your trace download; this guide refers to it as
`study-list.json`.

Create a specification file name `my_study.json` as described in the [Simulator configuration](#Simulator configuration) section.

Finally run the studies.

    ./install/bin/panthera cheetah --specification my_study.json --workloads study-list.json

Once the study is complete you can [rollup](rollup.md) the results and generate [reports](report.md).

#### Simulator configuration

The simulator configuration input is specified using the **--specification example.json** command line parameter.

The simulator configuration input is a JSON file that contains a list of configurations for the simulator.
Each configuration has a set of required key/value pairs.

* `name` - the name of configuration
* `exe` - the name of the simulator executable to run
* `parameters` - a list of parameters to be passed to the simulator. See [parameters](./parameters.md) for more information on parameter syntax.

Here is a sample configuration file.

```json
[
  {
    "name": "Siryn",
    "exe": "install/bin/amperesys",
    "parameters": [
      "-cfg", "src/ampere/core/config/siryn.cfg",
      "-sys.soc_0.core_0.instruction_stream_name", "{workload_path}",
      ["-sys.soc_0.core_0.l2c.warmup_ms", "{memstream_path}"],
      ["-sys.soc_0.warmup_ms", "{memstream_path}"],
      "-max_nanoseconds", "100000"]
  }
]
```

You may refer to `src/ampere/config/cheetah/json/*` for examples.

Note, the **names of the different configurations must be different**.

##### Optional Filename Arguments

You'll notice in the above example that the two model parameters referencing
`memstream_path` (`-sys.soc_0.core_0.l2c.warmup_ms` and `-sys.soc_0.warmup_ms`)
are in a nested command list instead of being directly contained in the top-level
"parameters" list. This indicates that these parameters are optional. They will
only be included if `memstream_path` exists in the trace list (not all traces in
our trace lists are guaranteed to have warmup files). If a required parameter
(i.e. one *not* in an additional level of list nesting) is missing, it is
expected to result in a failure. This feature allows the user to specify which
parameters may safely be omitted if their arguments are missing, and which
should "fail loudly".


##### Specifying EVP Arguments

When running a study, event processor arguments can be supplied using
a parameter block named "evp":

```json
{
    "name": "my experiment",
    "exe": "{panthera_root}/install/bin/amperesys",
    "parameters": {
        # simulation parameters here...
    }
    "evp": [
        "-i", "10us", "@count-pmus.cfg"
    ]
}
```

Note, `+summarize` and its usual arguments are included by default even when overriding `evp`. Other parameters that can be specified are:

* `interval`: for overriding `-i` value
* `evp_cfg_file`: for overriding `@base.cfg`

### Python Files

Instead of a JSON specification file, you are provided with a python
study/specification file which generates the commands used to run the study.
Usually one of the simulators from `panthera.cheetah.studies.*` is used, such as
`CoreSim` for "cheetah-like" studies.

Since these are *python* files, configurations/parameters/etc. can be
generated/iterated. The study file itself can be parameterized via:

* Arguments which will be exposed via the command line
* Expansion parameters within commands (i.e. `"{something}"`)

#### Simulators

There's a few existing simulator/study definitions to simplify common studies:

* `SimCmd` : Base `amperesys` study
* `CoreSim` : Basic `amperesys` study with cache preloading and other functionality
* `BobcatSim` : Basic `bobcat` study
* `SoCSim` : Basic SoC/Mesh study


#### Study Configs and Templates

Existing study configs/templates exist in `src/ampere/config/cheetah/python`, these include:

* `core_study.py` : A simple core+bobcat study
* `domino-study.py` : Complex domino mesh study
* `multiload_study.py` : Multiple aggressors/injectors in mesh

#### Running A Study

> **NOTE:** if you're using Slurm make sure you've received the appropriate permissions and know the configuration needed for your cluster

The study specification is really just a Python script. If you have your study in `spec_file.py`,
you can launch it like this:

    ./install/bin/panthera study spec_file.py ...

Using common core-simulation arguments, the full command may look like:

    ./install/bin/panthera study ./install/config/cheetah/python/core_study.py \
        --workloads study-list.json \
        --name my_study

#### Running an unsupported simulator/command

Sometimes it is more expedient to run a quick tool across the study list than
it is to do full modeling and run it with `amperesys`. But, you may still want to
run on a SLURM cluster for massive parallelism of your run. In these cases,
you may use (its `-h` output contains usage details):

    ./install/bin/panthera study ./install/config/cheetah/python/command_study.py ...

Which provides most of the functionality of `cheetah` but for arbitrary commands
(where they're the same template/expansion format as within cheetah).

## Workload list

The workload list input is specified using the `--workloads workloads.json` command line parameter.

The workload list input is another JSON file that contains a list of workloads to run on the simulator.
Like the simulator configuration each workloads has a set of required key/value pairs.

* `name` - short name of the workload
* `file` - relative path to the instruction stream file

Here is a sample workload file:

```json
[
  {
    "name": "gcc",
    "file": "src/ampere/core/test/gcc_0.is.xz"
  }
]
```

### Using a subset of workload list

For some studies it is useful to run a subset of the study list, in particular
if you want to iterate quickly on a feature which you know mostly affects a
small subset of workloads/traces.

We have a python script to make it easier to create subsets of study lists. It
handles fixing up paths contained in the study list as well as performing the
filtering for you. Upon each pass of the tool, you can filter based on
string/regular expression matches upon one of the trace, workload, or suite
field in the study list. For example, to filter the current study list to
contain only traces with weights >5% from cloud infrastructure and web
services, you could run:

```sh
$ ./install/bin/panthera tracing.filter_study_list \
    --input-list study-list.json \
    --output-list cloud_web_min_weight_5pct.json \
    --match suite --include CloudInfra --include WebServices \
    --min-weight 0.05
```

You would then use `cloud_web_min_weight_5pct.json` rather than the original
study list on the `panthera cheetah` command-line to launch your study.

Additional documentation for this script is available by passing `--help`.

## Parallel Node list

If running `parallel` across remote hosts, provide a node file for the machines
available in your environment.

By default the `parallel` tool runs jobs in parallel on the current system, but a *node list* enables running jobs on multiple systems.

The parallel *node list* input is specified using the `--parallel-node-file nodefile` command line parameter.

The parallel *node list* input is a text file that contains a list of machines (IP addresses) on which to simulate.
Each machine requires an associated login and a specification of how many threads to use on the machine.

Here is a sample parallel node file:

```sh
24/user@XX.XX.XXX.XX
24/user@XX.XX.XXX.XX
24/user@XX.XX.XXX.XX
100/user@XX.XX.X.XXX
```

## Building the list of simulations

Cheetah reads the configuration and workload files and creates the list of simulations to run.
The list of simulations the full cross-bar of the configurations and the workloads.

As part of building the list of simulations Cheetah creates a directory tree specified by the `name` parameter.
Inside of this directory, a directory is created for each of configurations, and in those directories a JSON file is created for each simulation job.

Assuming that the `name` parameters was set to "my_study" the directory tree would look like this.

```text
my_study
   |
   +- Siryn
        |
        +- gcc
```

## Running simulations

After creating the list of simulation jobs, Cheetah runs the simulations.
Currently Cheetah supports two methods of running the simulations.

1. The default method is to use GNU parallel to run the simulations in parallel on the local machine
2. An alternative is to run the simulations serially using the `--run serial` parameter

The second method is used by the fast core regression test.

## Simulation outputs

Each simulation job will generate output files based on the workload name.
For workload name foo the following files will be generated:

* `foo.log` - stdout and stderr from the simulator and other commands
* `foo.ts.csv.gz` - timeseries CSV file

### Simulation error report

After running the simulations Cheetah parses the simulation log file to identify any simulations that had errors.
It generates a JUnit compatible XML report file called `report.xml` in the study directory.

## Example run

Here is an example run of Cheetah for the core model using a default study configuration and a trace list with a single dhrystone instruction stream:

    install/bin/panthera cheetah --specification install/config/cheetah/json/siryn_study.json --workloads src/ampere/core/test/siryn_dhrystone.json --name example

This will create a directory name **example** with the following files:

| File                              | Description                                              |
| --------------------------------- | -------------------------------------------------------- |
| example/jobs.txt                  | list of jobs, input file to GNU parallel                 |
| example/report.xml                | XML simulation error report                              |
| example/siryn/dhrystone.json      | simulation job specification                             |
| example/siryn/dhrystone.log       | simulation output for dhrystone work on the siryn config |
| example/siryn/dhrystone.ts.csv.gz | time series CSV data file                                |

## Validating a Study

You can "validate" a study before you run it using `--validate`. This will pick
one trace from each configuration to run for `100` instructions (by default).
For example:

    ./install/bin/panthera cheetah --specification my_study.json \
      --workloads study-list.json \
      --validate

You can override how many instructions are run for validation by passing
`--validation N`. Additionally you can override what parameters are used for
validation with:

    ./install/bin/panthera cheetah --specification my_study.json \
      --workloads study-list.json \
      --validate \
      --validate-params='-event_stream_always true -sys.soc_0.core_0.gpc.max_warmup_instructions 100'

This will pick one trace from the workloads provided, and run one job per
configuration with the additional parameters:

    -event_stream_always true -sys.soc_0.core_0.gpc.max_warmup_instructions 100

Any of these options effectively validate that parameters are specified
correctly *before* launching the entire study. Any jobs that fail will prevent
study launch and will be printed out.

## Additional Examples

### Python

#### Basic Core Study

A very-basic core study may look like:

```python
from panthera.cheetah import run_study, argument_parser
from panthera.cheetah.studies.core import CoreSim, trace_list

configurations = [
    {
        'configuration': 'baseline',
    },
    {
        'configuration': 'extra_l1d_ways',
        'extra_core_args': ['-sys.soc_0.core_0.lsu.l1d_cache.ways', '8'],
    },
]

core = CoreSim(core='domino')

# Build an argument parser from the commands defined above
parser = argument_parser(prog=__file__, description="Run a Core study on a study list")
core.parser_add_args(parser)
args = parser.parse_args()

traces = trace_list(args.workloads)

# Combine the list of traces with the matrix of configurations
run_study(args.study,
          commands.apply(traces).apply([vars(args)]).apply(configurations),
          args)
```

Which creates a study with 2 configurations:

  * `baseline`: using the `domino.cfg`
  * `extra_l1d_ways`: using the `domino.cfg` and increasing L1D ways to 8

Note that `configurations` simply is an array of dicts, where each element of
`configurations` is effectively a configuration and each dict is a set of
expansion variables to override. These names may be *different* depending on the
simulator used, but they will be used as *expanded* values within the simulator commands.


#### Adding arguments

Since each simulator can have arguments, you can add additional ones to help
*parameterize* your study. For example:

```python
#...

parser = argument_parser(prog=__file__, description="Run a Core study on a study list")
core.parser_add_args(parser)
parser.add_argument("--ld1-ways-range", nargs=2, metavar=("FROM", "TO"), help=")
args = parser.parse_args()

configurations = [
    {
        "configuration": f"l1d_{ways}_ways",
        'extra_core_args': ['-sys.soc_0.core_0.lsu.l1d_cache.ways', '8'],
    }
]
```

#### Creating a simulator

Here is a more complex example based on how we might handle Panthera's core studies.

First you would probably want a sub-class of `Cmd` specialized for running Panthera
(this would be made available in a library; it's not something you would need to
reproduce in each study file):

```python
class SimCmd(Cmd):
    '''A default command set up for running panthera sim'''

    cmd          = '{panthera} sim {simexe} {sim} {evpsep} {evp}'
    panthera     = '{panthera_root}/bin/panthera'
    config       = '{panthera_root}/config'
    config_model = '{config}/model'
    config_sys   = '{config_model}/sys'
    simexe       = '{panthera_root}/bin/amperesys'
    evpsep       = Cmd.ifset('evp', '--')
    sim          = []
    evp          = []
```

And a further specialization, for running a study with a single core on a particular
trace (again, this would be provided in a common library):

```python
class CoreSim(SimCmd):
    outdir = '{study_name}/{exp}'
    sim = [
        '-sys.soc_0.core_0.instruction_stream_name', '{workload_root}/{file}', '{cache_preload}',
        '-sys.soc_0.core_0.gpc.max_instructions', '{max_instructions}'
    ]

    evp = [
        '-i', '{interval}', '{evp_cfg_file}',
        '+summarize', '--timeseries', '{outdir}/{name}.ts.csv', '--summary', '{outdir}/{name}.summary.csv'
    ]

    interval = '1us'
    evp_cfg_file = '@base.cfg'
    max_instructions = 10000000

    def cache_preload(self):
        '''Conditionally provide cache-preload switches, if the preload file exists.'''
        if self.pan_cache_preload_file:
            return '-sys.soc_0.core_0.l2c.mc_name {pan_cache_preload_file} -sys.soc_0.warmstart_mc {pan_cache_preload_file}'
        else:
            return ''
```

Given all that, building a "study specification" looks something like this:

```python
import json
from argparse import ArgumentParser
from panthera.cheetah import run_study, add_cheetah_args
from panthera.cheetah.studies.core import CoreSim

parser = ArgumentParser(description='Run a core study on a workload list')

add_cheetah_args(parser)
CoreSim.parser_add_args(parser)
args = parser.parse_args()

base_cmd = CoreSim(directory='my_experiment')
cmds = base_cmd.apply(json.load(open(args.workloads)))
run_study(args.study, cmds) # some slurm way to run a list of command lines
```
