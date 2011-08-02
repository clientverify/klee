//===-- CVMemoryManager.cpp -----------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "../Core/Common.h"

#include "../Core/CoreStats.h"
#include "../Core/Memory.h"
#include "CVMemoryManager.h"
#include "CVExecutionState.h"
#include "SharedObjects.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Solver.h"

#include "llvm/Support/CommandLine.h"

// REMOVE THIS IF TOPOLOGICAL COMPARISON IS DONE BETWEEN EXECUTION STATES
// Possible address allocation semantics:

// For all i,j in States: Addresses of following are equal 
//   Nth allocation at instruction k
// Storage: map< allocsite, vector<memoryobject> >
//
// For all i,j in States: Addresses of following are equal 
//   Nth allocation at instruction k of size B
// Storage: map< allocsite, map< int, vector<memoryobject> > >
//   
// For all i,j in States: Addresses of following are equal 
//   Nth allocation of size B
// Storage:  map< int, vector<memoryobject> >
//
// For all i,j in States: Addresses of following are equal 
//   Nth allocated byte
// Storage: Need contiguous memory allocation; mmap(ANON) with
// no actual freeing of memory 
//
// For all i,j in States: Addresses of following are equal 
//   Nth active byte at time t
// Storage: Need contiguous memory allocation; mmap(ANON) with
// primitive garbage collection
///   

namespace cliver {

CVMemoryManager::CVMemoryManager() {}

CVMemoryManager::~CVMemoryManager() { 
  while (!objects.empty()) {
    klee::MemoryObject *mo = objects.back();
    objects.pop_back();
    delete mo;
  }
}

klee::MemoryObject *CVMemoryManager::allocate(klee::ExecutionState &state,
    uint64_t size, bool local, bool global, const llvm::Value *allocsite) {

  if (size>10*1024*1024) {
    klee::klee_warning_once(0, "failing large alloc: %u bytes", 
        (unsigned) size);
    return 0;
  }
  CVExecutionState* cvstate = static_cast<CVExecutionState*>(&state);
  klee::MemoryObject tmp_mo(NULL, size, local, global, false, NULL);
  uint64_t address 
    = cvstate->address_manager()->next(tmp_mo, cvstate->context(), allocsite);

  if (!address)
    return 0;
  
  ++klee::stats::allocations;
  klee::MemoryObject *res = new klee::MemoryObject(address, size, local, 
      global, false, allocsite);
  objects.push_back(res);
  return res;
}

klee::MemoryObject *CVMemoryManager::allocateFixed(klee::ExecutionState &state,
    uint64_t address, uint64_t size, const llvm::Value *allocsite) {

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
                                                   allocsite);
  objects.push_back(res);
  return res;
}

void CVMemoryManager::deallocate(const klee::MemoryObject *mo) {
  bool found = false;

  for (objects_ty::iterator it = objects.begin(),
      ie = objects.end(); it != ie; ++it) {
		klee::MemoryObject *obj = *it;
    if (obj == mo) {
      found = true;
      free((void*)obj->address);
      objects.erase(it);
      delete obj;
      break;
    }
  }
  assert(found && "MemoryObject not found");
}

} // End cliver namespace

