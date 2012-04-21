//===-- ExecutionTree.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO Rename this file to ExecutionTraceManager.h
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TREE_H
#define CLIVER_EXECUTION_TREE_H

#include "cliver/EditDistance.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/TrackingRadixTree.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/Training.h"


#include "cliver/tree.h"

#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Analysis/Trace.h"

#include <set>
#include <list>
#include <map>
#include <iostream>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>

#define MAX(x,y) (((x)<(y))?(y):(x))
#define MIN(x,y) (!((y)<(x))?(x):(y))

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

typedef TrackingRadixTree< ExecutionTrace, BasicBlockID, CVExecutionState > 
    ExecutionTraceTree;


////////////////////////////////////////////////////////////////////////////////

class SocketEvent;

#if 0
class SocketEventEditDistance {
 public:
  SocketEventEditDistance() {}
  virtual int edit_distance(const SocketEvent* a, const SocketEvent* b) {
    return 0;
  }
};

class SocketEventEditDistanceXpilot : public SocketEventEditDistance {
 public:
  SocketEventEditDistanceXpilot() {}
  int edit_distance(const SocketEvent* a, const SocketEvent* b);
};

class SocketEventEditDistanceTetrinet: public SocketEventEditDistance {
 public:
  SocketEventEditDistanceTetrinet() {}
  int edit_distance(const SocketEvent* a, const SocketEvent* b);
};
#endif

////////////////////////////////////////////////////////////////////////////////

#if 0
template<class ScoreType, 
         class SequenceType, 
         class ElementType, 
         class ValueType>
class EditDistanceRowColumn {
  typedef boost::shared_ptr<EditDistanceRowColumn> EditDistanceRowColumnPtr;

 public:
  EditDistanceRowColumn() :
      depth_(0), 
      row_(0),
      full_costs_(NULL) {costs_[0] = costs_[1] = 0;}

  EditDistanceRowColumn(const ElementType &s_elem) :
      s_elem_(s_elem), 
      depth_(0), 
      row_(0),
      full_costs_(NULL) {costs_[0] = costs_[1] = 0;}

  EditDistanceRowColumn(EditDistanceRowColumnPtr e,
                        EditDistanceRowColumnPtr prev) :
      depth_(0), 
      row_(0),
      full_costs_(NULL) {
    costs_[0] = costs_[1] = 0;
    copy(e, prev);
  }

  ~EditDistanceRowColumn() {
    if (full_costs_)
      delete full_costs_;
  }

  void initialize(EditDistanceRowColumnPtr prev,
                  unsigned children_count) {
    prev_ = prev;

    if (prev_) {
      depth_ = prev_->depth_ + 1;
    }

    if (children_count > 1) {
      full_costs_ = new std::vector<ValueType>();
    }
  }

  void copy(EditDistanceRowColumnPtr e,
            EditDistanceRowColumnPtr prev) {
    s_elem_ = e->s_elem_;
    prev_ = prev;
    depth_ = e->depth_;
    row_ = e->row_;

    assert(!prev_ || (prev_->depth_ + 1) == depth_);

    if (e->full_costs_) {
      full_costs_ = new std::vector<ValueType>(*e->full_costs_);
    } else {
      costs_[0] = e->costs_[0];
      costs_[1] = e->costs_[1];
    }
  }

  void update(const SequenceType& t) {
    int start_index = row_;
    int end_index = t.size()+1;

    // Don't use shared pointers for speed.
    std::vector< EditDistanceRowColumn* > worklist;
    EditDistanceRowColumn* parent = prev_.get();
    while (parent && (parent->depth_ >= 0) 
            && (parent->row_ <= start_index)) {
      worklist.push_back(parent);
      parent = parent->prev_.get();
    }

    for (int j=start_index; j<end_index; ++j) {
      typename std::vector<EditDistanceRowColumn*>::reverse_iterator 
        it = worklist.rbegin(), ie = worklist.rend();
      for (; it!=ie; ++it) {
        (*it)->compute_cost(t); 
      }
      this->compute_cost(t); 
    }
  }

  inline const ElementType& s() { return s_elem_; }

  ValueType edit_distance() const { 
    return cost(row_-1); 
  }

