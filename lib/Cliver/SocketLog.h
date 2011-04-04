//===-- SocketLog.h ---------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// This file defines the SocketLog interface.
//
// SocketLog is an abstract interface that represents a sequence of buffers
// that were sent or received by an authoritative server. There should be 1 per
// session. The SocketLog object reads from a file and creates an ordered set
// of LogBuffer objects which represent the messages sent and received by the
// client. SocketLog also handles parsing and creating the associated
// LogBufferInfo object for each LogBuffer.
//

//===----------------------------------------------------------------------===//

#ifndef KLEE_SOCKETLOG_H
#define KLEE_SOCKETLOG_H

namespace klee {

class SocketLog {
public:
	SocketLog();
    : index(0), bytes(0), ktest(NULL) {}
  SocketLog(const struct KTest *_ktest) 
    : index(0), bytes(0), ktest(_ktest) {}
  int index;
  int bytes;
  const struct KTest *ktest;

  int compare(const SocketLog &b) {
    return !(index == b.index && bytes == b.bytes && ktest == b.ktest);
  }
private:

};

} // end namespace klee
#endif // KLEE_SOCKETLOG_H
