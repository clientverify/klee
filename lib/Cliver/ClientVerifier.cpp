//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLine.h"
#include "ClientVerifier.h"
#include "../lib/Core/SpecialFunctionHandler.h"
#include "CVExecutor.h"
#include "TestHelper.h"

namespace cliver {

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()) {
  cvstream_->init();
}

ClientVerifier::~ClientVerifier() {
	delete cvstream_;
}	

void ClientVerifier::prepare_to_run(CVExecutor *executor) {
	// init testing helper handlers
	executor->add_external_handler(
			"cliver_test_extract_pointers", ExternalHandler_test_extract_pointers);
}

} // end namespace cliver


