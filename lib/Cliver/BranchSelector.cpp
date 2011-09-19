//===-- BranchSelector.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "BranchSelector.h"
#include "llvm/Support/CommandLine.h"
#include "CVStream.h"
#include "CVExecutionState.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

BranchSelector::BranchSelector(CVExecutionState* state)
	: state_(state) {}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
