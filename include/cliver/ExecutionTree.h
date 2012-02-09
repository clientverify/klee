//===-- ExecutionTree.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// ??? Make CVExecutionState a template parameter to ExecutionTree?
// TODO Define add/update/remove semantics for ExecutionTree template class
// TODO optimize get_path
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
  class Tag {

  };
  typedef std::vector<const klee::KBasicBlock *> BasicBlockList;
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

  inline const klee::KBasicBlock* operator[](unsigned i) { return basic_blocks_[i]; }
  inline const klee::KBasicBlock* operator[](unsigned i) const { return basic_blocks_[i]; }

  bool operator==(const ExecutionTrace& b) const;
  bool operator!=(const ExecutionTrace& b) const;
  bool operator<(const ExecutionTrace& b) const;

  size_t size() const { return basic_blocks_.size(); } 

	void write(std::ostream &os);
	void read(std::ifstream &is, klee::KModule* kmodule);

 protected:
  void deserialize(klee::KModule* kmodule);
  friend class boost::serialization::access;
  template<class Archive>
  void save(Archive & ar, const unsigned int version) const
  {
    SerializedBasicBlockList serialized_basic_blocks;

    for (const_iterator it=begin(), ie=end(); it!=ie; ++it)
      serialized_basic_blocks.push_back((*it)->id);

    ar & serialized_basic_blocks;
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version)
  {
    serialized_basic_blocks_ = new SerializedBasicBlockList();
    ar & *serialized_basic_blocks_;
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

 private:
  BasicBlockList basic_blocks_;
  SerializedBasicBlockList* serialized_basic_blocks_;
};

////////////////////////////////////////////////////////////////////////////////

template<class Score, 
         class SequenceType, 
         class ValueType>
class EditDistanceColumn {
 public:
  EditDistanceColumn() {}
  EditDistanceColumn(const SequenceType s_elem)
    : s_elem_(s_elem) {}

  void update_banded(const EditDistanceColumn* prev, const SequenceType& t,
                     int band_width) {}

  void update(const EditDistanceColumn* prev, const SequenceType& t) {
    int start_index = costs_.size();
    int end_index = t.size();

    ValueType c1,c2,c3;
    if (prev == NULL) {
      for (int j=start_index; j<end_index; ++j) {
        int s_pos=0, t_pos=j-1;
        set_cost(j, (ValueType)j);
      }
    } else {
      for (int j=start_index; j<end_index; ++j) {
        int s_pos=0, t_pos=j-1;
        c1 = prev->cost(j-1) + score_.match(s_elem_,  t, s_pos, t_pos);
        c2 = this->cost(j-1) + score_.insert(s_elem_, t, s_pos, t_pos);
        c3 = prev->cost(j)   + score_.del(s_elem_,    t, s_pos, t_pos);
        set_cost(j, std::min(c1, std::min(c2, c3)));
      }
    }
  }

  const SequenceType& s() { return s_elem_; }

  ValueType cost(int j) const {
    assert(j > costs_);
    return costs_[j];
  }

  void set_cost(int j, ValueType cost) {
    if (costs_.capacity() < j)
      costs_.resize(costs_.size()*2);
    costs_[j] = cost;
  }

 private:
  SequenceType s_elem_; // size() == 1, one element of the static array
  std::vector<ValueType> costs_;
  Score score_;
};

