#!/usr/bin/python

from multiprocessing import Pool
import multiprocessing
import subprocess
import sys
import argparse
import difflib
import os
import gc
import time
import doctest
from levenshtein import *
from message_features import weighted_histogram
from collections import defaultdict

###############################################################################

# CONSTANT (IMPORTANT)
MAX_DIST_VALUE = 256

# GLOBAL VARIABLE (IMPORTANT)!
s_matcher = difflib.SequenceMatcher(None, [], [], autojunk=False)

def levenshtein_lower_bound(s1, s2): # assume s1 changes rarely, s2 frequently
    s_matcher.set_seq2(s1)
    s_matcher.set_seq1(s2)
    return (1.0 - s_matcher.real_quick_ratio()) * max(len(s1), len(s2))

def quick_levenshtein(s1, s2, maxdist=MAX_DIST_VALUE):
    """Quickly compute a Levenshtein distance with a certain bound.

    >>> a = range(10**6)
    >>> b = range(200)
    >>> c = range(400)
    >>> d = range(2, 10**6 - 10**3)
    >>> quick_levenshtein(a,a,200)
    0
    >>> quick_levenshtein(a,b,200)
    200
    >>> quick_levenshtein(a,c,200)
    200
    >>> quick_levenshtein(a,d,200)
    200
    >>> quick_levenshtein(b,a,200)
    200
    >>> quick_levenshtein(b,b,200)
    0
    >>> quick_levenshtein(b,c,200)
    200
    >>> quick_levenshtein(b,d,200)
    200
    >>> quick_levenshtein(c,a,200)
    200
    >>> quick_levenshtein(c,b,200)
    200
    >>> quick_levenshtein(c,c,200)
    0
    >>> quick_levenshtein(c,d,200)
    200
    >>> quick_levenshtein(d,a,200)
    200
    >>> quick_levenshtein(d,b,200)
    200
    >>> quick_levenshtein(d,c,200)
    200
    >>> quick_levenshtein(d,d,200)
    0
    >>> quick_levenshtein([1,2,3,4], [1,2,3,7,4],200)
    1
    """
    [s1, s2] = trim_common_prefix(s1, s2)
    [s1, s2] = trim_common_suffix(s1, s2)
    if abs(len(s1) - len(s2)) >= maxdist:
        return maxdist
    elif levenshtein_lower_bound(s1, s2) >= maxdist:
        return maxdist
    else:
        return levenshtein_ukkonen(s1, s2, maxdist)

def trim_common_prefix(s1, s2):
    prefix_len = len(os.path.commonprefix([s1, s2])) # doesn't just work on strings!
    return [ s1[prefix_len:], s2[prefix_len:] ]

def trim_common_suffix(s1, s2):
    s1_rev = s1[:]
    s1_rev.reverse()
    s2_rev = s2[:]
    s2_rev.reverse()
    suffix_len = len(os.path.commonprefix([s1_rev, s2_rev])) # doesn't just work on strings!
    if suffix_len > 0:
        return [s1[:-suffix_len], s2[:-suffix_len]]
    else:
        return [s1[:], s2[:]]

def jaccard(s1, s2):
    x = set(s1)
    y = set(s2)
    return 1.0 - 1.0*len(x&y)/len(x|y)

def msg_jaccard(m1, m2):
    if m1[0] != m2[0]:
        return 1.0
    else:
        return jaccard(m1[1], m2[1])

def ruzicka(s1, s2):
    """
    Compute Ruzicka distance between two histograms.

    >>> ruzicka([1, 1, 1, 0, 1], [0, 1, 0, 1, 0])
    0.8
    """
    numerator = sum(map(min, zip(s1, s2)))
    denominator = sum(map(max, zip(s1, s2)))
    if denominator == 0.0:
        return 0.0
    else:
        return 1.0 - float(numerator)/float(denominator)

def trace_ruzicka(h1, h2):
    hh1 = defaultdict(int)
    for k,v in h1.iteritems():
        hh1[k] = v
    hh2 = defaultdict(int)
    for k,v in h2.iteritems():
        hh2[k] = v
    numerator = 0
    denominator = 0
    for k in (set(hh1.keys()) | set(hh2.keys())):
        numerator += min(hh1[k], hh2[k])
        denominator += max(hh1[k], hh2[k])
    if denominator == 0:
        return 0.0
    else:
        return 1.0 - float(numerator)/float(denominator)

def msg_ruzicka(m1, m2):
    if m1[0] != m2[0]:
        return 1.0
    else:
        return ruzicka(m1[1], m2[1])

def msg_levenshtein(m1, m2, maxdist=MAX_DIST_VALUE):
    if m1[0] != m2[0]:
        return maxdist
    else:
        return quick_levenshtein(m1[1], m2[1], maxdist)

def quickdiff(s1, s2):
    s = difflib.SequenceMatcher(None,s1,s2)
    return 1.0 - s.quick_ratio()

def build_histogram(seq): # map: element -> count
    histogram = defaultdict(int)
    for s in seq:
        histogram[s] += 1
    return histogram

###############################################################################

