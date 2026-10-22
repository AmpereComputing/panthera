#! /bin/sh
# Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

#./install/bin/panthera tracing.create_image cmake-build-release/src/ampere/benchmarks/bm_bandwidth
./install/bin/panthera tracing.trace -w 1024 -o bandwidth_32K -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/0
./install/bin/panthera tracing.trace -w 2048 -o bandwidth_64K -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/1
./install/bin/panthera tracing.trace -w 4096 -o bandwidth_128K -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/2
./install/bin/panthera tracing.trace -w 8192 -o bandwidth_256K -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/3
./install/bin/panthera tracing.trace -w 16384 -o bandwidth_512K -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/4
./install/bin/panthera tracing.trace -w 32768 -o bandwidth_1M -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/5
./install/bin/panthera tracing.trace -w 65536 -o bandwidth_2M -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/6
./install/bin/panthera tracing.trace -w 131072 -o bandwidth_4M -- 'bm_bandwidth BENCHMARK_ITERATIONS=5' --gtest_filter=BandwidthTests/Bandwidth.Reads/7
./install/bin/panthera tracing.trace -w 262144 -o bandwidth_8M -- 'bm_bandwidth BENCHMARK_ITERATIONS=3' --gtest_filter=BandwidthTests/Bandwidth.Reads/8
./install/bin/panthera tracing.trace -w 524288 -o bandwidth_16M -- 'bm_bandwidth BENCHMARK_ITERATIONS=3' --gtest_filter=BandwidthTests/Bandwidth.Reads/9
./install/bin/panthera tracing.trace -w 1048576 -o bandwidth_32M -- 'bm_bandwidth BENCHMARK_ITERATIONS=3' --gtest_filter=BandwidthTests/Bandwidth.Reads/10
./install/bin/panthera tracing.trace -w 2097152 -o bandwidth_64M -- 'bm_bandwidth BENCHMARK_ITERATIONS=2' --gtest_filter=BandwidthTests/Bandwidth.Reads/11
./install/bin/panthera tracing.trace -w 4194304 -o bandwidth_128M -- 'bm_bandwidth BENCHMARK_ITERATIONS=2' --gtest_filter=BandwidthTests/Bandwidth.Reads/12
