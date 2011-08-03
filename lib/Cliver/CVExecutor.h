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
#include "../Core/SpecialFunctionHandler.h"
#include "ClientVerifier.h"

namespace cliver {

class CVExecutionState;
class StateMerger;
class ConstraintPruner;

class CVHandler : public klee::InterpreterHandler {
 public:
  //CVHandler(ClientVerifier *cv);
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
  CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ie);

  virtual ~CVExecutor();

  virtual void run(klee::ExecutionState &initialState);

  virtual const llvm::Module *
  setModule(llvm::Module *module, const ModuleOptions &opts);

  virtual void runFunctionAsMain(llvm::Function *f,
				                 int argc, char **argv, char **envp);

  virtual void executeMakeSymbolic(klee::ExecutionState &state, 
                                   const klee::MemoryObject *mo);

	ClientVerifier* client_verifier() { return cv_; }

	void add_external_handler(std::string name, 
			klee::SpecialFunctionHandler::ExternalHandler external_handler,
			bool has_return_value=true);

	void resolve_one(klee::ExecutionState *state, 
			klee::ref<klee::Expr> address_expr, klee::ObjectPair &result);

	void terminate_state(CVExecutionState *state);

	void bind_local(klee::KInstruction *target, 
			CVExecutionState *state, unsigned i);

	bool compute_truth(CVExecutionState* state, 
			klee::ref<klee::Expr>, bool &result);

  void add_constraint(CVExecutionState *state, 
			klee::ref<klee::Expr> condition);

	//void cv_run(klee::ExecutionState &initialState);

 private:
  ClientVerifier *cv_;
	StateMerger *merger_;
	ConstraintPruner *pruner_;
};

} // end cliver namespace

#endif // CLIVER_EXECUTOR_H
