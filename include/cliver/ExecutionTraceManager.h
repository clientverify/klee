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
#include "cliver/ExecutionTraceTree.h"
#include "cliver/HMMPathPredictor.h"
#include "cliver/LevenshteinRadixTree.h"
#include "cliver/KExtensionTree.h"
#include "cliver/Training.h"
#include "cliver/TrainingCluster.h"

#include "klee/Solver.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/util/Mutex.h"

#include "llvm/Analysis/Trace.h"

#include "folly/ThreadLocal.h"

#include <set>
#include <list>
#include <map>
#include <iostream>
#include <vector>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

typedef boost::unordered_map<ExecutionStateProperty*, ExecutionTraceEditDistanceTree*>
    StatePropertyEditDistanceTreeMap;

struct ExecutionStage;
typedef boost::unordered_map<ExecutionStateProperty*, ExecutionStage*> 
    StatePropertyStageMap;

typedef std::pair<ExecutionStateProperty*,ExecutionTraceTree::Node*> RemovedTracker;
typedef std::list< RemovedTracker > RemovedTrackerList;
typedef boost::unordered_map<ExecutionStage*, RemovedTrackerList >
    StageRemovedTrackerListMap;


////////////////////////////////////////////////////////////////////////////////

//typedef KMedoidsClusterer<SocketEvent, 
//    SocketEventDistanceMetric> SocketEventClusterer;
//typedef KMedoidsClusterer<TrainingObject, 
//    TrainingObjectDistanceMetric> TrainingObjectClusterer;

typedef KMeansClusterer<SocketEvent, 
    SocketEventDistanceMetric> SocketEventClusterer;
typedef KMeansClusterer<TrainingObject, 
    TrainingObjectDistanceMetric> TrainingObjectClusterer;

typedef TrainingObjectClusterManager<TrainingObjectClusterer,
    SocketEventClusterer> TrainingObjectManager;

////////////////////////////////////////////////////////////////////////////////

struct ExecutionStage {
  ExecutionStage() 
      : root_property(NULL), 
        parent_stage(NULL),
        etrace_tree(NULL), 
        socket_event(NULL),
        root_ed_tree(NULL),
        current_k(2) {}

  ExecutionStateProperty*          root_property;
  ExecutionStage*                  parent_stage;
  ExecutionTraceTree*              etrace_tree;

  // Used for training
  SocketEvent*            socket_event;              

  // Used for edit distance verification
  ExecutionTraceEditDistanceTree*  root_ed_tree;
  int                              current_k;
  StatePropertyEditDistanceTreeMap ed_tree_map;

};

////////////////////////////////////////////////////////////////////////////////

class ExecutionTraceManager : public ExecutionObserver {
 public:
  ExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize() {}
  virtual void notify(ExecutionEvent ev);
  virtual void process_all_states(std::vector<ExecutionStateProperty*> &states) {}
  virtual bool ready_process_all_states(ExecutionStateProperty* property) { return false; }

  virtual void set_stage(ExecutionStateProperty* p, ExecutionStage *s);
  virtual ExecutionStage* get_stage(ExecutionStateProperty* p);
  virtual void remove_from_stage(ExecutionStateProperty* p);

  virtual bool remove_property(ExecutionStateProperty* p);
  virtual void lazy_property_removals();

 protected:
  ClientVerifier *cv_;
  //StatePropertyStageMap stages_private_;
  StatePropertyStageMap stages_;
  klee::Mutex lock_;
  klee::Mutex ed_tree_lock_;

  class ETMTag; // segments global folly::ThreadLocal mutex
  folly::ThreadLocal< StageRemovedTrackerListMap, ETMTag > removed_trackers_;
};

class TrainingExecutionTraceManager : public ExecutionTraceManager {
 public:
  TrainingExecutionTraceManager(ClientVerifier *cv);
  void notify(ExecutionEvent ev);

 protected:
  void write_training_object(ExecutionStage* stage,
                             ExecutionStateProperty* property);
};

class VerifyExecutionTraceManager : public ExecutionTraceManager {
 public:
  VerifyExecutionTraceManager(ClientVerifier *cv);
  virtual void initialize();
  virtual void notify(ExecutionEvent ev);
  virtual void process_all_states(std::vector<ExecutionStateProperty*> &states);
  virtual bool ready_process_all_states(ExecutionStateProperty* property);

 private:
  void initialize_training_data();
  void clear_caches();
  void clear_execution_stage(ExecutionStateProperty *property);
  void recompute_property(ExecutionStateProperty *property);
  void update_edit_distance(ExecutionStateProperty *property, CVExecutionState *state);

  void create_ed_tree(CVExecutionState* state);

  void compute_self_training_stats(CVExecutionState* state,
                                   std::vector<TrainingObject*> &selected);

  ExecutionTraceEditDistanceTree* get_ed_tree(
      ExecutionStage* stage,
      ExecutionStateProperty *property);

  void delete_ed_tree(
      ExecutionStage* stage,
      ExecutionStateProperty *property);

  void set_ed_tree(
    ExecutionStage* stage,
    ExecutionStateProperty* property, 
    ExecutionTraceEditDistanceTree* ed_tree);

  ExecutionTraceEditDistanceTree* clone_ed_tree(ExecutionStateProperty *property);

  TrainingObjectSet training_data_;
  SocketEventSimilarity *similarity_measure_;

  // Training Filter
  TrainingFilterMap filter_map_;
  TrainingObjectManager *cluster_manager_;

  // Training objects that were found for the current verification task
  std::set<TrainingObject*> self_training_data_;

  // Map of round index to training object
  std::map<int, TrainingObject*> self_training_data_map_;

	unsigned last_round_cleared_;

  // Hidden Markov Model Path predictor (enabled via UseHMM)
  HMMPathPredictor* hmm_;

  // HMM training objects
  std::vector<std::shared_ptr<TrainingObject> > hmm_training_objs_;
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

