# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp


class BUCKET_INTERVAL:
    def __init__(self, start, end):
        self.start_bucket = start
        self.end_bucket = end


class Latency:
    def __init__(self, start_event, stop_event, name, bucket_start=0, bucket_end=7, hist_size=64):
        self.tx = {}
        self.name = name
        self.sum_latency = 0
        self.sum_latency_sq = 0
        self.max_latency = -1
        self.max_avg_latency = -1
        self.max_latency_list = []
        self.max_txid = 0
        self.min_latency = 2**20 - 1
        self.stdev = 0
        self.count = 0
        self.median = 0
        self.histogram = {}
        self.last_bucket = BUCKET_INTERVAL(0, 0)
        self._init_bucket_histogram(bucket_start, bucket_end, hist_size)
        evp.on(start_event, lambda e: self._start(e))
        evp.on(stop_event, lambda e: self._stop(e))
        evp.on("end_transaction", lambda e: self._end_transaction(e))
        # evp.end_simulation(lambda: self._dump_histogram)
        evp.end_simulation(self._dump_histogram)

    def _init_bucket_histogram(self, start, end, size):
        start_bucket = start
        end_bucket = end
        interval = end - start + 1
        for num in range(0, size):
            key = BUCKET_INTERVAL(start_bucket, end_bucket)
            self.histogram[key] = 0
            if num < (size - 1):
                start_bucket = end_bucket + 1
                end_bucket = start_bucket + interval - 1
        start_bucket = end_bucket + 1
        end_bucket = start_bucket
        key = BUCKET_INTERVAL(start_bucket, end_bucket)
        self.last_bucket = key
        self.histogram[key] = 0

    def _start(self, event):
        self.tx[event.data["txid"]] = event.time

    def _stop(self, event):
        txid = event.data["txid"]
        if txid in self.tx:
            latency = event.time - self.tx[txid]
            self._record(latency, txid)
            del self.tx[txid]

    def _end_transaction(self, event):
        txid = event.data.get("txid", None)
        if txid in self.tx:
            del self.tx[txid]

    def _record(self, latency, txid):
        assert latency >= 0
        self.sum_latency += latency
        self.sum_latency_sq += latency * latency
        self.count += 1
        self.max_latency_list.append(latency)
        if len(self.max_latency_list) > 10:
            self.max_latency_list.sort(reverse=True)
            self.max_latency_list.pop()
        if latency > self.max_latency:
            self.max_latency = latency
            self.max_txid = txid
        elif latency < self.min_latency:
            self.min_latency = latency

        if self.count > 1:
            self.stdev = (self.sum_latency_sq + (self.sum_latency * self.sum_latency) / self.count) / (self.count - 1)
            self.stdev = self.stdev**0.5
        self._latency_histogram(latency)

    def _latency_histogram(self, lat):
        updated = False
        for key in self.histogram:
            if key.end_bucket == key.start_bucket:
                continue
            if (int(lat / 1000) <= key.end_bucket) and (int(lat / 1000) >= key.start_bucket):
                self.histogram[key] += 1
                updated = True
                break
        if not updated:
            self.histogram[self.last_bucket] += 1

    def _print_distribution(self, percent):
        value = ""
        x = int(percent)
        while x > 0:
            value += "*"
            x = x - 2
        return value

    def _median(self):
        l = len(self.max_latency_list) // 2
        if (l % 2) == 0:
            # even
            return (self.max_latency_list[l] + self.max_latency_list[l - 1]) / 2
        else:
            # odd
            return self.max_latency_list[l]

    def _dump_histogram(self):
        print(f"{self.name}.histogram")
        if self.count <= 0:
            print("No events recorded in histogram")
            print("-------------------------------------")
            return
        print("-------------------------------------------------")
        print("{:>4}  {:4} : {:20} {:7} {:4}".format("Start", "End", "Dist", "Count", "Percent"))
        s = 1
        for key in self.histogram:
            percent = round(self.histogram[key] / self.count * 100, 2)
            dist = self._print_distribution(percent)
            if s < len(self.histogram):
                line = "{:>4} - {:4} : {:20} {:7} {:4}%".format(
                    key.start_bucket, key.end_bucket, dist, self.histogram[key], percent
                )
            else:
                line = "{:>4} -   Max: {:20} {:7} {:4}%".format(key.start_bucket, dist, self.histogram[key], percent)
            print(line)
            s += 1
        self.max_avg_latency = sum(self.max_latency_list) / len(self.max_latency_list)
        self.max_latency_list.sort()
        self.max_latency_list = [x / 1000 for x in self.max_latency_list]
        self.median = round(self._median(), 2)
        print("-------------------------------------------------")
        print(f"MaximumLatency          -> {round(self.max_latency / 1000, 2)} txid_{self.max_txid}")
        print(
            f"AvgMaximumLatency:10    -> {round(self.max_avg_latency / 1000, 2)} {self.max_latency_list} Max@:txid_{self.max_txid}"
        )
        print(f"MedianMaximumLatency:10 -> {self.median}")
        print(f"AverageLatency          -> {round(self.sum_latency / self.count / 1000, 2)}")
        print(f"TotalCount              -> {self.count}")
        print("-------------------------------------")

    # def _rename(self, event_name):
    #    return self.rename(event_name) if self.rename is not None else event_name


