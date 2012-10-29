//===-- TimerStatIncrementer.h ----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TIMERSTATINCREMENTER_H
#define KLEE_TIMERSTATINCREMENTER_H

#include "klee/Statistics.h"
#include "klee/Internal/Support/Timer.h"

namespace klee {
#ifdef DISABLE_TIMER_STATS
  class TimerStatIncrementer {
  public:
    TimerStatIncrementer(Statistic &_statistic) {}
    uint64_t check() { return 0; }
  };
}
#else
  class TimerStatIncrementer {
  private:
    WallTimer timer;
    Statistic &statistic;

  public:
    TimerStatIncrementer(Statistic &_statistic) : statistic(_statistic) {}
    ~TimerStatIncrementer() {
      statistic += timer.check(); 
    }

    uint64_t check() { return timer.check(); }
  };
#endif
}

#endif
