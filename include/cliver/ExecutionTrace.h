//===-- ExecutionTrace.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_H
#define CLIVER_EXECUTION_TRACE_H

#include <vector>
#include <list>
#include <iostream>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////
 
// ExecutionTrace: List of BasicBlock ids
typedef unsigned short BasicBlockID;
typedef std::vector<BasicBlockID> ExecutionTrace;
typedef std::list<BasicBlockID> ExecutionTraceList;

////////////////////////////////////////////////////////////////////////////////

// Print ExecutionTrace to std::ostream
inline std::ostream &operator<<(std::ostream &os, const ExecutionTrace &t) {
  for (ExecutionTrace::const_iterator i = t.begin(), e = t.end(); i!= e; ++i)
    os << (i == t.begin() ? "" : ",") << *i;
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_EXECUTION_TRACE_H
