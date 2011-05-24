//===-- SharedObjects.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_SHAREDOBJECTS_H
#define CLIVER_SHAREDOBJECTS_H

#include <stdint.h>
#include <set>
#include <string>
#include "CVStream.h"
#include "../Core/Memory.h"

namespace llvm {
  class Value;
}

namespace klee {
  class Array;
  class MemoryObject;
}

namespace cliver {
class CVContext;
class CVExecutionState;

class SharedObject {
 public:
  SharedObject(const CVContext* context, const llvm::Value* instruction)
    : context_(context), instruction_(instruction),
    ref_count_(0) {}
    //ref_count_(0), next_(NULL), previous_(NULL) {}

  //virtual void set_previous(SharedObject *previous) { previous_ = previous; }
  //virtual void set_next(SharedObject *next) { next_ = next; }

  virtual void inc_ref() { ref_count_++; }
  virtual void dec_ref() { ref_count_--; }
  virtual void add_state(CVExecutionState* state) { states_.insert(state); }
  const CVContext* context() const { return context_; }
  const llvm::Value* instruction() const { return instruction_; }

 protected: 
  const CVContext* context_;
  const llvm::Value* instruction_;
  int ref_count_;
  std::set<CVExecutionState*> states_;
  //SharedObject *next_;
  //SharedObject *previous_;
};

class SharedAddress : public SharedObject {
 public:
  SharedAddress(const CVContext* context, const llvm::Value* instruction,
      const klee::MemoryObject *mo);

  uint64_t address();
  ~SharedAddress();
  unsigned size() const { return size_; }

  SharedAddress* next()     { return next_; }
  SharedAddress* previous() { return previous_; }

  void set_previous(SharedAddress *previous) { previous_ = previous; }
  void set_next(SharedAddress *next) { next_ = next; }
 private:
  uint64_t address_;
  unsigned size_;
  bool local_;
  bool global_;
  bool fixed_;
  SharedAddress *next_;
  SharedAddress *previous_;
};

struct SizeSharedAddressLT {
  int operator()(const SharedAddress* a, const SharedAddress* b) const {
    return a->size() < b->size();
  }
};

struct ContextSharedAddressLT {
  int operator()(const SharedAddress* a, const SharedAddress* b) const {
    if (a->context() == b->context())
      return a->size() < b->size();
    return a->context() < b->context();
  }
};

struct AllocSiteSharedAddressLT {
  int operator()(const SharedAddress* a, const SharedAddress* b) const {
    if (a->instruction() == b->instruction())
      return a->size() < b->size();
    return a->instruction() < b->instruction();
  }
};

struct ContextAndAllocSiteSharedAddressLT {
  int operator()(const SharedAddress* a, const SharedAddress* b) const {
    if (a->context() == b->context()) {
      if (a->instruction() == b->instruction()) {
        return a->size() < b->size();
      }
      return a->instruction() < b->instruction();
    } 
    return a->context() < b->context();
  }
};

class AddressManager {
 public:
  AddressManager(CVExecutionState* state) : state_(state) {}
  void set_state(CVExecutionState *state) { state_ = state; }
  virtual uint64_t next(const klee::MemoryObject &mo, 
                        const CVContext* context,
                        const llvm::Value* instruction);
  virtual AddressManager* clone() { return new AddressManager(*this); }
 protected:
  CVExecutionState *state_;
};

template < class CompareType >
class SharedAddressManager : public AddressManager{
 public:
  SharedAddressManager(CVExecutionState *state)
    : AddressManager(state) {}

  uint64_t next(const klee::MemoryObject &mo, 
                const CVContext* context,
                const llvm::Value* instruction) {
    SharedAddress tmp_shared_address(context,instruction,&mo);
    SharedAddressSetIterator it = set_.find(&tmp_shared_address);
    SharedAddress *shared_address = *it;
    if (it == set_.end()) {
      shared_address = new SharedAddress(context, instruction, &mo);
    } else if (shared_address->next() == NULL) {
      SharedAddress* new_shared_address 
        = new SharedAddress(context, instruction, &mo);
      new_shared_address->set_previous(shared_address);
      shared_address->set_next(new_shared_address);
      set_.erase(it);
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

  virtual AddressManager* clone() {return new SharedAddressManager(*this); }

 private:
  typedef std::set< SharedAddress*, CompareType > SharedAddressSet;
  typedef typename SharedAddressSet::iterator SharedAddressSetIterator;
  SharedAddressSet set_;
};

class AddressManagerFactory {
 public:
  static AddressManager* create(CVExecutionState* state);
};

#if 0

class SharedSymbolic : public SharedObject {
 public:
  SharedSymbolic(const CVContext* context, const llvm::Value* instruction, 
      std::string basename, uint64_t size);
  ~SharedSymbolic();
  const klee::Array* array();
 private:
  klee::Array* array_;
  int count_;
  std::string name_;
};

struct NameSharedSymbolicLT {
  int operator()(const SharedSymbolic* a, const SharedSymbolic* b) const {
    return a->array->name < b->array->name;
  }
};

struct ContextSharedSymbolicLT {
  int operator()(const SharedSymbolic* a, const SharedSymbolic* b) const {
    if (a->context == b->context)
      return a->array->name < b->array->name;
    return a->context < b->context;
  }
};

struct AllocSiteSharedSymbolicLT {
  int operator()(const SharedSymbolic* a, const SharedSymbolic* b) const {
    if (a->instruction == b->instruction)
      return a->array->name < b->array->name;
    return a->instruction < b->instruction;
  }
};

struct ContextAndAllocSiteSharedSymbolicLT {
  int operator()(const SharedSymbolic* a, const SharedSymbolic* b) const {
    if (a->context == b->context) {
      if (a->instruction == b->instruction) {
        return a->array->name < b->array->name;
      }
      return a->instruction < b->instruction;
    } 
    return a->context < b->context;
  }
};

template < class CompareType >
class SharedSymbolicManager {
 public:
  SharedSymbolicManager(CVExecutionState *state)
    : state_(state) {}
  const Array* next(std::string* basename, uint64_t size,
                    const CVContext* context,
                    const llvm::Value* instruction);
 private:
  typedef std::set< SharedSymbolic*, CompareType > SharedSymbolicSet;
  SharedSymbolicSet set_;
  CVExecutionState *state_;
};
#endif

} // end cliver namespace

#endif // CLIVER_SHAREDOBJECTS_H
