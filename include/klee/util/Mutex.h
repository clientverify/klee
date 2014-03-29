//===-- Mutex.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MUTEX_H
#define KLEE_MUTEX_H

#include <boost/thread/barrier.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>

namespace boost { void throw_exception(std::exception const& e); }

namespace klee {

typedef boost::mutex Mutex;
typedef boost::recursive_mutex RecursiveMutex;

typedef boost::lock_guard<RecursiveMutex> RecursiveLockGuard;
typedef boost::lock_guard<Mutex> LockGuard;
typedef boost::unique_lock<Mutex> UniqueLock;

typedef boost::barrier Barrier;
typedef boost::condition_variable ConditionVariable;

} // end namespace klee

#endif /* KLEE_MUTEX_H */

