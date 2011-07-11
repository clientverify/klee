//===-- ClientVerifier.cpp---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ClientVerifier.h"
#include "llvm/Support/CommandLine.h"

namespace cliver {

int CVContext::next_id_ = 0;

CVContext::CVContext() : context_id_(increment_id()) {}

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()) {
  init();
}

ClientVerifier::~ClientVerifier() {
	delete cvstream_;
}	

void ClientVerifier::init() {
  cvstream_->init();
}

} // end namespace cliver


