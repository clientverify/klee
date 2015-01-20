///===-- RefCount.h ---------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_REFCOUNT_H
#define KLEE_REFCOUNT_H

#include <klee/util/Atomic.h>

namespace klee {

class RefCount
{
  Atomic<unsigned>::type count_;

public:
  RefCount() : count_(0) {}

  RefCount(unsigned init) : count_(init) {}

  RefCount(const RefCount& refcount) 
      : count_(refcount.get()) {}

  RefCount& operator++() {
    add_ref();
    return *this;
  }

  RefCount operator++(int /*unused*/) {
    return RefCount(add_ref());
  }

  RefCount& operator--() {
    release();
    return *this;
  }

  RefCount operator--(int /*unused*/) {
    return RefCount(release());
  }

#if defined (THREADSAFE_ATOMIC)
  unsigned get() const { 
    return count_.load(atomic_ns::memory_order_relaxed);
  }

  unsigned add_ref() {
    return count_.fetch_add(1, atomic_ns::memory_order_relaxed);
  }

  unsigned release() {
    return count_.fetch_sub(1, atomic_ns::memory_order_relaxed);
  }

  template<class U>
  bool release(U *ptr) {
    if (count_.fetch_sub(1, atomic_ns::memory_order_release) == 1) {
      atomic_ns::atomic_thread_fence(atomic_ns::memory_order_acquire);
      if (ptr) delete ptr;
      return true;
    }
    return false;
  }
#else
  unsigned get() const { 
    return count_;
  }

  unsigned add_ref() {
    return count_++;
  }

  unsigned release() {
    return count_--;
  }

  template<class U>
  bool release(U *ptr) {
    if (--count_ == 0) {
      if (ptr) delete ptr;
      return true;
    }
    return false;
  }
#endif

};


} // end namespace klee

#endif /* KLEE_REFCOUNT_H */

