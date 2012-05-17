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

typedef LevenshteinRadixTree<ExecutionTrace, BasicBlockID> 
    EditDistanceExecutionTree;

//typedef KLevenshteinRadixTree<ExecutionTrace, BasicBlockID> 
typedef KExtensionTree<ExecutionTrace, BasicBlockID> 
    KEditDistanceExecutionTree;

typedef boost::unordered_map<ExecutionStateProperty*,EditDistanceExecutionTree*>
    EditDistanceExecutionTreeMap;

typedef boost::unordered_map<ExecutionStateProperty*,KEditDistanceExecutionTree*>
    KEditDistanceExecutionTreeMap;

////////////////////////////////////////////////////////////////////////////////

/// TODO: Rename this class to ExecutionTraceManager
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

class RoundRobinTrainingExecutionTraceManager : public ExecutionTraceManager {
 public:
  RoundRobinTrainingExecutionTraceManager(ClientVerifier *cv);
  void initialize();
  void notify(ExecutionEvent ev);
 protected:
};

class VerifyExecutionTraceManager : public ExecutionTraceManager {
 public:
  VerifyExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);

 private:
  void clear_edit_distance_map();

  TrainingObjectSet training_data_;
  EditDistanceExecutionTreeMap edit_distance_map_;
  EditDistanceExecutionTree *root_tree_;
  SocketEventSimilarity *similarity_measure_;
};

class KExtensionVerifyExecutionTraceManager : public ExecutionTraceManager {
 public:
  KExtensionVerifyExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
  virtual void process_all_states(std::vector<ExecutionStateProperty*> &states);
  virtual bool ready_process_all_states();

 private:
  void recompute_property(ExecutionStateProperty *property);
  void clear_edit_distance_map();

  TrainingObjectSet training_data_;
  TrainingObjectList current_training_list_;
  KEditDistanceExecutionTreeMap edit_distance_map_;
  KEditDistanceExecutionTree *root_tree_;
  SocketEventSimilarity *similarity_measure_;
  int current_k_;
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTraceManagerFactory {
 public:
  static ExecutionTraceManager* create(ClientVerifier *cv);
};


} // end namespace cliver

#endif // CLIVER_EXECUTION_TRACE_MANAGER_H

