//===-- memset.c ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>
#include "models.h"

#if HAVE_MEMORY_FUNCTION_INTRINSICS
DEFINE_MODEL(void*, memset, void *dst, int s, size_t count) {

  // uclibc depends on the following behavior, although technically passing an
  // invalid pointer to memset causes undefined behavior according to the C
  // standard.
  if (count == 0) {
    return dst; // even if dst is NULL or otherwise invalid
  }

  return klee_memset(dst, s, count);
}
#endif

