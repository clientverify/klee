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
#include <stdint.h>
#include "../Core/MemoryManager.h"

namespace llvm {
  class Value;
}

namespace cliver {

//class klee::MemoryObject;

class CVMemoryManager : public klee::MemoryManager {
 public:
  CVMemoryManager() {}
  ~CVMemoryManager();

  virtual klee::MemoryObject *allocate(uint64_t size, bool isLocal, bool isGlobal,
                                       const llvm::Value *allocSite);

  virtual klee::MemoryObject *allocateFixed(uint64_t address, uint64_t size,
                                            const llvm::Value *allocSite);

  virtual void deallocate(const klee::MemoryObject *mo);
};

} // End cliver namespace

#endif
