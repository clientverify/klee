//===-- StatisticDataType.h -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATISTIC_DATA_TYPE_H
#define KLEE_STATISTIC_DATA_TYPE_H

#if defined(USE_FOLLY)
#include "folly/ThreadCachedInt.h"
#endif

namespace klee {

  template<class IntT, typename StorageType >
  class BasicStatisticDataType {
   public:
    BasicStatisticDataType() : data_(0) {}

    BasicStatisticDataType& operator=(IntT v) {
      data_ = v; return *this;
    }

    BasicStatisticDataType& operator+=(IntT inc) { 
      data_ += inc; return *this; 
    }

    void set(IntT d) { data_ = d; }

    IntT get() { return data_; }

   private:
    StorageType data_;
  };

#if defined(USE_FOLLY)
  template<class IntT>
  class FollyStatisticDataType {
   public:
    // Set Cache to INT_MAX so that individual threads never call flush()
    // in ThreadCacheInt, only need to do a flush on readFull()
    FollyStatisticDataType() : data_(0, INT_MAX) {}

    FollyStatisticDataType& operator=(IntT v) {
      data_.set(v); return *this;
    }

    FollyStatisticDataType& operator+=(IntT inc) { 
      data_.increment(inc); return *this; 
    }

    void set(IntT d) { data_.set(d); }

    IntT get() { return data_.readFull(); }

   private:
    folly::ThreadCachedInt<IntT> data_;
  };
#endif

// Definition of StatisticDataType based on compile time options, three options.
// The first two are thread-safe, and the first much faster with multiple
// threads.
#if defined(USE_FOLLY)
  typedef FollyStatisticDataType<uint64_t> StatisticDataType;
#elif defined(THREADSAFE_ATOMIC)
  typedef BasicStatisticDataType<uint64_t,klee::Atomic<uint64_t>::type > StatisticDataType;
#else
  typedef BasicStatisticDataType<uint64_t,uint64_t> StatisticDataType;
#endif

}

#endif
