//===-- CVMemoryManager.cpp -----------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "../Core/Common.h"

#include "../Core/CoreStats.h"
#include "../Core/Memory.h"
#include "CVMemoryManager.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"

#include "llvm/Support/CommandLine.h"

/***/

namespace cliver {

CVMemoryManager::~CVMemoryManager() { 
  while (!objects.empty()) {
    klee::MemoryObject *mo = objects.back();
    objects.pop_back();
    delete mo;
  }
}

klee::MemoryObject *CVMemoryManager::allocate(uint64_t size, bool isLocal, 
                                              bool isGlobal,
                                              const llvm::Value *allocSite) {
  if (size>10*1024*1024) {
    klee::klee_warning_once(0, "failing large alloc: %u bytes", (unsigned) size);
    return 0;
  }
  uint64_t address = (uint64_t) (unsigned long) malloc((unsigned) size);
  if (!address)
    return 0;
  
  ++klee::stats::allocations;
  klee::MemoryObject *res = new klee::MemoryObject(address, size, isLocal, 
      isGlobal, false, allocSite);
  objects.push_back(res);
  return res;
}

klee::MemoryObject *CVMemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                                   const llvm::Value *allocSite) {
#ifndef NDEBUG
  for (objects_ty::iterator it = objects.begin(), ie = objects.end();
       it != ie; ++it) {
    klee::MemoryObject *mo = *it;
    assert(!(address+size > mo->address && address < mo->address+mo->size) &&
           "allocated an overlapping object");
  }
#endif

  ++klee::stats::allocations;
  klee::MemoryObject *res = new klee::MemoryObject(address, size, false, true, true,
                                                   allocSite);
  objects.push_back(res);
  return res;
}

void CVMemoryManager::deallocate(const klee::MemoryObject *mo) {
  assert(0);
}

} // End cliver namespace

