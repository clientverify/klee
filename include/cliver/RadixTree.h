//===-- RadixTree.h ---------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_RADIXTREE_H
#define CLIVER_RADIXTREE_H

#include <algorithm>
#include <map>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

// Helper function for finding size of matching prefix
template <class InputIterator1, class InputIterator2>
int prefix_match (InputIterator1 first1, InputIterator1 last1,
           InputIterator2 first2, InputIterator2 last2) {
  InputIterator1 it1 = first1;
  InputIterator2 it2 = first2;

  int count = 0;
  while (it1 != last1 && it2 != last2) {
    if (*it1 != *it2)
      return count;
    ++it1;
    ++it2;
    ++count;
  }
  return count;
}

////////////////////////////////////////////////////////////////////////////////
            
template <class SequenceContainer, class ElementType>
class RadixNode {
  typedef typename SequenceContainer::iterator SequenceContainerIterator;

  class RadixEdge {
   public:

    RadixEdge(RadixNode* from, RadixNode* to, SequenceContainer& s) 
     : from_(from), to_(to), seq_(s) {}

    RadixEdge(RadixNode* from, RadixNode* to, 
              SequenceContainerIterator& begin, 
              SequenceContainerIterator& end) 
     : from_(from), to_(to), seq_(begin, end) {}

    RadixNode* to() { return to_; }

    RadixNode* from() { return from_; }

    void set_from(RadixNode* from) { from_ = from; }
    void set_to(RadixNode* to) { to_ = to; }

    void extend(SequenceContainerIterator _begin, 
                SequenceContainerIterator _end) {
      seq_.insert(seq_.end(), _begin, _end);
    }

    void erase(SequenceContainerIterator _begin, 
               SequenceContainerIterator _end) {
      seq_.erase(_begin, _end);
    }

    inline ElementType operator[](unsigned i) { return seq_[i]; }
    inline ElementType operator[](unsigned i) const { return seq_[i]; }

    SequenceContainerIterator begin() { return seq_.begin(); }
    SequenceContainerIterator end() { return seq_.end(); }

    ElementType key() const { return seq_[0]; }

    size_t size() { return seq_.size(); }

   private:
    RadixNode *to_;
    RadixNode *from_;
    SequenceContainer seq_;
  };

  typedef std::map<ElementType, RadixEdge*> TransitionMap;

 private:
  RadixEdge* get_edge(SequenceContainerIterator it) {
    if (tmap_.find(*it) != tmap_.end()) {
      return tmap_[*it];
    }
    return NULL;
  }

  RadixNode* add_edge(SequenceContainerIterator begin,
                      SequenceContainerIterator end) {
    RadixNode *node = new RadixNode();
    RadixEdge *edge = new RadixEdge(this, node, begin, end);
    node->set_parent_edge(edge);
    tmap_[*begin] = edge;
    return node;
  }

  // Non-recursive, no copying
  RadixNode* insert(SequenceContainerIterator begin, 
                    SequenceContainerIterator end) {
    RadixNode* curr_node = this;

    while (curr_node != NULL) {
      // Don't do anything if we insert an empty sequence
      if (begin == end) return curr_node;

      RadixEdge *edge = get_edge(begin);

      // If no transition on s, add new edge to leaf node
      if (edge == NULL) {
        return curr_node->add_edge(begin, end);
      }

      // Find position where match ends between edge and s 
      int pos = prefix_match(edge->begin(), edge->end(), begin, end);

      // If s is fully matched on this edge, return node it points to
      if ((begin + pos) == end && pos <= edge->size())
        return edge->to();

      // If (begin, end) match this edge completely
      if (pos == edge->size()) {

        // If leaf, just extend edge and return node edge points to
        if (edge->to()->leaf()) {
          edge->extend(begin + pos, end);
          return edge->to();
        }

        // Otherwise, add rest of s to node that edge points to
        begin += pos;
        curr_node = edge->to();

      } else {
        // Split existing edge
        RadixNode *split_node = add_edge(edge->begin(), edge->begin() + pos);

        // Erase top of old edge that was just copied
        edge->erase(edge->begin(), edge->begin() + pos);

        // Update parent to new node
        edge->set_from(split_node);

        // Update transition map for newly created node
        split_node->tmap_[edge->key()] = edge;

        // Current node is now split_node
        begin += pos;
        curr_node = split_node;
      }
    }
    return curr_node;
  }


 public:

  RadixNode() : parent_edge_(NULL) {}

  ~RadixNode() { 
    parent_edge_ = NULL;
  }

  // Return number of outgoing edges
  int degree() { return tmap_.size(); }

