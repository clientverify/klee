#!/usr/bin/python

import argparse
import sklearn
import sys
import doctest

###############################################################################

def read_distmtx(f):
    distmtx = []
    for line in f:
        if not line.strip():
            continue
        distrow = [float(x) for x in line.strip().split(",")]
        distmtx.append(distrow)
    if len(distmtx) == 0:
        raise EOFError("empty distance matrix")
    if len(distmtx) != len(distmtx[0]):
        raise Exception("non-square distance matrix")
    return distmtx

###############################################################################

def main():
    
    helptext = """Read distance matrix in CSV format on
    stdin. Agglomeratively cluster into a specified number of
    clusters, and print zero-indexed cluster IDs on stdout.

    Example: %(prog)s nclusters < distmtx.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('nclusters', type=int,
                        help="number of clusters")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-m', '--medoids', metavar='M',
                        type=argparse.FileType('w'),
                        help="write medoid indices (1-indexed) to file M")
    parser.add_argument('-s', '--selftest', action='store_true',
                        help="Run self-tests")
    global args
    args = parser.parse_args()
    if args.selftest:
        sys.exit(doctest.testmod(verbose=True)[0])

    dm = read_distmtx(sys.stdin)
    if args.verbose:
        print >>sys.stderr, "Loaded %dx%d distance matrix" % \
            (len(dm), len(dm[0]))
    if args.nclusters > len(dm):
        print >>sys.stderr, \
            "Warning: nclusters (%d) exceeds number of data points (%d)" % \
            (args.nclusters, len(dm))
        args.nclusters = len(dm)

    # FIXME: do actual clustering
    if args.verbose:
        print >>sys.stderr, "Clustering into %d clusters" % args.nclusters
    for i in xrange(len(dm)):
        print i % args.nclusters
    
    # FIXME: compute and print actual medoid indices
    if args.medoids:
        for i in xrange(args.nclusters):
            print >>args.medoids, i+1

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
