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

namespace cliver {

class ConstraintPruner;

class StateMerger {
 public:
	StateMerger( ConstraintPruner *pruner );
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

	bool compare_constraints(klee::ConstraintManager &a, 
			klee::ConstraintManager &b);
 private:
	ConstraintPruner *pruner_;

};

} // end namespace cliver
#endif // STATE_MERGER_H
