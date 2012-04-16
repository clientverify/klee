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
#include <stack>

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

  // RadixEdge: Represents an edge between two nodes, holds sequence data
  class RadixEdge {
   public:

    RadixEdge(RadixNode* from, RadixNode* to, SequenceContainer& s) 
     : from_(from), to_(to), seq_(s) {}

    RadixEdge(RadixNode* from, RadixNode* to, 
              SequenceContainerIterator begin, 
              SequenceContainerIterator end) 
     : from_(from), to_(to), seq_(begin, end) {}

    RadixNode* to() { return to_; }

    RadixNode* from() { return from_; }

    void set_from(RadixNode* from) { from_ = from; }
    void set_to(RadixNode* to) { to_ = to; }

    void extend(SequenceContainerIterator _begin, 
                SequenceContainerIterator _end) {
      seq_.insert(seq_.end(), _begin, _end);
    }

    void extend(ElementType e) {
      seq_.insert(seq_.end(), e);
    }

    void erase(SequenceContainerIterator _begin, 
               SequenceContainerIterator _end) {
      seq_.erase(_begin, _end);
    }

    inline ElementType operator[](unsigned i) { return seq_[i]; }
    inline ElementType operator[](unsigned i) const { return seq_[i]; }

    inline SequenceContainerIterator begin() { return seq_.begin(); }
    inline SequenceContainerIterator end() { return seq_.end(); }

    inline ElementType key() const { return seq_[0]; }

    inline size_t size() { return seq_.size(); }

   private:
    RadixNode *to_;
    RadixNode *from_;
    SequenceContainer seq_;
  };

  typedef std::map<ElementType, RadixEdge*> TransitionMap;

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

  RadixNode* insert(SequenceContainerIterator begin, 
                    SequenceContainerIterator end) {
    RadixNode* curr_node = this;

    while (curr_node != NULL) {
      // Don't do anything if we insert an empty sequence
      if (begin == end) return curr_node;

      RadixEdge *edge = curr_node->get_edge(begin);

      // If no transition on s, add new edge to leaf node
      if (edge == NULL)
        return curr_node->add_edge(begin, end);

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
        RadixNode *split_node 
            = curr_node->add_edge(edge->begin(), edge->begin() + pos);

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

  // Static destory method
  static void destroy(RadixNode* root) { 
    std::stack<RadixNode*> worklist; 
    worklist.push(root);
    while (!worklist.empty()) {
      RadixNode* node = worklist.top();
      typename TransitionMap::iterator 
          it = node->tmap_.begin(), iend = node->tmap_.end();
      worklist.pop();
      for (; it != iend; ++it) {
        RadixEdge* edge = it->second;
        worklist.push(edge->to());
        delete edge;
      }
      node->tmap_.clear();
      delete node;
    }
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

  // Return the parent node
  RadixNode* parent() { return parent_edge_ ? parent_edge_->from() : NULL; }

  // Insert a sequence into the tree rooted at this node
  RadixNode* insert(SequenceContainer &s) {
    return insert(s.begin(), s.end());
  }

  // Insert a one element sequence into the tree rooted at this node
  RadixNode* insert(ElementType e) {
    SequenceContainer s;
    s.insert(s.begin(), e);
    return insert(s.begin(), s.end());
  }

  // Return length of path to root from the current node
  size_t depth() {
    RadixEdge* curr_edge = parent_edge_;
    size_t edge_size_count = 0;
    while (curr_edge != NULL) {
      edge_size_count += curr_edge->size();
      curr_edge = curr_edge->from()->parent_edge();
    }
    return edge_size_count;
  }

  // Return concatenation of edges from root this node 
  void get(SequenceContainer &s) {
    std::stack<RadixEdge*> edges;

    RadixNode* curr_node = this;
    while (!curr_node->root()) {
      edges.push(curr_node->parent_edge());
      curr_node = curr_node->parent();
    }

    while (!edges.empty()) {
      RadixEdge* edge = edges.top();
      s.insert(s.end(), edge->begin(), edge->end());
      edges.pop();
    }
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

  /// Remove sequence that terminates at a leaf node, returns true if remove
  /// actually takes place
  bool remove(SequenceContainer &s) {
    // Lookup the node matching s
    RadixNode *node = lookup(s, /*exact = */ true);

    // If v is not in tree or is present at an internal node, do nothing
    if (node && node->leaf() && !node->root()) {

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
 
  RadixNode* extend_parent_edge(ElementType e) {
    assert(leaf());
    parent_edge_->extend(e);
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

  // Clone the radix tree rooted at this node and return
  RadixNode* clone() {
    // New root of the cloned radix tree
    RadixNode* root_node = new RadixNode();

    // Worklist holds a list of RadixNode pairs, clone and original respectively
    std::stack<std::pair<RadixNode*, RadixNode*> > worklist; 
    worklist.push(std::make_pair(root_node, this));

    while (!worklist.empty()) {
      RadixNode* dst_node = worklist.top().first;
      RadixNode* src_node = worklist.top().second;
      typename TransitionMap::iterator it = src_node->tmap_.begin();
      typename TransitionMap::iterator iend = src_node->tmap_.end();

      worklist.pop();
      for (; it != iend; ++it) {
        RadixEdge* src_edge = it->second;

        // Child clone node
        RadixNode* dst_to_node = new RadixNode();

        // Clone edge node
        RadixEdge* edge = new RadixEdge(dst_node, dst_to_node, 
                                        src_edge->begin(), src_edge->end());
        // Set 'parent_edge' (previously null)
        dst_to_node->set_parent_edge(edge);

        // Assign the edge to its key in the new node's transition map
        dst_node->tmap_[edge->key()] = edge;

        // Add new node pair to worklist
        worklist.push(std::make_pair(dst_to_node, (RadixNode*)src_edge->to()));
      }
    }
    return root_node;
  }

 private:
  RadixEdge* parent_edge_;
  TransitionMap tmap_;
};

template <class SequenceContainer, class ElementType>
class RadixTree {
 public:
  typedef RadixNode<SequenceContainer, ElementType> Node;

  RadixTree() {
    root_ = new Node();
  }

  ~RadixTree() {
    Node::destroy(root_);
  }

  RadixTree* clone() {
    return new RadixTree(root_->clone());
  }

  Node* insert(SequenceContainer &s) { 
    return root_->insert(s);
  }

  Node* extend(SequenceContainer &s, Node* node = NULL) { 
    if (node)
      return node->extend_parent_edge(s);
    return root_->insert(s);
  }

  Node* extend(ElementType e, Node* node = NULL) { 
    if (node)
      return node->extend_parent_edge(e);
    return root_->insert(e);
  }


  bool remove(SequenceContainer &s) {
    return root_->remove(s);
  }

  bool lookup(SequenceContainer &s) { 
    if (Node *res = root_->lookup(s))
      return true;
    return false;
  }

  void get(Node* node, SequenceContainer &s) { 
    node->get(s);
  }

  void print(std::ostream& os) {
    root_->print(os);
    os << std::endl;
  }

 private:
  RadixTree(Node* root) : root_(root) {}
  explicit RadixTree(RadixTree& rt) {}

  Node* root_;
};


} // end namespace cliver
#endif // CLIVER_RADIXTREE_H
