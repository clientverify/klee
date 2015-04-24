//===-- Time.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_UTIL_TIME_H
#define KLEE_UTIL_TIME_H
#include <llvm/Support/TimeValue.h>

#include <chrono>

namespace klee {
  namespace util {
    double getUserTime();
    double getWallTime();

    /// Wall time as TimeValue object.
    llvm::sys::TimeValue getWallTimeVal();

    typedef std::chrono::high_resolution_clock Clock;
    typedef Clock::time_point TimePoint;
    using DurationToSeconds = std::chrono::duration<double, std::chrono::seconds::period>;
  }
}

#endif
