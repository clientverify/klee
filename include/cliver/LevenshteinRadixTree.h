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
#include "cliver/EditDistanceTree.h"
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
  return os;
}

/// LevenshteinSequenceComparator: used by RadixTree to insert/lookup sequences
template <class T> 
class LevenshteinSequenceComparator {
 public:
  typedef typename std::vector<LevenshteinElement<T> >::iterator LIterator;

  /// Find size of matching prefix, only compares LevenshteinElement::e
  static size_t prefix_match(LIterator first1, LIterator last1,
                          LIterator first2, LIterator last2) {
    LIterator it1 = first1;
    LIterator it2 = first2;

    size_t count = 0;
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
:
  public RadixTree<std::vector<LevenshteinElement<T> >, 
                   LevenshteinElement<T>,
                   LevenshteinSequenceComparator<T> >,
  public EditDistanceTree<Sequence,T> {

  typedef LevenshteinElement<T> LElement;
  typedef std::vector<LevenshteinElement<T> > LSequence;
  typedef LevenshteinSequenceComparator<T> LComparator; 
  typedef RadixTree<LSequence, LElement, LComparator> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::SequenceIterator EdgeIterator;
  typedef typename This::EdgeMapIterator EdgeMapIterator;
 
#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  typedef Sequence sequence_type;
  typedef T element_type;

  LevenshteinRadixTree() : row_(0) { 
    this->root_ = new Node();
  }

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int k) {
    row_ = 0;
    reset();
  }

  virtual void add_data(Sequence &s) {
    this->insert(s);
  }

  virtual void update(Sequence &s_update) {
    Sequence suffix(s_update.begin() + row_, s_update.end());
    update_suffix(suffix);
  }

  /// Compute the minimum edit distance from s' to all sequences in the tree
  /// where s' is equal to the previously computed sequence + s
  virtual void update_suffix(Sequence &s) {
    min_distance_ = INT_MAX;

    // Perform |s| traversals of the RadixTree to compute the updated edit
    // distance
    for (unsigned j=0; j < s.size(); ++j) {
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
          EdgeIterator edge_it = edge->begin(), edge_ie = edge->end();
          for (; edge_it != edge_ie; ++edge_it) {

            e0 = e1;
            e1 = &(*edge_it);

            // If this is the first column of the DP matrix, d = row
            if (edge_it == edge->begin() && n == this->root_) {
              e1->d[curr] = row_;

            // Otherwise compute new d
            } else {

              // If this is the first element of the edge, the previous cell
              // e0 is the last element of the parent edge
              if (edge_it == edge->begin())
                e0 = &(n->parent_edge()->back());

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
          if (j == (s.size()-1) && edge->to()->leaf() && (e1->d[curr] < min_distance_) )
            min_distance_ = e1->d[curr];
          else
            nodes.push(edge->to());
        }
      }
    }
  }

  virtual void update_element(T t) {
    Sequence suffix;
    suffix.insert(suffix.end(), t);
    update_suffix(suffix);
  }

  virtual int min_distance() { return min_distance_; }

  virtual int row() { return row_; }

  virtual void delete_shared_data() {}

  virtual EditDistanceTree<Sequence,T>* clone_edit_distance_tree() {
    return this->clone_internal();
  }

  //===-------------------------------------------------------------------===//
  // Overrides of virtual RadixTree methods
  //===-------------------------------------------------------------------===//

  // Return true if tree contains s. Note this function hides
  // RadixTree::lookup() which takes a LevenshteinSequence as a parameter
  virtual bool lookup(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the Levenshtein sequence
    if (this->lookup_private(ls))
      return true;
    return false;
  }

  // Insert new sequence into this radix tree. Note this function hides
  // RadixTree::remove() which takes a LevenshteinSequence as a parameter.
  virtual Node* insert(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i)
      ls[i] = (i == 0) ? LElement(0, i): LElement(s[i - 1], i);

    // Insert newly created LevenshteinSequence
    return this->root_->insert(ls);
  }

  // Remove a sequence s from this tree, s will not be removed if it is only a
  // prefix match. Note this function hides RadixTree::remove() which takes a
  // LevenshteinSequence as a parameter.
  virtual bool remove(Sequence &s) {
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size()+1);
    for (unsigned i=0; i<s.size(); ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the node matching s
    Node *node = this->lookup_private(ls, /*exact = */ true);

    // Remove this node
    return this->remove_node(node);
  }

