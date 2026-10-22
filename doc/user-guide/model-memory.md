# Memory Model

Panthera models external memory with the generic `Memory` mesh device. It is
the supported memory endpoint for SoC configurations; detailed memory
controller implementations are not included.

## Adding Memory to a Topology

Add a `Memory` device to the SoC `devices` array. Unless a `prefix` is given,
the instance is named `memory_<n>`:

```json
{
    "type": "Memory",
    "position": [1, 0, 0, 0]
}
```

Existing topology profiles commonly select settings for every memory instance
with a regular expression:

```text
~sys.soc_0.memory_[0-9]+:cfg ../mc/sn-mc-study.cfg
```

The `config/model/mc/` path is retained for compatibility. These files
configure `Memory`; they do not choose a detailed memory-controller model.

## Loaded Latency

`Memory` varies request latency with the observed memory bandwidth rather than
using a single fixed latency. It tracks read and write traffic, then selects a
latency from `bw_latency_data`. Each entry supplies a write percentage and an
ordered list of bandwidth/latency pairs:

```json
[
    {
        "write_percentage": 0.25,
        "bw_latency": [[10, 150], [20, 250], [30, 400]]
    },
    {
        "write_percentage": 1.0,
        "bw_latency": [[10, 400], [20, 500], [30, 700]]
    }
]
```

The first value in each pair is the upper bound of a bandwidth range and the
second is its latency. Provide a final range high enough for the expected
traffic. The model chooses a curve according to the write percentage and
updates the effective latency periodically. `bandwidth_scale_factor` and
`latency_offset` can adjust a profile for a target system.

For the SoC topology and mesh-device syntax, see the [SoC Model](model-soc.md)
guide.
