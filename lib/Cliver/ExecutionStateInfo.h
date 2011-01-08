//===-- ExecutionStateInfo.h -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStateInfo interface.
//
// ExecutionStateInfo is an abstract interface that contains highlevel
// information, like the current round number. This information could be
// gathered in various ways; the client could be modified to call klee_*
// functions or the AddressSpace could be parsed to extract information. This
// metadata is used path searching, execution state merging and pruning and
// verfication against context information in a LogBufferInfo. This class
// should be extended and modified for each application that we want to verify.
// ExecutionStateInfo is used to sort the ExecutionStates to determine which
// should be executed next. ExecutionStates
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATEINFO_H
#define KLEE_CLVR_EXECUTIONSTATEINFO_H

namespace klee {
namespace clvr {
	class ExecutionStateInfo {
		public:
			//virtual ExecutionStateInfo(ExecutionState& es);
			virtual void update();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATEINFO_H
