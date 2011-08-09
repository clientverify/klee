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
	virtual ExecutionStateProperty* clone() { assert(0); return NULL; }
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
		NetworkClone, 
		EndState
	};
	TrainingProperty();
	TrainingProperty* clone() { return new TrainingProperty(*this); }
  void print(std::ostream &os) const;
	int compare(const ExecutionStateProperty &b) const;

	// Property values
	int training_round;
	TrainingState training_state;
	int start_instruction_id;
	int end_instruction_id;
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

	void get_pc_string(std::string &result);

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
