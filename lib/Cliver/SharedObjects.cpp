//===-- SharedObjects.cpp ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "SharedObjects.h"
#include "../Core/Memory.h"
#include "llvm/Support/CommandLine.h"

namespace {
enum AddressSharingType {
  NoSharing, BySize, ByAllocSite, ByContext, ByContextAndAllocSite 
};

llvm::cl::opt<AddressSharingType>
AddressSharing("address-sharing", 
  llvm::cl::desc("Choose how states share memory addresses."),
  llvm::cl::values(
    clEnumValN(NoSharing, "none", 
      "Don't share addresses between states"),
    clEnumValN(BySize, "size", 
      "Share addresses by allocation size"),
    clEnumValN(ByAllocSite, "allocsite", 
      "Share addresses by allocation site"),
    clEnumValN(ByContext, "context", 
      "Share addresses by context"),
    clEnumValN(ByContextAndAllocSite, "context-and-allocsite", 
      "Share addresses by allocation site"),
  clEnumValEnd),
  llvm::cl::init(NoSharing));
}
namespace cliver {

SharedAddress::SharedAddress(const CVContext* context, 
    const llvm::Value* instruction, const klee::MemoryObject *mo) 
    : SharedObject(context, instruction), address_(NULL),
      size_(mo->size), local_(mo->isLocal), 
      global_(mo->isGlobal), fixed_(mo->isFixed), 
      next_(NULL), previous_(NULL) {}

uint64_t SharedAddress::address() {
  if (!address_) {
    address_ = (uint64_t) (unsigned long) malloc((unsigned) size_);
    assert(address_ && "Malloc failed");
  }
  return address_;
}

SharedAddress::~SharedAddress() {
  if (address_ && ref_count_ == 0) free((void*)address_);
}

uint64_t AddressManager::next(const klee::MemoryObject &mo, 
                              const CVContext* context,
                              const llvm::Value* instruction) {
  uint64_t address = (uint64_t) (unsigned long) malloc((unsigned) mo.size);
  assert(address && "Malloc failed");
  return address;
}

AddressManager* AddressManagerFactory::create(CVExecutionState* state) {
  switch (AddressSharing) {
  case BySize:
    return new SharedAddressManager<SizeSharedAddressLT>(state);
  case ByContext:
    return new SharedAddressManager<ContextSharedAddressLT>(state);
  case ByAllocSite:
    return new SharedAddressManager<AllocSiteSharedAddressLT>(state);
  case ByContextAndAllocSite:
    return new SharedAddressManager<ContextAndAllocSiteSharedAddressLT>(state);
  case NoSharing: 
    break;
  }
  return new AddressManager(state);
}
#if 0
#endif


#if 0
template< class CompareType >
uint64_t SharedAddressManager::next(const klee::MemoryObject &mo,
                                    const CVContext* context, 
                                    const llvm::Value* instruction) {
  typename SharedAddressSet::iterator it 
    = set_.find(SharedAddress(context,instruction,&mo));
  SharedAddress *shared_address = NULL;

  // If we haven't yet seen a Shared Address of this Type or in this Group
  if (it == set_.end() || *it->next() == NULL) {
    SharedAddress* new_shared_address 
      = new SharedAddress(context, instruction, &mo);
    if (*it->next() == NULL) {
      shared_address = *it;
      new_shared_address->set_previous(shared_address);
      shared_address->set_next(new_shared_address);
      set_.erase(it);
    }
    shared_address = new_shared_address;
  } else {
    shared_address = shared_address->next();
    set_.erase(it);
  }
  set_.insert(shared_address);
  shared_address->add_state(state_);
  shared_address->inc_ref();
  return shared_address->address();
}


SharedSymbolic::SharedSymbolic(const CVContext* context, 
    const llvm::Value* instruction, std::string name, uint64_t size)
    : SharedObject(context, instruction), 
    name_(name), size_size(size), count_(-1), array_(NULL) {}
  
SharedSymbolic::~SharedSymbolic() {
  if (array_ != NULL && ref_count_ == 0) delete array_;
}

int SharedSymbolic::count() {
  if (count_ == -1) {
    if (previous() == NULL)
      count_ = 0;
    else
      count_ = previous()->count() + 1;
  }
  return count_;
}

const Array* SharedSymbolic::array() {
  if (array_ == NULL) {
    std::string name(name_);
    std::stringstream name;
    name << name_ << "_"<< count();
    if (context_)
      name << "_" << context_->id();
    if (instruction_) // instruction pointer
      name << "_" << instruction;

    array_ = new Array(name.str(), size_);
  }
  return array_;
}

template< class CompareType >
uint64_t SharedSymbolicManager::next(std::string &name, uint64_t size,
                                     const CVContext* context, 
                                     const llvm::Value* instruction) {
  SharedSymbolicSet::iterator it 
    = set_.find(SharedSymbolic(context,instruction,name,size));
  SharedSymbolic *shared_symbolic = NULL;

  // If we haven't yet seen a Shared Symbolic of this Type or in this Group
  if (it == set_.end() || *it->next() == NULL) {
    // XXX create unique name for this comparetype
    SharedSymbolic* new_shared_symbolic 
      = new SharedSymbolic(context,instruction,name,size);
    if (*it->next() == NULL) {
      shared_symbolic = *it;
      new_shared_symbolic->set_previous(shared_symbolic);
      shared_symbolic->set_next(new_shared_symbolic);
      set_.erase(it);
    }
    shared_symbolic = new_shared_symbolic;
  } else {
    shared_symbolic = shared_symbolic->next();
    set_.erase(it);
  }
  set_.insert(shared_symbolic);
  shared_symbolic->add_state(state_);
  shared_symbolic->inc_ref();
  return shared_symbolic->array();
}
#endif

} // end cliver namespace
