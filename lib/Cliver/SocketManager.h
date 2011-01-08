//===-- SocketManager.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::SocketManager interface.
//
// SocketManager is an abstract interface that keeps track of a current
// execution state's position in the SocketLog. It uses whatever information is
// needed to determine if a write operation or a read operation is
// valid/satisfiable. Each ExecutionState has a SocketManager. 
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_SOCKETMANAGER_H
#define KLEE_CLVR_SOCKETMANAGER_H

namespace klee {
namespace clvr {
	class SocketManager {
		public:
			SocketManager():
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_SOCKETMANAGER_H
