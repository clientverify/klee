//===-- RadixTree.h ---------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO: use boost::unordered_map for transition maps
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_RADIXTREE_H
#define CLIVER_RADIXTREE_H

#include <iostream>
#include <algorithm>
#include <map>
#include <stack>
#include "assert.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

template <class Sequence> 
class DefaultSequenceComparator {
 public:
  typedef typename Sequence::iterator SequenceIterator;

  // Find size of matching prefix
  static int prefix_match(SequenceIterator first1, SequenceIterator last1,
                          SequenceIterator first2, SequenceIterator last2) {
    SequenceIterator it1 = first1;
    SequenceIterator it2 = first2;

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

  static bool equal(SequenceIterator first1, SequenceIterator last1,
                    SequenceIterator first2 ) {
    return std::equal(first1, last1, first2);
  }
};

////////////////////////////////////////////////////////////////////////////////
            
//===----------------------------------------------------------------------===//
// RadixTree
//===----------------------------------------------------------------------===//
template <class Sequence, class Element, 
          class Compare = DefaultSequenceComparator<Sequence> >
//template <class Sequence, class Element, 
class RadixTree {
 public: 
  class Node; // Declaration of Node class
  class Edge; // Declaration of Edge class

  typedef typename Sequence::iterator SequenceIterator;
  typedef std::map<Element, Edge*> EdgeMap;
  typedef typename EdgeMap::iterator EdgeMapIterator;

  //===-------------------------------------------------------------------===//
  // Class Edge: Represents an edge between two nodes, holds sequence data
  //===-------------------------------------------------------------------===//
  class Edge {
   public:

    Edge(Node* from, Node* to, Sequence& s) 
    : from_(from), to_(to), seq_(s) {}

    Edge(Node* from, Node* to, Element e) 
    : from_(from), to_(to) { extend(e); }

    Edge(Node* from, Node* to, 
              SequenceIterator begin, 
              SequenceIterator end) 
    : from_(from), to_(to), seq_(begin, end) {}

    Node* to() { return to_; }
    Node* from() { return from_; }

    void set_from(Node* from) { from_ = from; }
    void set_to(Node* to) { to_ = to; }

    void extend(SequenceIterator _begin, SequenceIterator _end) {
      seq_.insert(seq_.end(), _begin, _end);
    }

    void extend(Element e) {
      seq_.insert(seq_.end(), e);
    }

    void erase(SequenceIterator _begin, 
              SequenceIterator _end) {
      seq_.erase(_begin, _end);
    }

    inline Element& operator[](unsigned i) { return seq_[i]; }
    inline const Element& operator[](unsigned i) const { return seq_[i]; }

    inline SequenceIterator begin() { return seq_.begin(); }
    inline SequenceIterator end() { return seq_.end(); }

    inline Element& key() { return seq_[0]; }
    inline Element& first_element() { return seq_[0]; }
    inline Element& last_element() { return seq_[this->size()-1]; }

    inline size_t size() { return seq_.size(); }

  protected:
    Node *to_;
    Node *from_;
    Sequence seq_;
  };

  //===-------------------------------------------------------------------===//
  // Class Node
  //===-------------------------------------------------------------------===//
  class Node {
   public:

    Node() : parent_edge_(NULL) {}

    ~Node() { 
      parent_edge_ = NULL;
    }

    // Return a ref to the edge map
    EdgeMap& edge_map() { return edge_map_; }

    // Return iterator for the begining of the edge map
    EdgeMapIterator begin() { return edge_map_.begin(); }

    // Return iterator for the end of the edge map
    EdgeMapIterator end() { return edge_map_.end(); }

    // Return number of outgoing edges
    int degree() { return edge_map_.size(); }

    // Return true if this node has no incoming edge
    bool root() { return NULL == parent_edge_; }

    // Return true if this node has no outgoing edges
    bool leaf() { return 0 == edge_map_.size(); }