class Histogram:
    def __init__(self, event, data, name, bucket_start=0, bucket_end=7, hist_size=64):
        self.name = name
        self.sum_usage = 0
        self.sum_usage_sq = 0
        self.max_usage = -1
        self.max_txid = 0
        self.count = 0
        self.stdev = 0
        self.histogram = {}
        self.last_bucket = BUCKET_INTERVAL(0, 0)
        self.init_bucket_histogram(bucket_start, bucket_end, hist_size)
        # self._get_value(event, data)
        evp.on(event, lambda e: self._get_value(e, data))
        evp.end_simulation(self._dump_histogram)

    def init_bucket_histogram(self, start, end, size):
        start_bucket = start
        end_bucket = end
        interval = end - start + 1
        self.histogram = {}
        for num in range(0, size):
            key = BUCKET_INTERVAL(start_bucket, end_bucket)
            self.histogram[key] = 0
            if num < (size - 1):
                start_bucket = end_bucket + 1
                end_bucket = start_bucket + interval - 1
        start_bucket = end_bucket + 1
        end_bucket = start_bucket
        key = BUCKET_INTERVAL(start_bucket, end_bucket)
        self.last_bucket = key
        self.histogram[key] = 0

    def _get_value(self, event, data, alloc=False):
        bt_usage = event.data[data]
        self._collect_usage(bt_usage)

    def _print_distribution(self, percent):
        value = ""
        x = int(percent)
        while x > 0:
            value += "*"
            x = x - 2
        return value

    def _dump_histogram(self):
        print(f"{self.name}.histogram")
        if self.count <= 0:
            print("No events recorded in histogram")
            print("-------------------------------------")
            return
        print("-------------------------------------------------")
        print("{:>4}  {:4} : {:20} {:7} {:4}".format("Start", "End", "Dist", "Count", "Percent"))
        s = 1
        for key in self.histogram:
            percent = round(self.histogram[key] / self.count * 100, 2)
            dist = self._print_distribution(percent)
            if s < len(self.histogram):
                line = "{:>4} - {:4} : {:20} {:7} {:4}%".format(
                    key.start_bucket, key.end_bucket, dist, self.histogram[key], percent
                )
            else:
                line = "{:>4} -   Max: {:20} {:7} {:4}%".format(key.start_bucket, dist, self.histogram[key], percent)
            s += 1
            print(line)
        print("-------------------------------------------------")
        print(f"Maximum    -> {round(self.max_usage, 2)}")
        print(f"Average    -> {round(self.sum_usage / self.count, 2)}")
        print(f"TotalCount -> {self.count}")
        print("-------------------------------------")

    def _usage_histogram(self, value):
        updated = False
        for key in self.histogram:
            if (int(value) <= key.end_bucket) and (int(value) >= key.start_bucket):
                self.histogram[key] += 1
                updated = True
                break
        if not updated:
            self.histogram[self.last_bucket] += 1

    def _collect_usage(self, value):
        self.sum_usage += value
        self.sum_usage_sq += value * value
        self.count += 1
        if value > self.max_usage:
            self.max_usage = value

        if self.count > 1:
            self.stdev = (self.sum_usage_sq + (self.sum_usage * self.sum_usage) / self.count) / (self.count - 1)
            self.stdev = self.stdev**0.5
        self._usage_histogram(value)
