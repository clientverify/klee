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

#include "cliver/ClientVerifier.h"
#include "cliver/RadixTree.h"
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

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

  typedef boost::unordered_set<TrackingObject*> TrackingObjectSet;
  typedef boost::unordered_map<TrackingObject*, Node*> TrackingObjectMap;
  typedef boost::unordered_map< Node*, TrackingObjectSet > NodeMap;

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
  template<class SequenceType>
  void extend(SequenceType &suffix, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (tracks(tracker)) {
      Node *node = get_node(tracker);

      // If tracker was recently cloned, we force a node split
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
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
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
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

  /// Get the complete Sequence associated with tracker
  template<class SequenceType>
  bool tracker_get(TrackingObject* tracker, SequenceType& s) {
    // If tracker is in the node map, look up the assocated leaf node
    if (tracks(tracker)) { 
      klee::TimerStatIncrementer training_timer(stats::training_time);
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
    if (tracks(parent)) {
      Node* node = get_node(parent);
      set_node(child, node);
      cloned_tracking_objects_.insert(parent);
      cloned_tracking_objects_.insert(child);
      return true;
    }
    return false;
  }

  /// Checks that no other tracker is references the node associated with this
  /// tracker, and removes the leaf edge associated with the node if there are no
  /// other references
  void remove_tracker(TrackingObject* tracker) {
    assert(tracks(tracker));
    Node* node = get_node(tracker);

    int ref_count = 0;
    if (cloned_tracking_objects_.count(tracker)) {
      // Check if another tracker is referencing this node
      ref_count = trackers_for_node_count(node);
    }
    // If no other tracker references this node, we can safely remove it
    if (ref_count <= 1) {
      this->remove_node_check_parent(node);
    }

    // Erase the tracker from the tracker node map
    erase_node_tracker(tracker);
    cloned_tracking_objects_.erase(tracker);
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {

    TrackingRadixTree *tree = new TrackingRadixTree();

    // Worklist holds a list of Node pairs, clone and original respectively
    std::stack<std::pair<Node*, Node*> > worklist; 
    worklist.push(std::make_pair(tree->root_, this->root_));

    // Node map used to copy node map
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
    typename TrackingObjectMap::iterator it = node_map_.begin(), 
        iend = node_map_.end();
    for (; it != iend; ++it) {
      assert(0 != nodemap.count(it->second));
      tree->set_node(it->first, nodemap[it->second]);
    }

    // Return deep-copy clone
    return tree;
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
    return node_map_.count(tracker) ? true : false;
  }

 private:

  inline Node* get_node(TrackingObject *tracker) {
    return node_map_[tracker];
  }

  inline void set_node(TrackingObject *tracker, Node* node) {
    node_map_[tracker] = node;
    tracking_objects_[node].insert(tracker);
  }

  inline void erase_node_tracker(TrackingObject *tracker) {
    TrackingObjectSet& to_set = tracking_objects_[node_map_[tracker]];
    to_set.erase(tracker);
    if (to_set.empty())
      tracking_objects_.erase(node_map_[tracker]);

    node_map_.erase(tracker);
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
            TrackingObjectSet& to_set = get_trackers_for_node(node);
            cloned_tracking_objects_.insert(to_set.begin(), to_set.end());
          }
          set_node(tracker, node);
        }
        cloned_tracking_objects_.insert(tracker);
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

  TrackingObjectSet& get_trackers_for_node(Node* node) {
    assert(tracking_objects_.count(node));
    return tracking_objects_[node];
  }

  size_t trackers_for_node_count(Node* node) {
    if (tracking_objects_.count(node) == 0)
      return 0;
    return tracking_objects_[node].size();
  }

  // Not supported, use extend() 
  virtual Node* insert(Sequence &s) { return NULL; }
  virtual Node* insert(Element e) { return NULL; }

  TrackingObjectMap node_map_;
  NodeMap tracking_objects_;
  TrackingObjectSet cloned_tracking_objects_;
};

#if 0
/// EdgeTrackingRadixTree: RadixTree that allows the tracking of nodes with 
/// pointers to external tracking objects

template <class Sequence, class Element, class TrackingObject> 
class EdgeTrackingRadixTree 
: public RadixTree<Sequence, Element> {

  typedef RadixTree<Sequence, Element> This;
  typedef typename This::Node Node;
  typedef typename This::Edge Edge;
  typedef typename This::EdgeMapIterator EdgeMapIterator;

  typedef boost::unordered_set<TrackingObject*> TrackingObjectSet;
  typedef boost::unordered_map<TrackingObject*, Node*> TrackingObjectMap;
  typedef boost::unordered_map< Node*, TrackingObjectSet > NodeMap;

  typedef std::pair<Edge*, size_t> EdgeOffset;

  typedef boost::unordered_set<TrackingObject*> TrackingObjectSet;
  typedef boost::unordered_map<TrackingObject*, EdgeOffset> TrackingObjectMap;
  typedef boost::unordered_map< EdgeOffset, TrackingObjectSet > EdgeOffsetMap;


#define foreach_edge(__node, __iterator) \
  EdgeMapIterator __iterator = __node->begin(); \
  EdgeMapIterator __iterator##_END = __node->end(); \
  for (; __iterator != __iterator##_END; ++__iterator)

 public:
  /// Default Constructor
  EdgeTrackingRadixTree() { 
    this->root_ = new Node();
  }

  /// Extend the edge associated with tracker by an Sequence suffix
  template<class SequenceType>
  void extend(SequenceType &suffix, TrackingObject* tracker) {
    // Look up node associated with tracker
    if (tracks(tracker)) {
      Node *node = get_node(tracker);

      // If tracker was recently cloned, we force a node split
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
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
      if (cloned_tracking_objects_.count(tracker)) {
        cloned_tracking_objects_.erase(tracker);
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

  // When a tracker is cloned, we force the next extension to split the node, but
  // until then, we maintain a set of trackers that have been cloned in
  // cloned_tracking_objects_. We also associated the child tracker with the with the same
  // node as the parent.
  bool clone_tracker(TrackingObject* child, TrackingObject* parent) {
    if (tracks(parent)) {
      Node* node = get_node(parent);
      set_node(child, node);
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
    assert(tracks(tracker));
    Node* node = get_node(tracker);

    int ref_count = 0;
    if (cloned_tracking_objects_.count(tracker)) {
      // Check if another tracker is referencing this node
      ref_count = trackers_for_node_count(node);
    }
    // If no other tracker references this node, we can safely remove it
    if (ref_count <= 1) {
      this->remove_node_check_parent(node);
    }

    // Erase the tracker from the tracker node map
    erase_node_tracker(tracker);
    cloned_tracking_objects_.erase(tracker);
  }

  /// Return a deep-copy of this RadixTree
  virtual This* clone() {

    EdgeTrackingRadixTree *tree = new EdgeTrackingRadixTree();

    // Worklist holds a list of Node pairs, clone and original respectively
    std::stack<std::pair<Node*, Node*> > worklist; 
    worklist.push(std::make_pair(tree->root_, this->root_));

    // Node map used to copy node map
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
    typename TrackingObjectMap::iterator it = node_map_.begin(), 
        iend = node_map_.end();
    for (; it != iend; ++it) {
      assert(0 != nodemap.count(it->second));
      tree->set_node(it->first, nodemap[it->second]);
    }

    // Return deep-copy clone
    return tree;
  }

  /// Get the depth of tracker's node
  size_t tracker_depth(TrackingObject* tracker) {
    assert(tracks(tracker));
    return get_node(tracker)->depth();
  }

  /// Query whether this tree has seen tracker
  inline bool tracks(TrackingObject* tracker) {
    return node_map_.count(tracker) ? true : false;
  }

 private:

  inline Node* get_node(TrackingObject *tracker) {
    return node_map_[tracker];
  }

  inline void set_node(TrackingObject *tracker, Node* node) {
    node_map_[tracker] = node;
    tracking_objects_[node].insert(tracker);
  }

  inline void erase_node_tracker(TrackingObject *tracker) {
    TrackingObjectSet& to_set = tracking_objects_[node_map_[tracker]];
    to_set.erase(tracker);
    if (to_set.empty())
      tracking_objects_.erase(node_map_[tracker]);

    node_map_.erase(tracker);
  }

  inline void set_edge_offset(TrackingObject *tracker, EdgeOffset &eo) {
    edge_offset_map_[tracker] = eo;
    tracking_objects_[eo].insert(tracker);
  }

  inline void erase_node_tracker(TrackingObject *tracker) {
    TrackingObjectSet& to_set = tracking_objects_[node_map_[tracker]];
    to_set.erase(tracker);
    if (to_set.empty())
      tracking_objects_.erase(node_map_[tracker]);

    node_map_.erase(tracker);
  }

  void insert_new_tracker(Sequence &s, TrackingObject* tracker, int type, Node* root = NULL) {
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
            TrackingObjectSet& to_set = get_trackers_for_node(node);
            cloned_tracking_objects_.insert(to_set.begin(), to_set.end());
          }
          set_node(tracker, node);
        }
        cloned_tracking_objects_.insert(tracker);
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

  TrackingObjectSet& get_trackers_for_node(Node* node) {
    assert(tracking_objects_.count(node));
    return tracking_objects_[node];
  }

  size_t trackers_for_node_count(Node* node) {
    if (tracking_objects_.count(node) == 0)
      return 0;
    return tracking_objects_[node].size();
  }

  TrackingObjectSet& get_trackers_for_edge_offset(EdgeOffset &eo) {
    assert(tracking_objects_.count(eo));
    return tracking_objects_[eo];
  }

  size_t trackers_for_edge_offset_count(EdgeOffset &eo) {
    if (tracking_objects_.count(eo) == 0)
      return 0;
    return tracking_objects_[eo].size();
  }

  // Not supported, use extend() 
  virtual Node* insert(Sequence &s) { return NULL; }
  virtual Node* insert(Element e) { return NULL; }

  TrackingObjectMap node_map_;
  NodeMap tracking_objects_;
  TrackingObjectSet cloned_tracking_objects_;
};
#endif


} // end namespace cliver

#endif // CLIVER_TRACKING_RADIX_TREE_H
