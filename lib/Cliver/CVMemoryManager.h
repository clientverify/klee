//===-- CVMemoryManager.h ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_MEMORYMANAGER_H
#define CLIVER_MEMORYMANAGER_H

#include <vector>
#include <map>
#include <stdint.h>
#include "../Core/MemoryManager.h"
#include "ClientVerifier.h"

namespace llvm {
  class Value;
}

namespace klee {
  class MemoryObject;
}

namespace cliver {
class CVExecutionState;

class CVMemoryManager : public klee::MemoryManager {
 public:
  CVMemoryManager();
  ~CVMemoryManager();

  virtual klee::MemoryObject* allocate(klee::ExecutionState &state, 
      uint64_t size, bool local, bool global, const llvm::Value *allocsite);

  virtual klee::MemoryObject *allocateFixed(klee::ExecutionState &state, 
      uint64_t address, uint64_t size, const llvm::Value *allocsite);

  virtual void deallocate(const klee::MemoryObject *mo);

 private:

};

} // End cliver namespace

#endif 
