//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryManager.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "klee/util/Thread.h"

#include "llvm/Support/CommandLine.h"

using namespace klee;

/***/

MemoryManager::MemoryManager() : activeObjects(0) {}

MemoryManager::~MemoryManager() { 
#if defined(MEMORY_MANAGER_OBJECT_TRACKING)
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    if (!mo->isFixed)
      free((void *)mo->address);
    objects.erase(mo);
    delete mo;
  }
#endif

  if (activeObjects)
    klee_warning("MemoryManager %x (TID:%d): %d objects not freed",
                 this, GetThreadID(), (unsigned)activeObjects);
}

MemoryObject *MemoryManager::allocate(uint64_t size, bool isLocal, 
                                      bool isGlobal,
                                      const llvm::Value *allocSite) {
  if (size>10*1024*1024)
    klee_warning_once(0, "Large alloc: %u bytes.  KLEE may run out of memory.", (unsigned) size);
  
  uint64_t address = (uint64_t) (unsigned long) malloc((unsigned) size);
  if (!address)
    return 0;
  
  ++stats::allocations;
  ++activeObjects;
  MemoryObject *res = new MemoryObject(address, size, isLocal, isGlobal, false,
                                       allocSite, this);
#if defined(MEMORY_MANAGER_OBJECT_TRACKING)
  objectsLock.lock();
  objects.insert(res);
  objectsLock.unlock();
#endif
  return res;
}

MemoryObject *MemoryManager::allocateFixed(uint64_t address, uint64_t size,
                                           const llvm::Value *allocSite) {
#if defined(MEMORY_MANAGER_OBJECT_TRACKING)
  objectsLock.lock();
  for (objects_ty::iterator it = objects.begin(), ie = objects.end();
       it != ie; ++it) {
    MemoryObject *mo = *it;
    if (address+size > mo->address && address < mo->address+mo->size)
      klee_error("Trying to allocate an overlapping object");
  }
  objectsLock.unlock();
#endif

  ++stats::allocations;
  ++activeObjects;
  MemoryObject *res = new MemoryObject(address, size, false, true, true,
                                       allocSite, this);
#if defined(MEMORY_MANAGER_OBJECT_TRACKING)
  objectsLock.lock();
  objects.insert(res);
  objectsLock.unlock();
#endif
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo) {
  assert(0);
}

void MemoryManager::markFreed(MemoryObject *mo) {
  --activeObjects;

#if defined(MEMORY_MANAGER_OBJECT_TRACKING)
  objectsLock.lock();
  if (objects.find(mo) != objects.end())
  {
    if (!mo->isFixed)
      free((void *)mo->address);
    objects.erase(mo);
  }
  objectsLock.unlock();
#else
  if (!mo->isFixed)
    free((void *)mo->address);
#endif
}
