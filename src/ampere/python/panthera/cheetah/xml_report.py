#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate XML report for study."""

import datetime
import socket
import sys
import xml.etree.ElementTree as ET
from typing import Dict

from .job import Job


class XMLReport:
    """Reads and writes test reports in the JUnit XML format.

    More info at: https://www.ibm.com/docs/en/developer-for-zos/16.0?topic=formats-junit-xml-format
    """

    ERROR_TAGS = [
        "failure",
        "error",  # for backwards compatability
    ]
    DEFAULT_ERROR_TAG = ERROR_TAGS[0]

    def __init__(self, tree: ET):
        self.tree = tree

    def testsuites(self):
        root = self.tree.getroot()
        return root.findall(".//testsuite")

    def summary(self) -> Dict[str, Dict[str, int]]:
        def get_errors(testsuite):
            for error_tag in self.ERROR_TAGS:
                error_attr = f"{error_tag}s"
                if error_attr in testsuite.attrib:
                    return int(testsuite.get(error_attr))

            return 0

        return {
            testsuite.attrib["name"]: {
                "jobs": int(testsuite.get("tests", 0)),
                "errors": get_errors(testsuite),
                "incompletes": int(testsuite.get("incompletes", 0)),
            }
            for testsuite in self.testsuites()
        }

    def print_summary(self) -> None:
        for name, testsuite in self.summary():
            print("Report for: {}".format(name))
            for name, value in testsuite.items():
                print("Number of {name}: {}".format(value))

    def errors(self) -> Dict[str, str]:
        errors = {}
        for testsuite in self.testsuites():
            for error_tag in self.ERROR_TAGS:
                testcases = testsuite.findall(f".//testcase[{error_tag}]")
                for testcase in testcases:
                    name = testcase.get("name")
                    message = testcase.find(error_tag).get("message")
                    errors[name] = message

        return errors

    def print_errors(self) -> None:
        for name, message in self.errors().items():
            print("{} failed: {}".format(name, message))

    def write(self, report_name: str) -> None:
        self.tree.write(report_name, encoding="UTF-8", xml_declaration=True)

    @classmethod
    def from_xml(cls, path: str) -> "XMLReport":
        return cls(ET.parse(path))

    @classmethod
    def timestamp(cls) -> str:
        return str(datetime.datetime.now().isoformat("T", "seconds"))

    @classmethod
    def hostname(cls) -> str:
        return socket.gethostname()

    @classmethod
    def create_testsuite(cls, name: str, parent):
        return ET.SubElement(
            parent,
            "testsuite",
            attrib={
                "name": name,
                "time": "0",
                "failures": "0",
                "package": "panthera",
                "timestamp": cls.timestamp(),
                "hostname": cls.hostname(),
            },
        )

    @classmethod
    def create_testcase(cls, job: Job, study: "Study", parent):
        name = job.directory + "/" + job.name
        attributes = {
            "name": name,
            "status": "run" if job.complete else "notrun",
            "classname": "panthera",
            "time": "0",
        }
        if study.parallel_info is not None:
            attributes["time"] = str(study.parallel_info.loc[name, "Runtime"])
            attributes["hostname"] = str(study.parallel_info.loc[name, "Host"])

        testcase = ET.SubElement(parent, "testcase", attrib=attributes)
        if job.error:
            error = {"message": job.error}
            ET.SubElement(testcase, cls.DEFAULT_ERROR_TAG, attrib=error)

        return testcase

    @classmethod
    def from_study(cls, study: "Study") -> "XMLReport":
        testsuites = ET.Element("testsuites")
        testsuite = cls.create_testsuite(study.name, testsuites)
        errors = 0
        incompletes = 0
        jobs = 0
        for job in study.all_jobs():
            cls.create_testcase(job, study, testsuite)
            if job.error:
                errors += 1
            if not job.complete:
                incompletes += 1
            jobs += 1

        testsuite.attrib["tests"] = str(jobs)
        testsuite.attrib["failures"] = str(errors)
        testsuite.attrib["incompletes"] = str(incompletes)  # Not defined in JUnit
        return cls(ET.ElementTree(testsuites))


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        print(arg)
        report = XMLReport.from_xml(arg)
        report.print_summary()
        report.print_errors()
