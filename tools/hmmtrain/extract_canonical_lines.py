#!/usr/bin/python

import sys
import argparse

###############################################################################

def main():
    
    helptext = """Using the duplication map (see dedupe.py), extract
    lines from the target file corresponding to the canonical
    representatives.

    Example: %(prog)s dupemap_msg.csv tpath_files.txt >
                      tpath_files_msg.txt
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('dupemapfile', type=argparse.FileType('r'),
                        help="""file that maps duplicates to
                                canonicals. the line '5,0' means that
                                line 5 in the original training file
                                maps to line 0 of the deduplicated
                                training file.""")
    parser.add_argument('targetfile', type=argparse.FileType('r'),
                        help="""target file from which to extract lines""")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    global args
    args = parser.parse_args()

    if args.verbose:
        print >>sys.stderr, "Loading duplicate mapping...",
    dupemap = []
    canonical2linenum = {}
    for line in args.dupemapfile:
        orig_id, canonical_id = [int(x) for x in line.strip().split(",")]
        assert len(dupemap) == orig_id
        dupemap.append(canonical_id)
        if canonical_id not in canonical2linenum:
            canonical2linenum[canonical_id] = orig_id
    num_canonicals = max(dupemap)+1
    assert sorted(list(set(dupemap))) == range(num_canonicals)
    if args.verbose:
        print >>sys.stderr, \
            "%d original rows mapping to %d canonical representatives" \
            % (len(dupemap), num_canonicals)

    if args.verbose:
        print >>sys.stderr, "Loading target file...",
    lines = []
    for line in args.targetfile:
        lines.append(line.strip())
    assert len(lines) == len(dupemap)
    if args.verbose:
        print >>sys.stderr, "%d lines" % len(lines)

    if args.verbose:
        print >>sys.stderr, "Extracting %d canonical lines" % num_canonicals
    for i in xrange(num_canonicals):
        print lines[canonical2linenum[i]]

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
