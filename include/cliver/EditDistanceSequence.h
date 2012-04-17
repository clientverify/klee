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
  int current;
  int previous;

  LevenshteinElement(T _e) : e(_e), current(0), previous(0) {}
  LevenshteinElement() : current(0), previous(0) {}
  bool operator==(const LevenshteinElement& le) const { return (e == le.e); }
  bool operator!=(const LevenshteinElement& le) const { return (e != le.e); }
  bool operator< (const LevenshteinElement& le) const { return (e < le.e); }
};

template< class T > 
std::ostream& operator<<(std::ostream& os, const LevenshteinElement<T>& le) {
  os << "(" << le.e << "," << le.previous << ", " << le.current << ")";
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
  typename EdgeMapIterator __iterator = __node->begin(); \
  typename EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

  LevenshteinRadixTree() { this->root_ = new Node(); }
  LevenshteinRadixTree(Node *root) { this->root_ = root; }

  // Return a deep-copy of this RadixTree
  virtual LRadixTree* clone() {
    return new LevenshteinRadixTree(this->clone_node(this->root_));
  }

  virtual bool lookup(Sequence &s) { 
    LSequence ls(s.size());
    for (int i=0; i<s.size(); ++i) ls[i].e = s[i];

    if (Node *res = this->lookup_private(ls))
      return true;
    return false;
  }

  // Insert new sequence into this radix tree
  virtual Node* insert(Sequence &s) { 
    LSequence ls(s.size());
    for (int i=0; i<s.size(); ++i) {
      ls[i].e = s[i];
      ls[i].current = i;
    } 
    return this->root_->insert(ls);
  }

  virtual bool remove(Sequence &s) {
    LSequence ls(s.size());
    for (int i=0; i<s.size(); ++i) ls[i].e = s[i];
    // Lookup the node matching s
    Node *node = this->lookup_private(ls, /*exact = */ true);
    return this->remove_node(node);
  }

  void init_distance_values() {
    // DP Initialization
    int depth = 0;
    std::stack<Node*> nodes;
    nodes.push(this->root_);
    while (!nodes.empty()) {
      Node* curr_node = nodes.top();
      nodes.pop();
      foreach_edge(curr_node, it) {
        Edge *edge = it->second;
        int depth = curr_node->depth();
        for (int i=0; i<edge->size(); ++i) {
          (*edge)[i].current = depth++;
        }
        if (!edge->to()->leaf())
          nodes.push(edge->to());
      }
    }
  }

  int min_edit_distance(Sequence &s) {
    int min_cost = INT_MAX;

    init_distance_values();

    for (int j=0; j < s.size(); ++j) {
      // walk tree from root and compute edit distance
      std::stack<Node*> nodes;
      nodes.push(this->root_);

      while (!nodes.empty()) {
        Node* curr_node = nodes.top();
        nodes.pop();
        foreach_edge(curr_node, it) {
          Edge *edge = it->second;

          LElement *e0, *e1;
          for (int i=0; i < edge->size(); ++i) {
            if (i == 0) {
              Edge* parent_edge = curr_node->parent_edge();
              e0 = &((*parent_edge)[parent_edge->size()-1]);
            } else {
              e0 = &((*edge)[i-1]);
            }
            e1 = &((*edge)[i]);
            int ins_or_del = std::min(e0->current + 1, e1->previous + 1);
            int replace = (s[j] == e1->e) ? e0->previous : e0->previous + 1;
            e1->previous = e1->current;
            e1->current = std::min(ins_or_del, replace);
          }

          if (edge->to()->leaf() && (e1->current < min_cost) ) {
            min_cost = e1->current;
          } else {
            nodes.push(edge->to());
          }
        }
      }
    }
    return min_cost;
  }

 private:
  bool initialized_;
};


} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_EDIT_DISTANCE_SEQUENCE_H

