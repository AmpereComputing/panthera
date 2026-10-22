# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import csv
import gzip
import hashlib
import os
import re

import pandas as pd


class Data:
    """A class for managing a tree of model data.

    Data is assumed to be in the form of CSV files which can be read into Pandas
    DataFrames. Meta data can be encoded in the directory names in the form
    'key.value', e.g. if you've got:

        study.my_study/experiment.two_req_channels/summary.csv
                      /experiment.one_req_channel/summary.csv

    you can call:

        df = Data('study.my_study').get('summary')

    and the resulting DataFrame will be shaped like this:

        study     experiment        metric1    metric2 ...
        ==============================================
        my_study  two_req_channels     w          x
        my_study  one_req_channel      y          z

    """

    CACHE_DIR = ".cache"
    INT_RE = re.compile(r"^[0-9]+$")
    FLOAT_RE = re.compile(r"^[0-9]+\.([0-9]+)*$")

    def __init__(self, data_dir, enable_cache=True):
        self.data_dir = data_dir
        self.cache_dir = os.path.join(data_dir, Data.CACHE_DIR)
        self.enable_cache = enable_cache

        if not os.path.exists(data_dir):
            raise FileNotFoundError(f"{data_dir} does not exist")
        elif not os.path.isdir(data_dir):
            raise FileExistsError(f"file {data_dir} exists but is not a directory")

        if self.enable_cache:
            if not os.path.exists(self.cache_dir):
                os.mkdir(self.cache_dir)
            elif not os.path.isdir(self.cache_dir):
                raise FileExistsError(f"cache {self.cache_dir} exists but is not a directory")

    def get(self, name, **kwargs):
        file_list = self._file_list(name, kwargs)
        df = self._cache_lookup(file_list)

        if df is not None:
            return df

        records = []
        for file, meta, _ in file_list:
            if file.endswith("gz"):
                csv_file = gzip.open(file, "rt")
            elif file.endswith("csv"):
                csv_file = open(file)
            else:
                raise Exception(f"Unable to load unexpected filetype: {file}")

            for row in csv.DictReader(csv_file):
                record = {k: self._convert(v) for k, v in row.items()}
                record.update(meta)
                records.append(record)
            csv_file.close()

        df = pd.DataFrame.from_records(records)
        self._cache_insert(file_list, df)
        return df

    def _file_list(self, name, kwargs):
        return sorted(
            [(file, meta, os.path.getmtime(file)) for file, meta in self._find(name) if self._meta_match(meta, kwargs)],
            key=lambda x: x[0],
        )

    def _cache_file(self, file_list):
        h = hashlib.sha256()

        for file, _, ts in file_list:
            h.update(file.encode())
            h.update(str(ts).encode())

        return os.path.join(self.cache_dir, h.hexdigest())

    def _cache_lookup(self, file_list):
        if not self.enable_cache:
            return
        cache_file = self._cache_file(file_list)
        return pd.read_pickle(cache_file) if os.path.exists(cache_file) else None

    def _cache_insert(self, file_list, df):
        if not self.enable_cache:
            return
        cache_file = self._cache_file(file_list)
        df.to_pickle(cache_file)

    def _find(self, name):
        csv_name = f"{name}.csv"
        csv_gz_name = f"{name}.csv.gz"
        if "." in self.data_dir:
            meta = Data._extract_key_value(self.data_dir)
        else:
            meta = {}
        yield from self._find_in_dir({name, csv_name, csv_gz_name}, self.data_dir, meta)

    @staticmethod
    def _find_in_dir(names, directory, meta):
        for path in os.scandir(directory):
            if path.is_dir():
                new_meta = meta.copy()
                if "." in path.name:
                    new_meta.update(Data._extract_key_value(path.name))
                yield from Data._find_in_dir(names, path.path, new_meta)
            else:
                if path.name in names:
                    yield path.path, meta

    @staticmethod
    def _extract_key_value(name):
        key, value = name.split(".", maxsplit=1)
        return {key: Data._convert(value)}

    @staticmethod
    def _meta_match(meta, filter):
        for k, v in filter.items():
            if k not in meta or meta[k] != v:
                return False

        return True

    @staticmethod
    def _convert(value):
        if Data.INT_RE.match(value):
            value = int(value)
        elif Data.FLOAT_RE.match(value):
            value = float(value)
        return value
