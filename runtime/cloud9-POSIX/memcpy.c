//===-- memcpy.c ----------------------------------------------------------===//
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
DEFINE_MODEL(void*, memcpy, void *dst, void const *src, size_t len) {

  // uclibc depends on the following behavior, although technically passing an
  // invalid pointer to memcpy causes undefined behavior according to the C
  // standard.
  if (len == 0) {
    return dst; // even if dst is NULL or otherwise invalid
  }

  return klee_memcpy(dst, src, len);
}
#endif

