///===-- Atomic.h -----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ATOMIC_H
#define KLEE_ATOMIC_H

#if defined (THREADSAFE_ATOMIC)
#include <atomic>
namespace atomic_ns = std;
#endif

namespace klee {

template<class T>
struct Atomic {
#if defined (THREADSAFE_ATOMIC)
  typedef std::atomic<T> type;
#else
  typedef T type;
#endif
};

} // end namespace klee

#endif /* KLEE_ATOMIC_H */

