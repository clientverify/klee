//===-- LockFreeQueue.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef __UTIL_LOCKFREEQUEUE_H_
#define __UTIL_LOCKFREEQUEUE_H_

#include "klee/util/Atomic.h"

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/stack.hpp>

namespace klee {

// Wrapper for boost::lockfree::queue that keeps track of size
template<class T>
class LockFreeQueue {
 public:
  LockFreeQueue() : queue_(0), size_(0) {}
  LockFreeQueue(int capacity) : queue_(capacity), size_(0) {}

  // WARNING: not thread safe
  bool empty() { return queue_.empty(); }

  bool push(T const & t, bool blocking=false) {
    if (blocking) {
      while(!queue_.push(t))
        ;
      ++size_;
      return true;
    } else {
      if (queue_.push(t)) {
        ++size_;
        return true;
      }
    }
    return false;
  }

  bool pop(T & ret, bool blocking=false) {
    if (blocking) {
      while(!queue_.pop(ret))
        ;
      --size_;
      return true;
    } else {
      if (queue_.pop(ret)) {
        --size_;
        return true;
      }
    }
    return false;
  }

  int size() {
    return size_;
  }

 private:
  klee::Atomic<int>::type size_;
  boost::lockfree::queue<T> queue_;
};

}

#endif

