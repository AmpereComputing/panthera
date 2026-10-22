

Assumptions

0) You have read the scripts before running them and modified them for your usage.
   The scripts look for ../panthera/install/ by default.

1) you've copied tracing_hints.h into this directory from src/ampere/tracing_tools

2) you've copied this "basic" directory to a sibling directory to your
   panthera git clone. The end result looks like this:

   some-path/
      panthera/install/...
      basic/
         00-compile-static.sh
         01-workload-setup.sh
         02-collect-traces.sh
         03-sim.sh
         04-json.sh
         README.txt
         basic.c
         tracing_hints.h


3) Build or obtain a compatible AArch64 kernel `Image` with Panthera's
   perf-semihosting patch applied, and copy it to the basic directory. The
   scripts use `Image` by default; set `KERNEL_IMAGE` to use another path.
   You'll need it for step 02-collect-traces.
   
