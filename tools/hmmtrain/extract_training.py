#!/usr/bin/python

import subprocess
import argparse
import os
import sys
import re

###############################################################################

def main():
    helptext = """Given list of tpath files on stdin with directories
    (one per line), build a CSV file where each record contains
    execution number, round number, direction, execution trace, and
    message bytes.

    Example: echo 'foo/bar/cliver-out-9/round_0048/baz.tpath' | %(prog)s
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-r', '--runprefix', metavar='P', default='cliver-out-',
                        help="""prefix for top-level directory of an execution,
                                which must be followed by an integer that
                                uniquely identifies the execution.
                                (default = 'cliver-out-')""")
    parser.add_argument('-x', '--omit-xpilot-headers',
                        dest='omit_xpilot_headers',
                        action='store_true', default=False,
                        help="""Omit processing of message headers
                                (assuming XPilot client model )""")
    global args
    args = parser.parse_args()

    file_index = 0
    for filepath in sys.stdin:
        file_index += 1
        filepath = filepath.strip()
        if args.verbose:
            print >>sys.stderr, "Processing #%d: %s" % (file_index, filepath)
        m = re.search(args.runprefix + '(\d+)/round_(\d+)/', filepath)
        if m:
            exec_num = int(m.group(1))
            round_num = int(m.group(2))
        else:
            print >>sys.stderr, \
                "Could not find execution and round number in:", filepath
            return 1

        d = dump_cliverstats(filepath)
        direction, trace, msg = parse_cliverstats(d)
        print csv_output_line(exec_num, round_num, direction, trace, msg)

    return 0

###############################################################################

def dump_cliverstats(path):
    """Dump cliverstats output as a single string"""
    if args.omit_xpilot_headers:
      return subprocess.check_output(["cliverstats", "-debug-socket",
                                      "-client-model=xpilot",
                                      "-print-omit-headers", "-input", path])
    else:
      return subprocess.check_output(["cliverstats", "-debug-socket", "-input", path])

def parse_cliverstats(d):
    """Given cliverstats output d, extract the direction
    (client-to-server vs server-to-client), execution trace (list of
    integer basic block IDs), and message bytes (list of integer byte
    values)."""
    lines = d.splitlines()
    assert len(lines) == 5
    message_line = lines[3]
    trace_line = lines[4]
    direction = message_line.split("]")[0].split("[")[-1] # SEND or RECV
    assert direction == "SEND" or direction == "RECV"
    if direction == "SEND":
        direction = 'c2s'
    else:
        direction = 's2c'
    message_bytes_hex = message_line.split("]")[-2].rstrip(":, ").split(":")
    message_bytes = [int(b, 16) for b in message_bytes_hex]
    trace = [int(b) for b in trace_line.split()[-1].split(',')]
    return direction, trace, message_bytes

def csv_output_line(exec_num, round_num, direction, trace, msg):
    """Create CSV output line given the parsed cliverstats data."""
    output_line = []
    output_line.append(str(exec_num))
    output_line.append(str(round_num))
    output_line.append(direction)
    output_line.append("|".join([str(x) for x in trace]))
    output_line.append(":".join([str(x) for x in msg]))
    return ",".join(output_line)

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
