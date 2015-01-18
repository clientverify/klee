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

#ifdef ENABLE_BOOST_ATOMIC
#include <boost/atomic.hpp>
#else
// Check for c++11 support
#if __cplusplus > 199711L
#include <atomic>
#endif
#endif

namespace klee {

template<class T>
struct Atomic {
#ifdef ENABLE_BOOST_ATOMIC
  typedef boost::atomic<T> type;
#else
// Check for c++11 support
#if __cplusplus > 199711L
  typedef std::atomic<T> type;
#else
  typedef T type;
#endif
#endif
};

} // end namespace klee

#endif /* KLEE_ATOMIC_H */

