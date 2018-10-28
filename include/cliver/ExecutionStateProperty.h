//===-- ExecutionStateProperty.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_STATE_PROPERTY_H
#define CLIVER_EXECUTION_STATE_PROPERTY_H

#include "cliver/CVExecutionState.h"
#include "cliver/ExecutionTraceTree.h"

#include <llvm/ADT/PriorityQueue.h>

#include <map>
#include <set>
#include <iostream>

//namespace klee {
//  class TimerStatIncrementer;
//}

namespace cliver {

class ExecutionStage;

////////////////////////////////////////////////////////////////////////////////

/// Classes that inherit from ExecutionStateProperty act as external "decorators"
/// for CVExecutionStates to describe a certain set of differentiating properties.
/// Each CVExecutionState has an internal ExecutionStateProperty, but a single
/// instance of this class can be used to represent a class of states when combined
/// with stl collections. This is how ExecutionStateProperty is used in the 
/// CVSearcher classes. Instances of ExecutionStateProperty are used to form
/// an ordering of CVExecutionStates, for example, LogIndexProperty uses the 
/// log_index of the NetworkManager to form a class of CVExecutionStates.
/// ExecutionStateProperty should be used as if it was pure virtual, i.e., not
/// used directly. The member variables of these classes are public for now so that 
/// a CVExecutionState is not needed to create an instance. Inherited classes
/// should preserve this functionality through public member variables or functions
/// to create instances without a given CVExecutionState.
///
class ExecutionStateProperty {
 public:
  ExecutionStateProperty();
  virtual void print(std::ostream &os) const;
  virtual int compare(const ExecutionStateProperty *p) const;
  virtual ExecutionStateProperty *clone();
  virtual void reset();

  ExecutionStateProperty& operator=(const ExecutionStateProperty& esp);

  void clone_helper(ExecutionStateProperty* p);

  int round; // current socket id
  int client_round; // client specific round number
  int hmm_round; // client specific round number
  int edit_distance; // edit distance 
  int symbolic_vars; // number of symbolic variables created for this round
  bool recompute;
  bool symbolic_model; // True if state's execution path has hit a modeled func
  bool is_recv_processing;
  size_t inst_count; // number of instructions in this round from this state
  int pass_count; // How many passes has this round been executed
  int bb_count; // how many basic blocks has this path executed since last recompute
  bool processing_select_event;
  long fifo_num;

  // For concurrent processing of events in ExecutionTraceManager
  ExecutionTraceTreeNode* tracker_node; // used by TrackingRadixTreeExt
  klee::Atomic<bool>::type tracker_cloned; // used by TrackingRadixTreeExt
  ExecutionStage* execution_stage; // used ExecutionTraceManager
  ExecutionTraceEditDistanceTree* ed_tree; // used by VerifyTraceManager
};

inline std::ostream &operator<<(std::ostream &os, 
		const ExecutionStateProperty &p) {
  p.print(os);
  return os;
}

class EditDistanceExecutionStateProperty : public ExecutionStateProperty {
 public:
  EditDistanceExecutionStateProperty();
	virtual int compare(const ExecutionStateProperty *p) const;
	virtual ExecutionStateProperty* clone();
};

inline std::ostream &operator<<(std::ostream &os, 
		const EditDistanceExecutionStateProperty &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class XPilotEditDistanceExecutionStateProperty : public ExecutionStateProperty {
 public:
  XPilotEditDistanceExecutionStateProperty();
	virtual int compare(const ExecutionStateProperty *p) const;
	virtual ExecutionStateProperty* clone();
};

inline std::ostream &operator<<(std::ostream &os,
		const XPilotEditDistanceExecutionStateProperty &p) {
  p.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

struct ExecutionStatePropertyFactory {
	static ExecutionStateProperty* create();
};

////////////////////////////////////////////////////////////////////////////////

struct ExecutionStatePropertyLT {
	bool operator()(const ExecutionStateProperty* a, 
			const ExecutionStateProperty* b) const;
};

typedef std::map<ExecutionStateProperty*,
								 ExecutionStateSet,
								 ExecutionStatePropertyLT> ExecutionStatePropertyMap;

typedef llvm::PriorityQueue< CVExecutionState*, 
				                     std::vector<CVExecutionState*>,
														 CVExecutionStateLT > ExecutionStatePriorityQueue;

typedef llvm::PriorityQueue< ExecutionStateProperty*, 
				                     std::vector<ExecutionStateProperty*>,
														 ExecutionStatePropertyLT > StatePropertyPriorityQueue;

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_EXECUTION_STATE_PROPERTY_H
