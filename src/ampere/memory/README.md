# Loaded-latency memory model

## Background

The supported Panthera memory endpoint is `Memory`. It models the
latency for requests that miss the SLC and reach external memory. A
fixed latency does not reflect the queueing caused by many active
cores, so this model varies latency with observed memory bandwidth.

For topology and configuration guidance, see the published [Memory
Model](../../../doc/user-guide/model-memory.md) guide. The
`config/model/mc/` directory name is retained for compatibility; its
profiles configure the `Memory` endpoint rather than selecting a
detailed memory-controller implementation.

## Overview

The memory model improves on a fixed-latency model by varying
the time that a memory transaction remains in the memory controller
based on the memory bandwidth that is being driven to the memory
controller. As the bandwidth increases the latency increases.  The
bandwidth latency curve that is used by this new memory model is based
on measurements from a system. Curves should be calibrated for the
system and workload being modeled.

One thing that we identified is that the BW/latency curve is different
based on the percentage of memory transactions which are
writes. Higher percentages of write traffic results in higher memory
latency at the same bandwidth. The memory model supports having
multiple different curves based on the percentage of write traffic.

The bandwidth/latency curves are stored in the model using the
[Boost Interval Container Library](https://www.boost.org/doc/libs/1_80_0/libs/icl/doc/html/index.html).
The data for the bandwidth/latency curves is provided to the model
in `JSON` format using the `bw_latency_data` parameter of the memory
module.

### Data format

The format for the JSON string is an ordered list of pairs of
numbers. The first number in the pair is the upper bound for that BW
region, the second number in the pair is the latency for the BW
region. The lower bound for the BW region starts at 0 and is
subsequently set by each pair in the list. It is an error if the BW
exceeds the highest BW value, so the input should be constructed
to ensure that the maximum BW can't be exceeded.

Here is an example of the JSON format.

```json
[
    {
        "write_percentage": 0.25,
        "bw_latency": [[10, 150], [20, 250], [30, 400]]
    },
    {
        "write_percentage": 0.50,
        "bw_latency": [[10.1, 250.2], [20.2, 350.3], [30.3, 500.5]]
    },
    {
        "write_percentage": 1.1,
        "bw_latency": [[10, 400.1], [20, 500.2], [30, 700.6]]
    }
]
```

See the bandwidth-latency curve design documentation for more information on
this format.

The data for the bandwidth latency curves comes from measurements of
the system being modeled.

## Modeling

The memory model implementation in Panthera tracks the number of read
and write requests made to the memory controller. Periodically the
memory model computes the read and write bandwidths from the requests
it has processed and determines a new latency based on the bandwidth
latency curves. The model uses this latency for next simulation
period. To provide some level of hysteresis in the system the model
uses a circular buffer to track the bandwidth over multiple simulation
periods. The circular buffer is implemented using the
`boost::circular_buffer` container class.

Each entry in the circular buffer is used to store the bandwidth for 1
simulation period. The memory model averages the memory bandwidth
across all entries in the circular buffer. Without this circular
buffer we observed that the memory latency would oscillate wildly
between the maximum and minimum latencies. We studied different values
for both the length of the simulation period and the number of entries
in the buffer. Based on these simulations the model currently uses a
simulation period of 750ns and buffer with 75 entries. These long time
periods cause the memory model to react slowly to changes in the rate
of incoming requests
