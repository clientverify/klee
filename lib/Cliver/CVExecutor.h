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

class CVExecutor : public klee::Executor {
 public:
  CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ie);

  virtual ~CVExecutor();

  virtual void run(klee::ExecutionState &initialState);

  virtual void stepInstruction(klee::ExecutionState &state);

  virtual void updateStates(klee::ExecutionState *current);

  virtual const llvm::Module *
  setModule(llvm::Module *module, const ModuleOptions &opts);

  virtual void runFunctionAsMain(llvm::Function *f,
				                 int argc, char **argv, char **envp);

  virtual void executeMakeSymbolic(klee::ExecutionState &state, 
                                   const klee::MemoryObject *mo);

  virtual void branch(klee::ExecutionState &state, 
              const std::vector< klee::ref<klee::Expr> > &conditions,
              std::vector<klee::ExecutionState*> &result);

  // Fork current and return states in which condition holds / does
  // not hold, respectively. One of the states is necessarily the
  // current state, and one of the states may be null.
  virtual StatePair fork(klee::ExecutionState &current, 
			klee::ref<klee::Expr> condition, bool isInternal);

  virtual void terminateState(klee::ExecutionState &state);

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

	void register_event(const CliverEventInfo& event_info);
	CliverEventInfo* lookup_event(llvm::Instruction *inst);

	void add_state(CVExecutionState* state);
	void remove_state(CVExecutionState* state);

	uint64_t check_memory_usage();
 private:
  ClientVerifier *cv_;
	StateMerger *merger_;
	ConstraintPruner *pruner_;
	std::map<unsigned, CliverEventInfo> instruction_events_;
	std::map<llvm::Function*, CliverEventInfo> function_call_events_;
};

} // end cliver namespace

#endif // CLIVER_EXECUTOR_H
