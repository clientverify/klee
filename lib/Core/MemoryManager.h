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

#include <set>
#include <stdint.h>

namespace llvm {
  class Value;
}

namespace klee {
  class MemoryObject;
  class ExecutionState;

  class MemoryManager {
  protected:
    typedef std::set<MemoryObject*> objects_ty;
    objects_ty objects;

  public:
    MemoryManager() {}
    virtual ~MemoryManager();

    virtual MemoryObject *allocate(ExecutionState &state, uint64_t size, bool isLocal, bool isGlobal,
                           const llvm::Value *allocSite);
    virtual MemoryObject *allocateFixed(ExecutionState &state, uint64_t address, uint64_t size,
                                const llvm::Value *allocSite);
    virtual void deallocate(const MemoryObject *mo);
  };

} // End klee namespace

#endif
