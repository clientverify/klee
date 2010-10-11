//===-- SocketLog.h ---------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::SocketLog interface.
//
// SocketLog is an abstract interface that representes a sequence of buffers
// that were sent or received by an authoritative server. There should be 1 per
// session. The SocketLog object reads from a file and creates an ordered set
// of LogBuffer objects which represent the messages sent and received by the
// client. SocketLog also handles parsing and creating the associated
// LogBufferInfo object for each LogBuffer.
//

//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_SOCKETLOG_H
#define KLEE_CLVR_SOCKETLOG_H

namespace klee {
namespace clvr {
	class SocketLog {
		public:
			SocketLog();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_SOCKETLOG_H
