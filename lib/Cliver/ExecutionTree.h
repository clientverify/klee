//===-- ExecutionTree.h -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO Rename this class to StateTree?
//
// Handle paths that reach null node, which is complete
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TREE_H
#define CLIVER_EXECUTION_TREE_H

#include "klee/Solver.h"
#include "ExecutionStateProperty.h"
#include "ExecutionObserver.h"
#include "llvm/Analysis/Trace.h"
#include <set>
#include <map>

#include <iostream>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "EditDistance.h"

#include "tree.h"

namespace llvm {
	class BasicBlock;
}

namespace klee {
	class KInstruction;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class BasicBlockEntryInfo {
 public:
  unsigned basic_block_entry_id;
  llvm::BasicBlock* basic_block_;
  std::set<CVExecutionState*> states;
  int pending_count;

  BasicBlockEntryInfo(CVExecutionState* state);
  BasicBlockEntryInfo() : basic_block_entry_id(0), pending_count(0) {}

  bool operator==(const BasicBlockEntryInfo& b) const;
  bool operator!=(const BasicBlockEntryInfo& b) const;

	template<class archive> 
	void serialize(archive & ar, const unsigned version) {
    ar & basic_block_entry_id;
	}
};

inline std::ostream& operator<<(std::ostream &os, const BasicBlockEntryInfo &b){
  os << "BBID: " << b.basic_block_entry_id;
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class ExecutionTrace {
  typedef std::vector<llvm::BasicBlock *> BasicBlockList;
 public:
  ExecutionTrace();
  void append(const ExecutionTrace& etrace);
  void prepend(const ExecutionTrace& etrace);

  llvm::BasicBlock* operator[](unsigned i) { return basic_blocks_[i]; }
  llvm::BasicBlock* get_block(unsigned i) { return basic_blocks_[i]; }

  size_t size() { return basic_blocks_.size(); } 

 private:
  BasicBlockList basic_blocks_;
};

////////////////////////////////////////////////////////////////////////////////

#define foreach_child(__type,__tree,__node,__iterator) \
   for ( __type::children_iterator __iterator = \
        __tree.begin_children_iterator(__node), __iterator##_END = \
        __tree.end_children_iterator(__node); \
        __iterator!=__iterator##_END; ++__iterator )

#define foreach_leaf(__type,__tree,__node,__iterator) \
   for ( __type::leaf_iterator __iterator = \
        __tree.begin_leaf_iterator(__node), __iterator##_END = \
        __tree.end_leaf_iterator(__node); \
        __iterator!=__iterator##_END; ++__iterator )

#define foreach_parent(__type,__tree,__node,__iterator) \
   for ( __type::pre_order_iterator __iterator = \
        __type::pre_order_iterator(__node.node), \
        __iterator##_END = __tree.root(); __iterator!=__iterator##_END; \
        __iterator = tree_.parent(__iterator) )

class ExecutionTree : public ExecutionObserver {

  typedef tree<BasicBlockEntryInfo*> tree_t;
  typedef tree_t::pre_order_iterator node_iterator;
  typedef tree_t::children_iterator child_iterator;
  typedef tree_t::leaf_iterator leaf_iterator;
  typedef std::map<CVExecutionState*, node_iterator> state_map_t;

  typedef std::vector<BasicBlockEntryInfo*>  ExecutionPath;
  typedef std::set<ExecutionPath*>  ExecutionPathSet;

 public:
  ExecutionTree();
  ~ExecutionTree();

  void notify(ExecutionEvent ev);

  void get_path_set(ExecutionPathSet &path_set);

 private:

  void add_child_node(node_iterator &node,
                      CVExecutionState* state);

  void get_path_from_leaf(leaf_iterator &leaf,
                          ExecutionPath &path);

  tree_t tree_;
  state_map_t state_map_; 
  state_map_t pending_cloned_states_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_EXECUTION_TREE_H