  void compute_cost(const SequenceType &t) {

    ValueType c1,c2,c3;
    int j = row_;
    if (depth_ == 0) {
      set_cost(j, (ValueType)j);

    } else {
      if (j == 0) {
        set_cost(j, depth_);
      } else {
        c1 = (prev_.get())->cost(j-1) + (t[j-1] == s_elem_ ? 0 : 1); /*ScoreType::match(s_elem_, t, j-1);*/
        c2 = this->cost(j-1)  + 1; /*ScoreType::insert(s_elem_, t, j-1);*/
        c3 = (prev_.get())->cost(  j) + 1; /*ScoreType::del(s_elem_, t, j-1);*/

        set_cost(j, MIN(c1, MIN(c2, c3)));
      }
    }
    row_++;
  }

 private:
  explicit EditDistanceRowColumn(const EditDistanceRowColumn& e); 

  inline ValueType cost(unsigned j) const {
    if (full_costs_)
      return (*full_costs_)[j];

    return costs_[j % 2];
  }

  inline void set_cost(unsigned j, ValueType cost) {
    if (full_costs_) {
      if (full_costs_->size() == j) {
        full_costs_->push_back(cost);
      } else {
        assert(full_costs_->size() > j);
        (*full_costs_)[j] = cost;
      }
    } else {
      costs_[j % 2] = cost;
    }
  }
 
  void debug_print(std::ostream& os) {
    os << "(" << this << ") prev: " << prev_.get()
       << " s: " << s_elem_
       << " depth: " << depth_
       << " row: " << row_
       << " full_costs: " << full_costs_
       << " costs[0]: " << costs_[0]
       << " costs[1]: " << costs_[1];
  }

  ElementType s_elem_; // one element of the s sequence
  EditDistanceRowColumnPtr prev_;

  unsigned depth_;
  unsigned row_;

  std::vector<ValueType>* full_costs_;
  ValueType costs_[2];
};

////////////////////////////////////////////////////////////////////////////////

template<class ScoreType, 
         class SequenceType, 
         class ElementType, 
         class ValueType>
class EditDistancedBandedRow {
  typedef boost::shared_ptr<EditDistancedBandedRow> EditDistancedBandedRowPtr;

 public:
  EditDistancedBandedRow() :
      depth_(0), 
      row_(0),
      k_(0),
      full_costs_(NULL) {costs_[0] = costs_[1] = 0;}

  EditDistancedBandedRow(const ElementType &s_elem) :
      s_elem_(s_elem), 
      depth_(0), 
      row_(0),
      k_(0),
      full_costs_(NULL) {costs_[0] = costs_[1] = 0;}

  EditDistancedBandedRow(EditDistancedBandedRowPtr e,
                        EditDistancedBandedRowPtr prev) :
      depth_(0), 
      row_(0),
      k_(0),
      full_costs_(NULL) {
    costs_[0] = costs_[1] = 0;
    copy(e, prev);
  }

  ~EditDistancedBandedRow() {
    if (full_costs_)
      delete full_costs_;
  }

  void initialize(EditDistancedBandedRowPtr prev,
                  unsigned children_count) {
    prev_ = prev;

    if (prev_) {
      depth_ = prev_->depth_ + 1;
    }

    if (children_count > 1) {
      full_costs_ = new std::vector<ValueType>();
    }
  }

  void copy(EditDistancedBandedRowPtr e,
            EditDistancedBandedRowPtr prev) {
    s_elem_ = e->s_elem_;
    prev_ = prev;
    depth_ = e->depth_;
    row_ = e->row_;

    assert(!prev_ || (prev_->depth_ + 1) == depth_);

    if (e->full_costs_) {
      full_costs_ = new std::vector<ValueType>(*e->full_costs_);
    } else {
      costs_[0] = e->costs_[0];
      costs_[1] = e->costs_[1];
    }
  }

