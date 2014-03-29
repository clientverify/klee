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

#include <boost/thread/thread.hpp>
#include <boost/thread/tss.hpp>

namespace klee {

typedef boost::thread Thread;
typedef boost::thread_group ThreadGroup;

template<class T>
struct ThreadSpecificPointer {
  typedef boost::thread_specific_ptr<T> type;
};

} // end namespace klee

#endif /* KLEE_THREAD_H */

