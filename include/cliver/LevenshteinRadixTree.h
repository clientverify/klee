//===-- LevenshteinRadixTree.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_LEVENSHTEIN_RADIX_TREE_H
#define CLIVER_LEVENSHTEIN_RADIX_TREE_H

#include "cliver/RadixTree.h"
#include <limits.h>

#include <vector>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

/// LevenshteinElement: Represents two cells of the dynamic programming matrix
/// for computing the Levenshtein edit distance between two sequences. Since we
/// are only concerned with computing the edit distance and not the actual edits
/// required, only the current and previous rows of the matrix are recorded.
template< class T >
class LevenshteinElement {
 public:
  T e; // A single element of the sequence
  int d[2]; // Current and previous values from the DP matrix

  LevenshteinElement(T _e, int _d = 0) { 
    e = _e;
    d[0] = d[1] = _d;
  }

  LevenshteinElement(T _e, int _d0, int _d1) { 
    e = _e;
    d[0] = _d0;
    d[1] = _d1;
  }

  LevenshteinElement() {
    d[0] = d[1] = 0;
  }

  bool operator==(const LevenshteinElement& le) const { return (e == le.e); }
  bool operator!=(const LevenshteinElement& le) const { return (e != le.e); }
  bool operator< (const LevenshteinElement& le) const { return (e < le.e); }
};

/// Print a LevenshteinElement
template< class T > 
std::ostream& operator<<(std::ostream& os, const LevenshteinElement<T>& le) {
  os << "(" << le.e << "," << le.d[0] << ", " << le.d[1] << ")";
}

/// LevenshteinSequenceComparator: used by RadixTree to insert/lookup sequences
template <class T> 
class LevenshteinSequenceComparator {
 public:
  typedef typename std::vector<LevenshteinElement<T> >::iterator LIterator;

  /// Find size of matching prefix, only compares LevenshteinElement::e
  static int prefix_match(LIterator first1, LIterator last1,
                          LIterator first2, LIterator last2) {
    LIterator it1 = first1;
    LIterator it2 = first2;

    int count = 0;
    while (it1 != last1 && it2 != last2) {
      if ((*it1).e != (*it2).e)
        return count;
      ++it1;
      ++it2;
      ++count;
    }
    return count;
  }

  /// Alternative to std::equal, only compares LevenshteinElement::e
  static bool equal(LIterator first1, LIterator last1,
                    LIterator first2 ) {
    LIterator it1 = first1;
    LIterator it2 = first2;

    int count = 0;
    while (it1 != last1) {
      if ((*it1).e != (*it2).e)
        return false;
      ++it1;
      ++it2;
    }
    return true;
  }
};

////////////////////////////////////////////////////////////////////////////////

/// LevenshteinRadixTree: Used to compute the edit distance from a single
/// sequence s to a collection of seqeunces stored in a RadixTree. Uses common
/// prefixes in the collection to reduce computation