  void update(const SequenceType& t, unsigned k) {
    k_ = MAX(k_, k);

    std::vector< EditDistancedBandedRow* > worklist;
    EditDistancedBandedRow* parent = prev_.get();
    int prev_d, prev_r, prev_k, prev_end;
    bool prev_is_full = false;
    bool finished = false;

    while (parent && !finished) {
      prev_d = parent->depth_;
      prev_r = parent->row_;
      prev_k = parent->k_;
      prev_is_full = parent->full_costs_ != NULL ? true : false;

      prev_end = (prev_d - prev_k) + 2*prev_k + 1;
      finished = true;
      if (prev_k < k_)
        finished = false;
      else if (prev_r < MIN(prev_end, t.size()+1))
        finished = false;

      parent = parent->prev_.get();
    }
    int w_first = worklist.size()-1, w_last = 0;
    int  w_first_depth = worklist[w_first]->depth_;

    //int start_index = row_;
    //int end_index = t.size()+1;
    //for (int j=start_index; j<end_index; ++j) {

    //  for (int w=w_first; w>=w_last; w--) {

    //  }

    //}

    //// Don't use shared pointers for speed.
    //std::vector< EditDistancedBandedRow* > worklist;
    //EditDistancedBandedRow* parent = prev_.get();
    //while (parent && (parent->depth_ >= 0) 
    //        && (parent->row_ <= start_index)) {
    //  worklist.push_back(parent);
    //  parent = parent->prev_.get();
    //}

    //for (int j=start_index; j<end_index; ++j) {
    //  typename std::vector<EditDistancedBandedRow*>::reverse_iterator 
    //    it = worklist.rbegin(), ie = worklist.rend();
    //  for (; it!=ie; ++it) {
    //    (*it)->compute_cost(t); 
    //  }
    //  this->compute_cost(t); 
    //}
  }

  inline const ElementType& s() { return s_elem_; }

  ValueType edit_distance() const { 
    return cost(row_-1); 
  }

  void compute_cost(const SequenceType &t) {

    ValueType c1,c2,c3;
    int j = row_;
    if (depth_ == 0) {
      set_cost(j, (ValueType)j);

    } else {
      if (j == 0) {
        set_cost(j, depth_);
      } else {
        c1 = (prev_.get())->cost(j-1) + ScoreType::match(s_elem_, t, j-1);
        c2 = this->cost(j-1)  + ScoreType::insert(s_elem_, t, j-1);
        c3 = (prev_.get())->cost(  j) + ScoreType::del(s_elem_, t, j-1);

        set_cost(j, MIN(c1, MIN(c2, c3)));
      }
    }
    row_++;
  }

 private:
  explicit EditDistancedBandedRow(const EditDistancedBandedRow& e); 

  inline ValueType cost(unsigned j) const {
    if (full_costs_)
      return (*full_costs_)[j];

    return costs_[j % 2];
  }

  inline void set_cost(unsigned j, ValueType cost) {
    if (full_costs_) {
      if (full_costs_->size() == j) {
        full_costs_->push_back(cost);
      } else {
        assert(full_costs_->size() > j);
        (*full_costs_)[j] = cost;
      }
    } else {
      costs_[j % 2] = cost;
    }
  }
 
  void debug_print(std::ostream& os) {
    os << "(" << this << ") prev: " << prev_.get()
       << " s: " << s_elem_
       << " depth: " << depth_
       << " row: " << row_
       << " full_costs: " << full_costs_
       << " costs[0]: " << costs_[0]
       << " costs[1]: " << costs_[1];
  }

  ElementType s_elem_; // one element of the s sequence
  EditDistancedBandedRowPtr prev_;

  unsigned depth_;
  unsigned row_;
  unsigned k_;

  std::vector<ValueType>* full_costs_;
  ValueType costs_[4];
};

////////////////////////////////////////////////////////////////////////////////

template<class DataType, class SeqTy, class ValTy>
class EditDistanceTree : public tree<boost::shared_ptr<DataType> > {

  typedef tree<boost::shared_ptr<DataType> > Tree;
  typedef tree_node_<boost::shared_ptr<DataType> > Node;
  typedef boost::shared_ptr<DataType> DataTypePtr;

