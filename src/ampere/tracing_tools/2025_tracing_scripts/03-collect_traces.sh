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
 offlinewarm=10000000000
 dmem_filter=65536
 imem_filter=32768
  onlinewarm=3001008 # to avoid quantization and print heartbeat
      length=20000001 # to show hbeat
    tracedir=is_traces.$$
       suite=$2

echo $tracebase

${panthera}/install/bin/panthera tracing.trace_intervals \
        --input-json ${tracebase}.calculate_simpoints.json \
        --start-measuring-after-online-warmup-instructions ${onlinewarm} \
        --start-online-warmup-after-offline-warmup-instructions ${offlinewarm} \
        --trace-runlength ${length} \
        --dcache-filter-size ${dmem_filter} \
        --icache-filter-size ${imem_filter}

echo "Tracing complete..."

# create tlist
${panthera}/install/bin/panthera tracing.manifest_to_workloads \
        ${tracebase} ${tracebase}.trace_intervals.json tracelist.json ${suite}

mkdir -p $tracedir
mv *.is.xz $tracedir
mv *.ms.xz $tracedir
mv *.bs.xz $tracedir
mv *.is.bbv $tracedir
mv tracelist.json $tracedir
cp *.png $tracedir

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

# copy to NFS
cp -a $tracedir $3/${suite}/${tracebase}
