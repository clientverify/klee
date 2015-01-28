//===-- Thread.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_THREAD_H
#define KLEE_THREAD_H

#include "klee/util/Atomic.h"
#include <boost/thread/thread.hpp>
#include <boost/thread/tss.hpp>
#include <boost/thread/once.hpp>

#define BOOST_LEXICAL_CAST_ASSUME_C_LOCALE
#include <boost/lexical_cast.hpp>

#define ONCE_FLAG_INIT BOOST_ONCE_INIT 
#define call_once boost::call_once

namespace klee {

typedef boost::thread Thread;
typedef boost::thread_group ThreadGroup;
typedef boost::once_flag OnceFlag;

template<class T>
struct ThreadSpecificPointer {
  typedef boost::thread_specific_ptr<T> type;
};

static uint64_t GetNativeThreadID() {
  return boost::lexical_cast<uint64_t>(boost::this_thread::get_id());
}

static int GetThreadID() {
  static ThreadSpecificPointer<int>::type threadID;
  static Atomic<int>::type threadCount(0);
  if (!threadID.get())
    threadID.reset(new int(++threadCount));
  return *threadID;
}

static unsigned GetHardwareConcurrency() {
  return boost::thread::hardware_concurrency();
}

} // end namespace klee

#endif /* KLEE_THREAD_H */

