//===-- CVExecutor.h --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTOR_H
#define CLIVER_EXECUTOR_H

#include "../Core/Executor.h"
#include "ClientVerifier.h"

namespace cliver {

class CVMemoryManager;

class CVHandler : public klee::InterpreterHandler {
 public:
  CVHandler(ClientVerifier *cv);
  std::ostream &getInfoStream() const;
  std::string getOutputFilename(const std::string &filename);
  std::ostream *openOutputFile(const std::string &filename);
  void incPathsExplored();
  void processTestCase(const klee::ExecutionState &state, 
                       const char *err, const char *suffix);
 private:
  ClientVerifier *cv_;
  int paths_explored_;
};

class CVExecutor : public klee::Executor {
 public:
  CVExecutor(ClientVerifier *cv,
      const InterpreterOptions &opts, 
      klee::InterpreterHandler *ie);

  virtual ~CVExecutor();

  virtual void runFunctionAsMain(llvm::Function *f,
				                 int argc, char **argv, char **envp);

  virtual void executeMakeSymbolic(klee::ExecutionState &state, 
                                   const klee::MemoryObject *mo);
 private:
  ClientVerifier *cv_;
};

} // end cliver namespace

#endif // CLIVER_EXECUTOR_H
