//===-- ExecutionStatePruner.h -----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStatePruner interface.
//
// ExecutionStatePruner is an abstract interface for a class that prunes
// ExecutionStates, meaning it removes, simplifies or canonicalizes the
// components of an ExecutionState(AddressSpace, symbolic variables,
// constraints). The intention being to allow multiple ExecutionStates to
// become equivalent so that they can be merged. These classes may be chained
// (liked Solvers) to keep their implementations simple.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATEPRUNER_H
#define KLEE_CLVR_EXECUTIONSTATEPRUNER_H

namespace klee {
namespace clvr {
	class ExecutionStatePruner {
		public:
			ExecutionStatePruner();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATEPRUNER_H
