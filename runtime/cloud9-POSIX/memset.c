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

#ifdef HAVE_MEMORY_FUNCTION_INTRINSICS
DEFINE_MODEL(void*, memset, void *dst, int s, size_t count) {
  return klee_memset(dst, s, count);
}
#endif

