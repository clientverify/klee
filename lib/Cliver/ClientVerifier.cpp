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

ClientVerifier::ClientVerifier() 
  : cvstream_(new CVStream()) {
  init();
}

ClientVerifier::~ClientVerifier() {
}	

void ClientVerifier::init() {
  cvstream_->init();
}

} // end namespace cliver


