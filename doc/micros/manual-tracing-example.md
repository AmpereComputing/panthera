# Manually tracing a micro/program

The files for this example are in `src/ampere/tracing_tools/examples/basic`.

0. Enter Panthera's Nix development environment, or another environment that
   provides the required tracing tools.

1. Choose a local working directory outside the checkout. This example uses
   `$prefix` as its parent directory.

2. Build Panthera in `$prefix/panthera`; see [the user guide](../UserGuide.md).
   After the build, `$prefix/panthera/install` should exist.
   The scripts and program used in this example live in
   `panthera/src/ampere/tracing_tools/examples/basic`.  However it is
   recommended that you do not do this work directly in your git repo
   as tracing and simulation create a lot of files that you do not
   want to manage with git.  Instead, It is suggested you copy the
   "basic" directory to a sibling directory of your panthera clone and
   then carry out the following experiment.

3. Write a little microbenchmark. Here's mine in `$prefix/basic/basic.c`:
   ```sh
   gcc -march=armv8.5-a -O3 basic.c -o basic -lm -static
   ```
   ```cpp title="basic/basic.c"
   #include "tracing_hints.h"
   #include <stdio.h>
   #include <stdint.h>
   #include <math.h>

   int main() {
       uint64_t i=0;
       float x=0;
       hint_start_tracing();
       for(i=0;i<10000;i++) {
          x = x + cos((double)i/10000.0);
       }
       hint_stop_tracing();
       printf("%lf\n",x);
   }
   ```

4. Compile and link it STATICALLY (static linking is very important or
   the benchmark probably will not run on the different distribution
   we run under qemu, alpine linux).

        % cat 00-compile-static.sh
        gcc -march=armv8.5-a -O3 basic.c -o basic -lm -static
        % ./00-compile-static.sh

5. Create qemu disk image (qcow - qemu copy on write) to hold the
   benchmark. Run the script shown.

        % cat 01-workload-setup.sh
        #!/bin/bash -x
            panthera=../panthera
              rootfs=http://dl-cdn.alpinelinux.org/alpine/v3.8/releases/aarch64/alpine-minirootfs-3.8.1-aarch64.tar.gz
            qcow_img=alpine.qcow2
              binary=basic
         rootfs_size=16G
           tracebase=basic

        ${panthera}/install/bin/panthera tracing.create_image \
            --rootfs ${rootfs}                                \
            --output ${qcow_img}                              \
            --rootfs-size ${rootfs_size}                      \
            ${binary} /

6. Build or obtain a compatible AArch64 kernel `Image` with Panthera's
   [perf-semihosting patch](../tracing/perf-semihosting-5.10.patch) applied,
   then copy it to the local directory. See the tracing documentation for
   kernel requirements.

7. Collect the trace. Run the script shown.

        % cat 03-collect-traces.sh
        #!/bin/bash

        panthera=../panthera
        basename=basic
        kernel=${KERNEL_IMAGE:-Image}

        ${panthera}/install/bin/panthera tracing.trace \
            --output=${basename}                       \
            --rootfs-hda=alpine.qcow2                  \
            -kernel ${kernel}                          \
           -- basic

    The local `Image` is used by default. Set `KERNEL_IMAGE` to use a kernel
    image at another location.

8. Simulate the workload. Run the script shown.

        % cat 03-sim.sh
        #!/bin/bash

        panthera=../panthera
        basename=basic_0

        ${panthera}/install/bin/panthera sim ${panthera}/install/bin/amperesys \
           -cfg ${panthera}/install/config/model/sys/siryn.cfg                 \
           -sys.soc_0.core_0.instruction_stream_name ${basename}.is.xz         \
           -sys.soc_0.core_0.gpc.max_instructions 10000                        \
           -- -i 10us @base.cfg +capture ${basename}.es +summarize --timeseries ${basename}.ts.csv --summary ${basename}.summary.csv


9. Create a json for the chrome or edge web viewer. Run the script shown.
   For more information about tracing see [this link](analysis/events-tracing.md).

        % cat 04-json.sh
        #!/bin/bash
        panthera=../panthera
        basename=basic_0

        ${panthera}/install/bin/panthera events.tracing ${basename}.es \
            @${panthera}/install/config/events.tracing/summary.cfg
