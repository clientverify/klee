//===-- ExecutionTraceTree.h ------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_TREE_H
#define CLIVER_EXECUTION_TRACE_TREE_H

#if 0
#include "cliver/RadixTree.h"
#include <set>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

typedef unsigned int BasicBlockID;
typedef std::vector<BasicBlockID> ExecutionTrace;

////////////////////////////////////////////////////////////////////////////////

class CVExecutiontracker;

/// TrackingRadixTree: RadixTree that allows the tracking of nodes with 
/// pointers to external tracking objects

template <Sequence, Element, TrackingObject> 
class TrackingRadixTree 
: public RadixTree<Sequence, Element> {

  typedef RadixTree<Sequence, Element> This;
  typedef This::Node Node;
  typedef This::Edge Edge;
  typedef This::EdgeMapIterator EdgeMapIterator;

  typedef std::map<TrackingObject*, Node*> TrackingObjectNodeMap;
  
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
        node_map_[tracker] = node->add_edge(suffix);
      // Otherwise extend parent edge of associated node
      } else {
        node->extend_parent_edge(suffix);
      }

    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      node_map_[tracker] = this->insert(suffix);
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
        node_map_[tracker] = node->add_edge(e);
      // Otherwise extend parent edge of associated node
      } else {
        node->extend_parent_edge(e);
      }

    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      node_map_[tracker] = this->insert(e);
    }
  }

  /// Get the complete Sequence associated with tracker
  void get(TrackingObject* tracker, Sequence& s) {
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
  void tracker_clone(TrackingObject* child, TrackingObject* parent) {
    assert(node_map_.count(parent));
    Node* node = node_map_[parent];
    node_map_[child] = node;
    cloned_tracking_objects_.insert(parent);
    cloned_tracking_objects_.insert(child);
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
      TrackingObjectNodeMap::iterator it = node_map_.begin(), 
          iend = node_map_.end();
      for (; it != iend && ref_count < 2; ++it) {
        if (it->second == node) ref_count++;
      }
    }
    // If no other tracker references this node, we can safely remove it
    if (ref_count < 2) {
      cloned_tracking_objects_.erase(tracker);
      this->remove_node(node);
    }

    // Erase the tracker from the tracker node map
    node_map_.erase(tracker);
  }

  /// Query whether this tree has seen tracker
  bool has_tracker(TrackingObject* tracker) {
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
    TrackingObjectNodeMap::iterator it = node_map_.begin(), 
        iend = node_map_.end();
    for (; it != iend; ++it) {
      assert(0 != nodemap.count(it->second));
      tree->node_map_[it->first] = nodemap[it->second];
    }

    // Return deep-copy clone
    return tree;
  }

 private:
  TrackingObjectNodeMap node_map_;
  std::set<TrackingObject*> cloned_tracking_objects_;
};

} // end namespace cliver
#endif

#endif // CLIVER_EXECUTION_TRACE_TREE_H
