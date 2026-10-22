#!/bin/bash
# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


if [ "$#" -ne 3 ]; then
  echo "Error: You must provide 3 arguments: Panthera path, Suite Name, Path to where snippets will be saved."
  echo "Usage: $0   "
  exit 1
fi


    panthera=$1
   tracebase=`basename $PWD`
 offlinewarm=50000000
  mem_filter=131072
  onlinewarm=0
      length=200000
    tracedir=rtl_traces.$$
       suite=$2
 dmem_filter=65536
 imem_filter=32768

echo $tracebase

${panthera}/install/bin/panthera tracing.trace_intervals \
        --input-json ${tracebase}.calculate_simpoints.json \
        --start-measuring-after-online-warmup-instructions ${onlinewarm} \
        --start-online-warmup-after-offline-warmup-instructions ${offlinewarm} \
        --trace-runlength ${length} \
        --dcache-filter-size ${dmem_filter} \
        --icache-filter-size ${imem_filter} \
    --perfval-snippets

echo "Tracing complete..."

# create tlist
${panthera}/install/bin/panthera tracing.manifest_to_workloads \
        ${tracebase} ${tracebase}.trace_intervals.json tracelist.json ${suite}

# move the good stuff into the tracedir...
/usr/bin/parallel gzip ::: *.elf
mkdir -p $tracedir
for i in *.elf.gz; do
    name=`echo $i | sed 's/.elf.gz//g'`
    mv $name.is.xz $tracedir
    mv $name.ms.xz $tracedir
    mv $name.bs.xz $tracedir
    mv $name.is.bbv $tracedir
    mv $i $tracedir
done
mv tracelist.json $tracedir
cp *.png $tracedir

# ...and put the leftover stuff into a junk directory.
junkdir=junk.$$
mkdir -p $junkdir
mv *.is.xz $junkdir
mv *.ms.xz $junkdir
mv *.bs.xz $junkdir
mv *.is.bbv $junkdir
mv *.plugin.log $junkdir
mv *.iss_terminal $junkdir
mv *.cosim_sysregs $junkdir


# generate disasm and footprint, and validate traces
cd $tracedir
for i in *.is.xz; do
    BASE=`echo ${i} | sed 's/is.xz/is/g'`
    echo "${panthera}/install/bin/is_disasm --e ${length} ${i} > ${BASE}.dis && gzip ${BASE}.dis"
    echo "${panthera}/install/bin/is_footprint --e ${length} ${i} > ${BASE}.foot"
    echo "${panthera}/install/bin/is_validate ${i} > ${BASE}.val"
done | /usr/bin/parallel
grep Warning *.val
cd -

# copy to NFS
cp -a $tracedir $3/${suite}/${tracebase}
