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

#include <boost/shared_ptr.hpp>
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
  typedef unsigned BasicBlockID;
  typedef std::vector<BasicBlockID> BasicBlockList;

  typedef BasicBlockList::iterator iterator;
  typedef BasicBlockList::const_iterator const_iterator;

  ExecutionTrace() {}
  ExecutionTrace(BasicBlockID bb) { this->push_back(bb); }

  void push_back(BasicBlockID kbb) {
    basic_blocks_.push_back(kbb);
  }

  void push_front(BasicBlockID kbb) {
    basic_blocks_.insert(basic_blocks_.begin(), kbb);
  }

  void push_back(const ExecutionTrace& etrace);
  void push_front(const ExecutionTrace& etrace);

  iterator begin() { return basic_blocks_.begin(); }
  iterator end() { return basic_blocks_.end(); }
  const_iterator begin() const { return basic_blocks_.begin(); }
  const_iterator end() const { return basic_blocks_.end(); }

  inline BasicBlockID operator[](unsigned i) { return basic_blocks_[i]; }
  inline BasicBlockID operator[](unsigned i) const { return basic_blocks_[i]; }

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
  void save(Archive & ar, const unsigned int version) const {
    ar & basic_blocks_;
  }

  template<class Archive>
  void load(Archive & ar, const unsigned int version) {
    ar & basic_blocks_;
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()

 private:
  BasicBlockList basic_blocks_;
};

////////////////////////////////////////////////////////////////////////////////

template<class ScoreType,
         class SequenceType, 
         class ElementType,
         class ValueType>
class EditDistanceColumn {
 public:
  EditDistanceColumn() : prev_(NULL), depth_(0) {}
  // Supply constructor with prev?
  EditDistanceColumn(const EditDistanceColumn* prev,
                     const ElementType &s_elem)
    : prev_(prev), s_elem_(s_elem) {
    assert(prev_);
    depth_ = prev_->depth_+1;
  }

  //void update_banded(const EditDistanceColumn* prev, const SequenceType& t,
  //                  int band_width) {}

  void update(const EditDistanceColumn* prev, const SequenceType& t) {
    int start_index = costs_.size();
    int end_index = t.size() + 1;

    // XXX FIXME TODO TEMP XXX
    assert(prev == prev_);
    start_index = 0;
    costs_.clear();
    if (costs_.size() <= t.size()+1)
      costs_.resize(t.size()+1);
    // XXX FIXME TODO TEMP XXX


    ValueType c1,c2,c3;
    if (depth_ == 0) {
      for (int j=start_index; j<end_index; ++j) {
        int s_pos=0, t_pos=j-1;
        set_cost(j, (ValueType)j);
      }
    } else {
      for (int j=start_index; j<end_index; ++j) {
        if (j == 0) {
          set_cost(j, depth_);
        } else {
          int s_pos=0, t_pos=j-1;
          c1 = prev->cost(j-1) + score_.match(s_elem_,  t, s_pos, t_pos);
          c2 = this->cost(j-1) + score_.insert(s_elem_, t, s_pos, t_pos);
          c3 = prev->cost(j)   + score_.del(s_elem_,    t, s_pos, t_pos);
          set_cost(j, std::min(c1, std::min(c2, c3)));
        }
      }
    }
    //for (int i = 0; i < end_index; ++i)
    //  std::cout << cost(i) << ",";
    //std::cout << "\n";
  }

  const ElementType& s() { return s_elem_; }

  inline ValueType cost(int j) const {
    assert(j < costs_.size());
    return costs_[j];
  }

  inline ValueType edit_distance() const {
    return costs_[costs_.size()-1];
  }

  inline void set_cost(int j, ValueType cost) {
    costs_[j] = cost;
  }

  std::vector<ValueType>& cost_vector() { return costs_; }

 private:
  const EditDistanceColumn* prev_;
  ElementType s_elem_; // one element of the s sequence
  int depth_;
  ScoreType score_;
  std::vector<ValueType> costs_;
};

