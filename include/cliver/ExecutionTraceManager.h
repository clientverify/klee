//===-- ExecutionTraceManager.h ---------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_TRACE_MANAGER_H
#define CLIVER_EXECUTION_TRACE_MANAGER_H

#include "cliver/EditDistance.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/TrackingRadixTree.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/KExtensionTree.h"
#include "cliver/Training.h"

#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/Analysis/Trace.h"

#include <set>
#include <list>
#include <map>
#include <iostream>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

typedef TrackingRadixTree< ExecutionTrace, BasicBlockID, ExecutionStateProperty> 
    ExecutionTraceTree;

typedef EditDistanceTree<ExecutionTrace, BasicBlockID> 
    ExecutionTraceEditDistanceTree;

typedef boost::unordered_map<ExecutionStateProperty*,ExecutionTraceEditDistanceTree*>
    StatePropertyEditDistanceTreeMap;

////////////////////////////////////////////////////////////////////////////////

class ExecutionTraceManager : public ExecutionObserver {
 public:
  ExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
  virtual void process_all_states(std::vector<ExecutionStateProperty*> &states) {}
  virtual bool ready_process_all_states() { return false; }
 protected:
  std::vector< ExecutionTraceTree* > tree_list_;
  ClientVerifier *cv_;

};

class TrainingExecutionTraceManager : public ExecutionTraceManager {
 public:
  TrainingExecutionTraceManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 protected:
  void write_training_object(CVExecutionState* state);
};

class VerifyExecutionTraceManager : public ExecutionTraceManager {
 public:
  VerifyExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
  virtual void process_all_states(std::vector<ExecutionStateProperty*> &states);
  virtual bool ready_process_all_states();

 private:
  void clear_edit_distance_map();
  void recompute_property(ExecutionStateProperty *property);
  void update_edit_distance(ExecutionStateProperty *property);

  // XXX rename?
  TrainingObjectSet training_data_;
  TrainingObjectList current_training_list_;

  StatePropertyEditDistanceTreeMap edit_distance_map_;
  ExecutionTraceEditDistanceTree *root_tree_;

  SocketEventSimilarity *similarity_measure_;
  int current_k_;
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTraceManagerFactory {
 public:
  static ExecutionTraceManager* create(ClientVerifier *cv);
};

////////////////////////////////////////////////////////////////////////////////

class EditDistanceTreeFactory {
 public:
  static ExecutionTraceEditDistanceTree* create();
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_EXECUTION_TRACE_MANAGER_H

