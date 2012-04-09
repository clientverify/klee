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

#include <map>

////////////////////////////////////////////////////////////////////////////////
// ArrayType must support substr/erase and array access pattern using brackets,
// and size()

namespace cliver {

// XXX convert string interface to OrderedCollection interface
// XXX Substring insertion is a no-op
// XXX Value storage in RadixEdge, pointer ref or *copy*?
template <class ArrayType, class ElementType>
class RadixNode {
  class REdge {
   public:
    REdge(RadixNode* from, RadixNode* to, ArrayType& v) 
     : from_(from), to_(to), value_(v) /*copy*/ {}
    RadixNode* to() { return to_; }
    RadixNode* from() { return from_; }
    ArrayType& value() { return value_; }
    void set_to(RadixNode* to) { to_ = to; }
    void set_from(RadixNode* from) { from_ = from; }
    void extend(ArrayType &v) {
      value_ += v;
    }

   private:
    RadixNode *to_;
    RadixNode *from_;
    ArrayType value_;
  };

  typedef std::map<ElementType, REdge*> TransitionMap;

 public:

  RadixNode() : parent_edge_(NULL) {}

  void set_parent_edge(REdge* e) {
    parent_edge_ = e;
  }

  REdge* parent_edge() {
    return parent_edge_;
  }

  bool root() { return NULL == parent_edge_; }
  bool leaf() { return 0 == tmap_.size(); }

  RadixNode* lookup(ArrayType &v) {
    RadixNode* node = this;
    REdge *edge = NULL;
    ArrayType tmp_v = v;
    int match_position = 0;

    while (node != NULL && tmp_v.size() > 0) {
      if (node->tmap_.find(tmp_v[0]) != node->tmap_.end()) {
        edge = node->tmap_[tmp_v[0]];
        match_position = find_match_position(edge->value(), tmp_v);
        tmp_v = tmp_v.substr(match_position);
        node = edge->to();
      } else {
        node = NULL;
      }
    }
    return node;
  }

  void remove(ArrayType &v) {
    RadixNode *node = lookup(v);
    // if v is not in tree or is present at an internal node we do nothing
    if (node && node->leaf() && !node->root()) {
      std::cout << "Removing " << v << "\n";

      REdge *edge = node->parent_edge();
      RadixNode *parent = edge->from();
      parent->tmap_.erase(edge->value()[0]);
      delete edge;

      if (parent->tmap_.size() == 1) {
        REdge *leaf_edge = parent->tmap_.begin()->second;

        // If leaf_edge->from is not root node
        if (!leaf_edge->from()->root()) {
          REdge *parent_edge = leaf_edge->from()->parent_edge();
          parent_edge->extend(leaf_edge->value());

          // Delete leaf node
          delete leaf_edge->to();
          // Delete leaf edge 
          delete leaf_edge;
        }
      }
    }
  }

  void insert(ArrayType &v) {
    // Find if current node has transition on first element of v
    if (tmap_.find(v[0]) == tmap_.end()) {
      // Create new node
      RadixNode *node = new RadixNode();
      REdge *edge = new REdge(this, node, v);
      node->set_parent_edge(edge);
      tmap_[v[0]] = edge;
      return;
    }

    // Find pattern match between edge and v 
    REdge *edge = tmap_[v[0]];
    int match_position = find_match_position(edge->value(), v);
    if (match_position == v.size() &&
        match_position <= edge->value().size()) {
      return;
    }

    // Now, either split existing edge or insert at node edge points to
    if (match_position == edge->value().size()) {
      ArrayType split_array = v.substr(match_position);
      if (!edge->to()->leaf()) {
        edge->to()->insert(split_array);
      } else {
        // if not leaf, just extend edge
        edge->extend(split_array);
      }
    } else {
      RadixNode* split_node = new RadixNode();
      ArrayType split_array_top = edge->value().substr(0, match_position);
      REdge *split_edge = new REdge(this, split_node, split_array_top);
                                    
      split_node->set_parent_edge(split_edge);
      tmap_[v[0]] = split_edge;

      // update original edge
      edge->value().erase(0, match_position);
      edge->set_from(split_node);
      split_node->tmap_[edge->value()[0]] = edge;

      if (match_position != v.size()) {
        ArrayType split_array_bottom = v.substr(match_position);
        split_node->insert(split_array_bottom);
      }
    }
  }

  int find_match_position(ArrayType &v1, ArrayType &v2) {
    int i = 0;
    while (i < v1.size() && i < v2.size()) {
      if (v1[i] == v2[i])
        ++i;
      else
        break;
    }
    return i;
  }

  ~RadixNode() { 
    parent_edge_ = NULL;
  }

 private:
  REdge* parent_edge_;
  TransitionMap tmap_;
};

template <class ArrayType, class ElementType>
class RadixTree {
  typedef RadixNode<ArrayType, ElementType> RNode;
 public:
  RadixTree() {
    root_ = new RNode();
  }

  ~RadixTree() {}
  void insert(ArrayType &v) { 
    root_->insert(v);
  }

  void remove(ArrayType &v) {
    root_->remove(v);
  }

  bool lookup(ArrayType &v) { 
    RNode *res = root_->lookup(v);
    if (res) {
      //if (res->parent_edge())
      //  std::cout << "Lookup (" << v << ") = " << res->parent_edge()->value() << " \n";
      //else 
      //  std::cout << "Lookup (" << v << ") = \"\" \n";
      return true;
    }
    //std::cout << "Lookup (" << v << ") = NULL  \n";
    return false;
  }

  std::vector<ArrayType>& prefix_matches(ArrayType &v) {}

 private:
  RNode* root_;
};


} // end namespace cliver
#endif // CLIVER_RADIXTREE_H