    // Set the incoming edge
    void set_parent_edge(Edge* e) { parent_edge_ = e; }

    // Return the incoming edge
    Edge* parent_edge() { return parent_edge_; }

    // Return the parent node
    Node* parent() { return parent_edge_ ? parent_edge_->from() : NULL; }

    // Insert a sequence into the tree rooted at this node
    Node* insert(Sequence &s) {
      return insert(s.begin(), s.end());
    }

    // Insert a one element sequence into the tree rooted at this node
    Node* insert(Element e) {
      Sequence s;
      s.insert(s.begin(), e);
      return insert(s.begin(), s.end());
    }

    Node* insert(SequenceIterator begin, 
                 SequenceIterator end) {
      Node* curr_node = this;

      while (curr_node != NULL) {
        // Don't do anything if we insert an empty sequence
        if (begin == end) return curr_node;

        Edge *edge = curr_node->get_edge(begin);

        // If no edge on s, add new edge to leaf node
        if (edge == NULL)
          return curr_node->add_edge(begin, end);

        // Find position where match ends between edge and s 
        int pos = Compare::prefix_match(edge->begin(), edge->end(), begin, end);

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
          Node *split_node = curr_node->split_edge(edge->key(), pos);
          
          //// Split existing edge
          //Node *split_node 
          //    = curr_node->add_edge(edge->begin(), edge->begin() + pos);

          //// Erase top of old edge that was just copied
          //edge->erase(edge->begin(), edge->begin() + pos);

          //// Update parent to new node
          //edge->set_from(split_node);

          //// Update edge map for newly created node
          //split_node->edge_map_[edge->key()] = edge;

          // Current node is now split_node
          begin += pos;
          curr_node = split_node;
        }
      }
      return curr_node;
    }

    // Return length of path to root from the current node
    size_t depth() {
      Edge* curr_edge = parent_edge_;
      size_t edge_size_count = 0;
      while (curr_edge != NULL) {
        edge_size_count += curr_edge->size();
        curr_edge = curr_edge->from()->parent_edge();
      }
      return edge_size_count;
    }

    // Return concatenation of edges from root this node 
    void get(Sequence &s) {
      std::stack<Edge*> edges;

      Node* curr_node = this;
      while (!curr_node->root()) {
        edges.push(curr_node->parent_edge());
        curr_node = curr_node->parent();
      }

      while (!edges.empty()) {
        Edge* edge = edges.top();
        s.insert(s.end(), edge->begin(), edge->end());
        edges.pop();
      }
    }

    // Lookup the edge associated the seqeuence element at 'it'
    Edge* get_edge(SequenceIterator it) {
      if (edge_map_.find(*it) != edge_map_.end()) {
        return edge_map_[*it];
      }
      return NULL;
    }

    // Lookup the edge associated the seqeuence element at 'it'
    Edge* get_edge(Element e) {
      if (edge_map_.find(e) != edge_map_.end()) {
        return edge_map_[e];
      }
      return NULL;
    }

    // Create and add an edge with the contents between begin and end to this
    // node and return the node that the new edge points to
    Node* add_edge(SequenceIterator begin,
                   SequenceIterator end) {
      Node *node = new Node();
      Edge *edge = new Edge(this, node, begin, end);
      node->set_parent_edge(edge);
      edge_map_[*begin] = edge;
      return node;
    }

    // Create and add an edge to this node with the contents of s and return the
    // node that the new edge points to
    Node* add_edge(Sequence &s) {
      return add_edge(s.begin(), s.end());
    }

    // Create and add an edge to this node that contains the element e and
    // return the node that the new edge points to
    Node* add_edge(Element e) {
      Node *node = new Node();
      Edge *edge = new Edge(this, node, e);
      node->set_parent_edge(edge);
      edge_map_[e] = edge;
      return node;
    }