def compute_distance_row(work_item, distFunc):
    x = work_item
    return [distFunc(x, y) for y in global_point_vector]

def compute_distance_row_jaccard(work_item):
    return compute_distance_row(work_item, jaccard)

def compute_distance_row_ruzicka(work_item):
    return compute_distance_row(work_item, trace_ruzicka)

def compute_msg_distance_row_jaccard(work_item):
    return compute_distance_row(work_item, msg_jaccard)

def compute_distance_row_levenshtein(work_item):
    return compute_distance_row(work_item, quick_levenshtein)

def compute_msg_distance_row_levenshtein(work_item):
    return compute_distance_row(work_item, msg_levenshtein)

def compute_msg_distance_row_ruzicka(work_item):
    return compute_distance_row(work_item, msg_ruzicka)

def compute_distance_row_quickdiff(work_item):
    return compute_distance_row(work_item, quickdiff)

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

def write_distmtx(dm, f):
    for row in dm:
        print >>f, ",".join([str(x) for x in row])

###############################################################################

def main():
    
    helptext = """Read CSV data on stdin containing Cliver training
    rounds (one per line), and produce a distance matrix of the
    requested type.  Leverage multiple processors, if available.

    The distance matrix can be computed on either the execution
    fragments or the messages.  The metric used can be any of
    Levenshtein, Jaccard, or Ruzicka (messages only).  Messages are
    partitioned into client-to-server vs server-to-client messages,
    and the distance between two messages sent in opposite directions
    is the maximum distance allowed in the range.

    Example: %(prog)s < training.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-f', '--fragment', action='store_true',
                        help="""compute distance matrix on execution fragments
                                (default: messages)""")
    parser.add_argument('-m', '--metric', metavar='M', default='Jaccard',
                        choices = ['Jaccard', 'mJaccard',
                                   'Levenshtein', 'Ruzicka'],
                        help="""Use metric %(metavar)s. Choices: %(choices)s.
                                (default: %(default)s)""")
    parser.add_argument('-l', '--headerlen', metavar='L', type=int, default=40,
                        help="""[Ruzicka] weighted histogram is biased
                                toward the first %(metavar)s bytes (default:
                                %(default)s)""")
    parser.add_argument('-s', '--selftest', action='store_true',
                        help="""Run self-tests""")
    global args
    args = parser.parse_args()
    if args.metric == "Ruzicka" and args.fragment:
        parser.error("The Ruzicka metric must be used with messages")
    if args.fragment:
        distance_matrix_type = "fragment"
    else:
        distance_matrix_type = "message"
    distance_matrix_type += " " + args.metric
    if args.selftest:
        sys.exit(doctest.testmod(verbose=True)[0])

    global global_point_vector

    if args.verbose:
        print >>sys.stderr, "Loading training data"
    t0 = time.time()
    data = load_training(sys.stdin)
    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Loaded %d traces in %f seconds" % (len(data),t1-t0)

    num_workers = max(multiprocessing.cpu_count() - 2, 1)
    os.nice(10) # set myself to low priority

    t0 = time.time()
    if args.verbose:
        print >>sys.stderr, \
            "Computing %d x %d %s distance matrix using %d processors" % \
            (len(data), len(data), distance_matrix_type, num_workers)
        if args.metric == "Ruzicka":
            print >>sys.stderr, "Ruzicka: using estimated header length of %d" \
                % args.headerlen

    if args.fragment and args.metric == 'Jaccard': # Fragment Jaccard
        traces = [set(x[3]) for x in data]
        global_point_vector = traces
        dist_func = compute_distance_row_jaccard
    elif args.fragment and args.metric == 'mJaccard': # Frag multiset Jaccard
        traces = [build_histogram(x[3]) for x in data]
        global_point_vector = traces
        dist_func = compute_distance_row_ruzicka
    elif args.fragment and args.metric == 'Levenshtein': # Fragment Levenshtein
        traces = [x[3] for x in data]
        global_point_vector = traces
        dist_func = compute_distance_row_levenshtein
    elif not args.fragment and args.metric == 'Jaccard': # Message Jaccard
        msgs = [ [x[2], set(x[4])] for x in data]
        global_point_vector = msgs
        dist_func = compute_msg_distance_row_jaccard
    elif not args.fragment and args.metric == 'Levenshtein': # Message Lev
        msgs = [ [x[2], x[4]] for x in data]
        global_point_vector = msgs
        dist_func = compute_msg_distance_row_levenshtein
    elif not args.fragment and args.metric == 'Ruzicka': # Message Ruzicka
        msgs = [ [x[2], weighted_histogram(x[4],args.headerlen)] for x in data]
        global_point_vector = msgs
        dist_func = compute_msg_distance_row_ruzicka

    p = Pool(num_workers)
    distmtx = p.map(dist_func, global_point_vector, chunksize=1)
    p.close()
    p.join()
    write_distmtx(distmtx, sys.stdout)
    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Distance matrix (%dx%d) computed in %f seconds." \
            % (len(distmtx), len(distmtx[0]), (t1-t0))

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
