//===-- CVExecutionState.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTIONSTATE_H
#define CLIVER_EXECUTIONSTATE_H

#include "klee/ExecutionState.h"
#include "ClientVerifier.h"

#include <list>

namespace klee {
class KFunction;
class MemoryManager;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutionState;
class CVExecutor;
class NetworkManager;
class PathManager;

////////////////////////////////////////////////////////////////////////////////

class ExecutionStateProperty {
 public:
  virtual void print(std::ostream &os) const {};
	virtual int compare(const ExecutionStateProperty &p) const {}
	virtual ExecutionStateProperty* clone() { return new ExecutionStateProperty(*this); }
};

inline std::ostream &operator<<(std::ostream &os, 
		const ExecutionStateProperty &p) {
  p.print(os);
  return os;
}
 
struct ExecutionStatePropertyLT {
	bool operator()(const ExecutionStateProperty* a, 
			const ExecutionStateProperty* b) const;
};

////////////////////////////////////////////////////////////////////////////////

typedef std::set<CVExecutionState*> ExecutionStateSet;

typedef std::map<ExecutionStateProperty*,
								 ExecutionStateSet,
								 ExecutionStatePropertyLT> ExecutionStatePropertyMap;

struct ExecutionStatePropertyFactory {
	static ExecutionStateProperty* create();
};

////////////////////////////////////////////////////////////////////////////////

class LogIndexProperty : public ExecutionStateProperty {
 public: 
	LogIndexProperty();
	LogIndexProperty* clone() { return new LogIndexProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// event signal handlers
	static void handle_pre_event(CVExecutionState *state, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, CliverEvent::Type et);

	// Property values
	int socket_log_index;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingProperty : public ExecutionStateProperty {
 public: 
	enum TrainingState {
		PrepareExecute=0, 
		Execute, 
		Merge, 
		PrepareNetworkClone, 
		NetworkClone, 
		Record, 
		EndState
	};
	TrainingProperty();
	TrainingProperty* clone() { return new TrainingProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// event signal handlers
	static void handle_pre_event(CVExecutionState *state, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, CliverEvent::Type et);

	// Property values
	int training_round;
	TrainingState training_state;
	unsigned start_instruction_id;
	unsigned end_instruction_id;
};

////////////////////////////////////////////////////////////////////////////////

#define MAX_TRAINING_PHASE 7

class TrainingPhaseProperty : public ExecutionStateProperty {
 public: 
	TrainingPhaseProperty();
	TrainingPhaseProperty* clone() { return new TrainingPhaseProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// event signal handlers
	static void handle_pre_event(CVExecutionState *state, CliverEvent::Type et);
	static void handle_post_event(CVExecutionState *state, CliverEvent::Type et);

	// Property values
	int training_phase;
};


////////////////////////////////////////////////////////////////////////////////

class CVExecutionState : public klee::ExecutionState {
 public:
  CVExecutionState(klee::KFunction *kF, klee::MemoryManager *mem);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  virtual ~CVExecutionState();
  virtual CVExecutionState *branch();
  CVExecutionState *clone();

	int compare(const CVExecutionState& b) const;

  void initialize(CVExecutor* executor);
  int id() { return id_; }
  const CVContext* context() { return context_; }

	NetworkManager* network_manager() const { return network_manager_; }
	PathManager*    path_manager() { return path_manager_; }
	ExecutionStateProperty* property()			  { return property_; }

 private:
  int increment_id() { return next_id_++; }

  int id_;
  static int next_id_;
  CVContext* context_;
	NetworkManager* network_manager_;
	PathManager* path_manager_;
	ExecutionStateProperty* property_;
};

struct CVExecutionStateLT {
	bool operator()(const CVExecutionState* a, const CVExecutionState* b) const;
};

} // End cliver namespace

#endif
