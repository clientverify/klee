//===-- ExecutionStateSerializer.h -------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::ExecutionStateSerializer interface.
//
// ExecutionStateSerializer manages the serialization of ExecutionState objects
// so that they may be stored to disk. This might be done if system memory is
// low. (LOW PRIORITY)
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_EXECUTIONSTATESERIALIZER_H
#define KLEE_CLVR_EXECUTIONSTATESERIALIZER_H

namespace klee {
namespace clvr {
	class ExecutionStateSerializer {
		public:
			ExecutionStateSerializer();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_EXECUTIONSTATESERIALIZER_H
