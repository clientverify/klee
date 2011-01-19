//===-- Memory.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "Memory.h"

#include "Context.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "klee/util/BitArray.h"

#include "ObjectHolder.h"

#include <llvm/Function.h>
#include <llvm/Instruction.h>
#include <llvm/Value.h>
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

namespace {
  cl::opt<bool>
  UseConstantArrays("use-constant-arrays",
                    cl::init(true));
}

/***/

ObjectHolder::ObjectHolder(const ObjectHolder &b) : os(b.os) { 
  if (os) ++os->refCount; 
}

ObjectHolder::ObjectHolder(ObjectState *_os) : os(_os) { 
  if (os) ++os->refCount; 
}

ObjectHolder::~ObjectHolder() { 
  if (os && --os->refCount==0) delete os; 
}
  
ObjectHolder &ObjectHolder::operator=(const ObjectHolder &b) {
  if (b.os) ++b.os->refCount;
  if (os && --os->refCount==0) delete os;
  os = b.os;
  return *this;
}

/***/

int MemoryObject::counter = 0;

MemoryObject::~MemoryObject() {
}

void MemoryObject::getAllocInfo(std::string &result) const {
  llvm::raw_string_ostream info(result);

  info << "MO" << id << "[" << size << "]";

  if (allocSite) {
    info << " allocated at ";
    if (const Instruction *i = dyn_cast<Instruction>(allocSite)) {
      info << i->getParent()->getParent()->getNameStr() << "():";
      info << *i;
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(allocSite)) {
      info << "global:" << gv->getNameStr();
    } else {
      info << "value:" << *allocSite;
    }
  } else {
    info << " (no allocation info)";
  }
  
  info.flush();
}

/***/

ObjectState::ObjectState(const MemoryObject *mo)
  : copyOnWriteOwner(0),
    refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(0, 0),
    size(mo->size),
    readOnly(false) {
  if (!UseConstantArrays) {
    // FIXME: Leaked.
    static unsigned id = 0;
    const Array *array = new Array("tmp_arr" + llvm::utostr(++id), size);
    updates = UpdateList(array, 0);
  }
}


ObjectState::ObjectState(const MemoryObject *mo, const Array *array)
  : copyOnWriteOwner(0),
    refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(array, 0),
    size(mo->size),
    readOnly(false) {
  makeSymbolic();
}

ObjectState::ObjectState(const ObjectState &os) 
  : copyOnWriteOwner(0),
    refCount(0),
    object(os.object),
    concreteStore(new uint8_t[os.size]),
    concreteMask(os.concreteMask ? new BitArray(*os.concreteMask, os.size) : 0),
    flushMask(os.flushMask ? new BitArray(*os.flushMask, os.size) : 0),
    knownSymbolics(0),
    updates(os.updates),
    size(os.size),
    readOnly(false) {
  assert(!os.readOnly && "no need to copy read only object?");

  if (os.knownSymbolics) {
    knownSymbolics = new ref<Expr>[size];
    for (unsigned i=0; i<size; i++)
      knownSymbolics[i] = os.knownSymbolics[i];
  }

  memcpy(concreteStore, os.concreteStore, size*sizeof(*concreteStore));
}

ObjectState::~ObjectState() {
  if (concreteMask) delete concreteMask;
  if (flushMask) delete flushMask;
  if (knownSymbolics) delete[] knownSymbolics;
  delete[] concreteStore;
}

/***/

