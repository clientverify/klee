#!/usr/bin/python

import math

###############################################################################
# http://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#Python
def levenshtein(s1, s2):
    """Compute the Levenshtein (edit) distance between two sequences.

    >>> t = ["","a","b","ab","abc","example","samples"]
    >>> dm = [[levenshtein(x,y) for y in t] for x in t]
    >>> dm
    [[0, 1, 1, 2, 3, 7, 7], [1, 0, 1, 1, 2, 6, 6], [1, 1, 0, 1, 2, 7, 7], [2, 1, 1, 0, 1, 6, 6], [3, 2, 2, 1, 0, 6, 6], [7, 6, 7, 6, 6, 0, 3], [7, 6, 7, 6, 6, 3, 0]]
    """
    if len(s1) < len(s2):
        return levenshtein(s2, s1)

    # len(s1) >= len(s2)
    if len(s2) == 0:
        return len(s1)

    previous_row = range(len(s2) + 1)
    for i, c1 in enumerate(s1):
        current_row = [i + 1]
        for j, c2 in enumerate(s2):
            insertions = previous_row[j + 1] + 1 # j+1 instead of j since previous_row and current_row are one character longer
            deletions = current_row[j] + 1       # than s2
            substitutions = previous_row[j] + (c1 != c2)
            current_row.append(min(insertions, deletions, substitutions))
        previous_row = current_row
 
    return previous_row[-1]

def levenshtein_ukkonen(s1, s2, maxdist):
    """Compute the Levenshtein (edit) distance between two sequences,
    with the Ukkonen optimization that bounds the maximum distance to
    t.  This algorithm computes the distance in O(t*min(m,n)) time and
    O(min(t,m,n)).

    Reference: Ukkonen, Esko. "Algorithms for Approximate String
    Matching."  Information and Control, International Conference on
    Foundations of Computation Theory, 64, no. 13 (January 1985):
    100118. doi:10.1016/S0019-9958(85)80046-2.

    NOTE: Fixed two mistakes in the original algorithm.

    >>> t = ["","a","b","ab","abc","example","samples"]
    >>> dm = [[levenshtein_ukkonen(x,y,3) for y in t] for x in t]
    >>> dm
    [[0, 1, 1, 2, 3, 3, 3], [1, 0, 1, 1, 2, 3, 3], [1, 1, 0, 1, 2, 3, 3], [2, 1, 1, 0, 1, 3, 3], [3, 2, 2, 1, 0, 3, 3], [3, 3, 3, 3, 3, 0, 3], [3, 3, 3, 3, 3, 3, 0]]
    """
    if len(s1) < len(s2):
        return levenshtein_ukkonen(s2, s1, maxdist)
    if len(s1) - len(s2) >= maxdist:
        return maxdist
    if maxdist >= max(len(s1), len(s2)):
        return levenshtein(s1, s2)
    if len(s2) == 0:
        return len(s1)

    # The following terrible variable names are due to the original paper.
    a = s1
    b = s2
    m = len(a)
    n = len(b)
    t = min(maxdist, max(m,n))
    p = int(math.floor((t - abs(n-m))/2.0)) # Mistake in original paper
    r = [t] * (abs(n-m)+2*p+2)
    k = -p + (n - m)
    k_prime = k
    for i in xrange(m+1):
        for j in xrange(abs(n-m)+2*p+1):
            if (i == 0) and (j+k == 0):
                r[j] = 0
            elif j+k < 0 or j+k > n: # Missing bounds check in original paper
                continue
            else:
                r[j] = min(r[j] + int(a[i-1] != b[j+k-1]),
                           r[j+1] + 1,
                           r[j-1] + 1)
        k += 1
    return min(r[abs(n-m) + 2*p + k_prime], t)

###############################################################################

if __name__ == "__main__":
    import doctest
    doctest.testmod()
