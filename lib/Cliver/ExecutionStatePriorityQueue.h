//===-- ExecutionStatePriorityQueue.h ----------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStatePriorityQueue interface.
//
// ExecutionStatePriorityQueue is an abstract interface for an object that
// maintains an ordering of ExecutionStates to determine which is to be
// executed next. An ExecutionStatePriorityQueue maintains one or more
// ExecutionStateScorer objects that it uses to determine the ordering.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATEPRIORITYQUEUE_H
#define KLEE_CLVR_EXECUTIONSTATEPRIORITYQUEUE_H

namespace klee {
namespace clvr {
	class ExecutionStatePriorityQueue {
		public:
			ExecutionStatePriorityQueue();
			virtual ExecutionState& pop();
			virtual void setValidity(ExecutionState& es, boolean validity);
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATEPRIORITYQUEUE_H
