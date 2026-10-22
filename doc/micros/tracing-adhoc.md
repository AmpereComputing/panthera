# Ad Hoc Benchmark Tracing
## Generating QCOW Image

To generate a qcow2 image containing all of the microbenchmarks in a panthera
build, use the following command-line, (assuming 'build' is your CMake build
directory):

```
./install/bin/panthera tracing.create_image --output microbenchmarks.qcow2 build/src/ampere/benchmarks/bm_* /
```

The `tracing.create_image` command will download an Alpine Linux rootfs by default, and
copy the specified binaries into the rootfs.

Note: If you care about 'vector' benchmarks containing vector instructions,
please ensure you are running with a `Release` build of panthera.

## Collecting Traces

Once you have created your qcow image, it is simply a matter of running all the
tests to generate ELF files and instruction streams. Notice the
`snippets.package` commands sprinkled throughout - those are creating the
beginnings of a 'snippet' release directory for these microbenchmarks.

```
#!/bin/bash -eux

# ALU
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o alu_dependent_adds -- bm_alu --gtest_filter=LatALU.DependentAdds
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o alu_independent_adds -- bm_alu --gtest_filter=LatALU.IndependentAdds
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_independent_fadds -- bm_alu --gtest_filter=LatALU.IndependentFadds
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o alu_independent_fmuls -- bm_alu --gtest_filter=LatALU.IndependentFmuls
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_dependent_fadds -- bm_alu --gtest_filter=LatALU.DependentFadds
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_dependent_fmuls -- bm_alu --gtest_filter=LatALU.DependentFmuls

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_alu alu_*dependent*.elf

# ALU Fusion
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o alu_fusion_movmovk -- bm_alu_fusion --gtest_filter=ALUFusion.MovMovkFusion

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_fusion alu_fusion*.elf

# ALU Mem
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_independent_loads -- bm_alu_mem --gtest_filter=ALUMem.IndependentLoads
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_independent_stores -- bm_alu_mem --gtest_filter=ALUMem.IndependentStores
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_independent_adds_loads -- bm_alu_mem --gtest_filter=ALUMem.IndependentAddsLoads
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_load_store -- bm_alu_mem --gtest_filter=ALUMem.LoadStore
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_load_store_fsu -- bm_alu_mem --gtest_filter=ALUMem.LoadStoreFSU
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o alu_mem_independent_fadds_stores -- bm_alu_mem --gtest_filter=ALUMem.IndependentFaddsStores

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_alu_mem alu_mem_*.elf

# Branches
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o branch_indirect_4_targets -- bm_branch --gtest_filter=Indirect.IndirectBranchWithFourTargets
for i in $(seq 0 20); do
  ./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o branch_conditional_direct_variable_$i -- bm_branch --gtest_filter=ConditionalDirectBranchTests/Branch.VariableHistoryConditionalDirect/$i;
done
for i in $(seq 21 24); do
  ./install/bin/panthera tracing.trace --trace-elf --max-instructions 100000 --rootfs-hda microbenchmarks.qcow2 -o branch_conditional_direct_variable_$i -- bm_branch --gtest_filter=ConditionalDirectBranchTests/Branch.VariableHistoryConditionalDirect/$i;
done

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_branch branch_*.elf

# Calls
./install/bin/panthera tracing.trace --max-instructions 1000000 --trace-elf --rootfs-hda microbenchmarks.qcow2 -o calls_repeated -- bm_call --gtest_filter=Call.RepeatedCalls

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_call calls_repeated*.elf

# Loop
./install/bin/panthera tracing.trace --trace-elf --rootfs-hda microbenchmarks.qcow2 -o loop_double_branch -- bm_loop --gtest_filter=Loop.DoubleBranchLoop

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_loop loop_double_branch*.elf

# mem bandwidth
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o mem_bw_strided_32K -- bm_mem_bw --gtest_filter=BwMemRdTests/Strided.Reads/0
./install/bin/panthera tracing.trace --trace-elf --max-instructions 100000 --rootfs-hda microbenchmarks.qcow2 -o mem_bw_strided_128K -- bm_mem_bw --gtest_filter=BwMemRdTests/Strided.Reads/1
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o mem_bw_random_strided_32K -- bm_mem_bw --gtest_filter=BwMemRdTests/RandomStrided.Reads/0
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o mem_bw_random_strided_128K -- bm_mem_bw --gtest_filter=BwMemRdTests/RandomStrided.Reads/1

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_mem_bw mem_bw_*.elf

# lat_mem_rd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 500000 --rootfs-hda microbenchmarks.qcow2 -o lat_mem_rd_32K -- bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/1
./install/bin/panthera tracing.trace --trace-elf --max-instructions 500000 --rootfs-hda microbenchmarks.qcow2 -o lat_mem_rd_128K -- bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/2
./install/bin/panthera tracing.trace --trace-elf --max-instructions 500000 --rootfs-hda microbenchmarks.qcow2 -o lat_mem_rd_1M -- bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/3
./install/bin/panthera tracing.trace --trace-elf --max-instructions 100000 --rootfs-hda microbenchmarks.qcow2 -o lat_mem_rd_2M -- bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/4
./install/bin/panthera tracing.trace --trace-elf --max-instructions 500000 --rootfs-hda microbenchmarks.qcow2 -o lat_mem_rd_8M -- bm_mem_lat --gtest_filter=LatMemRdTests/RandomStrided.Reads/5

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_mem_lat lat_mem_rd*.elf

# scalar
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_add_char -- bm_scalar --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_scale_char -- bm_scalar --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_mac_char -- bm_scalar --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_sum_char -- bm_scalar --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_add_int -- bm_scalar --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_scale_int -- bm_scalar --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_mac_int -- bm_scalar --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_sum_int -- bm_scalar --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_add_longlong -- bm_scalar --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_scale_longlong -- bm_scalar --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_mac_longlong -- bm_scalar --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_sum_longlong -- bm_scalar --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_add_float -- bm_scalar --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_scale_float -- bm_scalar --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_mac_float -- bm_scalar --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_sum_float -- bm_scalar --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_add_double -- bm_scalar --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_scale_double -- bm_scalar --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_mac_double -- bm_scalar --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_sum_double -- bm_scalar --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o scalar_float_accum -- bm_scalar --gtest_filter=FloatTest1K.TestFloatAccum

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_scalar scalar_*.elf

# vector
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_add_char -- bm_vector --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_scale_char -- bm_vector --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_mac_char -- bm_vector --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_sum_char -- bm_vector --gtest_filter=VectorTest1K/0.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_add_int -- bm_vector --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_scale_int -- bm_vector --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_mac_int -- bm_vector --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_sum_int -- bm_vector --gtest_filter=VectorTest1K/1.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_add_longlong -- bm_vector --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_scale_longlong -- bm_vector --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_mac_longlong -- bm_vector --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_sum_longlong -- bm_vector --gtest_filter=VectorTest1K/2.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_add_float -- bm_vector --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_scale_float -- bm_vector --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_mac_float -- bm_vector --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_sum_float -- bm_vector --gtest_filter=VectorTest1K/3.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_add_double -- bm_vector --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_scale_double -- bm_vector --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_mac_double -- bm_vector --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_sum_double -- bm_vector --gtest_filter=VectorTest1K/4.TestVectorAdd
./install/bin/panthera tracing.trace --trace-elf --max-instructions 1000000 --rootfs-hda microbenchmarks.qcow2 -o vector_float_accum -- bm_vector --gtest_filter=FloatTest1K.TestFloatAccum

./install/bin/panthera snippets.package -o microbenchmark_snippets -s microbenchmarks bm_vector vector_*.elf
```
