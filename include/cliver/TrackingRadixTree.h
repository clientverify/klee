//===-- TrackingRadixTree.h ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRACKING_RADIX_TREE_H
#define CLIVER_TRACKING_RADIX_TREE_H

#include "cliver/RadixTree.h"
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <set>

namespace cliver {

/// TrackingRadixTree: RadixTree that allows the tracking of nodes with 
/// pointers to external tracking objects

template <class Sequence, class Element, class TrackingObject> 
class TrackingRadixTree 
: public RadixTree<Sequence, Element> {

  typedef RadixTree<Sequence, Element> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::EdgeMapIterator EdgeMapIterator;

  typedef boost::unordered_map<TrackingObject*, Node*> TrackingObjectNodeMap;
  typedef boost::unordered_set<TrackingObject*> TrackingObjectSet;
  
#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  /// Default Constructor
  TrackingRadixTree() { 
    this->root_ = new Node();
  }

  /// Extend the edge associated with tracker by an Sequence suffix
  void extend(Sequence &suffix, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (node_map_.count(tracker)) {
      Node *node = node_map_[tracker];

      // If tracker was recently cloned, we force a node split
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
        if (node->get_edge(suffix.begin())) {
          Sequence full_s;
          node->get(full_s);
          full_s.insert(full_s.end(), suffix.begin(), suffix.end());
          insert_new_tracker(full_s, tracker);
        } else {
          node_map_[tracker] = node->add_edge(suffix);
        }
      // Otherwise extend parent edge of associated node
      } else {
        assert(node->leaf());
        node->extend_parent_edge(suffix);
      }
    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      insert_new_tracker(suffix, tracker);
    }
  }

