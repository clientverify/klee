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

	AddressSpaceGraph *asg_build = new AddressSpaceGraph(&state->addressSpace);
	asg_build->build_graph();
	delete asg_build;

	AddressSpaceGraph *asg = new AddressSpaceGraph(&state->addressSpace);

	for (klee::MemoryMap::iterator it=state->addressSpace.objects.begin(),
			ie=state->addressSpace.objects.end(); it!=ie; ++it) {
		MemoryObjectNode *a = new MemoryObjectNode(it->second);
		MemoryObjectNode *b = new MemoryObjectNode(it->second);
		asg->extract_pointers(a);
		asg->extract_pointers_by_resolving(b);
		if (a->out_degree() != b->out_degree()) {
			cv_warning("AddressSpaceGraph Test: degree mismatch %d != %d",
					a->out_degree(), b->out_degree());
			(*it->second).print(*cv_warning_stream);
			return;
		} else {
			for (unsigned i=0; i<a->out_degree(); ++i) {
				PointerEdge *a_edge=a->out_edge(i), *b_edge=b->out_edge(i);
				if (a_edge->offset != b_edge->offset) {
					cv_warning("AddressSpaceGraph Test: edge offset mismatch %d != %d",
						a_edge->offset, b_edge->offset);
					(*it->second).print(*cv_warning_stream);
					return;
				} else if (a_edge->points_to_address != b_edge->points_to_address) {
					cv_warning("AddressSpaceGraph Test: edge points_to_address mismatch %ld != %ld",
						a_edge->points_to_address, b_edge->points_to_address);
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
