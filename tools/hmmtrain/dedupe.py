#!/usr/bin/python

import sys
import argparse
import difflib
import os
import gc
import time
import doctest
from collections import defaultdict


###############################################################################

def load_training(f):
    data = []
    for line in f:
        [exec_num, round_num, direction, trace_string, msg_string] = \
            line.strip().split(",")
        trace = [int(x) for x in trace_string.split("|")]
        msg = [int(x) for x in msg_string.split(":")]
        data.append([exec_num, round_num, direction, trace, msg])
    return data

def write_training_row(row, f):
    exec_num = row[0]
    round_num = row[1]
    direction = row[2]
    trace = row[3]
    msg = row[4]
    trace_str = "|".join([str(x) for x in trace])
    msg_str = ":".join([str(x) for x in msg])
    print >>f, ",".join([exec_num, round_num, direction, trace_str, msg_str])

def write_training(data, f):
    for row in data:
        write_training_row(row, f)

def write_dupe_map(dupe_map, f):
    for i,j in enumerate(dupe_map):
        print >>f, ",".join([str(i), str(j)])

###############################################################################

def main():
    
    helptext = """Read CSV data containing Cliver training rounds (one
    per line), and produce a deduplicated version of the training data
    as output, where deduplication may computed on either fragments
    (basic blocks) or messages (bytes).  Also write a file that
    captures the mapping of duplicates to canonical representatives.

    Example: %(prog)s training.csv training_dedupe_msg.csv dupemap_msg.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('infile', type=argparse.FileType('r'),
                        help="""Original training file csv""")
    parser.add_argument('outfile', type=argparse.FileType('w'),
                        help="""Deduplicated training file csv""")
    parser.add_argument('dupemapfile', type=argparse.FileType('w'),
                        help="""write mapping of duplicates to
                                canonicals to file. the line '5,0'
                                means that line 5 in the original
                                training file maps to line 0 of the
                                deduplicated training file.""")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-t', '--type', metavar='T', default='message',
                        choices=['message','fragment'],
                        help="""Deduplicate on data type
                                %(metavar)s. Choices: %(choices)s.
                                (default: %(default)s)""")
    parser.add_argument('-m', '--metric', metavar='M', default='exact',
                        choices = ['exact', 'Jaccard'],
                        help="""Use metric %(metavar)s. Choices: %(choices)s.
                                (default: %(default)s)""")
    parser.add_argument('-l', '--headerlen', metavar='L', type=int, default=-1,
                        help="""[messages only] consider only first
                        %(metavar)s bytes of header, where -1 means to
                        use the entire header (default: %(default)s)""")
    parser.add_argument('-r', '--recvdup', action='store_true',
                        help="consider all RECV (s2c) messages duplicates")
    parser.add_argument('-s', '--selftest', action='store_true',
                        help="""Run self-tests""")
    global args
    args = parser.parse_args()
    if args.selftest:
        sys.exit(doctest.testmod(verbose=True)[0])

    if args.verbose:
        print >>sys.stderr, "Loading training data"
    t0 = time.time()
    training_data = load_training(args.infile)
    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Loaded %d training rows in %f seconds" % \
            (len(training_data),t1-t0)

    #### Run deduplication ####
    t0 = time.time()
    if args.verbose:
        print >>sys.stderr, "Deduplicating %ss using %s metric." % \
            (args.type, args.metric)

    # Map: relevant_data -> index of canonical representative
    canonical_map = {}
    # Map: original index -> index of canonical representative
    dupe_map = []
    # Deduped data
    deduped_training_data = []
    # Dupe count
    dupes_removed = 0
    dupes_c2s_removed = 0
    dupes_s2c_removed = 0
    original_c2s = 0
    original_s2c = 0
    
    for row in training_data:
        
        # Extract relevant columns (direction and fragment/message)
        direction = row[2]
        if direction == "c2s":
            original_c2s += 1
        elif direction == "s2c":
            original_s2c += 1
        if args.type == "fragment":
            relevant_data = row[3]
        elif args.type == "message":
            if args.headerlen == -1:
                relevant_data = row[4]
            else:
                relevant_data = row[4][:args.headerlen]
            if args.recvdup and direction == "s2c":
                relevant_data = []
        if args.metric == 'Jaccard':
            relevant_data = tuple(sorted(list(set(relevant_data))))
        elif args.metric == 'exact':
            relevant_data = tuple(relevant_data)
        relevant_data = (direction, relevant_data)

        # Check for duplicate and establish mappings
        if relevant_data in canonical_map:
            dupe_map.append(canonical_map[relevant_data])
            dupes_removed += 1
            if relevant_data[0] == "c2s":
                dupes_c2s_removed += 1
            elif relevant_data[0] == "s2c":
                dupes_s2c_removed += 1
        else:
            deduped_training_data.append(row)
            canonical_index = len(deduped_training_data) - 1
            canonical_map[relevant_data] = canonical_index
            dupe_map.append(canonical_index)

    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Removed %d duplicates (%d remain) in %f seconds." \
            % (dupes_removed, len(deduped_training_data), (t1-t0))
        print >>sys.stderr, "  c2s: %d removed, %d remain" % \
            (dupes_c2s_removed, original_c2s - dupes_c2s_removed)
        print >>sys.stderr, "  s2c: %d removed, %d remain" % \
            (dupes_s2c_removed, original_s2c - dupes_s2c_removed)

    #### Write output ####
    if args.verbose:
        print >>sys.stderr, "Saving training data"
    t0 = time.time()
    write_training(deduped_training_data, args.outfile)
    write_dupe_map(dupe_map, args.dupemapfile)
    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Saved %d rows in %f seconds" % \
            (len(deduped_training_data),t1-t0)

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