  typedef std::set<typename SeqTy::ID> SeqTyIDSet;
  typedef std::map<Node*, std::set<typename SeqTy::ID> > NodeIDMap;
  typedef std::map<typename SeqTy::ID, Node* > IDNodeMap;
  typedef std::set<Node*> NodeSet;

#define foreach_child(__node,__iterator) \
  typename Tree::pre_order_iterator __iterator##_BASE(__node); \
  typename Tree::children_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_children_iterator(__iterator##_BASE), \
       __iterator##_END = this->end_children_iterator(__iterator##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_pre_order(__node,__iterator) \
  typename Tree::pre_order_iterator __iterator##_BASE(__node); \
  typename Tree::pre_order_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_pre_order_iterator(__iterator##_BASE), \
       __iterator##_END = this->end_pre_order_iterator(__iterator##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_leaf(__node,__iterator) \
  typename Tree::pre_order_iterator __iterator##_BASE(__node); \
  typename Tree::leaf_iterator __iterator, __iterator##_END; \
  for (__iterator = this->begin_leaf_iterator(__iterator##_BASE), \
       __iterator##_END = this->end_leaf_iterator(__iterator##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_leaf_other(__tree,__node,__iterator) \
  typename Tree::pre_order_iterator __iterator##_BASE(__node); \
  typename Tree::leaf_iterator __iterator, __iterator##_END; \
  for (__iterator = __tree->begin_leaf_iterator(__iterator##_BASE), \
       __iterator##_END = __tree->end_leaf_iterator(__iterator##_BASE); \
       __iterator!=__iterator##_END; ++__iterator )

#define foreach_parent(__node,__iterator) \
  typename Tree::pre_order_iterator __iterator(__node); \
   for (; __iterator != this->root(); __iterator = this->parent(__iterator) )

 public:
  EditDistanceTree() {
    DataTypePtr root_data(new DataType());
    this->set_root(root_data);
  }

  // disable!? make private?

  ~EditDistanceTree() { this->clear(); }

  void append_sequence(const SeqTy& t) {}

  EditDistanceTree* clone(std::set<typename SeqTy::ID>* seq_set=NULL) {
    EditDistanceTree* edtree = new EditDistanceTree();

    // Copy current tree root to new tree, provide NULL parent
    (*(edtree->root()))->copy(*(this->root()), DataTypePtr() );

    clone_helper(edtree);

    foreach_leaf_other(edtree, edtree->root().node, leaf_it) {
      edtree->leaf_nodes_.insert(leaf_it.node);
      //typename SeqTy::ID seq_id = *(edtree->id_map_[leaf_it.node].begin());
      //edtree->leaf_node_id_map_[seq_id] = leaf_it.node;
    }
    assert(this->size() == edtree->size());

    return edtree;
  }

  void clone_helper(EditDistanceTree* other) {
   
    std::stack<std::pair<Node*,Node*> > worklist;
    worklist.push(std::make_pair(this->root().node, other->root().node));
    while (!worklist.empty()) {
      Node* src = worklist.top().first;
      Node* dst = worklist.top().second;
      worklist.pop();

      // Only necessary for leaf nodes
      if (id_map_.count(src)) {
        other->id_map_[dst].insert(id_map_[src].begin(), 
                                   id_map_[src].end());
        typename SeqTyIDSet::iterator seq_it = id_map_[src].begin();
        typename SeqTyIDSet::iterator seq_ie = id_map_[src].end();
        for (; seq_it!=seq_ie; ++seq_it) {
          other->leaf_node_id_map_[*seq_it] = dst;
        }
      }

      foreach_child (src, child_it) {
        DataTypePtr data(new DataType(*child_it, dst->data));
        Node* new_dst = other->append_child(dst, data).node;
        Node* new_src = child_it.node;

        // Add to worklist
        worklist.push(std::make_pair(new_src, new_dst));
      }
    }
  } 

  void initialize() {
    foreach_pre_order (this->root().node, it) {
      DataTypePtr parent_data;
      if (this->parent(it).node)
        parent_data = *(this->parent(it));
      it.node->data->initialize(parent_data, 
                                this->number_of_children(it));
    }

    foreach_leaf(this->root().node, leaf_it) {
      leaf_nodes_.insert(leaf_it.node);
    }
  }

  void insert(const SeqTy& s, typename SeqTy::ID seq_id) {
    Node* curr_node = this->root().node;
    typename SeqTy::const_iterator it = s.begin(), ie = s.end();
    for (; it!=ie; ++it) {
      Node* child_node = NULL;
      foreach_child (curr_node, child_it) {
        if ((*child_it)->s() == *it) {
          child_node = child_it.node;
          break;
        }
      }
      if (child_node == NULL) {
        DataTypePtr data(new DataType(*it));
        curr_node = this->append_child(curr_node, data).node;
      } else {
        curr_node = child_node;
      }
    }
    id_map_[curr_node].insert(seq_id);
    leaf_node_id_map_[seq_id] = curr_node;
  }

  void compute_t(const SeqTy& t, 
                 ValTy x,
                 const std::vector<typename SeqTy::ID>& id_list,
                 ValTy* min_ed,
                 typename SeqTy::ID* seq_id) {
    //for (int i=0; i<t.size()+1; i++) {
    //  std::cout << ".";
    //  foreach_pre_order (this->root().node, node_it) {
    //    (*node_it)->compute_cost(t);
    //  }
    //}
    //std::cout << "\n";

    *min_ed = INT_MAX;
    
    for (int i=0; i<id_list.size(); ++i) {
      //std::cout << ".";
      Node* node = leaf_node_id_map_[id_list[i]];
      assert(node);
      node->data->update(t);
      ValTy ed = node->data->edit_distance();
      if (ed < *min_ed) {
        *min_ed = ed;
        *seq_id = id_list[i];
        x = ed;
      }
    }
    //std::cout << "\n";
  }

  void compute_t(const SeqTy& t) {
    //for (int i=0; i<t.size()+1; i++) {
    //  std::cout << ".";
    //  foreach_pre_order (this->root().node, node_it) {
    //    (*node_it)->compute_cost(t);
    //  }
    //}
    //std::cout << "\n";
    typename NodeSet::iterator it = leaf_nodes_.begin(), ie = leaf_nodes_.end();
    for (; it!=ie; ++it) {
      //std::cout << ".";
      (*it)->data->update(t);
    }
    //std::cout << "\n";
  }


  void min_edit_distance(ValTy& result, typename SeqTy::ID& id, int max_depth,
                         std::vector<typename SeqTy::ID>* id_list=NULL) {
    result = INT_MAX;
    // iterate over leafnodes that of length less than depth.
    typename NodeSet::iterator it = leaf_nodes_.begin(), ie = leaf_nodes_.end();
    for (; it!=ie; ++it) {
      if ((*it)->data->depth() < max_depth) {
        ValTy ed = (*it)->data->edit_distance();
        if (result > ed) {
          result = ed;
          id = *(id_map_[*it].begin());
        }
      }
    }

  }

  void min_edit_distance(ValTy& result, typename SeqTy::ID& id, 
                         std::vector<typename SeqTy::ID>* id_list=NULL) {
    result = INT_MAX;
    typename NodeSet::iterator it = leaf_nodes_.begin(), ie = leaf_nodes_.end();
    for (; it!=ie; ++it) {
      ValTy ed = (*it)->data->edit_distance();
      if (result > ed) {
        result = ed;
        id = *(id_map_[*it].begin());
      }
    }
  }

  void get_all_distances(std::vector<ValTy>& edit_distance_list,
                         std::vector<typename SeqTy::ID>* id_list=NULL) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      assert(leaf_nodes_.count(leaf_it.node));
      if (id_list) {
        assert(id_map_.count(leaf_it.node) && id_map_[leaf_it.node].size() == 1);
        typename SeqTy::ID seq_id = *(id_map_[leaf_it.node].begin());
        id_list->push_back(seq_id);
      }
      edit_distance_list.push_back((*leaf_it)->edit_distance());
    }
  }

  void get_all_sequences(std::vector<SeqTy>& sequence_list,
                         std::vector<typename SeqTy::ID>* id_list=NULL) {
    Node* root_node = this->root().node;
    assert(root_node);
    foreach_leaf(root_node, leaf_it) {
      SeqTy seq;
      get_path(leaf_it.node, seq);
      sequence_list.push_back(seq);
      if (id_list) {
        assert(id_map_.count(leaf_it.node) && id_map_[leaf_it.node].size() == 1);
        typename SeqTy::ID seq_id = *(id_map_[leaf_it.node].begin());
        id_list->push_back(seq_id);
      }
    }
  }

 private:
  explicit EditDistanceTree(const EditDistanceTree& e);

  void get_path(Node* node, SeqTy& path) {
    foreach_parent (node, parent_it) {
      path.push_front((*parent_it)->s());
    }
  }

  SeqTy t_;
  NodeIDMap id_map_;
  IDNodeMap leaf_node_id_map_;
  NodeSet leaf_nodes_;

#undef foreach_child
#undef foreach_pre_order
#undef foreach_leaf
#undef foreach_parent
};

////////////////////////////////////////////////////////////////////////////////

template<class ListDataType, class DataType>
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
          break;
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

#endif

////////////////////////////////////////////////////////////////////////////////

// ExecutionTrace Score
//typedef Score< ExecutionTrace, ExecutionTrace::ID, int> ETScore;

// EditDistance trees
//typedef EditDistanceRowColumn< ETScore, ExecutionTrace, 
//                               ExecutionTrace::ID, int > EDColumn;
//typedef EditDistanceTree< EDColumn, ExecutionTrace, int > EDTree;
//
//// EditDistance flavors
//typedef EditDistanceTable<ETScore,ExecutionTrace,int> ExecutionTraceEDT;
//typedef EditDistanceRow<ETScore,ExecutionTrace,int>   ExecutionTraceEDR;
//typedef EditDistanceUkkonen<ETScore,ExecutionTrace,int> ExecutionTraceEDU;
//typedef EditDistanceUkkonen<ETScore,ExecutionTrace,int> ExecutionTraceEDU;
//typedef EditDistanceDynamicUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUD;
//typedef EditDistanceStaticUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUS;
//typedef EditDistanceFullUKK<ETScore,ExecutionTrace,int> ExecutionTraceEDUF;

// Default EditDistance
//typedef ExecutionTraceEDR ExecutionTraceED;

////////////////////////////////////////////////////////////////////////////////

// Basic ExecutionTree
//typedef ExecutionTree<ExecutionTrace, ExecutionTrace::ID> ExecutionTraceTree;

//typedef std::map<CVExecutionState*, EDTree*> ExecutionStateEDTreeMap;

//typedef std::map<ExecutionTrace::ID, TrainingObject*> TrainingObjectIDMap;
//typedef std::map<ExecutionTrace::ID, TrainingObject*> TrainingObjectIDMap;
//typedef std::set<TrainingObject*, TrainingObjectTraceLT> TrainingObjectSet;
//typedef std::vector<TrainingObject*> TrainingObjectList;

typedef LevenshteinRadixTree<ExecutionTrace, BasicBlockID> EditDistanceExecutionTree;
typedef boost::unordered_map<CVExecutionState*, EditDistanceExecutionTree*> EditDistanceExecutionTreeMap;

////////////////////////////////////////////////////////////////////////////////

/// TODO: Rename this class to ExecutionTraceManager
class ExecutionTreeManager : public ExecutionObserver {
 public:
  ExecutionTreeManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
 protected:
  std::vector< ExecutionTraceTree* > tree_list_;
  ClientVerifier *cv_;

};

class TrainingExecutionTreeManager : public ExecutionTreeManager {
 public:
  TrainingExecutionTreeManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 protected:

};

class VerifyExecutionTreeManager : public ExecutionTreeManager {
 public:
  VerifyExecutionTreeManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);

  int min_edit_distance();
  void update_min_edit_distance(CVExecutionState* state, int ed);
  void reset_min_edit_distance();

 private:
  void clear_edit_distance_map();

  TrainingObjectSet training_data_;
  EditDistanceExecutionTreeMap edit_distance_map_;

  /*
  // ExecutionTree objects
  EDTree* ed_tree_;
  ExecutionStateEDTreeMap state_tree_map_;

  // Training data
  TrainingObjectSet training_set_;
  TrainingObjectIDMap id_map_;
  TrainingObjectList training_by_length_;

  // Removed States
  std::set<CVExecutionState*> removed_states_;

  // Edit distance
  std::stack<std::pair<CVExecutionState*, int> > current_min_;
  */
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTreeManagerFactory {
 public:
  static ExecutionTreeManager* create(ClientVerifier *cv);
};


} // end namespace cliver

#endif // CLIVER_EXECUTION_TREE_H