const UpdateList &ObjectState::getUpdates() const {
  // Constant arrays are created lazily.
  if (!updates.root) {
    // Collect the list of writes, with the oldest writes first.
    
    // FIXME: We should be able to do this more efficiently, we just need to be
    // careful to get the interaction with the cache right. In particular we
    // should avoid creating UpdateNode instances we never use.
    unsigned NumWrites = updates.head ? updates.head->getSize() : 0;
    std::vector< std::pair< ref<Expr>, ref<Expr> > > Writes(NumWrites);
    const UpdateNode *un = updates.head;
    for (unsigned i = NumWrites; i != 0; un = un->next) {
      --i;
      Writes[i] = std::make_pair(un->index, un->value);
    }

    std::vector< ref<ConstantExpr> > Contents(size);

    // Initialize to zeros.
    for (unsigned i = 0, e = size; i != e; ++i)
      Contents[i] = ConstantExpr::create(0, Expr::Int8);

    // Pull off as many concrete writes as we can.
    unsigned Begin = 0, End = Writes.size();
    for (; Begin != End; ++Begin) {
      // Push concrete writes into the constant array.
      ConstantExpr *Index = dyn_cast<ConstantExpr>(Writes[Begin].first);
      if (!Index)
        break;

      ConstantExpr *Value = dyn_cast<ConstantExpr>(Writes[Begin].second);
      if (!Value)
        break;

      Contents[Index->getZExtValue()] = Value;
    }

    // FIXME: We should unique these, there is no good reason to create multiple
    // ones.

    // Start a new update list.
    // FIXME: Leaked.
    static unsigned id = 0;
    const Array *array = new Array("const_arr" + llvm::utostr(++id), size,
                                   &Contents[0],
                                   &Contents[0] + Contents.size());
    updates = UpdateList(array, 0);

    // Apply the remaining (non-constant) writes.
    for (; Begin != End; ++Begin)
      updates.extend(Writes[Begin].first, Writes[Begin].second);
  }

  return updates;
}

void ObjectState::makeConcrete() {
  if (concreteMask) delete concreteMask;
  if (flushMask) delete flushMask;
  if (knownSymbolics) delete[] knownSymbolics;
  concreteMask = 0;
  flushMask = 0;
  knownSymbolics = 0;
}

void ObjectState::makeSymbolic() {
  assert(!updates.head &&
         "XXX makeSymbolic of objects with symbolic values is unsupported");

  // XXX simplify this, can just delete various arrays I guess
  for (unsigned i=0; i<size; i++) {
    markByteSymbolic(i);
    setKnownSymbolic(i, 0);
    markByteFlushed(i);
  }
}

void ObjectState::initializeToZero() {
  makeConcrete();
  memset(concreteStore, 0, size);
}

void ObjectState::initializeToRandom() {  
  makeConcrete();
  for (unsigned i=0; i<size; i++) {
    // randomly selected by 256 sided die
    concreteStore[i] = 0xAB;
  }
}

/*
Cache Invariants
--
isByteKnownSymbolic(i) => !isByteConcrete(i)
isByteConcrete(i) => !isByteKnownSymbolic(i)
!isByteFlushed(i) => (isByteConcrete(i) || isByteKnownSymbolic(i))
 */

void ObjectState::fastRangeCheckOffset(ref<Expr> offset,
                                       unsigned *base_r,
                                       unsigned *size_r) const {
  *base_r = 0;
  *size_r = size;
}

void ObjectState::flushRangeForRead(unsigned rangeBase, 
                                    unsigned rangeSize) const {
  if (!flushMask) flushMask = new BitArray(size, true);
 
  for (unsigned offset=rangeBase; offset<rangeBase+rangeSize; offset++) {
    if (!isByteFlushed(offset)) {
      if (isByteConcrete(offset)) {
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       ConstantExpr::create(concreteStore[offset], Expr::Int8));
      } else {
        assert(isByteKnownSymbolic(offset) && "invalid bit set in flushMask");
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       knownSymbolics[offset]);
      }

      flushMask->unset(offset);
    }
  } 
}

void ObjectState::flushRangeForWrite(unsigned rangeBase, 
                                     unsigned rangeSize) {
  if (!flushMask) flushMask = new BitArray(size, true);

  for (unsigned offset=rangeBase; offset<rangeBase+rangeSize; offset++) {
    if (!isByteFlushed(offset)) {
      if (isByteConcrete(offset)) {
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       ConstantExpr::create(concreteStore[offset], Expr::Int8));
        markByteSymbolic(offset);
      } else {
        assert(isByteKnownSymbolic(offset) && "invalid bit set in flushMask");
        updates.extend(ConstantExpr::create(offset, Expr::Int32),
                       knownSymbolics[offset]);
        setKnownSymbolic(offset, 0);
      }

      flushMask->unset(offset);
    } else {
      // flushed bytes that are written over still need
      // to be marked out
      if (isByteConcrete(offset)) {
        markByteSymbolic(offset);
      } else if (isByteKnownSymbolic(offset)) {
        setKnownSymbolic(offset, 0);
      }
    }
  } 
}

