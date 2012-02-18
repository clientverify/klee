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

#include "cliver/CVExecutionState.h"

namespace klee {
	class ConstraintManager;
}

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class AddressSpaceGraph;
class ConstraintPruner;
class ClientVerifier;

struct MergeInfo {
	AddressSpaceGraph *graph;
};

class StateMerger {
 public:
	StateMerger( ConstraintPruner *pruner, ClientVerifier *cv );
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

 protected:
  bool callstacks_equal(CVExecutionState *state_a, CVExecutionState *state_b);

	bool constraints_equal(
		const AddressSpaceGraph *asg_a, const AddressSpaceGraph *asg_b,
		const klee::ConstraintManager *a, const klee::ConstraintManager *b);

	ConstraintPruner *pruner_;
  ClientVerifier *cv_;
};

////////////////////////////////////////////////////////////////////////////////

class SymbolicStateMerger : public StateMerger {
 public:
	SymbolicStateMerger( ConstraintPruner *pruner, ClientVerifier *cv );
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

 private:
	std::map<CVExecutionState*, MergeInfo> previous_states_;
};

} // end namespace cliver
#endif // STATE_MERGER_H
