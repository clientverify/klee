#!/usr/bin/python

import argparse
import sys
import doctest
import numpy as np

###############################################################################
def read_states_and_emissions(f, num_states, num_emis):
    states = []
    emissions = []
    for line in f:
        if not line.strip():
            continue
        fields = [int(x) for x in line.strip().split(",")]
        assert len(fields) == 2
        [state, emission] = fields
        assert (0 <= state) and (state < num_states)
        assert (0 <= emission) and (emission < num_emis)
        states.append(state)
        emissions.append(emission)
    return [states, emissions]

def read_session_ids(f):
    """Read the session IDs only (CSV contains session,round)"""
    s = []
    for line in f:
        if not line.strip():
            continue
        session_id = int(line.strip().split(",")[0])
        s.append(session_id)
    return s

def split_into_sessions(session_ids, data_pairs):
    sessions = {}
    for sid, dp in zip(session_ids, data_pairs):
        if sid not in sessions:
            sessions[sid] = [dp]
        else:
            sessions[sid].append(dp)
    return sessions

def count_transitions(sessions, num_states, num_emis):
    priors_counts = np.zeros(num_states, dtype=int)
    trans_counts = np.zeros((num_states, num_states), dtype=int)
    emis_counts = np.zeros((num_states, num_emis), dtype=int)
    for sid in sorted(sessions.keys()):
        session = sessions[sid]
        for i, [state, emission] in enumerate(session):
            if i == 0:
                priors_counts[state] += 1
                emis_counts[state,emission] += 1
            else:
                trans_counts[prev_state,state] += 1
                emis_counts[state,emission] += 1
            prev_state = state
    return [priors_counts, trans_counts, emis_counts]

def normalize_rows(x):
    if x.ndim == 1:
        s = np.sum(x)
        norm_x = x.astype(float) / s
        return np.nan_to_num(norm_x)
    elif x.ndim == 2:
        s = np.sum(x, axis=1)
        norm_x = x.astype(float) / s[:, None]
        return np.nan_to_num(norm_x)
    else:
        raise Exception("matrix has too many dimensions")

###############################################################################

def main():
    
    helptext = """Given a CSV file (on stdin) of states and emissions,
    output a vector of initial probabilities for states, a transition
    matrix, and an emission matrix.  Each row in the CSV is a
    "state,emission" pair where the each state and emission is
    identified by a zero-indexed integer.

    Example: %(prog)s 20 15 -s sessions.csv < states_and_emissions.csv
    """
    parser = argparse.ArgumentParser(description=helptext)
    parser.add_argument('nstates', type=int,
                        help="number of states")
    parser.add_argument('nemissions', type=int,
                        help="number of emission types")
    parser.add_argument('-v', '--verbose', action='store_true',
                        help="be more chatty on stderr")
    parser.add_argument('-s', '--sessions', type=argparse.FileType('r'),
                        help="'sessions and rounds' CSV file")
    parser.add_argument('-t', '--pseudotr', metavar='T', type=float,
                        default=0.0,
                        help="""pseudotransitions: pad transition
                        matrix with small (expected) number of
                        transitions in data set, to avoid zero entries""")
    parser.add_argument('-e', '--pseudoe', metavar='E', type=float,
                        default=0.0,
                        help="""pseudoemissions: pad emission matrix
                        with small (expected) number of emissions in
                        data set, to avoid zero entries""")
    parser.add_argument('-T', '--selftest', action='store_true',
                        help="Run self-tests")
    global args
    args = parser.parse_args()
    if args.selftest:
        sys.exit(doctest.testmod(verbose=True)[0])

    states, emissions = \
        read_states_and_emissions(sys.stdin, args.nstates, args.nemissions)
    data_pairs = zip(states, emissions)
    if args.verbose:
        print >>sys.stderr, "Loaded sequence of %d states/emissions" % \
            len(data_pairs)

    # Split into sessions if information is provided.
    if args.sessions:
        session_ids = read_session_ids(args.sessions)
    else:
        session_ids = [0]*len(data_pairs)
    sessions = split_into_sessions(session_ids, data_pairs)
    if args.verbose:
        print >>sys.stderr, "Data split into %d sessions" % len(sessions)

    # Count transitions and emissions
    if args.verbose:
        print >>sys.stderr, "Counting transitions and emissions"
    priors_counts, trans_counts, emis_counts = \
        count_transitions(sessions, args.nstates, args.nemissions)

    # Add pseudotransitions and emissions
    if args.verbose:
        print >>sys.stderr, "Adding pseudotransitions and pseudoemissions"
    priors_counts = priors_counts.astype(float) + args.pseudotr
    trans_counts = trans_counts.astype(float) + args.pseudotr
    emis_counts = emis_counts.astype(float) + args.pseudoe

    # Normalize the matrices (so that rows sum to probability 1)
    if args.verbose:
        print >>sys.stderr, "Normalizing HMM matrices"
    priors = normalize_rows(priors_counts)
    trans = normalize_rows(trans_counts)
    emis = normalize_rows(emis_counts)

    # Create output of the following format
    #
    # NumStates: 2
    # NumEmissions: 6
    # Priors:
    # 0.4 0.6 
    # TransitionMatrix:
    # 0.95 0.05 
    # 0.1 0.9 
    # EmissionMatrix:
    # 0.166667 0.166667 0.166667 0.166667 0.166667 0.166667 
    # 0.01 0.01 0.01 0.01 0.01 0.95
    if args.verbose:
        print >>sys.stderr, "Training finished, creating HMM output."
    print "NumStates: %d" % args.nstates
    print "NumEmissions: %d" % args.nemissions
    print "Priors:"
    print " ".join([str(x) for x in priors])
    print "TransitionMatrix:"
    for row in trans:
        print " ".join([str(x) for x in row])
    print "EmissionMatrix:"
    for row in emis:
        print " ".join([str(x) for x in row])

    return 0

###############################################################################

if __name__ == "__main__":
    sys.exit(main())