  /// Extend the edge associated with tracker by a single element, id
  void extend(Element e, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (node_map_.count(tracker)) {
      Node *node = node_map_[tracker];

      // If tracker was recently cloned, we force a node split
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
        if (node->get_edge(e)) {
          Sequence full_s;
          node->get(full_s);
          full_s.insert(full_s.end(), e);
          insert_new_tracker(full_s, tracker);
        } else {
          node_map_[tracker] = node->add_edge(e);
        }
      // Otherwise extend parent edge of associated node
      } else {
        assert(node->leaf());
        assert(node->root() || node->parent_edge()->from()->edge_map()[node->parent_edge()->key()] == node->parent_edge());
        node->extend_parent_edge_element(e);
      }

    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      Sequence s(1, e);
      insert_new_tracker(s, tracker);
    }
  }

  /// Get the complete Sequence associated with tracker
  bool tracker_get(TrackingObject* tracker, Sequence& s) {
    // If tracker is in the node map, look up the assocated leaf node
    if (node_map_.count(tracker)) { 
      this->get(node_map_[tracker], s);
      return true;
    }
    return false;
  }

  // When a tracker is cloned, we force the next extension to split the node, but
  // until then, we maintain a set of trackers that have been cloned in
  // cloned_tracking_objects_. We also associated the child tracker with the with the same
  // node as the parent.
  bool clone_tracker(TrackingObject* child, TrackingObject* parent) {
    if (node_map_.count(parent)) {
      Node* node = node_map_[parent];
      //if (!node->leaf())
      //   return false;
      node_map_[child] = node;
      cloned_tracking_objects_.insert(parent);
      cloned_tracking_objects_.insert(child);
      return true;
    }
    return false;
  }

  // Checks that no other tracker is references the node associated with this
  // tracker, and removes the leaf edge associated with the node if there are no
  // other references
  void remove_tracker(TrackingObject* tracker) {
    assert(node_map_.count(tracker));
    Node* node = node_map_[tracker];

    int ref_count = 0;
    if (cloned_tracking_objects_.count(tracker)) {
      // Iterate over all nodes to see if another tracker is referencing this node
      typename TrackingObjectNodeMap::iterator it = node_map_.begin(), 
          iend = node_map_.end();
      for (; it != iend && ref_count < 2; ++it) {
        if (it->second == node) ref_count++;
      }
    }
    // If no other tracker references this node, we can safely remove it
    if (ref_count <= 1) {
      cloned_tracking_objects_.erase(tracker);
      this->remove_node_check_parent(node);
    }

    // Erase the tracker from the tracker node map
    node_map_.erase(tracker);
  }

  /// Query whether this tree has seen tracker
  bool tracks(TrackingObject* tracker) {
    return node_map_.count(tracker) ? true : false;
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {

    TrackingRadixTree *tree = new TrackingRadixTree();

    // Worklist holds a list of Node pairs, clone and original respectively
    std::stack<std::pair<Node*, Node*> > worklist; 
    worklist.push(std::make_pair(tree->root_, this->root_));

    // Node map used to copy node_map_
    std::map<Node*, Node*> nodemap;
    nodemap[this->root_] = tree->root_;

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

        // Add the node pair to the node map
        nodemap[(Node*)src_edge->to()] = dst_to_node;
      }
    }

    // Create node_map_ for clone using same trackers but cloned Node ptrs
    typename TrackingObjectNodeMap::iterator it = node_map_.begin(), 
        iend = node_map_.end();
    for (; it != iend; ++it) {
      assert(0 != nodemap.count(it->second));
      tree->node_map_[it->first] = nodemap[it->second];
    }

    // Return deep-copy clone
    return tree;
  }

  size_t depth(TrackingObject *tracker) {
    assert(node_map_.count(tracker));
    Node* node = node_map_[tracker];
    return node->depth();
  }

 private:
  void insert_new_tracker(Sequence &s, TrackingObject* tracker) {
    // Check for a prefix match of this sequence
    std::pair<Node*, int> node_offset = this->prefix_lookup(s);

    Node* node = node_offset.first;
    int offset = node_offset.second;

    if (node != NULL) {
      // If there is a prefix of s that is an exact match in the tree
      if (offset > 0) {
        node_map_[tracker] = node->add_edge(s.begin() + (s.size() - offset), 
                                            s.end());
      // If there is a prefix in the tree that is an exact match for s
      } else {

        if (offset < 0) {
          Edge* edge = node->parent_edge();
          Node* parent_node = node->parent_edge()->from();
          int pos = edge->size() + offset;
          node_map_[tracker] = parent_node->split_edge(edge->key(), pos);
        } else {
          // Find any other trackers that terminate at this node
          TrackingObjectSet to_set;
          get_trackers_for_node(node, to_set);
          cloned_tracking_objects_.insert(to_set.begin(), to_set.end());
          node_map_[tracker] = node;
        }
        cloned_tracking_objects_.insert(tracker);
      }
    } else {
      node_map_[tracker] = this->root_->insert(s);
    }
  }

  void get_trackers_for_node(Node* node, TrackingObjectSet &to_set) {
    // Iterate over all nodes to see if another tracker is referencing this node
    typename TrackingObjectNodeMap::iterator it = node_map_.begin(), 
        iend = node_map_.end();
    for (; it != iend; ++it) {
      if (it->second == node)
        to_set.insert(it->first);
    }
  }

  bool remove_node_check_parent(Node *node) {
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

        bool has_ref = false;
        // Iterate over all nodes to see if another tracker is referencing the
        // parent node, if not it is safe to delete
        typename TrackingObjectNodeMap::iterator it = node_map_.begin(), 
            iend = node_map_.end();
        for (; it != iend && !has_ref; ++it) {
          if (it->second == parent) has_ref = true;
        }

        if (!has_ref) {
          Edge *merge_edge = parent->edge_map().begin()->second;

          parent->parent_edge()->extend(merge_edge->begin(), merge_edge->end());
          parent->parent_edge()->set_to(merge_edge->to());
          parent->parent_edge()->to()->set_parent_edge(parent->parent_edge());

          delete parent;
          delete merge_edge;
        }
      }
      return true;
    }
    return false;
  }

  // Not supported, use extend() 
  virtual Node* insert(Sequence &s) { return NULL; }
  virtual Node* insert(Element e) { return NULL; }

  TrackingObjectNodeMap node_map_;
  TrackingObjectSet cloned_tracking_objects_;
};

} // end namespace cliver

#endif // CLIVER_TRACKING_RADIX_TREE_H
