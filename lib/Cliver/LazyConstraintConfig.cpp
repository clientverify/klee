//===-- LazyConstraintConfig.cpp -------------------------------*- C++ -*-===//
//
// <insert license>
//
//===---------------------------------------------------------------------===//
//
// Implementation of LazyConstraintConfig (database of trigger functions)
//
//===---------------------------------------------------------------------===//

#include "cliver/CVStream.h"
#include "CVCommon.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "cliver/LazyConstraint.h"
#include "cliver/LazyConstraintConfig.h"

#include <utility>
#include <vector>

namespace cliver {

extern llvm::cl::opt<bool> DebugLazyConstraint;

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

///////////////////////////////////////////////////////////////////////////////

// sample "prohibitive" function
static unsigned int p(unsigned int x) { return 641 * x; }

// inverse of sample "prohibitive" function
static unsigned int p_inv(unsigned int x) { return 6700417 * x; }

// trigger version of p()
static int trigger_p(const unsigned char *in_buf, size_t in_len,
                     unsigned char *out_buf, size_t out_len) {
  const unsigned int *input = (const unsigned int *)in_buf;
  unsigned int *output = (unsigned int *)out_buf;
  if (input == NULL || in_len != sizeof(*input))
    return -1;
  if (output == NULL || out_len != sizeof(*output))
    return -1;

  *output = p(*input);

  return 0; // success
}

// trigger version of p_inv()
static int trigger_p_inv(const unsigned char *in_buf, size_t in_len,
                         unsigned char *out_buf, size_t out_len) {
  const unsigned int *input = (const unsigned int *)in_buf;
  unsigned int *output = (unsigned int *)out_buf;
  if (input == NULL || in_len != sizeof(*input))
    return -1;
  if (output == NULL || out_len != sizeof(*output))
    return -1;

  *output = p_inv(*input);

  return 0; // success
}

///////////////////////////////////////////////////////////////////////////////

void LazyTriggerFuncDB::preload() {
  // For testing only. Don't give these names to any real functions.
  Instance().insert("__LAZYTEST_p", trigger_p);
  Instance().insert("__LAZYTEST_p_inv", trigger_p_inv);

  // Add more functions here.
}

} // end namespace cliver
