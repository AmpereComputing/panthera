#!/bin/bash
# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


    panthera=$(realpath ${PWD}/../../../../../)
   tracebase=nqueens-20
 offlinewarm=5000000
 dmem_filter=65536
 imem_filter=32768
  onlinewarm=0
      length=200000
    tracedir=rtl_traces.$$
       suite=Micro

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

# generate disasm and footprint, and validate traces
cd $tracedir
for i in *.is.xz; do
    BASE=`echo ${i} | sed 's/is.xz/is/g'`
    MSFILE=${i%.is.xz}.ms.xz
    echo "${panthera}/install/bin/is_disasm --e ${length} ${i} > ${BASE}.dis && gzip ${BASE}.dis" 
    echo "${panthera}/install/bin/is_footprint -e ${length} ${i} > ${BASE}.foot" 
    if [ -f ${MSFILE} ]
    then
        echo "${panthera}/install/bin/is_working_set --ms ${MSFILE} --is ${i} > ${BASE}.wss"
    else
        echo "${panthera}/install/bin/is_working_set --is ${i} > ${BASE}.wss"
    fi
    echo "${panthera}/install/bin/is_validate ${i} > ${BASE}.val"
done | /usr/bin/parallel
grep Warning *.val
cd -

# Copy "$tracedir" to the desired output location, if needed.
