# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Rollup weighted metrics from summary/timeseries."""

import json
import logging
import os
from multiprocessing import Pool

import pandas as pd
import scipy.stats

from . import CONFIGURATION, SUITE, TRACE, WEIGHT, WORKLOAD
from .metric_formula import FormulaCache, MetricFormula
from .timeseries import MetricData, Summary, TimeSeries

worker = None


class RollupWorker:
    def __init__(self, formula_filenames, output_directory, keep_factored):
        self.formulas = FormulaCache(filenames=formula_filenames)
        self.output_directory = output_directory
        self.keep_factored = keep_factored


def setup_worker(*args):
    global worker
    worker = RollupWorker(*args)


class Rollup:
    def __init__(self, verbose=False):
        self.study = None
        self.formulas = None
        self.summaries = []
        self.timeseries = []
        self.df = None
        self.meta = []
        self.verbose = verbose
        self._study_list = None

    def print_joblist_warning(self, jobgenerator, classification, verbose=False):
        joblist = list(jobgenerator)
        if len(joblist) == 0:
            return

        print(f"WARNING: Omitting {len(joblist)} {classification} jobs from rollup!")
        if verbose:
            print(f"The logfiles for these {classification} jobs follow:")
            for job in joblist:
                print(f" - {job.log_relpath}")
        else:
            example_logfile = joblist[0].log_relpath
            print(f"One such {classification} job's log file is {example_logfile}")

    @classmethod
    def _open_study_list(cls, path):
        with open(path, "r") as slf:
            study_list = json.load(slf)

        return {trace["name"]: trace for trace in study_list}

    @classmethod
    def from_study(
        cls,
        study,
        verbose=False,
        timeseries=False,
        run_metrics=False,
        study_list=None,
        summary_suffix=None,
        timeseries_suffix=None,
    ):
        new_rollup = cls(verbose=verbose)
        new_rollup.study = study
        if study_list:
            new_rollup._study_list = cls._open_study_list(study_list)

        new_rollup.print_joblist_warning(study.failed_jobs(), "failed", verbose)
        new_rollup.print_joblist_warning(study.incomplete_jobs(), "incomplete", verbose)

        for job in study.successful_jobs():
            if new_rollup._study_list is not None:
                meta = new_rollup._study_list.get(job.name)
                if meta is None:
                    logging.debug(f"Skipping {job.name}, not in study-list")
                    continue
                job.meta.update(meta)

            summary = None
            try:
                summary = Summary.from_job(
                    job, suffix=summary_suffix, additional_metrics=job.run_metrics if run_metrics else None
                )
            except Exception:
                pass

            if timeseries or summary is None:
                try:
                    timeseries = TimeSeries.from_job(
                        job, suffix=timeseries_suffix, additional_metrics=job.run_metrics if run_metrics else None
                    )
                    new_rollup.timeseries.append(timeseries)
                    if summary is None:
                        summary = timeseries
                except Exception:
                    logging.info(f"bad timeseries file for {job.name} in {job.directory}")

            if summary is None:
                logging.info(f"bad summary file for {job.name} in {job.directory}")
            else:
                new_rollup.summaries.append(summary)

        if not timeseries and not new_rollup.summaries:
            raise RuntimeError("No summary/timeseries found for study")

        return new_rollup

    @classmethod
    def from_summaries(cls, paths, verbose=False):
        paths = paths if isinstance(paths, list) else [paths]
        new_rollup = cls(verbose=verbose)
        new_rollup.summaries = [MetricData(path) for path in paths]
        return new_rollup

    def read_formula_file(self, filename):
        self.formulas = FormulaCache(filenames=[filename])

    def read_formula_files(self, filenames):
        self.formulas = FormulaCache(filenames=filenames)

    @property
    def formula_filenames(self):
        if self.formulas is None:
            return None
        return self.formulas.formula_filenames

    @staticmethod
    def rollup_summary(summary):
        meta = pd.json_normalize(summary.meta).iloc[0].to_dict()
        df = summary.df
        aggregated = df.sum()
        aggregated["time"] = df.loc[:, "stop_time"].max()
        for key in iter(meta):
            if key == WEIGHT:
                # Ensure WEIGHT is a number instead of a string
                aggregated[key] = float(meta[key])
            else:
                aggregated[key] = meta[key]
        aggregated.name = summary.name
        return aggregated, set(meta.keys())

    def create_dataframe(self, workers=4, chunksize=20, fillna=True):
        sums = []
        meta_names = set()
        with Pool(workers) as p:
            results = p.imap_unordered(Rollup.rollup_summary, self.summaries, chunksize)
            for s, m in results:
                sums.append(s)
                meta_names.update(m)

        if fillna:
            return pd.DataFrame(sums).fillna(0), list(meta_names)
        else:
            return pd.DataFrame(sums), list(meta_names)

    def rollup(self, by, mean="geometric", workers=4, fillna=True):
        if self.df is None:
            self.df, self.meta = self.create_dataframe(workers=workers, fillna=fillna)

        if by != TRACE:
            # Multiply numeric portion of dataframe by weight, sum weighted
            # per-trace statistics by workload, and compute formulas
            nonweight_meta = set(self.meta)
            cols_to_modify = [col for col in self.df.columns if col not in list(nonweight_meta)]
            df_modify = self.df.loc[:, cols_to_modify].astype("float64").multiply(self.df[WEIGHT], axis=0)
            df_others = self.df.drop(columns=cols_to_modify)

            df = pd.concat([df_modify, df_others], axis=1)
            df = df.groupby([WORKLOAD, CONFIGURATION, SUITE]).sum(numeric_only=True)
            df = self.compute_formulas(df)

            # If rolling up by suite, combine workload-level stats (defaulting
            # to using geomean)
            if by == SUITE:
                if mean == "geometric":
                    df = df.groupby([SUITE, CONFIGURATION]).aggregate(scipy.stats.gmean)
                else:
                    df = df.groupby([SUITE, CONFIGURATION]).mean()
        else:
            df = self.compute_formulas(self.df)

        df = df.sort_index(axis=1)
        df.index.name = by
        return df.reset_index().set_index(by)

    @staticmethod
    def rollup_ts(ts):
        global worker
        df = ts.compute_formulas(worker.formulas, keep_factored=worker.keep_factored, emit_warnings=False)

        output_directory = worker.output_directory
        if output_directory is None:
            output_filename = ts.filename
        else:
            config_name = os.path.basename(os.path.dirname(ts.filename))
            output_directory = os.path.join(output_directory, config_name)
            os.makedirs(output_directory, exist_ok=True)
            output_filename = os.path.join(output_directory, os.path.basename(ts.filename))

        df.to_csv(output_filename)
        return output_filename

    def rollup_timeseries(self, output_directory=None, keep_factored=False, workers=4, chunksize=20):
        if self.formulas is None or output_directory is None:
            return

        logging.info(f"rolling up {len(self.timeseries)} timeseries")
        with Pool(
            workers, initializer=setup_worker, initargs=[self.formula_filenames, output_directory, keep_factored]
        ) as p:
            results = p.map(Rollup.rollup_ts, self.timeseries, chunksize)

        logging.info(f"rollup of {len(results)} timeseries complete")

    def compute_formulas(self, df):
        if self.formulas is None:
            return df

        metric = MetricFormula(df)
        self.formulas.compute_all(metric)

        return metric.df
