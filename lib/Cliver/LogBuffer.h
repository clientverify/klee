//===-- LogBuffer.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the clvr::LogBuffer and clvr::LogBufferInfo interfaces.
//
// LogBuffer is an abstract class that holds a symbolic or concrete region of
// raw bytes and may also hold metadata in the form of a LogBufferInfo object.
// For example, in XPilot, the metadata would include the round number in which
// a message was sent.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CLVR_LOGBUFFER_H
#define KLEE_CLVR_LOGBUFFER_H

namespace klee {
namespace clvr {
	class LogBufferInfo {
		public:
			LogBufferInfo();
		private:
	};

	class LogBuffer {
		public:
			LogBuffer();
		private:
	};

} // end namespace clvr 
} // end namespace klee
#endif // KLEE_CLVR_LOGBUFFER_H
