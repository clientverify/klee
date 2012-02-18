//===-- TestHelper.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/TestHelper.h"
#include "cliver/AddressSpaceGraph.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "CVCommon.h"

#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"
#include "klee/Executor.h"
#include "klee/ExecutionState.h"
#include "klee/SpecialFunctionHandler.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/ADT/Twine.h"

#include <errno.h>

namespace cliver {

void ExternalHandler_test_extract_pointers(klee::Executor* executor,
		klee::ExecutionState *state, klee::KInstruction *target, 
    std::vector<klee::ref<klee::Expr> > &arguments) {

	cv_message("test_extract_pointers: START");
	AddressSpaceGraph *asg= new AddressSpaceGraph(state);
	asg->build();
	delete asg;

	asg = new AddressSpaceGraph(state);

	for (klee::MemoryMap::iterator it=state->addressSpace.objects.begin(),
			ie=state->addressSpace.objects.end(); it!=ie; ++it) {
		PointerList results_a, results_b;
		asg->extract_pointers(it->second, results_a);
		asg->extract_pointers_by_resolving(it->second, results_b);
		if (results_a.size() != results_b.size()) {
			cv_warning("pointer extraction count mismatch %d != %d", 
                 (int)results_a.size(), (int)results_b.size());
			(*it->second).print(*cv_warning_stream);
			return;
		} else {
			for (unsigned i=0; i<results_a.size(); ++i) {
				PointerProperties pa = results_a[i], pb = results_b[i];
				if (pa.offset != pb.offset) {
					cv_warning("edge offset mismatch %d != %d", pa.offset, pb.offset);
					(*it->second).print(*cv_warning_stream);
					return;
				} else if (pa.address != pb.address) {
					cv_warning("edge points_to_address mismatch %ld != %ld", pb.address, pb.address);
					(*it->second).print(*cv_warning_stream);
					return;
				}
			}
		}
	}
	delete asg;
	cv_message("test_extract_pointers: PASSED");
}

}