  // Return true if this node has no incoming edge
  bool root() { return NULL == parent_edge_; }

  // Return true if this node has no outgoing edges
  bool leaf() { return 0 == tmap_.size(); }

  // Set the incoming edge
  void set_parent_edge(RadixEdge* e) { parent_edge_ = e; }

  // Return the incoming edge
  RadixEdge* parent_edge() { return parent_edge_; }

  // Insert a sequence into the tree rooted at this node
  RadixNode* insert(SequenceContainer &s) {
    return insert(s.begin(), s.end());
  }

  /// Lookup the sequence s, starting from the edge at this node, return
  /// the node that the parent of edge containing the suffix of s.
  RadixNode* lookup(SequenceContainer &s, bool exact = false) {

    RadixNode* curr_node = this;
    int pos = 0;

    while (pos < s.size()) {

      // Find matching edge for current element of s
      if (RadixEdge *edge = curr_node->get_edge(s.begin() + pos)) {
        size_t remaining = s.size() - pos;

        // Edge size is equal to remaining # elements in s
        if (edge->size() == remaining) {
          if (std::equal(edge->begin(), edge->end(), s.begin() + pos))
            return edge->to();
          else
            return NULL;
          
        // Edge size is greater than remaining # elements in s
        } else if (edge->size() > remaining) {
          if (!exact && std::equal(s.begin() + pos, s.end(), edge->begin()))
            return edge->to();
          else
            return NULL;

        // Edge size is less than remaining # elements in s
        } else {
          if (!std::equal(edge->begin(), edge->end(), s.begin() + pos))
            return NULL;
        }

        pos += edge->size();
        curr_node = edge->to();

      } else {
        // No match in transition map in current node
        return NULL;
      }
    }

    return NULL;
  }

  // Removes a sequence that terminates at a leaf node, returns true if a remove
  // actually takes place
  bool remove(SequenceContainer &s) {
    // Lookup the node matching s
    RadixNode *node = lookup(s, /*exact = */ true);

    // If v is not in tree or is present at an internal node, do nothing
    if (node && node->leaf() && !node->root()) {

      // TODO check length of edge == s
      RadixEdge *edge = node->parent_edge();
      RadixNode *parent = edge->from();
      parent->tmap_.erase(edge->key());
      delete edge;
      delete node; 
      // If parent now only has one child, merge child edge with parent edge
      // and delete parent
      if (!parent->root() && parent->degree() == 1) {
        RadixEdge *merge_edge = parent->tmap_.begin()->second;

        parent->parent_edge()->extend(merge_edge->begin(), merge_edge->end());
        parent->parent_edge()->set_to(merge_edge->to());
        parent->parent_edge()->to()->set_parent_edge(parent->parent_edge());

        delete parent;
        delete merge_edge;
      }
      return true;
    }
    return false;
  }
  
  RadixNode* extend_parent_edge(SequenceContainer &s) {
    assert(leaf());
    parent_edge_->extend(s.begin(), s.end());
    return this;
  }

  void print(std::ostream& os, int depth = 0) {

    int parent_edge_size = 0;

    if (parent_edge_) {
      for (int i = 0; i < depth; ++i)
        os << " ";
      for (int i = 0; i < parent_edge_->size(); ++i)
        os << (*parent_edge_)[i];
      os << std::endl;

      parent_edge_size = parent_edge_->size();
    }

    typename TransitionMap::iterator it=tmap_.begin(), iend = tmap_.end();
    for (; it != iend; ++it) {
      RadixNode* node = (*(it->second)).to();
      node->print(os, depth + parent_edge_size);
    }

  }

 private:
  RadixEdge* parent_edge_;
  TransitionMap tmap_;
};

template <class SequenceContainer, class ElementType>
class RadixTree {
  typedef RadixNode<SequenceContainer, ElementType> Node;
 public:
  RadixTree() {
    root_ = new Node();
  }

  ~RadixTree() {
    delete root_;
  }

  Node* insert(SequenceContainer &v) { 
    return root_->insert(v);
  }

  Node* extend(Node* node, SequenceContainer &v) { 
    if (node)
      return node->extend_parent_edge(v);
    return NULL;
  }

  bool remove(SequenceContainer &v) {
    return root_->remove(v);
  }

  bool lookup(SequenceContainer &v) { 
    Node *res = root_->lookup(v);
    if (res) {
      return true;
    }
    return false;
  }

  void print(std::ostream& os) {
    root_->print(os);
    os << std::endl;
  }

 private:
  Node* root_;
};


} // end namespace cliver
#endif // CLIVER_RADIXTREE_H