    Node* extend_parent_edge(Sequence &s) {
      assert(leaf());
      parent_edge_->extend(s.begin(), s.end());
      return this;
    }
  
    Node* extend_parent_edge(Element e) {
      assert(leaf());
      parent_edge_->extend(e);
      return this;
    }

    // Split an edge keyed on e at pos
    Node* split_edge(Element e, int pos) {

      // Lookup edge
      Edge *edge = this->get_edge(e);

      // Return NULL if not found
      if (edge == NULL)
        return NULL;

      // Split existing edge
      Node *split_node = this->add_edge(edge->begin(), edge->begin() + pos);

      // Erase top of old edge that was just copied
      edge->erase(edge->begin(), edge->begin() + pos);

      // Update parent to new node
      edge->set_from(split_node);

      // Update edge map for newly created node
      split_node->edge_map_[edge->key()] = edge;

      return split_node;
    }

    // Simple print routine
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

      EdgeMapIterator it=edge_map_.begin(), iend = edge_map_.end();
      for (; it != iend; ++it) {
        Node* node = (*(it->second)).to();
        node->print(os, depth + parent_edge_size);
      }
    }

  protected:
    Edge* parent_edge_;
    EdgeMap edge_map_;
  };

 public:
  // Constructor: Create new RadixTree
  RadixTree() { root_ = new Node(); }

  // Destructor: Delete RadixTree non-recursively
  ~RadixTree() {
    std::stack<Node*> worklist; 
    worklist.push(root_);
    while (!worklist.empty()) {
      Node* node = worklist.top();
      EdgeMapIterator 
          it = node->edge_map().begin(), iend = node->edge_map().end();
      worklist.pop();
      for (; it != iend; ++it) {
        Edge* edge = it->second;
        worklist.push(edge->to());
        delete edge;
      }
      node->edge_map().clear();
      delete node;
    }
  }

  // Return a deep-copy of this RadixTree
  virtual RadixTree* clone() {
    return new RadixTree(clone_node(root_));
  }

  // Insert new sequence into this radix tree
  virtual Node* insert(Sequence &s) { 
    return root_->insert(s);
  }

  // Insert new sequence of a single element into this radix tree
  virtual Node* insert(Element e) { 
    return root_->insert(e);
  }

  // Lookup Sequence in the RadixTree
  virtual bool lookup(Sequence &s) { 
    if (Node *res = lookup_private(s))
      return true;
    return false;
  }

  // Return the Sequence stored in the radix tree from root to node
  virtual void get(Node* node, Sequence &s) { 
    node->get(s);
  }

  // Write a text version of the RadixTree to os
  virtual void print(std::ostream& os) {
    root_->print(os);
    os << std::endl;
  }

  // If there is a path from root to leaf node that is equal to s, remove the
  // leaf node and parent edge
  virtual bool remove(Sequence &s) {
    // Lookup the node matching s
    Node *node = lookup_private(s, /*exact = */ true);
    return remove_node(node);
  }

 protected: 

  bool remove_node(Node *node) {
    // If v is not in tree or is present at an internal node, do nothing
    if (node && node->leaf() && !node->root()) {

      Edge *edge = node->parent_edge();
      Node *parent = edge->from();
      parent->edge_map().erase(edge->key());
      delete edge;
      delete node; 

      // If parent now only has one child, merge child edge with parent edge
      // and delete parent
      if (!parent->root() && parent->degree() == 1) {
        Edge *merge_edge = parent->edge_map().begin()->second;

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

  // Return a deep-copy of this RadixTree
  Node* clone_node(Node* root_node) {
    // New root of the cloned radix tree
    Node* clone_node = new Node();

    // Worklist holds a list of Node pairs, clone and original respectively
    std::stack<std::pair<Node*, Node*> > worklist; 
    worklist.push(std::make_pair(clone_node, root_node));

    while (!worklist.empty()) {
      Node* dst_node = worklist.top().first;
      Node* src_node = worklist.top().second;
      EdgeMapIterator 
          it = src_node->edge_map().begin(), iend = src_node->edge_map().end();

      worklist.pop();
      for (; it != iend; ++it) {
        Edge* src_edge = it->second;

        // Child clone node
        Node* dst_to_node = new Node();

        // Clone edge node
        Edge* edge = new Edge(dst_node, dst_to_node, 
                                        src_edge->begin(), src_edge->end());
        // Set 'parent_edge' (previously null)
        dst_to_node->set_parent_edge(edge);

        // Assign the edge to its key in the new node's edge map
        dst_node->edge_map()[edge->key()] = edge;

        // Add new node pair to worklist
        worklist.push(std::make_pair(dst_to_node, (Node*)src_edge->to()));
      }
    }
    return clone_node;
  }

  /// Lookup the sequence s, starting from the edge at this node, return
  /// the node that the parent of edge containing the suffix of s.
  Node* lookup_private(Sequence &s, bool exact = false) {

    Node* curr_node = root_;
    int pos = 0;

    while (pos < s.size()) {

      // Find matching edge for current element of s
      if (Edge *edge = curr_node->get_edge(s.begin() + pos)) {
        size_t remaining = s.size() - pos;

        // Edge size is equal to remaining # elements in s
        if (edge->size() == remaining) {
          if (Compare::equal(edge->begin(), edge->end(), s.begin() + pos))
            return edge->to();
          else
            return NULL;
          
        // Edge size is greater than remaining # elements in s
        } else if (edge->size() > remaining) {
          if (!exact && Compare::equal(s.begin() + pos, s.end(), edge->begin()))
            return edge->to();
          else
            return NULL;

        // Edge size is less than remaining # elements in s
        } else {
          if (!Compare::equal(edge->begin(), edge->end(), s.begin() + pos))
            return NULL;
        }

        pos += edge->size();
        curr_node = edge->to();

      } else {
        // No match in edge map for current node
        return NULL;
      }
    }

    return NULL;
  }

  // Returns a (Node* n, int i) pair if there is an prefix of s that is an exact
  // match in the tree, or if there is a prefix of the contents of the tree that
  // is an exact match of s. If the former, i is positive; if the latter, i is
  // negative or zero. If n is NULL, there is not a 'prefix' match.
  std::pair<Node*, int> prefix_lookup(Sequence &s) {
    std::pair<Node*, int> no_prefix_match(NULL, 0);
    Node* curr_node = root_;
    int pos = 0;
    size_t remaining;

    while (pos < s.size()) {
      remaining = s.size() - pos;

      // Find matching edge for current element of s
      if (Edge *edge = curr_node->get_edge(s.begin() + pos)) {

        int match_len = Compare::prefix_match(edge->begin(), edge->end(),
                                              s.begin() + pos, s.end());
        int offset = match_len - (int)edge->size();

        // No prefix match
        if ((int)match_len < remaining && (int)match_len < edge->size())
          return no_prefix_match;
        // A prefix of the tree is an exact match to s
        else if (match_len == remaining) 
          return std::make_pair(edge->to(), match_len - (int)edge->size());
        // A prefix of s has an exact match in the tree (s overlaps leaf node)
        else if (match_len == (int)edge->size() && edge->to()->leaf())
          return std::make_pair(edge->to(), (int)remaining - match_len);

        // edge is equal to s from (pos) to (pos + |edge|)
        pos += edge->size();
        curr_node = edge->to();
      } else {
        if (curr_node != this->root_ && curr_node->leaf() && pos > 0)
          return std::make_pair(curr_node, (int)remaining);
        else 
          return no_prefix_match;
      }

    }
    return no_prefix_match;
  }

 public:

  Node* root_;

 private:
  RadixTree(Node* root) : root_(root) {}
  explicit RadixTree(RadixTree& rt) {}
};

} // end namespace cliver
#endif // CLIVER_RADIXTREE_H
