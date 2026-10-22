# Stream Tools

## Event-Stream Tools

### Prefetcher Validator

`es_prefetcher_validator` allows for replaying events through various prefetcher
models. Prefetchers are initialized similarly to `amperesys` and can use the
same config files. Event setup (per prefetcher) can use config files in
`config/model/core/prefetching/validator/*`. See `--help` for more info.

For example:

    ./install/bin/es_prefetcher_validator -event_stream <es file> \
        -top.l2c:cfg ./install/config/model/core/prefetching/validator/rtl/l2p_bop.cfg \
        -top.l2c.prefetcher:cfg ./install/config/model/core/connor/l2c_prefetcher.cfg

If more output is desired you can add `-top.l2c.debug true` to include
intermediate events/dumps.

It can also be run with `panthera sim`:

    ./install/bin/panthera sim ./install/bin/es_prefetcher_validator \
        -event_stream <es file> \
        -top.l2c:cfg ./install/config/model/core/prefetching/validator/rtl/l2p_bop.cfg \
        -top.l2c.prefetcher:cfg ./install/config/model/core/connor/l2c_prefetcher.cfg \
        -- +count -a +summarize

Which will provide events form the replay and prefetcher.
