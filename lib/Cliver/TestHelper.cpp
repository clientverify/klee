//===-- TestHelper.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "TestHelper.h"
#include "CVExecutor.h"
#include "AddressSpaceGraph.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "../Core/SpecialFunctionHandler.h"
#include "../Core/Executor.h"
#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"

#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/ADT/Twine.h"

#include <errno.h>

namespace cliver {

void ExternalHandler_test_extract_pointers(klee::Executor* executor,
		klee::ExecutionState *state, klee::KInstruction *target, 
    std::vector<klee::ref<klee::Expr> > &arguments) {

	AddressSpaceGraph *asg = new AddressSpaceGraph(&state->addressSpace);
	asg->test_extract_pointers();

}

}