bool ObjectState::isByteConcrete(unsigned offset) const {
  return !concreteMask || concreteMask->get(offset);
}

bool ObjectState::isByteFlushed(unsigned offset) const {
  return flushMask && !flushMask->get(offset);
}

bool ObjectState::isByteKnownSymbolic(unsigned offset) const {
  return knownSymbolics && knownSymbolics[offset].get();
}

void ObjectState::markByteConcrete(unsigned offset) {
  if (concreteMask)
    concreteMask->set(offset);
}

void ObjectState::markByteSymbolic(unsigned offset) {
  if (!concreteMask)
    concreteMask = new BitArray(size, true);
  concreteMask->unset(offset);
}

void ObjectState::markByteUnflushed(unsigned offset) {
  if (flushMask)
    flushMask->set(offset);
}

void ObjectState::markByteFlushed(unsigned offset) {
  if (!flushMask) {
    flushMask = new BitArray(size, false);
  } else {
    flushMask->unset(offset);
  }
}

void ObjectState::setKnownSymbolic(unsigned offset, 
                                   Expr *value /* can be null */) {
  if (knownSymbolics) {
    knownSymbolics[offset] = value;
  } else {
    if (value) {
      knownSymbolics = new ref<Expr>[size];
      knownSymbolics[offset] = value;
    }
  }
}

/***/

ref<Expr> ObjectState::read8(unsigned offset) const {
  if (isByteConcrete(offset)) {
    return ConstantExpr::create(concreteStore[offset], Expr::Int8);
  } else if (isByteKnownSymbolic(offset)) {
    return knownSymbolics[offset];
  } else {
    assert(isByteFlushed(offset) && "unflushed byte without cache value");
    
    return ReadExpr::create(getUpdates(), 
                            ConstantExpr::create(offset, Expr::Int32));
  }    
}

ref<Expr> ObjectState::read8(ref<Expr> offset) const {
  assert(!isa<ConstantExpr>(offset) && "constant offset passed to symbolic read8");
  unsigned base, size;
  fastRangeCheckOffset(offset, &base, &size);
  flushRangeForRead(base, size);

  if (size>4096) {
    std::string allocInfo;
    object->getAllocInfo(allocInfo);
    klee_warning_once(0, "flushing %d bytes on read, may be slow and/or crash: %s", 
                      size,
                      allocInfo.c_str());
  }
  
  return ReadExpr::create(getUpdates(), ZExtExpr::create(offset, Expr::Int32));
}

void ObjectState::write8(unsigned offset, uint8_t value) {
  //assert(read_only == false && "writing to read-only object!");
  concreteStore[offset] = value;
  setKnownSymbolic(offset, 0);

  markByteConcrete(offset);
  markByteUnflushed(offset);
}

void ObjectState::write8(unsigned offset, ref<Expr> value) {
	// can happen when ExtractExpr special cases
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    write8(offset, (uint8_t) CE->getZExtValue(8));
  } else {
    setKnownSymbolic(offset, value.get());
      
    markByteSymbolic(offset);
    markByteUnflushed(offset);
  }
}

void ObjectState::write8(ref<Expr> offset, ref<Expr> value) {
  assert(!isa<ConstantExpr>(offset) && "constant offset passed to symbolic write8");
  unsigned base, size;
  fastRangeCheckOffset(offset, &base, &size);
  flushRangeForWrite(base, size);

  if (size>4096) {
    std::string allocInfo;
    object->getAllocInfo(allocInfo);
    klee_warning_once(0, "flushing %d bytes on read, may be slow and/or crash: %s", 
                      size,
                      allocInfo.c_str());
  }
  
  updates.extend(ZExtExpr::create(offset, Expr::Int32), value);
}

/***/

ref<Expr> ObjectState::read(ref<Expr> offset, Expr::Width width) const {
  // Truncate offset to 32-bits.
  offset = ZExtExpr::create(offset, Expr::Int32);

  // Check for reads at constant offsets.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(offset))
    return read(CE->getZExtValue(32), width);

  // Treat bool specially, it is the only non-byte sized write we allow.
  if (width == Expr::Bool)
    return ExtractExpr::create(read8(offset), 0, Expr::Bool);

  // Otherwise, follow the slow general case.
  unsigned NumBytes = width / 8;
  assert(width == NumBytes * 8 && "Invalid write size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(AddExpr::create(offset, 
                                           ConstantExpr::create(idx, 
                                                                Expr::Int32)));
    Res = idx ? ConcatExpr::create(Byte, Res) : Byte;
  }

  return Res;
}

