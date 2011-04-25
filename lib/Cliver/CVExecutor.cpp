//===-- CVExecutor.cpp -====-------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "CVExecutor.h"
#include "CVMemoryManager.h"

namespace cliver {

CVHandler::CVHandler(ClientVerifier *cv)
  : cv_(cv), 
    paths_explored_(0) {

  }

std::ostream &CVHandler::getInfoStream() const { 
  return cv_->getCVStream()->info_stream();
}

std::string CVHandler::getOutputFilename(const std::string &filename) { 
  return cv_->getCVStream()->getOutputFilename(filename);
}

std::ostream *CVHandler::openOutputFile(const std::string &filename) {
  return cv_->getCVStream()->openOutputFile(filename);
}

void CVHandler::incPathsExplored() {
  paths_explored_++;
}

void CVHandler::processTestCase(const klee::ExecutionState &state, 
    const char *err, const char *suffix) {
}


CVExecutor::CVExecutor(ClientVerifier *cv, const InterpreterOptions &opts, 
                       klee::InterpreterHandler *ie)
 : klee::Executor(opts, ie),
   cv_(cv) {
  memory = new CVMemoryManager();
}

CVExecutor::~CVExecutor() {

}

} // end namespace cliver

