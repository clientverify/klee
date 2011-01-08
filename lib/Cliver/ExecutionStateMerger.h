//===-- ExecutionStateMerger.h -----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStateMerger interface.
//
// ExecutionStateMerger is a abstract interface for a class that takes two or
// more ExecutionStates and returns a merged version of the input if possible.
// The most basic merger simply checks for exact equivalence, a more
// sophisticated merger could combine constraints or memory contents into
// disjunctions for example. It also might be possible to design this object so
// that they may be chained 
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATEMERGER_H
#define KLEE_CLVR_EXECUTIONSTATEMERGER_H

namespace klee {
namespace clvr {
	class ExecutionStateMerger {
		public:
			ExecutionStateMerger();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATEMERGER_H