ref<Expr> ObjectState::read(unsigned offset, Expr::Width width) const {
  // Treat bool specially, it is the only non-byte sized write we allow.
  if (width == Expr::Bool)
    return ExtractExpr::create(read8(offset), 0, Expr::Bool);

  // Otherwise, follow the slow general case.
  unsigned NumBytes = width / 8;
  assert(width == NumBytes * 8 && "Invalid write size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(offset + idx);
    Res = idx ? ConcatExpr::create(Byte, Res) : Byte;
  }

  return Res;
}

void ObjectState::write(ref<Expr> offset, ref<Expr> value) {
  // Truncate offset to 32-bits.
  offset = ZExtExpr::create(offset, Expr::Int32);

  // Check for writes at constant offsets.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(offset)) {
    write(CE->getZExtValue(32), value);
    return;
  }

  // Treat bool specially, it is the only non-byte sized write we allow.
  Expr::Width w = value->getWidth();
  if (w == Expr::Bool) {
    write8(offset, ZExtExpr::create(value, Expr::Int8));
    return;
  }

  // Otherwise, follow the slow general case.
  unsigned NumBytes = w / 8;
  assert(w == NumBytes * 8 && "Invalid write size!");
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(AddExpr::create(offset, ConstantExpr::create(idx, Expr::Int32)),
           ExtractExpr::create(value, 8 * i, Expr::Int8));
  }
}

void ObjectState::write(unsigned offset, ref<Expr> value) {
  // Check for writes of constant values.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    Expr::Width w = CE->getWidth();
    if (w <= 64) {
      uint64_t val = CE->getZExtValue();
      switch (w) {
      default: assert(0 && "Invalid write size!");
      case  Expr::Bool:
      case  Expr::Int8:  write8(offset, val); return;
      case Expr::Int16: write16(offset, val); return;
      case Expr::Int32: write32(offset, val); return;
      case Expr::Int64: write64(offset, val); return;
      }
    }
  }

  // Treat bool specially, it is the only non-byte sized write we allow.
  Expr::Width w = value->getWidth();
  if (w == Expr::Bool) {
    write8(offset, ZExtExpr::create(value, Expr::Int8));
    return;
  }

  // Otherwise, follow the slow general case.
  unsigned NumBytes = w / 8;
  assert(w == NumBytes * 8 && "Invalid write size!");
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, ExtractExpr::create(value, 8 * i, Expr::Int8));
  }
} 

void ObjectState::write16(unsigned offset, uint16_t value) {
  unsigned NumBytes = 2;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

void ObjectState::write32(unsigned offset, uint32_t value) {
  unsigned NumBytes = 4;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

void ObjectState::write64(unsigned offset, uint64_t value) {
  unsigned NumBytes = 8;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)));
  }
}

/* NUKLEAR KLEE begin */
int ObjectState::compare(const ObjectState &b) const {
 
  //if (object != b.object) {
  //  return -1;
  //}

  //if (object->id != b.object->id) {
  //  return -1;
  //}

  //if (updates.getSize() < b.updates.getSize()) return -1;
  //else if (updates.getSize() > b.updates.getSize()) return 1;    

  // XXX build comparison into update, make fast
  const UpdateNode *an=updates.head, *bn=b.updates.head;
  for (; an && bn; an=an->next,bn=bn->next) {
    if (an==bn) { // exploit shared list structure
      return 0;
    } else {
      if (int res = an->compare(*bn))
        return res;
    }
  }

  //if (updates.root != NULL && b.updates.root != NULL) {
  //  if (updates.compare(b.updates)) {
  //    return -1;
  //  }
  //} else if (updates.root != b.updates.root) {
  //  return -1;
  //}

  if (size != b.size) {
    return -1;
  }
  //if (copyOnWriteOwner == b.copyOnWriteOwner) return 0;
  
  if (!hasKnownSymbolics() && !b.hasKnownSymbolics()
      && !hasFlushMask() && !b.hasFlushMask()
      && hasConcreteStore() && b.hasConcreteStore()) {
    for (unsigned i=0; i<size; i++) {
      if (concreteStore[i] != b.concreteStore[i])
        return -1;
    }
  } else {
    for (unsigned i=0; i<size; i++) {
      if (isByteConcrete(i) != b.isByteConcrete(i)) return -1;
      if (isByteKnownSymbolic(i) != b.isByteKnownSymbolic(i)) return -1;
      if (isByteFlushed(i) != b.isByteFlushed(i)) return -1;
      if (read8(i) != b.read8(i)) return -1;
    }
  }
  
  //for (unsigned i=0; i<size; i++) {
  //  if (isByteConcrete(i) != b.isByteConcrete(i)) return -1;
  //  if (isByteKnownSymbolic(i) != b.isByteKnownSymbolic(i)) return -1;
  //  if (isByteFlushed(i) != b.isByteFlushed(i)) return -1;
  //  if (read8(i) != b.read8(i)) return -1;
  //}


  return 0;
}

