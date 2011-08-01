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

class StateMerger {
 public:
	StateMerger();
	virtual void merge( ExecutionStateSet &state_set, 
			ExecutionStateSet &merged_set);

 private:

};

} // end namespace cliver
#endif // STATE_MERGER_H
