//===-- TrackingRadixTreeExtExt.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRACKING_RADIX_TREE_EXT_H
#define CLIVER_TRACKING_RADIX_TREE_EXT_H

#include "CVStream.h"
#include "cliver/ClientVerifier.h"
#include "cliver/RadixTree.h"

#include "klee/util/Mutex.h"

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

namespace cliver {

/// TrackingRadixTreeExt: "External" version of TrackingRadixTree that requires
/// minimal locking for concurrent access. This is a modified version of
/// TrackingRadixTree that eliminates the map of tracking objects to nodes by
/// storing a pointer to the Node in the TrackingObject itself. TrackingObject
/// must have member variables:
///
/// bool tracker_cloned
/// Node* tracker_node
///
/// NB: This class is only thread-safe for insertions. Removals can only safely
/// be done by a single thread with exlcusive access.

template <class Sequence, class Element, class TrackingObject> 
class TrackingRadixTreeExt 
: public RadixTree<Sequence, Element, DefaultSequenceComparator<Sequence>, klee::Mutex > {

 public:
  typedef RadixTree<Sequence, Element, DefaultSequenceComparator<Sequence>, klee::Mutex > This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::EdgeMapIterator EdgeMapIterator;

 private:
  typedef boost::unordered_set<TrackingObject*> TrackingObjectSet;
  typedef boost::unordered_map<TrackingObject*, Node*> TrackingObjectMap;
  typedef boost::unordered_map< Node*, TrackingObjectSet > NodeMap;

#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  /// Default Constructor
  TrackingRadixTreeExt() { 
    this->root_ = new Node();
  }

  /// Extend the edge associated with tracker by an Sequence suffix
  template<class SequenceType>
  void extend(SequenceType &suffix, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (tracks(tracker)) {
      Node *node = get_node(tracker);

      // If tracker was recently cloned, we force a node split
      if (tracker->tracker_cloned) {
        tracker->tracker_cloned = false;
        erase_node_tracker(tracker);
        if (node->get_edge(suffix.begin())) {
          insert_new_tracker(suffix, tracker, 0, node);
        } else {
          set_node(tracker, node->add_edge(suffix));
        }
      // Otherwise extend parent edge of associated node
      } else {
        assert(node->leaf());
        node->extend_parent_edge(suffix);
      }
    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      insert_new_tracker(suffix, tracker, 1);
    }
  }

  /// Extend the edge associated with tracker by a single element, id
  void extend_element(Element e, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (tracks(tracker)) {
      Node *node = get_node(tracker);

      // If tracker was recently cloned, we force a node split
      if (tracker->tracker_cloned) {
        tracker->tracker_cloned = false;
        erase_node_tracker(tracker);
        if (node->get_edge(e)) {
          Sequence s(1, e);
          insert_new_tracker(s, tracker, 2, node);
        } else {
          set_node(tracker, node->add_edge(e));
        }
      // Otherwise extend parent edge of associated node
      } else {
        assert(node->leaf());
        assert(node->root() || node->parent_edge() ==
               node->parent_edge()->from()->edge_map()[node->parent_edge()->key()]);
        node->extend_parent_edge_element(e);
      }

    // If tracker not in map, extend a new edge from the root with suffix
    } else {
      Sequence s(1, e);
      insert_new_tracker(s, tracker, 3);
    }
  }

  /// Extend the edge associated with tracker by a single element, id
  void extend_element_ext(Element e, TrackingObject* tracker) {
    extend_element(e, tracker);
  }

  /// Get the complete Sequence associated with tracker
  template<class SequenceType>
  bool tracker_get(TrackingObject* tracker, SequenceType& s) {
    // If tracker is in the node map, look up the assocated leaf node
    if (tracks(tracker)) { 
      this->get(get_node(tracker), s);
      return true;
    }
    return false;
  }

  /// When a tracker is cloned, we force the next extension to split the node,
  /// but until then, we maintain a set of trackers that have been cloned in
  /// cloned_tracking_objects_. We also associated the child tracker with the
  /// with the same node as the parent.
  bool clone_tracker(TrackingObject* child, TrackingObject* parent) {
    if (parent) {
      Node* node = get_node(parent);
      set_node(child, node);
      child->tracker_cloned = true;
      parent->tracker_cloned = true;
      child->tracker_node = node;
    }
    return false;
  }

  /// Checks that no other tracker is references the node associated with this
  /// tracker, and removes the leaf edge associated with the node if there are no
  /// other references
  void remove_tracker(TrackingObject* tracker) {
    assert(tracks(tracker));
    Node* node = get_node(tracker);
    remove_tracker(tracker, node);
  }

  void remove_tracker(TrackingObject* tracker, Node* node) {
    int ref_count = trackers_for_node_count(node);

    // If no other tracker references this node, we can safely remove it
    if (ref_count <= 1) {
      this->remove_node_check_parent(node);
    }

    // Erase the tracker from the tracker node map
    erase_node_tracker(tracker);
    tracker->tracker_cloned = false;
  }

  void remove_tracker_lazy(TrackingObject* tracker) {
    Node* node = get_node(tracker);
    klee::LockGuard guard(lock_);
    tracking_objects_[node].erase(tracker);
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {
    // Not supported with external node tracking
    return NULL;
  }

  /// Get the depth of tracker's node
  size_t tracker_depth(TrackingObject* tracker) {
    assert(tracks(tracker));
    return get_node(tracker)->depth();
  }

  /// Get the leaf element of tracker's node
  Element leaf_element(TrackingObject* tracker) {
    assert(tracks(tracker));
    Node* node = get_node(tracker);
    assert(node->leaf());
    return node->parent_edge()->back();
  }

  /// Query whether this tree has seen tracker
  inline bool tracks(TrackingObject* tracker) {
    return (tracker->tracker_node != NULL) ? true : false;
  }

  inline Node* get_node(TrackingObject *tracker) {
    return tracker->tracker_node;
  }

 private:

  inline void set_node(TrackingObject *tracker, Node* node) {
    tracker->tracker_node = node;
    {
      klee::LockGuard guard(lock_);
      tracking_objects_[node].insert(tracker);
    }
  }

  inline void erase_node_tracker(TrackingObject *tracker) {
    klee::LockGuard guard(lock_);
    TrackingObjectSet& to_set = tracking_objects_[tracker->tracker_node];
    to_set.erase(tracker);

    if (to_set.empty())
      tracking_objects_.erase(tracker->tracker_node);

     tracker->tracker_node = NULL;
  }

  void insert_new_tracker(Sequence &s, TrackingObject* tracker, 
                          int type, Node* root = NULL) {
    if (root == NULL)
      root = this->root_;

    // Check for a prefix match of this sequence
    std::pair<Node*, int> node_offset = this->prefix_lookup(s, root);

    Node* node = node_offset.first;
    int offset = node_offset.second;

    if (node != NULL) {
      // If there is a prefix of s that is an exact match in the tree
      if (offset > 0) {
        typename Sequence::iterator s_begin_offset = s.begin();
        std::advance(s_begin_offset, s.size() - offset);
        set_node(tracker, node->add_edge(s_begin_offset, s.end()));

      // If there is a prefix in the tree that is an exact match for s
      } else {
        if (offset < 0) {
          Edge* edge = node->parent_edge();
          Node* parent_node = node->parent_edge()->from();
          int pos = edge->size() + offset;
          set_node(tracker, parent_node->split_edge(edge->key(), pos));
        } else {
          // Find any other trackers that terminate at this node
          if (trackers_for_node_count(node)) {
            set_cloned_trackers_for_node(node);
          }
          set_node(tracker, node);
        }
        tracker->tracker_cloned = true;
      }
    } else {
      set_node(tracker, root->insert(s));
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

      // If parent now only has one child, merge child edge with parent edge and
      // delete parent
      if (!parent->root() && parent->degree() == 1) {

        // Check if another tracker is referencing the parent node, if not it is
        // safe to delete

        if (trackers_for_node_count(parent) == 0) {
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

  TrackingObjectSet get_trackers_for_node(Node* node) {
    klee::LockGuard guard(lock_);
    assert(tracking_objects_.count(node));
    return tracking_objects_[node];
  }

  void set_cloned_trackers_for_node(Node* node) {
    klee::LockGuard guard(lock_);
    assert(tracking_objects_.count(node));
    auto to_set = tracking_objects_.find(node);
    if (to_set != tracking_objects_.end()) {
      for (auto t : to_set->second) {
        t->tracker_cloned = true;
      }
    }
  }

  size_t trackers_for_node_count(Node* node) {
    klee::LockGuard guard(lock_);
    if (tracking_objects_.count(node) == 0)
      return 0;
    return tracking_objects_[node].size();
  }

  // Not supported, use extend() 
  virtual Node* insert(Sequence &s) { return NULL; }
  virtual Node* insert(Element e) { return NULL; }

  NodeMap tracking_objects_;
  klee::Mutex lock_;
};

} // end namespace cliver

#endif // CLIVER_TRACKING_RADIX_TREE_EXT_H
