//===-- EditDistanceSequence.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EDIT_DISTANCE_SEQUENCE_H
#define CLIVER_EDIT_DISTANCE_SEQUENCE_H

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

template< class T >
class LevenshteinElement {
 public:
  T e;
  int d[2];

  LevenshteinElement(T _e, int _d = 0) { 
    e = _e;
    d[0] = d[1] = _d;
  }

  LevenshteinElement() {
    d[0] = d[1] = 0;
  }

  bool operator==(const LevenshteinElement& le) const { return (e == le.e); }
  bool operator!=(const LevenshteinElement& le) const { return (e != le.e); }
  bool operator< (const LevenshteinElement& le) const { return (e < le.e); }
};

template< class T > 
std::ostream& operator<<(std::ostream& os, const LevenshteinElement<T>& le) {
  os << "(" << le.e << "," << le.d[0] << ", " << le.d[1] << ")";
}

template <class T> 
class LevenshteinSequenceComparator {
 public:
  typedef typename std::vector<LevenshteinElement<T> >::iterator LIterator;

  // Find size of matching prefix
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

template <class Sequence, class T> 
class LevenshteinRadixTree 
: public RadixTree<std::vector<LevenshteinElement<T> >, 
                   LevenshteinElement<T>,
                   LevenshteinSequenceComparator<T> > {

 public:
  typedef LevenshteinElement<T> LElement;
  typedef std::vector<LevenshteinElement<T> > LSequence;
  typedef LevenshteinSequenceComparator<T> LComparator; 
  typedef RadixTree<LSequence, LElement, LComparator> LRadixTree;
  typedef typename LRadixTree::Node Node;
  typedef typename LRadixTree::Edge Edge;
  typedef typename LRadixTree::EdgeMapIterator EdgeMapIterator;
 
#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

  LevenshteinRadixTree() : curr_(0) { this->root_ = new Node(); }
  LevenshteinRadixTree(Node *root) : curr_(0) { this->root_ = root; }

  // Return a deep-copy of this RadixTree
  virtual LRadixTree* clone() {
    LevenshteinRadixTree *lrt = new LevenshteinRadixTree(this->clone_node(this->root_));
    lrt->curr_ = this->curr_;
    return lrt;
  }

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

  virtual bool remove(Sequence &s) {
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size()+1);
    for (int i=0; i<s.size(); ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the node matching s
    Node *node = this->lookup_private(ls, /*exact = */ true);

    // Remove this node
    return this->remove_node(node);
  }

  void reset_distance_values() {
    // DP Initialization
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

  virtual int lookup_cost(Sequence &s) { 
    // Convert Sequence into an LevenshteinSequence
    LSequence ls(s.size() + 1);
    for (int i=0; i<s.size() + 1; ++i) ls[i].e = (i == 0) ? 0 : s[i - 1];

    // Lookup the Levenshtein sequence
    if (Node *res = this->lookup_private(ls, /*exact = */ true)) {
      LElement& e = res->parent_edge()->last_element();
      return e.d[curr_];
    } else if (Node *res = this->lookup_private(ls)) {
      size_t diff = res->depth() - ls.size();
      Edge *edge = res->parent_edge();
      LElement& e = (*edge)[edge->size() - diff - 1];
      return e.d[curr_];
    }
    return -1 ;
  }

  // Fill out edit distance values for a given sequence and return the minimum
  // cost edit distance found in the tree
  int min_edit_distance(Sequence &s) {
    int min_cost = INT_MAX;

    LElement root_element(0);

    for (int j=0; j < s.size()+1; ++j) {
      // walk tree from root and compute edit distance
      std::stack<Node*> nodes;
      nodes.push(this->root_);

      curr_ = j % 2;

      while (!nodes.empty()) {
        Node* n = nodes.top();
        nodes.pop();

        foreach_edge(n, it) {
          Edge *edge = it->second;

          if (j == 0) {

            int depth = n->depth();
            for (int i=0; i<edge->size(); ++i)
              (*edge)[i].d[curr_] = depth++;

            if (!edge->to()->leaf())
              nodes.push(edge->to());

          } else {

            T t = s[j-1];
            LElement *e0, *e1;

            for (int i=0; i<edge->size(); ++i) {

              e1 = &((*edge)[i]);

              if (i == 0 && n == this->root_) {
                e1->d[curr_] = j;
              } else {

                if (i == 0)
                  e0 = &(n->parent_edge()->last_element());
                else
                  e0 = &((*edge)[i-1]);

                int ins_or_del 
                  = std::min(e0->d[curr_] + 1, e1->d[curr_^1] + 1);
                int match_or_replace 
                  = (t == e1->e) ? e0->d[curr_^1] : e0->d[curr_^1] + 1;

                e1->d[curr_] = std::min(ins_or_del, match_or_replace);
              }
            }

            if (j == s.size() && edge->to()->leaf() && (e1->d[curr_] < min_cost) )
              min_cost = e1->d[curr_];
            else
              nodes.push(edge->to());
          }
        }
      }
    }
    return min_cost;
  }

 private:
  int curr_; // either 0 or 1
};


} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_EDIT_DISTANCE_SEQUENCE_H