template<class EDNodeTy, class SeqTy, class ValTy>
class EditDistanceTree : public tree<EDNodeTy> {

#define foreach_child(__node,__iterator) \
  typename tree<EDNodeTy>::pre_order_iterator __node##_BASE(__node); \
  typename tree<EDNodeTy>::children_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_children_iterator(__node##_BASE), \
       __iterator##_END = this->end_children_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_pre_order(__node,__iterator) \
  typename tree<EDNodeTy>::pre_order_iterator __node##_BASE(__node); \
  typename tree<EDNodeTy>::pre_order_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_pre_order_iterator(__node##_BASE), \
       __iterator##_END = this->end_pre_order_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_leaf(__node,__iterator) \
  typename tree<EDNodeTy>::pre_order_iterator __node##_BASE(__node); \
  typename tree<EDNodeTy>::leaf_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_leaf_iterator(__node##_BASE), \
       __iterator##_END = this->end_leaf_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_parent(__node,__iterator) \
  typename tree<EDNodeTy>::pre_order_iterator __iterator(__node); \
   for (; __iterator != this->root(); __iterator = this->parent(__iterator) )


  typedef tree_node_<EDNodeTy> Node;

 public:
  EditDistanceTree() {
    EDNodeTy root_data;
    this->set_root(root_data);
  }
  ~EditDistanceTree() {}

  void append_sequence(const SeqTy& t) {
    SeqTy updated_t(t_);
    updated_t.push_back(t);
  }

  void insert(const SeqTy& s, const std::string* seq_name=NULL) {
    typename SeqTy::const_iterator it = s.begin(), ie = s.end();
    Node* curr_node = this->root().node;
    for (; it!=ie; ++it) {
      Node* child_node = NULL;
      foreach_child (curr_node, child_it) {
        if ((*child_it).s().size() == 1 && (*child_it).s()[0] == *it) {
          child_node = child_it.node;
          break;
        }
      }
      if (child_node == NULL) {
        SeqTy tmp_seq(*it);
        EDNodeTy data(tmp_seq);
        curr_node = this->append_child(curr_node, data).node;
      } else {
        curr_node = child_node;
      }
      if (seq_name)
        name_map_[curr_node].push_back(seq_name);
    }
  }

  void compute_t(const SeqTy& t) {
    Node* root_node = this->root().node;
    foreach_pre_order (root_node, it) {
      Node* parent = this->parent(it).node;
      EDNodeTy* prev_data = (parent ? &(parent->data) : NULL);
      (*it).update(prev_data, t);
    }
  }

  void get_all_sequences(std::vector<SeqTy>& sequence_list,
                         std::vector<const std::string*>* name_list=NULL) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      SeqTy seq;
      get_path(leaf_it.node, seq);
      sequence_list.push_back(seq);
      if (name_list) {
        assert(name_map_.count(leaf_it.node) && name_map_[leaf_it.node].size() == 1);
        const std::string* name = name_map_[leaf_it.node][0];
        name_list->push_back(name);
      }
    }
  }

 private:

  void get_path(Node* node, SeqTy& path) {
    foreach_parent (node, parent_it) {
      path.push_front((*parent_it).s());
    }
  }

  SeqTy t_;
  std::map<Node*, std::vector<const std::string*> > name_map_;

#undef foreach_child
#undef foreach_pre_order
#undef foreach_leaf
#undef foreach_parent
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
    this->set_root(root_data);
  }

  ~ExecutionTree() {
    std::set<NodeRef*> to_delete;
    {
      typename StateNodeMap::iterator
        it = state_map_.begin(), ie = state_map_.end();
      for (; it!=ie; ++it)
        to_delete.insert(it->second);
    }
    {
      typename NodeMap::iterator
        it = node_map_.begin(), ie = node_map_.end();
      for (; it!=ie; ++it)
        to_delete.insert(it->second);
    }
    {
      typename std::set<NodeRef*>::iterator
        it = to_delete.begin(), ie = to_delete.end();
      for (; it!=ie; ++it)
        delete *it;
    }
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
      ref->node = this->append_child(this->root(), data).node;
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
        new_ref->node = this->append_child(parent_node, data).node;
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

#undef foreach_child
#undef foreach_parent
#undef foreach_leaf
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManager : public ExecutionObserver {
 public:
  typedef ExecutionTree<ExecutionTrace> ExecutionTraceTree;
  ExecutionTreeManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
 protected:
  std::list< ExecutionTraceTree* > trees_;
  ClientVerifier *cv_;

};

class TrainingExecutionTreeManager : public ExecutionTreeManager {
 public:
  TrainingExecutionTreeManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 private:

};

class VerifyExecutionTreeManager : public ExecutionTreeManager {
 public:
  VerifyExecutionTreeManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 private:
  int read_traces(std::vector<std::string> &filename_list);
  std::map<ExecutionTrace,std::string> training_trace_map_;
  CVExecutionState* last_state_seen_;

};

class TrainingTestExecutionTreeManager : public ExecutionTreeManager {
 public:
  typedef EditDistanceTree< EditDistanceColumn<Score<ExecutionTrace,int>,ExecutionTrace,int>, ExecutionTrace, int > EDTree;
  TrainingTestExecutionTreeManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 private:
  int read_traces(std::vector<std::string> &filename_list);
  std::map<ExecutionTrace,std::string> training_trace_map_;
  EDTree* ed_tree_;

};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManagerFactory {
 public:
  static ExecutionTreeManager* create(ClientVerifier *cv);
};


} // end namespace cliver

#endif // CLIVER_EXECUTION_TREE_H

