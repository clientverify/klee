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
#include "ExecutionObserver.h"

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

  virtual void transferToBasicBlock(llvm::BasicBlock *dst, 
                                    llvm::BasicBlock *src,
                                    klee::ExecutionState &state);

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

	//void register_event(const CliverEventInfo& event_info);
	//CliverEventInfo* lookup_event(llvm::Instruction *inst);
  
  void register_function_call_event(const char **fname,
                                    ExecutionEventType event_type);

	void add_state(CVExecutionState* state);

	void add_state_internal(CVExecutionState* state);

	void rebuild_solvers();

	uint64_t check_memory_usage();

	klee::KInstruction* get_instruction(unsigned id);

  void handle_pre_execution_events(klee::ExecutionState &state);

  void handle_post_execution_events(klee::ExecutionState &state);

 private:
  ClientVerifier *cv_;
	StateMerger *merger_;
	ConstraintPruner *pruner_;
	std::map<llvm::Function*, ExecutionEventType> function_call_events_;
};

} // end cliver namespace

extern cliver::CVExecutor *g_executor;

#endif // CLIVER_EXECUTOR_H