void ObjectState::computeDigest(EVP_MD_CTX *mdctx) {
 
  //EVP_DigestUpdate(mdctx, &object, sizeof(object));

  //EVP_DigestUpdate(mdctx, &object->id, sizeof(object->id));
 
  //EVP_DigestUpdate(mdctx, &updates.root, sizeof(updates.root));

  updates.computeDigest(mdctx);

  EVP_DigestUpdate(mdctx, &size, sizeof(size));

  bool do_concrete = false;
  if (!hasKnownSymbolics() 
      && !hasFlushMask() 
      && hasConcreteStore() ) {
    do_concrete = true;
  }
  for (unsigned i=0; i<size; i++) {
    bool byte_concrete = isByteConcrete(i);
    bool byte_symbolic = isByteKnownSymbolic(i);
    bool byte_flushed = isByteFlushed(i);
    
    EVP_DigestUpdate(mdctx, &byte_concrete, sizeof(byte_concrete));
    EVP_DigestUpdate(mdctx, &byte_symbolic, sizeof(byte_symbolic));
    EVP_DigestUpdate(mdctx, &byte_flushed, sizeof(byte_flushed));
    if (do_concrete) {
      EVP_DigestUpdate(mdctx, &concreteStore[i], sizeof(concreteStore[i]));
    } else {
      ref<Expr> e = read8(i);
      unsigned hash = e->hash();
      EVP_DigestUpdate(mdctx, &hash, sizeof(hash));
    }
  }
  
}

void ObjectState::print(std::ostream &os) const {
  os << "-- ObjectState --\n";
  os << "\tMemoryObject: " << object << "\n";
  os << "\tMemoryObject ID: " << object->id << "\n";
  os << "\tMemoryObject Name: " << object->name << "\n";
  os << "\tRoot Object: " << updates.root << "\n";
  if (updates.root)
    os << "\tArray Name: " << updates.root->name << "\n";
  else
    os << "\tArray Name: \n";
  os << "\tSize: " << size << "\n";

  os <<"\tAlloc Site: \n";
  if (object->allocSite) {
		std::string str;
		llvm::raw_string_ostream info(str);
		if (const Instruction *i = dyn_cast<Instruction>(object->allocSite)) {
			info << i->getParent()->getParent()->getNameStr() << "():";
			info << *i;
		} else if (const GlobalValue *gv = dyn_cast<GlobalValue>(object->allocSite)) {
			info << "global:" << gv->getNameStr();
		} else {
			info << "value:" << *object->allocSite;
		}
		info.flush();
		os << str;
	} else {
		os << " (no alloc info)";
	}
  os << "\n";


  os << "\tBytes:\n";
  ref<Expr> prev_e = ConstantExpr::alloc(1, Expr::Bool);
  int start = -1;
  for (unsigned i=0; i<size; i++) {
    bool concrete = isByteConcrete(i);
    bool knownSym = isByteKnownSymbolic(i);
    bool flushed = isByteFlushed(i);

    ref<Expr> e = read8(i);
    if (prev_e == e) {
      if (start == -1) {
        start = i;
      }
    } 
    if (prev_e != e || i==size-1) {
      if (start != -1) {
        os << "\t\t[" << start << "]-[" <<i-1<<"]"
           << " = " << prev_e << "\n";
        start = -1;
      }
      os << "\t\t["<<i<<"]"
                 << " concrete? " << concrete
                 << " known-sym? " << knownSym 
                 << " flushed? " << flushed << " = ";
      os << e << "\n";
    }
    prev_e = e;
  }

  os << "\tUpdates:\n";
  for (const UpdateNode *un=updates.head; un; un=un->next) {
    os << "\t\t[" << un->index << "] = " << un->value << "\n";
  }
}

