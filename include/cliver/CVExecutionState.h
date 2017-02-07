//===-- CVExecutionState.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_STATE_H
#define CLIVER_EXECUTION_STATE_H

#include "klee/ExecutionState.h"
#include "cliver/CVAssignment.h"
#include "cliver/LazyConstraint.h"

#include <list>
#include <sstream>
#include <ostream>

namespace klee {
struct KFunction;
}

namespace cliver {
class AddressManager;
class CVContext;
class CVExecutionState;
class CVExecutor;
class ClientVerifier;
class ExecutionStateProperty;
class NetworkManager;
class SearcherStage;

////////////////////////////////////////////////////////////////////////////////

class CVExecutionState : public klee::ExecutionState {
  // unique id for each state (used for debugging)
  static klee::Atomic<unsigned>::type  next_id_;

 public:
  CVExecutionState(klee::KFunction *kF);
  CVExecutionState(const std::vector< klee::ref<klee::Expr> > &assumptions);
  ~CVExecutionState();
  virtual CVExecutionState *branch();
  virtual void addSymbolic(const klee::MemoryObject *mo,
                           const klee::Array *array);
  CVExecutionState *clone(ExecutionStateProperty* property = NULL);

	int compare(const CVExecutionState& b) const;

  void initialize(ClientVerifier *cv);
  unsigned id() const { return id_; }
  const CVContext* context() { return context_; }

	NetworkManager* network_manager() const { return network_manager_; }
	ExecutionStateProperty* property() { return property_; }
	void set_property(ExecutionStateProperty* property) { property_ = property; }

  void print(std::ostream &os) const;

  ClientVerifier* cv() { return cv_; }

  bool basic_block_tracking() { return basic_block_tracking_; }
  void set_basic_block_tracking(bool b) { basic_block_tracking_ = b; }

  bool event_flag() { return event_flag_; }
  void set_event_flag(bool b) { event_flag_ = b; }

  SearcherStage* searcher_stage() { return searcher_stage_; }
  void set_searcher_stage(SearcherStage* s) { searcher_stage_ = s; }

  static unsigned next_id() { return next_id_; }

  std::string get_unique_array_name(const std::string &s);

  void erase_self();
  void erase_self_permanent();

  unsigned get_current_basic_block();

  CVAssignment& multi_pass_assignment() { 
    return multi_pass_assignment_; 
  }

  void set_multi_pass_assignment(CVAssignment &cva) { 
    multi_pass_assignment_ = cva; 
  }

  void set_multi_pass_bindings_prev(const klee::Assignment::bindings_ty &b) {
    multi_pass_previous_round_bindings_ = b;
  }

  klee::Assignment::bindings_ty& multi_pass_bindings_prev() {
    return multi_pass_previous_round_bindings_;
  }

  LazyConstraintDispatcher &get_lazy_constraint_dispatcher() {
    return lazy_constraint_dispatcher_;
  }

  CVExecutionState* multi_pass_clone_;
 private:
  unsigned increment_id() { return next_id_++; }

  unsigned id_;
  bool event_flag_;
  CVContext* context_;
	NetworkManager* network_manager_;
	ExecutionStateProperty* property_;
  ClientVerifier *cv_;
  bool basic_block_tracking_;
  std::map<std::string, uint64_t> array_name_index_map_;
  CVAssignment multi_pass_assignment_;
  klee::Assignment::bindings_ty multi_pass_previous_round_bindings_;
  LazyConstraintDispatcher lazy_constraint_dispatcher_;
  SearcherStage *searcher_stage_;
};

////////////////////////////////////////////////////////////////////////////////

std::ostream &operator<<(std::ostream &os, const CVExecutionState &s);

////////////////////////////////////////////////////////////////////////////////

typedef std::set<CVExecutionState*> ExecutionStateSet;

struct CVExecutionStateLT {
	bool operator()(const CVExecutionState* a, const CVExecutionState* b) const;
};

////////////////////////////////////////////////////////////////////////////////

} // End cliver namespace


#endif // CLIVER_EXECUTION_STATE_H
