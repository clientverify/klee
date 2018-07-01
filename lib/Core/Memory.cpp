//===-- Memory.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"

#include "Context.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "klee/util/BitArray.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/util/ArrayCache.h"

#include "ObjectHolder.h"
#include "MemoryManager.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <sstream>

using namespace llvm;
using namespace klee;

uint16_t poison_val = 0xDEAD;

uint16_t * get_rep_buf (uint8_t  * val_ptr);

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
  if (parent)
    parent->markFreed(this);
}

void MemoryObject::getAllocInfo(std::string &result) const {
  llvm::raw_string_ostream info(result);

  info << "MO" << id << "[" << size << "]";

  if (allocSite) {
    info << " allocated at ";
    if (const Instruction *i = dyn_cast<Instruction>(allocSite)) {
      info << i->getParent()->getParent()->getName() << "():";
      info << *i;
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(allocSite)) {
      info << "global:" << gv->getName();
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
  : refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(0, 0),
    size(mo->size),
    readOnly(false) {
  mo->refCount++;
  if (!UseConstantArrays) {
    static unsigned id = 0;
    const Array *array =
        getArrayCache()->CreateArray("tmp_arr" + llvm::utostr(++id), size);
    updates = UpdateList(array, 0);
  }
  memset(concreteStore, 0, size);
}


ObjectState::ObjectState(const MemoryObject *mo, const Array *array)
  : refCount(0),
    object(mo),
    concreteStore(new uint8_t[mo->size]),
    concreteMask(0),
    flushMask(0),
    knownSymbolics(0),
    updates(array, 0),
    size(mo->size),
    readOnly(false) {
  mo->refCount++;
  //AH Changed to memset to 0 before call to makeSymbolic.
   memset(concreteStore, 0, size);

  makeSymbolic();
 
}

ObjectState::ObjectState(const ObjectState &os) 
  : refCount(0),
    object(os.object),
    concreteStore(new uint8_t[os.size]),
    concreteMask(os.concreteMask ? new BitArray(*os.concreteMask, os.size) : 0),
    flushMask(os.flushMask ? new BitArray(*os.flushMask, os.size) : 0),
    knownSymbolics(0),
    updates(os.updates),
    size(os.size),
    readOnly(false) {
  assert(!os.readOnly && "no need to copy read only object?");
  if (object)
    object->refCount++;

  if (os.knownSymbolics) {
    knownSymbolics = new ref<Expr>[size];
    for (unsigned i=0; i<size; i++)
      knownSymbolics[i] = os.knownSymbolics[i];
  }

  memcpy(concreteStore, os.concreteStore, size*sizeof(*concreteStore));
}

ObjectState::~ObjectState() {
  delete concreteMask;
  delete flushMask;
  delete[] knownSymbolics;
  //delete[] concreteStore;

  if (object)
  {
    assert(object->refCount > 0);
    object->refCount--;
    if (object->refCount == 0)
    {
      delete object;
    }
  }
}

ArrayCache *ObjectState::getArrayCache() const {
  assert(object && "object was NULL");
  return object->parent->getArrayCache();
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

    static unsigned id = 0;
    const Array *array = getArrayCache()->CreateArray(
        "const_arr" + llvm::utostr(++id), size, &Contents[0],
        &Contents[0] + Contents.size());
    updates = UpdateList(array, 0);

    // Apply the remaining (non-constant) writes.
    for (; Begin != End; ++Begin)
      updates.extend(Writes[Begin].first, Writes[Begin].second);
  }

  return updates;
}

void ObjectState::makeConcrete() {
  delete concreteMask;
  delete flushMask;
  delete[] knownSymbolics;
  concreteMask = 0;
  flushMask = 0;
  knownSymbolics = 0;
}

void ObjectState::makeSymbolic() {
  assert(!updates.head &&
         "XXX makeSymbolic of objects with symbolic values is unsupported");

  // XXX simplify this, can just delete various arrays I guess
  printf("Calling makeSymbolic() on object with conc store at 0x%llx \n", (uint64_t) &(this->concreteStore[0]));
  
  for (unsigned i=0; i<size; i++) {
    markByteSymbolic(i);
    setKnownSymbolic(i, 0);
    markByteFlushed(i);

  }

  printf("After calling makeSymbolic(), we have the following concrete values \n");
  for (int i = 0; i < size; i++)
    printf("Val at %d is 0x%x \n",i, this->concreteStore[i]);

  printf("Exiting makeSymbolic() \n");
  
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

  //printf("Entering isByteConcrete \n");
  //Check to see if rep buffer has the poison tag
  //uint64_t base = (uint64_t) &(this->concreteStore[0]);
  uint64_t base = (uint64_t) (concreteStore);
  uint8_t * addr = (uint8_t *) ((uint64_t) offset + base);
  //printf("Check1 offset is %llu, base is %llu \n", offset, base);
  uint16_t * repBuf = get_rep_buf (addr);
  //printf("repBuf is %p \n", repBuf);
  //printf("Check 1.5 \n");
  bool hasPoisonTag = (*repBuf == poison_val);

  //printf("Offset is %u \n", offset);
  
  //printf("Concrete store is located at 0x%llx \n ", &(this->concreteStore[0]));
  
  //printf("repBuf val is 0x%x \n", repBuf);
  
  //We should be setting the concrete mask in 2s or not at all.
  unsigned evenOffset;
  if ( offset %2 ==0)
    evenOffset = offset;
  else
    evenOffset = offset -1;
  unsigned oddOffset = evenOffset + 1;
  
  //printf("Check2 \n");
  
  //We only say a byte is symbolic if
  // 1. neither of the concrete mask bits are set in it's representative buffer (or the concrete mask doesn't exist)
  // 2. AND the buffer is poison

  //printf("Calling isByteConcrete with evenOffsetMask bit %d, oddOffsetMask bit %d, and hasPoisonTag %d  \n", concreteMask->get(evenOffset), concreteMask->get(oddOffset), hasPoisonTag );
  
  //A byte is concrete if it's not symbolic.
  return (!concreteMask || (concreteMask->get(evenOffset) && concreteMask->get(oddOffset))) || (hasPoisonTag == false) ;
  
  //return !concreteMask || concreteMask->get(offset);
}

bool ObjectState::isByteFlushed(unsigned offset) const {
  return flushMask && !flushMask->get(offset);
}
  //Updated for TASE
  //Need to make sure buffer hasn't been made concrete during native execution
  //TODO: Revisit this later to make sure it's consistent with our overall poisoning scheme
bool ObjectState::isByteKnownSymbolic(unsigned offset) const {

  if (!isByteConcrete(offset))
    return knownSymbolics && knownSymbolics[offset].get();
  else
    return false;

  //return knownSymbolics && knownSymbolics[offset].get();
}

//Updated for TASE
//We should be marking 2 bytes at a time as concrete.
//Need to be careful when calling because of this.
void ObjectState::markByteConcrete(unsigned offset) {
  if (concreteMask) {
    unsigned evenOffset;
    if ( offset %2 ==0)
      evenOffset = offset;
    else
      evenOffset = offset -1;
    unsigned oddOffset = evenOffset + 1;
    concreteMask->set(evenOffset);
    concreteMask->set(oddOffset);
  }
}

//This is a potentially destructive call in TASE.
//Need to make sure we've already saved any concrete
//contents of the byte potentially not being made symbolic
//in the two byte buffer.
void ObjectState::markByteSymbolic(unsigned offset) {
  if (!concreteMask)
    concreteMask = new BitArray(size, true);

  unsigned evenOffset;
  if ( offset %2 ==0)
    evenOffset = offset;
  else
    evenOffset = offset -1;
  unsigned oddOffset = evenOffset + 1;
  
  concreteMask->unset(evenOffset);
  concreteMask->unset(oddOffset);

  //Poison the rep buffer.
  uint64_t base = (uint64_t) &(this->concreteStore[0]);
  uint8_t * addr = (uint8_t *) ((uint64_t) offset + base);
  uint16_t * repBuf = get_rep_buf (addr);
   *repBuf = poison_val;
  
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


//AH: Made this helper function to find the representative 2-byte aligned buffer for a byte.
uint16_t * get_rep_buf (uint8_t  * val_ptr) {
  uint64_t raw_address = (uint64_t) val_ptr;
  if (raw_address % 2 == 0)
    return (uint16_t *)  val_ptr;
  else {
    raw_address = raw_address - 1;
    return (uint16_t *) raw_address;
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


//TODO: Eventually need to add logic to the write8
//that takes care of concrete writes in the interpreter
//to a poison buffer.  Right now we don't catch that
//a concrete 2 byte write to a 2 byte symbolic buffer
//should totally concretize the buffer.  We just basically
//add a constraint now that the two byte buffer happens
//to equal a constant value.
void ObjectState::write8(unsigned offset, uint8_t value, bool twoByteAligned) {
  //assert(read_only == false && "writing to read-only object!");

  
  
  //Case 1: Byte's rep buffer is concrete, or it's safe to clobber the value because
  //it's part of a two byte aligned concrete write operation.
  if (isByteConcrete(offset) || twoByteAligned) {
    concreteStore[offset] = value;
    setKnownSymbolic(offset, 0);
    markByteConcrete(offset);
    markByteUnflushed(offset);
  } else {
    //Case 2:  We are writing to a buffer that has one or more symbolic bytes
    //Let's keep the buffer symbolic but add the value as a constant value.
    ref <Expr> constVal = ConstantExpr::create(value, Expr::Int8);
    setKnownSymbolic(offset, constVal.get());
    markByteSymbolic(offset);
    markByteUnflushed(offset);
  }
  
  /*
  concreteStore[offset] = value;
  setKnownSymbolic(offset, 0);

  markByteConcrete(offset);
  markByteUnflushed(offset);
  */
}


void ObjectState::write8(unsigned offset, ref<Expr> value) {
  unsigned evenOffset;
  if ( offset %2 ==0)
    evenOffset = offset;
  else
    evenOffset = offset -1;
  unsigned oddOffset = evenOffset + 1;

  unsigned otherOffset;
  if (offset == evenOffset) {
    otherOffset = oddOffset;
  } else {
    otherOffset = evenOffset;
  }

  uint64_t base = this->object->address;
  uint8_t * otherAddr = (uint8_t *) ((uint64_t) otherOffset + base);

  //printf("In write 8 \n");
  
  // can happen when ExtractExpr special cases
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    write8(offset, (uint8_t) CE->getZExtValue(8));
  } else {
    //printf("Debug1 \n");
    //TODO Double check for TASE
    //Remember, "isByteConcrete" is really checking if the buffer containing offset
    //is poisoned and contains a symbolic value in either or both bytes.
    if (isByteConcrete(offset)) {
      //In this case, we need to poison the buffer and
      //assign the symbolic value at offset without
      //cloberring the concrete value in offset +1 (or offset -1).
      //printf("Debug2 \n");
      ref <Expr> constVal = ConstantExpr::create(*otherAddr, Expr::Int8);
      setKnownSymbolic(otherOffset, constVal.get());
      markByteSymbolic(otherOffset);
      markByteUnflushed(otherOffset);
      //printf("Debug3 \n");
      
      //After other byte is taken care of, business as usual
      setKnownSymbolic(offset, value.get());

      //printf("Debug 4 \n");
      markByteSymbolic(offset);
      markByteUnflushed(offset);
    } else {
      //printf("Debug 5 \n");
      setKnownSymbolic(offset, value.get());
      
      markByteSymbolic(offset);
      markByteUnflushed(offset);
    }
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
  assert(width == NumBytes * 8 && "Invalid read size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(AddExpr::create(offset, 
                                           ConstantExpr::create(idx, 
                                                                Expr::Int32)));
    Res = i ? ConcatExpr::create(Byte, Res) : Byte;
  }

  return Res;
}




ref<Expr> ObjectState::read(unsigned offset, Expr::Width width) const {
  // Treat bool specially, it is the only non-byte sized write we allow.

  if (width == Expr::Bool)
    return ExtractExpr::create(read8(offset), 0, Expr::Bool);

  // Otherwise, follow the slow general case.
  unsigned NumBytes = width / 8;
  assert(width == NumBytes * 8 && "Invalid width for read size!");
  ref<Expr> Res(0);
  for (unsigned i = 0; i != NumBytes; ++i) {

    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    ref<Expr> Byte = read8(offset + idx);
    Res = i ? ConcatExpr::create(Byte, Res) : Byte;
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
    if (w <= 64 && klee::bits64::isPowerOfTwo(w)) {
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
   //printf("Writing to concrete store at addr %lu, hex %p  \n", ((void *) &concreteStore[offset]  ), (void *) &concreteStore[offset] );
  // Otherwise, follow the slow general case.

  
  unsigned NumBytes = w / 8;
  assert(w == NumBytes * 8 && "Invalid write size!");
  for (unsigned i = 0; i != NumBytes; ++i) {
    //printf("Calling write in memory.cpp \n");
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, ExtractExpr::create(value, 8 * i, Expr::Int8));
  }
} 

void ObjectState::write16(unsigned offset, uint16_t value) {

  //


  //Optimization to handle concrete writes to 2 byte aligned buffers containing
  //symbolic taint that may be completely cleanly clobbered with a concrete value.
  bool twoByteAligned = false;
  uint64_t baseAddr = this->getObject()->address;
  if ((((uint64_t) offset + baseAddr) % 2) == 0)
    twoByteAligned = true;

  //if (twoByteAligned)
    //printf("Found twoByteAligned write \n");
    
  unsigned NumBytes = 2;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)), twoByteAligned);
  }
}

