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

#include "cliver/ClientVerifier.h"
#include "cliver/ExecutionObserver.h"

#include "../../lib/Core/Executor.h"
#include "../../lib/Core/SpecialFunctionHandler.h"

namespace cliver {

class CVExecutionState;
class ExecutionStateProperty;
class StateMerger;
class ConstraintPruner;

class CVExecutor : public klee::Executor {
 public:
  CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ie);

  virtual ~CVExecutor();

  virtual void executeCall(klee::ExecutionState &state, 
                           klee::KInstruction *ki,
                           llvm::Function *f,
                           std::vector< klee::ref<klee::Expr> > &arguments);

  virtual void callExternalFunction(klee::ExecutionState &state,
                                    klee::KInstruction *target,
                                    llvm::Function *function,
                                    std::vector< klee::ref<klee::Expr> > &arguments);

  virtual void run(klee::ExecutionState &initialState);

  virtual void execute(klee::ExecutionState *initialState, klee::MemoryManager* memory);

  virtual void stepInstruction(klee::ExecutionState &state);

  virtual void runFunctionAsMain(llvm::Function *f,
				                 int argc, char **argv, char **envp);

  virtual void executeMakeSymbolic(klee::ExecutionState &state, const klee::MemoryObject *mo,
                           const std::string &name);

  virtual void branch(klee::ExecutionState &state, 
              const std::vector< klee::ref<klee::Expr> > &conditions,
              std::vector<klee::ExecutionState*> &result);

  //// Fork current and return states in which condition holds / does
  //// not hold, respectively. One of the states is necessarily the
  //// current state, and one of the states may be null.
  virtual klee::Executor::StatePair fork(klee::ExecutionState &current, 
			klee::ref<klee::Expr> condition, bool isInternal);

  void terminateStateOnExit(klee::ExecutionState &state);

	ClientVerifier* client_verifier() { return cv_; }

	void add_external_handler(std::string name, 
			klee::SpecialFunctionHandler::ExternalHandler external_handler,
			bool has_return_value=true);

	void resolve_one(klee::ExecutionState *state, 
			klee::ref<klee::Expr> address_expr, klee::ObjectPair &result,
      bool writeable);

	void terminate_state(CVExecutionState *state);

	void bind_local(klee::KInstruction *target, 
			CVExecutionState *state, unsigned i);

	bool compute_truth(CVExecutionState* state, 
			klee::ref<klee::Expr>, bool &result);

	bool compute_false(CVExecutionState* state, 
			klee::ref<klee::Expr>, bool &result);

  void add_constraint(CVExecutionState *state, 
			klee::ref<klee::Expr> condition);

	//void register_event(const CliverEventInfo& event_info);
	//CliverEventInfo* lookup_event(llvm::Instruction *inst);
  
  void register_function_call_event(const char **fname,
                                    ExecutionEventType event_type);

	void add_state(CVExecutionState* state);

	void add_state_internal(CVExecutionState* state);

	void remove_state_internal(CVExecutionState* state);

  void remove_state_internal_without_notify(CVExecutionState* state);

	void rebuild_solvers();

	void update_memory_usage();

	size_t memory_usage() { return memory_usage_mbs_; }

	klee::KInstruction* get_instruction(unsigned id);

  void handle_pre_execution_events(klee::ExecutionState &state);

  void handle_post_execution_events(klee::ExecutionState &state);

  klee::KModule* get_kmodule() { return kmodule; }

  size_t states_size() { return states.size(); }

  std::string get_string_at_address(CVExecutionState* state, 
                                    klee::ref<klee::Expr> address_expr);

  void reset_replay_path(std::vector<bool> *replay_path=NULL);

  const std::vector<bool>* replay_path() { return replayPath; }

  unsigned replay_position() { return replayPosition; }

  void add_finished_state(CVExecutionState* state);

  std::set<ExecutionStateProperty*>& finished_states() { return finished_states_; }

  void ktest_copy(CVExecutionState* state, klee::KInstruction *target,
                  std::string &name, int ktest_index, klee::ObjectState* os,
                  unsigned os_offset, unsigned len);

 private:
  ClientVerifier *cv_;
	StateMerger *merger_;
	ConstraintPruner *pruner_;
	std::map<llvm::Function*, ExecutionEventType> function_call_events_;
  size_t memory_usage_mbs_;
  std::set<ExecutionStateProperty*> finished_states_;

  std::set<klee::ExecutionState*> states;
};

} // end cliver namespace

#endif // CLIVER_EXECUTOR_H
