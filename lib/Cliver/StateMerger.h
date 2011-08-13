//===-- StateMerger.h -------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef STATE_MERGER_H
#define STATE_MERGER_H

#include "CVExecutionState.h"
#include "AddressSpaceGraph.h"

namespace cliver {

struct MergeInfo {
	AddressSpaceGraph *graph;
};

class ConstraintPruner;

class StateMerger {
 public:
	StateMerger( ConstraintPruner *pruner );
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

 protected:
  bool callstacks_equal(
		const AddressSpaceGraph &asg_a, const AddressSpaceGraph &asg_b,
		CVExecutionState *state_a, CVExecutionState *state_b);

	bool constraints_equal(
		const AddressSpaceGraph &asg_a, const AddressSpaceGraph &asg_b,
		klee::ConstraintManager &a, klee::ConstraintManager &b);

	ConstraintPruner *pruner_;
};

////////////////////////////////////////////////////////////////////////////////

class SymbolicStateMerger : public StateMerger {
 public:
	SymbolicStateMerger( ConstraintPruner *pruner );
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

 private:
	std::map<CVExecutionState*, MergeInfo> previous_states_;
};

} // end namespace cliver
#endif // STATE_MERGER_H