  /// Return a deep-copy of this /* only used for testing */
  virtual This* clone() {
    return this->clone_internal();
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  /// Lookup the cost associated with edit distance to s
  int lookup_edit_distance(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Find an exact match for the Levenshtein sequence ls
    if (Node *res = this->lookup_private(ls, /*exact = */ true)) {
      LElement& e = res->parent_edge()->back();

      // Return the most recently computed edit distance
      return e.d[row_ % 2];

    // If an exact match is not found, find a prefix match
    } else if (Node *res = this->lookup_private(ls)) {
      Edge *edge = res->parent_edge();

      // Compute the size of the mismatch suffix stored in the tree
      size_t diff = res->depth() - ls.size();

      // Find the element that corresponds to the last element of s
      LElement *e = &(*(edge->begin() + (edge->size() - diff - 1)));

      // Return the most recently computed edit distance
      return e->d[row_ % 2];
    }
    return -1 ;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  // Internally used constructor
  LevenshteinRadixTree(Node *root) : row_(0) {
    this->root_ = root;
  }

  /// Return a deep-copy of this RadixTree
  LevenshteinRadixTree* clone_internal() {
    LevenshteinRadixTree *lrt 
      = new LevenshteinRadixTree(this->clone_node(this->root_));
    lrt->row_ = this->row_;
    lrt->min_distance_ = this->min_distance_;
    return lrt;
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
        EdgeIterator edge_it = edge->begin(), edge_ie = edge->end(); 
        for (; edge_it != edge_ie; ++edge_it)
          edge_it->d[0] = depth++;
        if (!edge->to()->leaf())
          nodes.push(edge->to());
      }
    }
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  int min_distance_;
  int row_;
};

////////////////////////////////////////////////////////////////////////////////

//===-------------------------------------------------------------------===//
// KLevenshteinRadixTree 
//===-------------------------------------------------------------------===//
template <class Sequence, class T> 
class KLevenshteinRadixTree 
: public RadixTree<std::vector<LevenshteinElement<T> >, 
                   LevenshteinElement<T>,
                   LevenshteinSequenceComparator<T> >,
  public EditDistanceTree<Sequence,T> {


  typedef LevenshteinElement<T> LElement;
  typedef std::vector<LevenshteinElement<T> > LSequence;
  typedef LevenshteinSequenceComparator<T> LComparator; 
  typedef RadixTree<LSequence, LElement, LComparator> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::SequenceIterator EdgeIterator;
  typedef typename This::EdgeMapIterator EdgeMapIterator;
  typedef std::pair<Edge*, size_t> EdgeOffset;

 #define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  typedef Sequence sequence_type;
  typedef T element_type;

  /// Default Constructor
  KLevenshteinRadixTree() : row_(0), k_(INT_MAX) { 
    this->root_ = new Node();
  }

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int k) {
    k_ = k;
    row_ = 0;
    reset();
  }

  virtual void add_data(Sequence &s) { this->insert(s); }

  // Compute the minimum edit distance from s' to all prefix in the tree where
  // s' is equal to the previously computed sequence + s
  virtual void update(Sequence &s_update) {
    Sequence suffix(s_update.begin() + row_, s_update.end());
    update_suffix(suffix);
  }

  // Compute the minimum edit distance from s' to all prefix sequences in the
  // tree where s' is equal to the previously computed sequence + s
  virtual void update_suffix(Sequence &s) {
    for (unsigned j=0; j < s.size(); ++j) {
      update_element(s[j]);
    }
  }

  virtual void update_element(T t) {
    row_++;

    int col_start = std::max(row_ - k_, 0);

    std::vector<EdgeOffset> edge_offsets;
    get_at_depth(this->root_, col_start, edge_offsets);
    //std::cout << "Edge offset count is " <<edge_offsets.size() << " at depth" << col_start
    //    << std::endl;

    min_distance_ = min_prefix_distance_ = INT_MAX;

    for (unsigned i=0; i<edge_offsets.size(); ++i) {
      update_edge_element(t, edge_offsets[i]);
    }
  }

  virtual int min_distance() { return min_prefix_distance_; }

  virtual int row() { return row_; }

  virtual void delete_shared_data() {}

  virtual EditDistanceTree<Sequence,T>* clone_edit_distance_tree() {
    return this->clone_internal();
  }

