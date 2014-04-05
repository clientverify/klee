//===-- MemoryManager.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORYMANAGER_H
#define KLEE_MEMORYMANAGER_H

#include "klee/util/Atomic.h"
#include "klee/util/Mutex.h"
#include <set>
#include <stdint.h>

namespace llvm {
  class Value;
}

namespace klee {
  class MemoryObject;

  class MemoryManager {
  private:
    Atomic<unsigned>::type activeObjects;
#ifndef NDEBUG
    typedef std::set<MemoryObject*> objects_ty;
    objects_ty objects;
    SpinLock objectsLock;
#endif

  public:
    MemoryManager();
    ~MemoryManager();

    MemoryObject *allocate(uint64_t size, bool isLocal, bool isGlobal,
                           const llvm::Value *allocSite);
    MemoryObject *allocateFixed(uint64_t address, uint64_t size,
                                const llvm::Value *allocSite);
    void deallocate(const MemoryObject *mo);
    void markFreed(MemoryObject *mo);
  };

} // End klee namespace

#endif
