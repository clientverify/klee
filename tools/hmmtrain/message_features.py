#!/usr/bin/python

import sys
import argparse
import math

###############################################################################

def main():
    helptext = """Reads CSV data on stdin containing Cliver training
    rounds (one per line), and produces a message feature vector for
    each round.  The message feature vector can be either a weighted
    histogram, or a sequence of binary features indicating
    (position:bytevalue).  The default type is a histogram feature
    vector.

    Example: %(prog)s < training.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-l', '--headerlen', metavar='L', type=int, default=40,
                        help="""[histogram feature option] approximate header
                                length, so that weighted histogram is
                                biased toward the first L bytes
                                (default = 40)""")
    parser.add_argument('-b', '--binary', action='store_true',
                        help="""use binary features indicating
                                position:bytevalue (default is histogram)""")
    parser.add_argument('-m', '--maxfeat', metavar='M', type=int, default=500,
                        help="""[binary feature option] maximum number of
                                binary features to use (default = 500)""")
    global args
    args = parser.parse_args()

    # default case: weighted histogram feature vector
    if not args.binary:
        if args.verbose:
            print >>sys.stderr, "Weighted histogram feature vectors with " + \
                "estimated header length %d" % args.headerlen
        for line in sys.stdin:
            message_bytes = get_message_bytes(line)
            fv = [get_direction(line), len(message_bytes)]
            hist = weighted_histogram(message_bytes, args.headerlen)
            fv.extend(hist)
            print ",".join([str(x) for x in fv])
            
    # binary feature vector
    else:
        if args.verbose:
            print >>sys.stderr, "Binary feature vectors of " + \
                "maximum length %d" % args.maxfeat
        
    return 0

def weighted_histogram(s, headerlen):
    """Compute the weighted histogram of byte values, using a decay
    rate adjusted for the given header length."""
    decay_rate = math.pow(0.5, 1.0/headerlen)
    h = [0.0] * 256
    weight = 1.0
    for x in s:
        h[x] += weight
        weight *= decay_rate
    return h

def get_message_bytes(csvline):
    fields = csvline.split(",")
    message_bytes = [int(x) for x in fields[4].split(":")]
    return message_bytes

def get_direction(csvline):
    """Return 1 for client-to-server, -1 for server-to-client."""
    fields = csvline.split(",")
    direction = fields[2]
    assert direction == 'c2s' or direction == 's2c'
    if direction == 'c2s':
        return 1
    else:
        return -1

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