void ObjectState::write32(unsigned offset, uint32_t value) {

  //Optimization to handle concrete writes to 2 byte aligned buffers containing
  //symbolic taint that may be completely cleanly clobbered with a concrete value.
  bool twoByteAligned = false;
  uint64_t baseAddr = this->getObject()->address;
  if ((((uint64_t) offset + baseAddr) % 2) == 0)
    twoByteAligned = true;

  //if (twoByteAligned)
  //printf("Found twoByteAligned write \n");
  
  unsigned NumBytes = 4;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)), twoByteAligned);
  }
}

void ObjectState::write64(unsigned offset, uint64_t value) {

  //Optimization to handle concrete writes to 2 byte aligned buffers containing
  //symbolic taint that may be completely cleanly clobbered with a concrete value.
  bool twoByteAligned = false;
  uint64_t baseAddr = this->getObject()->address;
  if ((((uint64_t) offset + baseAddr) % 2) == 0)
    twoByteAligned = true;


  //if (twoByteAligned)
  //printf("Found twoByteAligned write \n");
  
    
  unsigned NumBytes = 8;
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned idx = Context::get().isLittleEndian() ? i : (NumBytes - i - 1);
    write8(offset + idx, (uint8_t) (value >> (8 * i)), twoByteAligned);
  }
}

void ObjectState::print() {
  llvm::errs() << "-- ObjectState --\n";
  llvm::errs() << "\tMemoryObject ID: " << object->id << "\n";
  llvm::errs() << "\tRoot Object: " << updates.root << "\n";
  llvm::errs() << "\tSize: " << size << "\n";

  llvm::errs() << "\tBytes:\n";
  for (unsigned i=0; i<size; i++) {
    llvm::errs() << "\t\t["<<i<<"]"
               << " concrete? " << isByteConcrete(i)
               << " known-sym? " << isByteKnownSymbolic(i)
               << " flushed? " << isByteFlushed(i) << " = ";
    ref<Expr> e = read8(i);
    llvm::errs() << e << "\n";
  }

  llvm::errs() << "\tUpdates:\n";
  for (const UpdateNode *un=updates.head; un; un=un->next) {
    llvm::errs() << "\t\t[" << un->index << "] = " << un->value << "\n";
  }
}
