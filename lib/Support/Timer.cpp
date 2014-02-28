//===-- Timer.cpp ---------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Config/Version.h"
#include "klee/Internal/Support/Timer.h"

#include "llvm/Support/Process.h"

using namespace klee;
using namespace llvm;

WallTimer::WallTimer() {
#ifdef USE_BOOST_TIMER

#else
  sys::TimeValue now(0,0),user(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);
  startMicroseconds = now.usec();
#endif
}

uint64_t WallTimer::check() {
#ifdef USE_BOOST_TIMER
  // check_times.user and check_times.system are in nanoseconds
  boost::timer::cpu_times const check_times(timer.elapsed());
  //return ((check_times.system + check_times.user) / 1000);
  return ((check_times.wall) / 1000);
#else
  sys::TimeValue now(0,0),user(0,0),sys(0,0);
  sys::Process::GetTimeUsage(now,user,sys);
  return now.usec() - startMicroseconds;
#endif
}
