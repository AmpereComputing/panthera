#! /usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
Run the Panthera simulator.

Execute the simulator binary and the event stream processor.
"""

import argparse
import logging as log
import os
import os.path
import shlex
import signal
import subprocess
import sys

from . import install_dir, setup_logging

setup_logging()

BINDIR = os.path.join(install_dir, "bin")


def run(simcmd, proccmd, timeout, gdb, gdb_evp):
    # Create pipe from simulator to event_stream processor
    ev_sim_out = os.pipe()
    os.set_inheritable(ev_sim_out[0], True)
    os.set_inheritable(ev_sim_out[1], True)

    # Create pipe from event_stream processor to simulator
    ev_sim_in = os.pipe()
    os.set_inheritable(ev_sim_in[0], True)
    os.set_inheritable(ev_sim_in[1], True)

    # Pass event stream output file descriptor to the simulator
    simcmd[1:1] = ["-event_stream_fd", str(ev_sim_out[1]), "-resp_stream_fd", str(ev_sim_in[0])]
    proccmd[1:1] = ["--in_fd", str(ev_sim_out[0]), "--out_fd", str(ev_sim_in[1])]

    def gdb_args(gdb_arg):
        args = ["gdb"]
        if gdb_arg is not True:
            args += shlex.split(gdb_arg)
        if args[-1].strip() == "--args":
            return args
        return args + ["--args"]

    if gdb:
        simcmd[0:0] = gdb_args(gdb)

    if gdb_evp:
        proccmd[0:0] = gdb_args(gdb_evp)

    log.info(" ".join(proccmd))

    # Make sure there aren't leftover pending signals so we don't think one of
    # our children processes has exited earlier than it has
    while len(signal.sigpending()) > 0:
        signal.sigwait(signal.sigpending())

    # Create the simulator process passing the event stream file descriptor
    log.info(" ".join(simcmd))
    simulator = subprocess.Popen(simcmd, pass_fds=[ev_sim_in[0], ev_sim_out[1]])

    # Create the event stream processor which takes the event stream on stdin and outputs responses on stdout
    processor = subprocess.Popen(proccmd, pass_fds=[ev_sim_in[1], ev_sim_out[0]])

    # When the simulator and processor start close our copies of the event streams
    os.close(ev_sim_out[1])
    os.close(ev_sim_out[0])
    os.close(ev_sim_in[1])
    os.close(ev_sim_in[0])

    if sys.version_info >= (3, 8):
        signal.pthread_sigmask(signal.SIG_BLOCK, signal.valid_signals())
    else:
        signal.pthread_sigmask(signal.SIG_BLOCK, range(1, signal.NSIG))

    # Wait for someone to exit (or other fatal signal)
    sig = signal.sigwaitinfo({signal.SIGCHLD, signal.SIGINT, signal.SIGTERM})
    # Python: CLD_STOPPED did not enter python until 3.9. so hacking....
    if sys.version_info >= (3, 9):
        while sig.si_code == os.CLD_STOPPED or sig.si_code == os.CLD_CONTINUED:
            sig = signal.sigwaitinfo({signal.SIGCHLD, signal.SIGINT, signal.SIGTERM})
    else:
        while sig.si_code == 5 or sig.si_code == 6:
            sig = signal.sigwaitinfo({signal.SIGCHLD, signal.SIGINT, signal.SIGTERM})

    if sig.si_signo == signal.SIGINT or sig.si_signo == signal.SIGTERM:
        log.error("interrupted, exiting")
        simulator.kill()
        processor.kill()
        return -1

    simstatus = simulator.poll()
    espstatus = processor.poll()

    if espstatus is not None:
        log.info("event stream processor finished")
        # The event stream processor exited first, so clean up the simulator
        simstatus = await_peer(simulator, timeout)
    elif simstatus is not None:
        log.info("simulator finished")
        # Wait for the event stream processor to exit
        espstatus = await_peer(processor, timeout)

    if espstatus == 0 and simstatus == -13:
        # If the event stream processor exited normally, and the simulator
        # got SIGPIPE, this means that the event stream processor stopped
        # the simulation early. It is not actually an error; so fake like
        # everything is okay.
        print("Simulation complete: stopped by event stream processor", file=sys.stderr)
        simstatus = 0

    report_status("simulator", simstatus)
    report_status("event stream processor", espstatus)

    if simstatus != 0:
        return simstatus
    else:
        return espstatus


def await_peer(subproc, timeout):
    try:
        status = subproc.wait(timeout)
    except subprocess.TimeoutExpired:
        log.error("timeout expired; cleaning up")
        subproc.kill()
        status = subproc.wait()
    return status


def report_status(tool, status):
    if status is None:
        log.error(f"{tool} did not exit yet")
    elif status != 0:
        if status < 0:
            log.error(f"{tool} died with signal {-status}")
        else:
            log.error(f"{tool} returned non-zero exit status ({status})")


def print_metrics(filename, metrics):
    import pandas as pd

    df = pd.read_csv(filename)
    for metric in metrics:
        if metric in df:
            stat = df[metric].sum()
            print(f"{metric} {stat}")
        else:
            log.warning(f"Couldn't find {metric} in {filename}")


def main(cmdline_args):
    if "--" in cmdline_args:
        pos = cmdline_args.index("--")
        simcmd = cmdline_args[1:pos]
        proccmd = cmdline_args[pos + 1 :]
    else:
        simcmd = cmdline_args[1:]
        proccmd = []

    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False, add_help=False)
    # NOTE: short args should not be used, otherwise they can partially be consumed especially
    # since other tools (e.g. amperesys) use single '-' long options, which is unusual
    parser.add_argument("--help", action="help", help="show this help message and exit")
    parser.add_argument(
        "--timeout",
        type=int,
        default=300,
        help="seconds before killing processes at exit",
    )
    parser.add_argument("--verbose", action="store_true", help="turn on informational messages")
    parser.add_argument(
        "--gdb",
        nargs="?",
        const=True,
        metavar="GDB_ARGS",
        help="run the simulator under gdb, with optional arguments passed to gdb",
    )
    parser.add_argument(
        "--gdb-evp",
        nargs="?",
        const=True,
        metavar="GDB_ARGS",
        help="run the event processor under gdb, with optional arguments passed to gdb",
    )
    args, simcmd = parser.parse_known_args(simcmd)

    if args.verbose:
        log.root.setLevel(log.INFO)

    def readd_exec_if_consumed(arg):
        if arg and os.path.exists(arg) and os.access(arg, os.X_OK):
            simcmd.insert(0, arg)
            return True
        return arg

    args.gdb = readd_exec_if_consumed(args.gdb)
    args.gdb_evp = readd_exec_if_consumed(args.gdb_evp)

    if not simcmd or not os.path.exists(simcmd[0]):
        simcmd.insert(0, os.path.join(BINDIR, "amperesys"))

    if not proccmd or not os.path.exists(proccmd[0]):
        proccmd.insert(0, os.path.join(BINDIR, "evp"))

    return run(simcmd, proccmd, args.timeout, args.gdb, args.gdb_evp)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
