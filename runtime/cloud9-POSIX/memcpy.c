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

#ifdef HAVE_MEMORY_FUNCTION_INTRINSICS
DEFINE_MODEL(void*, memcpy, void *dst, void const *src, size_t len) {
  return klee_memcpy(dst, src, len);
}
#endif

