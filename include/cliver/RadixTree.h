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
#include <string>
#include "assert.h"

////////////////////////////////////////////////////////////////////////////////

#include <boost/graph/adjacency_list.hpp>
#ifdef USE_GRAPHVIZ
#include <boost/graph/graphviz.hpp>
#endif
#include <boost/lexical_cast.hpp>

// For writing RadixTree to dot file
struct dot_vertex { 
  dot_vertex() : name("") {}
  dot_vertex(std::string _name) : name(_name) {}
  std::string name; 
};
struct dot_edge   { 
  dot_edge() : name("") {}
  dot_edge(std::string _name) : name(_name) {}
  std::string name; 
};
typedef boost::adjacency_list<
  boost::listS, boost::vecS, boost::directedS, dot_vertex, dot_edge > dot_graph;
typedef boost::graph_traits<dot_graph>::vertex_descriptor dot_vertex_desc;
typedef boost::graph_traits<dot_graph>::edge_descriptor dot_edge_desc;

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

template <class Sequence> 
class DefaultSequenceComparator {
 public:
  typedef typename Sequence::iterator SequenceIterator;

  // Find size of matching prefix
  static size_t prefix_match(SequenceIterator first1, SequenceIterator last1,
                          SequenceIterator first2, SequenceIterator last2) {
    SequenceIterator it1 = first1;
    SequenceIterator it2 = first2;

    size_t count = 0;
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
  typedef Sequence sequence_type;
  typedef Element element_type;

 protected:
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

    //Edge() : from_(NULL), to_(NULL) {}

    Edge(Node* from, Node* to, Sequence& s) 
    : from_(from), to_(to), seq_(s) {}

    Edge(Node* from, Node* to, Element e) 
    : from_(from), to_(to) { extend(e); }

    Edge(Node* from, Node* to, 
              SequenceIterator begin, 
              SequenceIterator end) 
    : from_(from), to_(to), seq_(begin, end) {}

    inline Node* to() { return to_; }
    inline Node* from() { return from_; }

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

    inline SequenceIterator begin() { return seq_.begin(); }
    inline SequenceIterator end() { return seq_.end(); }

    inline Element& key() { return *(seq_.begin()); }
    inline Element& back() { return *(seq_.begin()+(seq_.size()-1)); }

    inline size_t size() { return seq_.size(); }

    Sequence& seq() { return seq_; }

  protected:
    Node *from_;
    Node *to_;
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
        size_t pos = Compare::prefix_match(edge->begin(), edge->end(), begin, end);

        // If s is fully matched on this edge, return node it points to
        SequenceIterator begin_pos(begin);
        std::advance(begin_pos, pos);

        if (begin_pos == end && pos <= edge->size())
          return edge->to();

        // If (begin, end) match this edge completely
        if (pos == edge->size()) {

          // If leaf, just extend edge and return node edge points to
          if (edge->to()->leaf()) {
            edge->extend(begin_pos, end);
            return edge->to();
          }

          // Otherwise, add rest of s to node that edge points to
          begin = begin_pos;
          curr_node = edge->to();

        } else {
          // Split existing edge
          Node *split_node = curr_node->split_edge(edge->key(), pos);
          
          // Current node is now split_node
          begin = begin_pos;
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
    template<class SequenceType>
    void get(SequenceType &s) {
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
    template <class SequenceType> 
    Node* add_edge(SequenceType &s) {
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
  
    Node* extend_parent_edge_element(Element e) {
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

      SequenceIterator edge_begin_pos = edge->begin();
      std::advance(edge_begin_pos, pos);

      // Split existing edge
      Node *split_node = this->add_edge(edge->begin(), edge_begin_pos);

      // Erase top of old edge that was just copied
      edge->erase(edge->begin(), edge_begin_pos);

      // Update parent to new node
      edge->set_from(split_node);

      // Update edge map for newly created node
      split_node->edge_map_[edge->key()] = edge;

      return split_node;
    }

    // Simple print routine
    void print(std::ostream& os, unsigned depth = 0) {
      int parent_edge_size = 0;

      if (parent_edge_) {
        for (unsigned i = 0; i < depth; ++i) os << " ";
        SequenceIterator it = parent_edge_->begin(), ie = parent_edge_->end();
        std::stringstream ss;
        for (; it != ie; ++it) ss << *it << ","; 
        os << ss.str() << std::endl;
        parent_edge_size = ss.str().size();
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
  virtual ~RadixTree() {
    std::stack<Node*> worklist; 
    if (this->root_) {
      worklist.push(this->root_);
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
  virtual Node* insert_element(Element e) { 
    return root_->insert(e);
  }

  // Lookup Sequence in the RadixTree
  virtual bool lookup(Sequence &s) { 
    if (lookup_private(s))
      return true;
    return false;
  }

  // Return the Sequence stored in the radix tree from root to node
  template <class SequenceType>
  void get(Node* node, SequenceType &s) { 
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

  virtual void write_dot(std::ostream &os, 
                         bool edge_labels = true, std::string edge_delim="") {
    dot_graph graph;

    std::stack<std::pair<Node*, dot_vertex_desc> > worklist; 
    worklist.push(std::make_pair(root_, boost::add_vertex(graph)));

    while (!worklist.empty()) {

      Node* node = worklist.top().first;
      dot_vertex_desc v_desc = worklist.top().second;
      worklist.pop();

      EdgeMapIterator 
          it = node->edge_map().begin(), iend = node->edge_map().end();

      for (; it != iend; ++it) {
        Edge *edge = it->second;

        std::string name;
        if (edge_labels) {
          SequenceIterator sit = edge->begin(), sie = edge->end();
          for (; sit != sie; ++sit) {
            name += boost::lexical_cast<std::string>(*sit) + edge_delim;
          }
        }
        dot_edge e(name);

        dot_vertex v(boost::lexical_cast<std::string>(edge->size()));
        dot_vertex_desc v_to_desc = boost::add_vertex(v, graph);
        boost::add_edge(v_desc, v_to_desc, e, graph);

        worklist.push(std::make_pair(edge->to(), v_to_desc));
      }
    }
#ifdef USE_GRAPHVIZ
    if (edge_labels) 
      boost::write_graphviz(os, graph,
          boost::make_label_writer(boost::get(&dot_vertex::name, graph)),
          boost::make_label_writer(boost::get(&dot_edge::name, graph)));
    else
      boost::write_graphviz(os, graph,
          boost::make_label_writer(boost::get(&dot_vertex::name, graph)));
#endif
  }

  // Returns the number of elements in the tree, i.e. sum over |edges|
  size_t element_count() {
    size_t count = 0; 
    std::stack<Node*> worklist; 
    if (root_) {
      worklist.push(root_);
      while (!worklist.empty()) {
        Node* node = worklist.top();
        EdgeMapIterator 
            it = node->edge_map().begin(), iend = node->edge_map().end();
        worklist.pop();
        for (; it != iend; ++it) {
          Edge* edge = it->second;
          count += edge->size();
          worklist.push(edge->to());
        }
      }
    }
    return count;
  }

  // Returns max leaf depth
  size_t maximum_leaf_depth() {
    size_t max_depth = 0; 
    std::stack<Node*> worklist; 
    if (root_) {
      worklist.push(root_);
      while (!worklist.empty()) {
        Node* node = worklist.top();

        if (node->leaf())
          max_depth = std::max(max_depth, node->depth());

        EdgeMapIterator 
            it = node->edge_map().begin(), iend = node->edge_map().end();
        worklist.pop();
        for (; it != iend; ++it) {
          Edge* edge = it->second;
          worklist.push(edge->to());
        }
      }
    }
    return max_depth;
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
    size_t pos = 0;

    while (pos < s.size()) {

      SequenceIterator s_begin_pos = s.begin();
      std::advance(s_begin_pos, pos);

      // Find matching edge for current element of s
      if (Edge *edge = curr_node->get_edge(s_begin_pos)) {
        size_t remaining = s.size() - pos;

        // Edge size is equal to remaining # elements in s
        if (edge->size() == remaining) {
          if (Compare::equal(edge->begin(), edge->end(), s_begin_pos))
            return edge->to();
          else
            return NULL;
          
        // Edge size is greater than remaining # elements in s
        } else if (edge->size() > remaining) {
          if (!exact && Compare::equal(s_begin_pos, s.end(), edge->begin()))
            return edge->to();
          else
            return NULL;

        // Edge size is less than remaining # elements in s
        } else {
          if (!Compare::equal(edge->begin(), edge->end(), s_begin_pos))
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
  std::pair<Node*, int> prefix_lookup(Sequence &s, Node* root = NULL) {
    if (root == NULL)
      root = root_;

    std::pair<Node*, int> no_prefix_match(NULL, 0);
    Node* curr_node = root;
    size_t pos = 0;
    size_t remaining;


    while (pos < s.size()) {
      remaining = s.size() - pos;

      SequenceIterator s_begin_pos = s.begin();
      std::advance(s_begin_pos, pos);

      // Find matching edge for current element of s
      if (Edge *edge = curr_node->get_edge(s_begin_pos)) {

        size_t match_len = Compare::prefix_match(edge->begin(), edge->end(),
                                                 s_begin_pos, s.end());
        // No prefix match
        if (match_len < remaining && match_len < edge->size())
          return no_prefix_match;
        // A prefix of the tree is an exact match to s
        else if (match_len == remaining) 
          return std::make_pair(edge->to(), match_len - (int)edge->size());
        // A prefix of s has an exact match in the tree (s overlaps leaf node)
        else if (match_len == edge->size() && edge->to()->leaf())
          return std::make_pair(edge->to(), (int)remaining - (int)match_len);

        // edge is equal to s from (pos) to (pos + |edge|)
        pos += edge->size();
        curr_node = edge->to();
      } else {
        if (curr_node != root && curr_node->leaf() && pos > 0)
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
