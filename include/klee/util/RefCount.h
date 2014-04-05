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
    unsigned fetch_count = get();
    add_ref();
    return RefCount(fetch_count);
  }

  RefCount& operator--() {
    release();
    return *this;
  }

  RefCount operator--(int /*unused*/) {
    unsigned fetch_count = get();
    release();
    return RefCount(fetch_count);
  }

#ifdef ENABLE_BOOST_ATOMIC
  unsigned get() const { 
    return count_.load(boost::memory_order_relaxed);
  }

  unsigned add_ref() {
    return count_.fetch_add(1, boost::memory_order_relaxed);
  }

  void release() {
    count_.fetch_sub(1, boost::memory_order_relaxed);
  }

  template<class U>
  bool release(U *ptr) {
    if (count_.fetch_sub(1, boost::memory_order_release) == 1) {
      boost::atomic_thread_fence(boost::memory_order_acquire);
      if (ptr) delete ptr;
      return true;
    }
    return false;
  }

#else
  unsigned get() const { 
    return count_;
  }

  void add_ref() {
    ++count_;
  }

  void release() {
    --count_;
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

