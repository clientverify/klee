#!/usr/bin/python

import sys
import argparse
import doctest

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
    
    helptext = """Read CSV data containing cluster ids from a
    deduplicated version of the training data, as well as the dupemap
    (see dedupe.py).  Output a file with the same number of lines as
    the original data, but where each original row of the training
    data is replaced by an integer cluster id.

    Example: %(prog)s cluster_ids_msg.csv dupemap_msg.csv >
                      cluster_ids_orig_msg.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('clusterfile', type=argparse.FileType('r'),
                        help="""cluster assignment of each canonical
                                representative (from deduping)""")
    parser.add_argument('dupemapfile', type=argparse.FileType('r'),
                        help="""file that maps duplicates to
                                canonicals. the line '5,0' means that
                                line 5 in the original training file
                                maps to line 0 of the deduplicated
                                training file.""")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    global args
    args = parser.parse_args()

    if args.verbose:
        print >>sys.stderr, "Loading cluster ids of canonical representatives"
    cluster_ids = []
    for line in args.clusterfile:
        cluster_ids.append(int(line.strip()))
    num_clusters = len(set(cluster_ids))
    assert sorted(list(set(cluster_ids))) == range(num_clusters)
    if args.verbose:
        print >>sys.stderr, "Loaded %d rows (assigned to %d clusters)" % \
            (len(cluster_ids), num_clusters)

    if args.verbose:
        print >>sys.stderr, "Loading duplicate mapping"
    dupemap = []
    for line in args.dupemapfile:
        orig_id, canonical_id = [int(x) for x in line.strip().split(",")]
        assert len(dupemap) == orig_id
        dupemap.append(canonical_id)
    assert sorted(list(set(dupemap))) == range(len(cluster_ids))
    if args.verbose:
        print >>sys.stderr, \
            "Loaded %d original rows that map to %d canonical representatives" \
            % (len(dupemap), len(cluster_ids))

    if args.verbose:
        print >>sys.stderr, "Assigning cluster ids to original rows"
    for canonical_id in dupemap:
        print cluster_ids[canonical_id]

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
