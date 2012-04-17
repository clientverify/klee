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

    inline Element key() const { return seq_[0]; }

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
          Node *split_node 
              = curr_node->add_edge(edge->begin(), edge->begin() + pos);

          // Erase top of old edge that was just copied
          edge->erase(edge->begin(), edge->begin() + pos);

          // Update parent to new node
          edge->set_from(split_node);

          // Update edge map for newly created node
          split_node->edge_map_[edge->key()] = edge;

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

    Edge* get_edge(SequenceIterator it) {
      if (edge_map_.find(*it) != edge_map_.end()) {
        return edge_map_[*it];
      }
      return NULL;
    }

    // Create and add an edge to this node and return the node that
    // the new edge points to.
    Node* add_edge(SequenceIterator begin,
                   SequenceIterator end) {
      Node *node = new Node();
      Edge *edge = new Edge(this, node, begin, end);
      node->set_parent_edge(edge);
      edge_map_[*begin] = edge;
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

  virtual Node* extend(Sequence &s, Node* node = NULL) { 
    if (node)
      return node->extend_parent_edge(s);
    return root_->insert(s);
  }

  virtual Node* extend(Element e, Node* node = NULL) { 
    if (node)
      return node->extend_parent_edge(e);
    return root_->insert(e);
  }

  virtual bool lookup(Sequence &s) { 
    if (Node *res = lookup_private(s))
      return true;
    return false;
  }

  virtual void get(Node* node, Sequence &s) { 
    node->get(s);
  }

  virtual void print(std::ostream& os) {
    root_->print(os);
    os << std::endl;
  }

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

 public:

  Node* root_;

 private:
  RadixTree(Node* root) : root_(root) {}
  explicit RadixTree(RadixTree& rt) {}
};

} // end namespace cliver
#endif // CLIVER_RADIXTREE_H