void ObjectState::print_diff(std::vector<ObjectState*> &_ovec, std::ostream &os) const {
  std::vector<ObjectState*>ovec(_ovec);
  //ovec.push_back(const_cast<ObjectState*>(this));
  unsigned s = ovec.size();
  os << "-- ObjectState --\n";

  os << "\tMemoryObjects: ";
  for(unsigned i=0;i<s;i++) 
    os << ovec[i]->object << ", ";
  os << "\n";

  os << "\tMemoryObject IDs: ";
  for(unsigned i=0;i<s;i++) 
    os << ovec[i]->object->id << ", ";
  os << "\n";

  os << "\tMemoryObject Names: ";
  for(unsigned i=0;i<s;i++) 
    os << ovec[i]->object->name << ", ";
  os << "\n";

  os << "\tRoot Objects: ";
  for(unsigned i=0;i<s;i++) 
    os << ovec[i]->updates.root << ", ";
  os << "\n";

  os << "\tArray Names: ";
  for(unsigned i=0;i<s;i++) 
    if (ovec[i]->updates.root)
      os << ovec[i]->updates.root->name << ", ";
    else
      os << " None, ";
  os << "\n";

  os << "\tSizes: ";
  for(unsigned i=0;i<s;i++) 
    os << ovec[i]->size << ", ";
  os << "\n";

  os <<"\tAlloc Sites: ";
  for(unsigned i=0;i<1;i++) {
		const llvm::Value* as = ovec[i]->object->allocSite;
    if (as) {
			std::string str;
			llvm::raw_string_ostream info(str);
			if (const Instruction *i = dyn_cast<Instruction>(as)) {
				info << i->getParent()->getParent()->getNameStr() << "():";
				info << *i;
			} else if (const GlobalValue *gv = dyn_cast<GlobalValue>(as)) {
				info << "global: " << gv->getNameStr();
			} else {
				info << "value: " << *as;
			}
			info.flush();
			os << str;
		} else {
			os << " (no alloc info)";
		}
		os << ", ";
	}
  os << "\n";

  for(unsigned i=1;i<s;i++) 
    assert(ovec[i]->size == ovec[i-1]->size);

  os << "\tDifferent Bytes:\n";
  ref<Expr> prev_e = ConstantExpr::alloc(0, Expr::Bool);
  int start = -1;
  for (unsigned i=0; i<size; i++) {

    bool all_equal = true;
    for(unsigned j=1;j<s;j++) {
      if (ovec[j-1]->read8(i) != ovec[j]->read8(i)) {
        all_equal = false;
        break;
      }
    }

    if (all_equal) {

      ref<Expr> e = ovec[0]->read8(i);
      if (prev_e == e) {
        if (start == -1) {
          start = i;
        }
      } 
      if (prev_e != e || i==size-1) {
        if (start != -1) {
          os << "\t\t[" << start << "]-[" <<i-1<<"]"
             << " = " << prev_e << "\n";
          start = -1;
        }
        os << "\t\t["<<i<<"] = " << e << "\n";
      }
      prev_e = e;

    } else {
      if (start != -1) {
        os << "\t\t[" << start << "]-[" <<i-1<<"]"
           << " = " << prev_e << "\n";
        start = -1;
      }
      os << "\t\t["<<i<<"] = ";
      for(unsigned j=0;j<s;j++) 
        os << ovec[j]->read8(i) << ", ";
      os << "\n";
    }
  }

  os << "\tUpdates:\n";
  for(unsigned i=0;i<s;i++)  {
    for (const UpdateNode *un=ovec[i]->updates.head; un; un=un->next) {
      os << "\t\t[" << i << "][" << un->index << "] = " << un->value << "\n";
    }
  }

}

void ObjectState::print() {
  print(std::cerr);
}
/* NUKLEAR KLEE end */
