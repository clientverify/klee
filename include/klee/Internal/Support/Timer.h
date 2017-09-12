//===-- Timer.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TIMER_H
#define KLEE_TIMER_H

#include <stdint.h>
#ifdef USE_BOOST_TIMER
#include <boost/timer/timer.hpp>
#endif

namespace klee {
  class WallTimer {
#ifdef USE_BOOST_TIMER
    boost::timer::cpu_timer timer;
#else
    uint64_t startMicroseconds;
#endif
    
  public:
    WallTimer();

    /// check - Return the delta since the timer was created, in microseconds.
    uint64_t check();
  };
}

#endif

