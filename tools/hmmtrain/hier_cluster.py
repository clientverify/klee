#!/usr/bin/python

import argparse
import numpy as np
import sklearn.cluster as skc
import scipy.sparse
import sys
import doctest
import math
import time

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
    return np.asarray(distmtx)

def canonicalize_ids(cluster_ids):
    """Relabel cluster ids in ascending order of first occurrence.

    >>> canonicalize_ids([3,3,2,0,0,1])
    array([0, 0, 1, 2, 2, 3])
    """
    canonical_cluster_ids = []
    mapping = {} # old -> new
    next_id = 0
    for old_id in cluster_ids:
        if old_id in mapping:
            canonical_cluster_ids.append(mapping[old_id])
        else:
            mapping[old_id] = next_id
            canonical_cluster_ids.append(next_id)
            next_id += 1
    return np.asarray(canonical_cluster_ids, dtype=int)

def compute_cluster_medoid(dm, cluster_ids, id_of_interest):
    """Example:

    >>> def dist(a,b):
    ...     return ((a[0]-b[0])**2.0 + (a[1]-b[1])**2.0)**0.5
    >>> points = [(10,10),(11,9),(9,11),(-10,-10),(-11,-9),(-9,-11)]
    >>> dm = np.asarray([[dist(x,y) for y in points] for x in points])
    >>> ac = skc.AgglomerativeClustering(2,affinity="precomputed",linkage="complete")
    >>> cluster_ids = ac.fit_predict(dm) # distance, not affinity matrix
    >>> cluster_ids = canonicalize_ids(cluster_ids)
    >>> print cluster_ids
    [0 0 0 1 1 1]
    >>> print compute_cluster_medoid(dm, cluster_ids, 0)
    0
    >>> print compute_cluster_medoid(dm, cluster_ids, 1)
    3
    """
    members = np.argwhere(cluster_ids == id_of_interest).flatten()
    sliced_dm = dm[members[:,np.newaxis], members]
    mean_distances = np.mean(sliced_dm, axis=1)
    medoid = members[np.argmin(mean_distances)]
    assert cluster_ids[medoid] == id_of_interest
    if args.verbose:
        print >>sys.stderr, \
            "Cluster %d\t(size = %d)\tavg distance to medoid = %s" % \
            (id_of_interest, len(members), str(np.min(mean_distances)))
    return medoid

def find_threshold(dm):
    """Find the theoretical threshold of the graph represented by
    distance matrix dm that still retains a single connected component
    with high probability, i.e. approximately n log n edges"""
    distances = []
    for i in xrange(len(dm)):
        for j in xrange(i):
            distances.append(dm[i,j])
    distances.sort()
    n = float(len(dm))
    idx = int(round(2.0 * n * math.log(n)))
    if idx >= len(distances):
        idx = len(distances) - 1
    return distances[idx]

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
    parser.add_argument('-f', '--fast', action='store_true',
                        help="trade off some accuracy to cluster quickly")
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

    # Hierarchical clustering with "average" linkage
    t0 = time.time()
    if args.verbose:
        print >>sys.stderr, "Clustering into %d clusters" % args.nclusters
    if args.fast:
        print >>sys.stderr, "Using faster (less accurate) clustering method"
        thresh = find_threshold(dm)
        print >>sys.stderr, "Thresholding at max distance", thresh
        conn_graph = (dm <= thresh).astype(int)
        def distance_func(x,y):
            return dm[x[0], y[0]]
        ac = skc.AgglomerativeClustering(n_clusters=args.nclusters,
                                         connectivity=conn_graph,
                                         affinity=distance_func,
                                         linkage="average")
        cluster_ids = ac.fit_predict(np.arange(len(dm)).reshape(len(dm),1))
    else:
        ac = skc.AgglomerativeClustering(n_clusters=args.nclusters,
                                         affinity="precomputed",
                                         linkage="average")
        cluster_ids = ac.fit_predict(dm) # distance, not affinity matrix
    cluster_ids = canonicalize_ids(cluster_ids)
    assert len(cluster_ids) == len(dm)
    t1 = time.time()
    if args.verbose:
        print >>sys.stderr, "Clustering completed in %f seconds." % (t1-t0)

    # Compute and print medoid indices to file
    medoid_indices = []
    if args.medoids:
        for i in xrange(args.nclusters):
            medoid_index = compute_cluster_medoid(dm,cluster_ids,i)
            medoid_indices.append(medoid_index)
            print >>args.medoids, medoid_index + 1 # 1-indexed in output file

    # "Re-assign" every data point to nearest medoid
    distances_to_medoids = dm[medoid_indices,:]
    cluster_reassignments = np.argmin(distances_to_medoids, axis=0)
    for c in cluster_reassignments:
        print c

    if args.verbose:
        num_reassigned = 0
        for i in xrange(len(cluster_ids)):
            if cluster_ids[i] != cluster_reassignments[i]:
                num_reassigned += 1
        print >>sys.stderr, "%d out of %d reassigned to different cluster" % \
            (num_reassigned, len(cluster_ids))

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
