//===-- ExecutionTree.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// ??? Make CVExecutionState a template parameter to ExecutionTree?
// TODO Define add/update/remove semantics for ExecutionTree template class
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TREE_H
#define CLIVER_EXECUTION_TREE_H

#include "cliver/EditDistance.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/tree.h"

#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Analysis/Trace.h"

#include <set>
#include <map>
#include <iostream>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

namespace llvm {
	class BasicBlock;
}

namespace klee {
	class KInstruction;
	class KModule;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class ExecutionTrace {
 public:
  typedef std::list<const klee::KBasicBlock *> BasicBlockList;
  typedef std::vector<unsigned> SerializedBasicBlockList;

  typedef BasicBlockList::iterator iterator;
  typedef BasicBlockList::const_iterator const_iterator;

  ExecutionTrace() : serialized_basic_blocks_(0) {}
  ExecutionTrace(const klee::KBasicBlock* bb) 
    : serialized_basic_blocks_(0) { basic_blocks_.push_back(bb); }

  void push_back(const ExecutionTrace& etrace);
  void push_back_bb(const klee::KBasicBlock* kbb);
  void push_front(const ExecutionTrace& etrace);
  void push_front_bb(const klee::KBasicBlock* kbb);

  iterator begin() { return basic_blocks_.begin(); }
  iterator end() { return basic_blocks_.end(); }
  const_iterator begin() const { return basic_blocks_.begin(); }
  const_iterator end() const { return basic_blocks_.end(); }

  bool operator==(const ExecutionTrace& b) const { return true; }
  bool operator!=(const ExecutionTrace& b) const { return false; }

  size_t size() const { return basic_blocks_.size(); } 

  void deserialize(klee::KModule* kmodule);

 protected:
  friend class boost::serialization::access;
  template<class Archive>
  void save(Archive & ar, const unsigned int version) const
  {
    serialized_basic_blocks_ = new SerializedBasicBlockList();

    for (iterator it=begin(), ie=end(); it!=ie; ++it)
      serialized_basic_blocks_->push_back((*it)->id);

    ar & *serialized_basic_blocks_;
    delete serialized_basic_blocks_;
    serialized_basic_blocks_ = 0;
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version)
  {
    serialized_basic_blocks_ = new SerializedBasicBlockList();
    ar & *serialized_basic_blocks_;
  }

 private:
  BasicBlockList basic_blocks_;
  SerializedBasicBlockList* serialized_basic_blocks_;
};

////////////////////////////////////////////////////////////////////////////////

template<class ListDataType>
class ExecutionTree : public tree<ListDataType> {

#define foreach_child(__node,__iterator) \
  typename tree<ListDataType>::pre_order_iterator __node##_BASE(__node); \
  typename tree<ListDataType>::children_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_children_iterator(__node##_BASE), \
       __iterator##_END = this->end_children_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_parent(__node,__iterator) \
  typename tree<ListDataType>::pre_order_iterator __iterator(__node); \
   for (; __iterator != this->root(); __iterator = this->parent(__iterator) )

#define foreach_leaf(__node,__iterator) \
  typename tree<ListDataType>::pre_order_iterator __node##_BASE(__node); \
  typename tree<ListDataType>::leaf_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_leaf_iterator(__node##_BASE), \
       __iterator##_END = this->end_leaf_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

  typedef tree_node_<ListDataType> Node;

  struct NodeRef {
    NodeRef() : node(NULL), count(1) {}
    Node* node;
    int count;
  };

  typedef std::map<CVExecutionState*, NodeRef*> StateNodeMap;
  typedef std::map<Node*, NodeRef*> NodeMap;

  NodeRef* increment(NodeRef* ref) {
    ref->count++;
    return ref;
  }

  NodeRef* decrement(NodeRef* ref) {
    assert(ref->count > 0);
    ref->count--;
    if (ref->count == 0) {
      typename tree<ListDataType>::pre_order_iterator node_it(ref->node);
      if (this->number_of_children(node_it) == 0) {
        node_map_.erase(ref->node);
        this->erase(node_it);
        delete ref;
        return NULL;
      }
    }
    return ref;
  }

 public:

  ExecutionTree() {
    ListDataType root_data;
    set_root(root_data);
  }

  const ListDataType& get_state_data(CVExecutionState* state) {}

  void get_path(CVExecutionState* state,
                ListDataType& path) {
    assert(has_state(state));
    Node* node = state_map_[state]->node;
    assert(node);
    foreach_parent (node, parent_it) {
      path.push_front(*parent_it);
    }
  }

  bool has_state(CVExecutionState* state) {
    return state_map_.count(state) != 0;
  }

  void remove_state(CVExecutionState* state) {
    assert(has_state(state));
    decrement(state_map_[state]);
    state_map_.erase(state);
  }

  void add_state(CVExecutionState* state, CVExecutionState* parent) {
    assert(!has_state(state));
    if (parent) {
      assert(has_state(parent));
      state_map_[state] = increment(state_map_[parent]);
    } else {
      state_map_[state] = new NodeRef();
    }
  }

  void update_state(CVExecutionState* state, ListDataType &data) {
    assert(has_state(state));
    NodeRef *ref = state_map_[state];
    if (ref->node == NULL) {
      assert(ref->count == 1);
      ref->node = append_child(this->root(), data).node;
      node_map_[ref->node] = ref;
    } else {
      Node *parent_node = ref->node;
      bool found_child_match = false;
      foreach_child (parent_node, child_it) {
        if (*(child_it) == data) {
          found_child_match = true;
          assert(node_map_.count(child_it.node));
          state_map_[state] = increment(node_map_[child_it.node]);
        }
      }
      if (!found_child_match) {
        NodeRef *new_ref = new NodeRef();
        new_ref->node = append_child(parent_node, data).node;
        state_map_[state] = new_ref;
        node_map_[new_ref->node] = new_ref;
      }
      ref = decrement(ref);
      assert(ref);
    }
  }

 private:
  // Map of States to NodeRef objects
  StateNodeMap state_map_;
  // Map of Nodes to NodeRef objects
  NodeMap node_map_;
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManager : public ExecutionObserver {
 public:
  ExecutionTreeManager();
  void notify(ExecutionEvent ev);
 private:
  std::vector< ExecutionTree<ExecutionTrace>* > trees_;

};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_EXECUTION_TREE_H

