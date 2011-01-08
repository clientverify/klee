//===-- ExecutionStateScorer.h ----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStateScorer interface.
//
// ExecutionStateScorer is used to sort the ExecutionStates to determine which
// should be executed next. This class could be extended and modified for each
// application that we want to verify. ExecutionStateScorer implementations may
// use various data to produce a score set, it might be paired with an app
// specific ExecutionStateInfo implemention to produce an application specific
// score or it may be generic and perform static analysis or even maintain
// some sort of branch predicition history and logic.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATESCORER_H
#define KLEE_CLVR_EXECUTIONSTATESCORER_H

namespace klee {
namespace clvr {
	class ExecutionStateScorer {
		public:
			ExecutionStateScorer();

			// compute() takes a set of ExecutionState objects and computes a score
			// value for each. The algorithm for computing the score will vary between
			// implementation. 
			virtual void compute(std::set<ExecutionState&> esv);

			// score() returns a value between 0 and 1 that is a measure of how likely
			// the ExecutionState associated with this object will be successful at the
			// next socket event or at the next branch, depending on the
			// implementation.
			virtual double score(ExecutionState &es);

			// weight() returns the a measure of the importance that should be
			// attributed to this object's score(). This may be a constant value set at
			// initialization or it may change over time as the object is given
			// feedback on it's success.
			virtual double weight();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATESCORER_H