  //===-------------------------------------------------------------------===//
  // Overrides of virtual RadixTree methods
  //===-------------------------------------------------------------------===//

  /// Return true if tree contains s
  /// Note this function hides RadixTree::lookup() which takes a
  /// LevenshteinSequence as a parameter
  virtual bool lookup(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the Levenshtein sequence
    if (this->lookup_private(ls))
      return true;
    return false;
  }

  // Insert new sequence into this radix tree. Note this function hides
  // RadixTree::remove() which takes a LevenshteinSequence as a parameter.
  // Direct insert is not supported, use EditDistance Interface method add_data
  virtual Node* insert(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i)
      ls[i] = (i == 0) ? LElement(0, i): LElement(s[i - 1], i);

    // Insert newly created LevenshteinSequence
    return this->root_->insert(ls);
  }

  // Remove a sequence s from this tree, s will not be removed if it is only a
  // prefix match. Note this function hides RadixTree::remove() which takes a
  // LevenshteinSequence as a parameter. 
  virtual bool remove(Sequence &s) {
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size()+1);
    for (unsigned i=0; i<s.size(); ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the node matching s
    Node *node = this->lookup_private(ls, /*exact = */ true);

    // Remove this node
    return this->remove_node(node);
  }

  /// Return a deep-copy of this RadixTree /* only used for testing */
  virtual This* clone() {
    return this->clone_internal();
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//
  
  int min_edit_distance() { return min_distance_; }

  int min_prefix_distance() { return min_prefix_distance_; }

  /// Lookup the edit distance associated with edit distance to s 
  int lookup_edit_distance(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (unsigned i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Find an exact match for the Levenshtein sequence ls
    if (Node *res = this->lookup_private(ls, /*exact = */ true)) {
      LElement& e = res->parent_edge()->back();

      // Return the most recently computed edit distance
      return e.d[row_ % 2];

    // If an exact match is not found, find a prefix match
    } else if (Node *res = this->lookup_private(ls)) {
      Edge *edge = res->parent_edge();

      // Compute the size of the mismatch suffix stored in the tree
      size_t diff = res->depth() - ls.size();

      // Find the element that corresponds to the last element of s
      LElement *e = &(*(edge->begin() + (edge->size() - diff - 1)));

      // Return the most recently computed edit distance
      return e->d[row_ % 2];
    }
    return -1 ;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  // Internally used constructor
  KLevenshteinRadixTree(Node *root) : row_(0) {
    this->root_ = root;
  }

  KLevenshteinRadixTree* clone_internal() {
    KLevenshteinRadixTree *lrt 
      = new KLevenshteinRadixTree(this->clone_node(this->root_));
    lrt->row_ = this->row_;
    lrt->min_prefix_distance_ = min_prefix_distance_;
    lrt->min_distance_ = this->min_distance_;
    lrt->k_ = this->k_;
    return lrt;
  }

  inline int max_column() {
    if (k_ == INT_MAX) return INT_MAX;
    return row_ + k_ + 1;
  }

  inline int min_column() {
    return std::max(row_ - k_, 0);
  }

  // Edit distance computation
  void update_edge_element(T t, EdgeOffset &root_edge) {

    std::stack<EdgeOffset> edges;
    edges.push(root_edge);

    // curr is always 0 or 1
    int curr = row_ % 2;
    int prev = curr ^ 1;

    int max_depth = max_column();

    while (!edges.empty()) {
      Edge* edge = edges.top().first;
      int offset = edges.top().second;
      int depth = edge->from()->depth() + edges.top().second;
      edges.pop();
      assert(edge);

      // e0 is the previous cell, e1 is the current cell
      LElement *e0 = NULL, *e1 = NULL; 

      // For each element of the edge
      EdgeIterator edge_it = edge->begin(), edge_ie = edge->end();

      if (offset > 0) {
        std::advance(edge_it, offset-1);
        e1 = &(*edge_it);
        std::advance(edge_it, 1);
      }

      for (; edge_it != edge_ie && depth < max_depth; ++edge_it, ++depth) {

        e0 = e1;
        e1 = &(*edge_it);

        // If this is the first column of the DP matrix, d = row
        if (edge_it == edge->begin() && edge->from() == this->root_) {
          e1->d[curr] = row_;

        // Otherwise compute new d
        } else {

          // If this is the first element of the edge, the previous cell
          // e0 is the last element of the parent edge
          if (edge_it == edge->begin())
            e0 = &(edge->from()->parent_edge()->back());

          int e0_d_curr = e0->d[curr];
          int e0_d_prev = e0->d[prev];
          int e1_d_prev = e1->d[prev];

          // max column increments by 1 each row, so e1_d_prev is undefined,
          // when depth == max_column
          if (depth == max_column())
            e1_d_prev = INT_MAX - 1;

          if (depth == (row_ - k_))
            e0_d_curr = INT_MAX - 1;

          // Compute minimum cost of insert or delete
          int ins_or_del 
            = std::min(e0_d_curr + 1, e1_d_prev + 1);

          // Compute minimum cost of replace or match if equal
          int match_or_replace = (t == e1->e) ? e0_d_prev : e0_d_prev + 1;

          int res = std::min(ins_or_del, match_or_replace);

          if (res > k_)
            res = INT_MAX - 1;

          // Store the minimum cost operation
          e1->d[curr] = res;

          //// DEBUG OUTPUT
          //std::cout << "row: " << row_ << ", depth: " << depth 
          //    << ", t: " << t << ", e1->e: " << e1->e
          //    << ", e0_d_curr = " << e0_d_curr 
          //    << ", e0->d[curr] = " << e0->d[curr]
          //    << ", e0_d_prev = " << e0_d_prev
          //    << ", e0->d[prev] = " << e0->d[prev]
          //    << ", ins: " << e0_d_curr + 1 
          //    << ", del: " << e1_d_prev + 1 
          //    << ", mat: " << e0_d_prev 
          //    << ", rep: " << e0_d_prev + 1 << "\n";

        }

        //// DEBUG OUTPUT
        //if (min_prefix_distance_ > e1->d[curr] &&
        //    e1->d[curr] != (INT_MAX - 1)) {
        //  LSequence min_s;
        //  edge->to()->get(min_s);
        //  std::cout << "New min_pfx_dist: " << min_prefix_distance_ 
        //      << " to " << e1->d[curr] << ": ";
        //  for (int x=0; x<min_s.size(); x++)
        //    std::cout << min_s[x];
        //  std::cout << std::endl;
        //}
        //std::cout << "row: " << row_ << ", depth: " << depth 
        //    << ", val: " << e1->d[curr] << "\n";
        
        min_prefix_distance_ = std::min(min_prefix_distance_, e1->d[curr]);
      }

      if (depth < max_depth) {
        foreach_edge(edge->to(), it) {
          Edge *child_edge = it->second;
          edges.push(EdgeOffset(child_edge, 0));
        }
      }

      if (edge->to()->leaf() && edge_it == edge->end()) {
        if (e1->d[curr] < min_distance_) {
          min_distance_ = e1->d[curr];
        }
        //// DEBUG
        //LSequence min_s;
        //edge->to()->get(min_s);
        //std::cout << "KL: min distance: " << min_distance_ << ": ";
        //for (int x=0; x<min_s.size(); x++)
        //  std::cout << min_s[x];
        //std::cout << std::endl;
      }
    }
    
    if (min_prefix_distance_ == (INT_MAX - 1))
      min_prefix_distance_ = INT_MAX;

    return;
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
        EdgeIterator edge_it = edge->begin(), edge_ie = edge->end(); 
        for (; edge_it != edge_ie; ++edge_it)
          edge_it->d[0] = depth++;
        if (!edge->to()->leaf())
          nodes.push(edge->to());
      }
    }
  }

  void get_at_depth(Node* root, int depth, 
                    std::vector<EdgeOffset>& edge_offsets) {
    typedef std::pair<Node*, int> NodeDepthPair;

    std::stack<NodeDepthPair> nodes;
    nodes.push(NodeDepthPair(root, root->depth()));

    while (!nodes.empty()) {
      NodeDepthPair ndp = nodes.top();
      nodes.pop();
      foreach_edge(ndp.first, it) {
        Edge *edge = it->second;
        if ((ndp.second + edge->size()) > depth) {
          size_t offset = depth - ndp.second;
          edge_offsets.push_back(EdgeOffset(edge, offset));
        } else {
          nodes.push(NodeDepthPair(edge->to(), ndp.second + edge->size()));
        }
      }
    }
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  int min_prefix_distance_;
  int min_distance_;
  int row_;
  int k_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_LEVENSHTEIN_RADIX_TREE_H