////////////////////////////////////////////////////////////////////////////////
// TODO BOOST shared pointer

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
  ~EditDistanceTree() { this->clear(); }

  void append_sequence(const SeqTy& t) {}

  void insert(const SeqTy& s, const std::string* seq_name=NULL) {
    Node* curr_node = this->root().node;
    typename SeqTy::const_iterator it = s.begin(), ie = s.end();
    for (; it!=ie; ++it) {
      Node* child_node = NULL;
      foreach_child (curr_node, child_it) {
        //if ((*child_it).s().size() == 1 && (*child_it).s()[0] == *it) {
        if ((*child_it).s() == *it) {
          child_node = child_it.node;
          break;
        }
      }
      if (child_node == NULL) {
        //SeqTy tmp_seq(*it);
       EDNodeTy data(&(curr_node->data), *it);
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
      EDNodeTy* parent_data = parent ? &(parent->data) : NULL;
      (*it).update(parent_data, t);
    }
  }

  void get_all_distances(std::vector<ValTy>& edit_distance_list,
                         std::vector<const std::string*>* name_list=NULL) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      EDNodeTy* data = &(*(leaf_it));
      edit_distance_list.push_back(data->edit_distance());
      if (name_list) {
        assert(name_map_.count(leaf_it.node) && name_map_[leaf_it.node].size() == 1);
        const std::string* name = name_map_[leaf_it.node][0];
        name_list->push_back(name);
      }
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

  void get_cost_matrix(const std::string* name,
                       std::vector< std::vector<ValTy> > &cost_matrix) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      assert(name_map_.count(leaf_it.node) && name_map_[leaf_it.node].size() == 1);
      if (name == name_map_[leaf_it.node][0]) {
        cost_matrix.insert(cost_matrix.begin(),(*leaf_it).cost_vector());
        get_cost_matrix_for_node(leaf_it.node, cost_matrix);
        return;
      }
    }
  }

  void get_node_list(const std::string* name,
                     std::vector< EDNodeTy > &node_list) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      assert(name_map_.count(leaf_it.node) && name_map_[leaf_it.node].size() == 1);
      if (name == name_map_[leaf_it.node][0]) {

        foreach_parent (leaf_it.node, parent_it) {
          node_list.insert(node_list.begin(),(*parent_it));
        }
        node_list.insert(node_list.begin(),(*(this->root())));
        return;
      }
    }
  }

 private:

  void get_cost_matrix_for_node(Node* node, 
                                std::vector< std::vector<ValTy> > &cost_matrix) {
    foreach_parent (node, parent_it) {
      cost_matrix.insert(cost_matrix.begin(),(*parent_it).cost_vector());
    }
    cost_matrix.insert(cost_matrix.begin(),(*(this->root())).cost_vector());
  }

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

template<class DataType, class ListDataType>
class ExecutionTree : public tree<DataType> {

#define foreach_child(__node,__iterator) \
  typename tree<DataType>::pre_order_iterator __node##_BASE(__node); \
  typename tree<DataType>::children_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_children_iterator(__node##_BASE), \
       __iterator##_END = this->end_children_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_parent(__node,__iterator) \
  typename tree<DataType>::pre_order_iterator __iterator(__node); \
   for (; __iterator != this->root(); __iterator = this->parent(__iterator) )

#define foreach_leaf(__node,__iterator) \
  typename tree<DataType>::pre_order_iterator __node##_BASE(__node); \
  typename tree<DataType>::leaf_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_leaf_iterator(__node##_BASE), \
       __iterator##_END = this->end_leaf_iterator(__node##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

  typedef tree_node_<DataType> Node;

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
      typename tree<DataType>::pre_order_iterator node_it(ref->node);
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
    DataType root_data;
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

  DataType get_state_data(CVExecutionState* state) {}

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

  void update_state(CVExecutionState* state, DataType data) {
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

// ExecutionTrace Score
typedef Score< ExecutionTrace, unsigned, int> ETScore;

// EditDistance trees
typedef EditDistanceColumn< ETScore, ExecutionTrace, unsigned, int > EDColumn;
typedef EditDistanceTree< EDColumn, ExecutionTrace, int > EDTree;

// EditDistance flavors
typedef EditDistanceTable<ETScore,ExecutionTrace,int> ExecutionTraceEDT;
typedef EditDistanceRow<ETScore,ExecutionTrace,int>   ExecutionTraceEDR;
typedef EditDistanceUkkonen<ETScore,ExecutionTrace,int> ExecutionTraceEDU;
typedef EditDistanceUkkonen<ETScore,ExecutionTrace,int> ExecutionTraceEDU;
typedef EditDistanceDynamicUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUD;
typedef EditDistanceStaticUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUS;
typedef EditDistanceFullUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUF;

// Default EditDistance
typedef ExecutionTraceEDR ExecutionTraceED;

////////////////////////////////////////////////////////////////////////////////

// Basic ExecutionTree
typedef ExecutionTree<unsigned, ExecutionTrace> ExecutionTraceTree;

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManager : public ExecutionObserver {
 public:
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

