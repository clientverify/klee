//===-- ExecutionTrace.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//

#include "cliver/ExecutionTrace.h"
#include "CVCommon.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

bool ExecutionTrace::operator==(const ExecutionTrace& b) const { 
  return basic_blocks_ == b.basic_blocks_;
}

bool ExecutionTrace::operator!=(const ExecutionTrace& b) const { 
  return basic_blocks_ != b.basic_blocks_;
}

bool ExecutionTrace::operator<(const ExecutionTrace& b) const { 
  return basic_blocks_ < b.basic_blocks_;
}

void ExecutionTrace::push_back(const ExecutionTrace& etrace){
  basic_blocks_.insert(basic_blocks_.end(), etrace.begin(), etrace.end());
}

// XXX Inefficient
void ExecutionTrace::push_front(const ExecutionTrace& etrace){
  basic_blocks_.insert(basic_blocks_.begin(), etrace.begin(), etrace.end());
}

std::ostream& operator<<(std::ostream& os, const ExecutionTrace &etrace) {
  foreach (ExecutionTrace::BasicBlockID kbb, etrace) {
    os << kbb << ", ";
  }
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

