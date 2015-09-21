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

#include "klee/util/Atomic.h"

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/stack.hpp>

#include <boost/thread/barrier.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/smart_ptr/detail/spinlock.hpp>

namespace boost { void throw_exception(std::exception const& e); }

namespace klee {

typedef boost::detail::spinlock SpinLock;

typedef boost::mutex Mutex;
typedef boost::shared_mutex SharedMutex;
typedef boost::recursive_mutex RecursiveMutex;

typedef boost::lock_guard<RecursiveMutex> RecursiveLockGuard;
typedef boost::lock_guard<Mutex> LockGuard;
typedef boost::lock_guard<SpinLock> SpinLockGuard;
typedef boost::unique_lock<Mutex> UniqueLock;

typedef boost::shared_lock<SharedMutex> SharedLock;
typedef boost::unique_lock<SharedMutex> UniqueSharedLock;
typedef boost::upgrade_lock<SharedMutex> UpgradeSharedLock;
typedef boost::upgrade_to_unique_lock<SharedMutex> UpgradeToUniqueSharedLock;

template<class T>
struct Guard {
  typedef boost::lock_guard<T> type;
};

typedef boost::barrier Barrier;
typedef boost::condition_variable ConditionVariable;

template<class T>
struct LockFreeStack {
  typedef boost::lockfree::stack<T> type;
};

} // end namespace klee

#endif /* KLEE_MUTEX_H */

