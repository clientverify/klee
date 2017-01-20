//===-- LazyConstraint.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// 
// Implementation of the LazyConstraint and LazyConstraintDispatcher classes
//
//===----------------------------------------------------------------------===//

#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "../Core/AddressSpace.h"
#include "../Core/Context.h"
#include "../Core/Memory.h"

#include "klee/util/ExprUtil.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "cliver/LazyConstraint.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cliver {

llvm::cl::opt<bool>
DebugLazyConstraint("debug-lazy-constraint", llvm::cl::init(false));


#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
  __CVDEBUG(DebugLazyConstraint, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
  __CVDEBUG_S(DebugLazyConstraint, __state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x) \
  __CVDEBUG_S2(DebugLazyConstraint, __state_id_1, __state_id_2, __x) \

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#undef CVDEBUG_S2
#define CVDEBUG_S2(__state_id_1, __state_id_2, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

bool LazyConstraint::trigger(const CVAssignment &cva,
                             LazyConstraint::ExprVec &real_constraints) const {
  //findSymbolicObjects
  return false;
}

} // end namespace cliver