template <class Sequence, class T> 
class LevenshteinRadixTree 
: public RadixTree<std::vector<LevenshteinElement<T> >, 
                   LevenshteinElement<T>,
                   LevenshteinSequenceComparator<T> > {

 public:
  typedef LevenshteinElement<T> LElement;
  typedef std::vector<LevenshteinElement<T> > LSequence;
  typedef LevenshteinSequenceComparator<T> LComparator; 
  typedef RadixTree<LSequence, LElement, LComparator> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::EdgeMapIterator EdgeMapIterator;
 
#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

  /// Default Constructor
  LevenshteinRadixTree() : row_(0) { 
    this->root_ = new Node();
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {
    LevenshteinRadixTree *lrt 
      = new LevenshteinRadixTree(this->clone_node(this->root_));
    lrt->row_ = this->row_;
    return lrt;
  }

  /// Return true if tree contains s
  virtual bool lookup(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (int i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the Levenshtein sequence
    if (Node *res = this->lookup_private(ls))
      return true;
    return false;
  }

  // Insert new sequence into this radix tree
  virtual Node* insert(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (int i=0; i<s.size() + 1; ++i)
      ls[i] = (i == 0) ? LElement(0, i): LElement(s[i - 1], i);

    // Insert newly created LevenshteinSequence
    return this->root_->insert(ls);
  }

  /// Remove a sequence s from this tree, s will not be removed if it is only
  /// a prefix match
  virtual bool remove(Sequence &s) {
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size()+1);
    for (int i=0; i<s.size(); ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the node matching s
    Node *node = this->lookup_private(ls, /*exact = */ true);

    // Remove this node
    return this->remove_node(node);
  }

  /// Lookup the cost associated with edit distance to s
  int lookup_cost(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (int i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Find an exact match for the Levenshtein sequence ls
    if (Node *res = this->lookup_private(ls, /*exact = */ true)) {
      LElement& e = res->parent_edge()->last_element();

      // Return the most recently computed edit distance
      return e.d[row_ % 2];

    // If an exact match is not found, find a prefix match
    } else if (Node *res = this->lookup_private(ls)) {
      Edge *edge = res->parent_edge();

      // Compute the size of the mismatch suffix stored in the tree
      size_t diff = res->depth() - ls.size();

      // Find the element that corresponds to the last element of s
      LElement& e = (*edge)[edge->size() - diff - 1];

      // Return the most recently computed edit distance
      return e.d[row_ % 2];
    }
    return -1 ;
  }

  /// Compute the minimum edit distance from s to all sequences in the tree
  int min_edit_distance(Sequence &s) {
    // If row is not zero, we reset any previous distance values to be the
    // first row in the DP matrix
    if (row_ != 0)
      reset();
    return min_edit_distance_suffix(s);
  }

  /// Compute the minimum edit distance from s_update to all sequences in the tree
  /// where s_update is equal to the previously computed sequence + suffix
  int min_edit_distance_update(Sequence &s_update) {
    Sequence suffix(s_update.begin() + row_, s_update.end());
    return min_edit_distance_suffix(suffix);
  }

  /// Compute the minimum edit distance from s' to all sequences in the tree
  /// where s' is equal to the previously computed sequence + t
  int min_edit_distance_suffix(T t) {
    Sequence suffix;
    suffix.insert(suffix.end(), t);
    return min_edit_distance_suffix(suffix);
  }

  /// Compute the minimum edit distance from s' to all sequences in the tree
  /// where s' is equal to the previously computed sequence + s
  int min_edit_distance_suffix(Sequence &s) {
    int min_cost = INT_MAX;

    // Perform |s| traversals of the RadixTree to compute the updated edit
    // distance
    for (int j=0; j < s.size(); ++j) {
      std::stack<Node*> nodes;
      nodes.push(this->root_);

      // Increment member variable row_ 
      row_++;

      // curr is always 0 or 1
      int curr = row_ % 2;
      int prev = curr ^ 1;

      // The current element of sequence s
      T t = s[j];

      while (!nodes.empty()) {
        Node* n = nodes.top();
        nodes.pop();

        foreach_edge(n, it) {
          Edge *edge = it->second;

          // e0 is the previous cell, e1 is the current cell
          LElement *e0, *e1; 

          // For each element of the edge
          for (int i=0; i<edge->size(); ++i) {

            e1 = &((*edge)[i]);

            // If this is the first column of the DP matrix, d = row
            if (i == 0 && n == this->root_) {
              e1->d[curr] = row_;

            // Otherwise compute new d
            } else {

              // If this is the first element of the edge, the previous cell
              // e0 is the last element of the parent edge
              if (i == 0)
                e0 = &(n->parent_edge()->last_element());
              else
                e0 = &((*edge)[i-1]);

              // Compute minimum cost of insert or delete
              int ins_or_del 
                = std::min(e0->d[curr] + 1, e1->d[prev] + 1);

              // Compute minimum cost of replace or match if equal
              int match_or_replace = (t == e1->e) ? e0->d[prev] : e0->d[prev]+1;

              // Store the minimum cost operation
              e1->d[curr] = std::min(ins_or_del, match_or_replace);
            }
          }

          // Update the minimum cost if we have reached a leaf node and are at
          // the end of s, otherwise push the next node onto the stack
          if (j == (s.size()-1) && edge->to()->leaf() && (e1->d[curr] < min_cost) )
            min_cost = e1->d[curr];
          else
            nodes.push(edge->to());
        }
      }
    }
    return min_cost;
  }

  // Reset the distance values, i.e. fill out the top row in the DP matrix
  void reset() {
    row_ = 0;
    std::stack<Node*> nodes;
    nodes.push(this->root_);
    while (!nodes.empty()) {
      Node* n = nodes.top();
      nodes.pop();
      foreach_edge(n, it) {
        Edge *edge = it->second;
        int depth = n->depth();
        for (int i=0; i<edge->size(); ++i) {
          (*edge)[i].d[0] = depth++;
        }
        if (!edge->to()->leaf())
          nodes.push(edge->to());
      }
    }
  }

 private:

  LevenshteinRadixTree(Node *root) : row_(0) {
    this->root_ = root;
  }

  int row_;
};

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_LEVENSHTEIN_RADIX_TREE_H

