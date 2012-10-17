//===-- ExecutionTraceManager.cpp -------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO: Method to merge ExecutionTrees
// TODO: Method to modify pre-existing ExecutionTree
//
// TODO: Combine KEditDistance and EditDistance trees
//===----------------------------------------------------------------------===//

#include "cliver/ExecutionTraceManager.h"

#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/EditDistance.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/NetworkManager.h"
#include "cliver/SocketEventMeasurement.h"
#include "cliver/Training.h"
#include "cliver/TrainingCluster.h"

#include "CVCommon.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/System/Process.h"
#include "llvm/Function.h"

#include <omp.h>

#include <fstream>
#include <algorithm>

namespace cliver {

llvm::cl::opt<unsigned>
MaxKExtension("max-k-extension",llvm::cl::init(16));

llvm::cl::opt<bool>
EditDistanceAtCloneOnly("edit-distance-at-clone-only",llvm::cl::init(true));

llvm::cl::opt<bool>
BasicBlockDisabling("basicblock-disabling",llvm::cl::init(false));

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

llvm::cl::opt<unsigned>
ClusterSize("cluster-size",llvm::cl::init(4));

llvm::cl::opt<bool>
UseClustering("use-clustering",llvm::cl::init(true));

llvm::cl::opt<bool>
UseClusteringHint("use-clustering-hint",llvm::cl::init(false));

llvm::cl::list<std::string> TrainingPathFile("training-path-file",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify a training path file (.tpath)"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> TrainingPathDir("training-path-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify directory containing .tpath files"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> SelfTrainingPathFile("self-training-path-file",
	llvm::cl::ZeroOrMore,
	llvm::cl::desc("Specify a training path file (.tpath) for the log we are verifying (debug)"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> SelfTrainingPathDir("self-training-path-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::desc("Specify directory containing .tpath files for the log we are verifying (debug)"),
	llvm::cl::value_desc("tpath directory"));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugExecutionTree, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugExecutionTree, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

// Helper for debug output
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	ros << ki.info->id << ":" << *ki.inst;
	//str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

ExecutionTraceManager::ExecutionTraceManager(ClientVerifier* cv) : cv_(cv) {}

void ExecutionTraceManager::initialize() {
  tree_list_.push_back(new ExecutionTraceTree() );
}

void ExecutionTraceManager::notify(ExecutionEvent ev) {
  if (cv_->executor()->replay_path())
    return;

  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  ExecutionStateProperty *property = NULL, *parent_property = NULL;
  if (state)
    property = state->property();
  if (parent) 
    parent_property = parent->property();

  switch (ev.event_type) {

    case CV_SELECT_EVENT: {
      CVDEBUG("SELECT_EVENT");
      property->is_recv_processing = false;
    }

    case CV_BASICBLOCK_ENTRY: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      ExecutionStage* stage = stages_[property];

      if (stage->etrace_tree) {
        if (state->basic_block_tracking() || !BasicBlockDisabling)
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
      }
    }
    break;

    case CV_STATE_REMOVED: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];
      if (stage->etrace_tree) {
        stage->etrace_tree->remove_tracker(property);
      }
      stages_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Cloned state: " << *state);
      ExecutionStage* stage = stages_[parent_property];
      stages_[property] = stage;
      if (stage->etrace_tree) {
        stage->etrace_tree->clone_tracker(property, parent_property);
      }
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      // Increment stat counter
      stats::stage_count += 1;

      if (!stages_.empty() && stages_.count(parent_property) && 
          stages_[parent_property]->etrace_tree->tracks(parent_property)) {

        CVDEBUG("End state: " << *parent);
        new_stage->parent_stage = stages_[parent_property];
      }

      stages_[property] = new_stage;

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        ExecutionStateProperty* finished_property = parent_property;
        assert(stages_.count(finished_property));

        cv_->print_all_stats();
        cv_->executor()->setHaltExecution(true);
      }
    }
    break;

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingExecutionTraceManager::TrainingExecutionTraceManager(ClientVerifier* cv) 
  : ExecutionTraceManager(cv) {}

void TrainingExecutionTraceManager::initialize() {}

// Write this state's path and associated socket event data to file
void TrainingExecutionTraceManager::write_training_object(
    ExecutionStage* stage, ExecutionStateProperty* property) {

  //assert(tree_list_.back()->tracks(property));
  assert(stage->etrace_tree->tracks(property));

  // Get path from the execution tree
  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  // Create training object and write to file
  TrainingObject training_obj(&etrace, stage->socket_event);
  training_obj.write(property, cv_);
}

void TrainingExecutionTraceManager::notify(ExecutionEvent ev) {

  // No Events if we are replaying a path that was expelled from cache
  if (cv_->executor()->replay_path())
    return;

  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  ExecutionStateProperty *property = NULL, *parent_property = NULL;
  if (state)
    property = state->property();
  if (parent) 
    parent_property = parent->property();

  switch (ev.event_type) {

    case CV_SELECT_EVENT: {
      CVDEBUG("SELECT_EVENT");
      property->is_recv_processing = false;
    }

    case CV_BASICBLOCK_ENTRY: {
      assert(stages_.count(property));

      if (!property->is_recv_processing) {
        ExecutionStage* stage = stages_[property];

        if (state->basic_block_tracking() || !BasicBlockDisabling)
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
      }
    }
    break;

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];
      if (stage->etrace_tree->tracks(property))
        stage->etrace_tree->remove_tracker(property);
      stages_.erase(property);
 
    }
    break;

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
      ExecutionStage* stage = stages_[parent_property];
      stages_[property] = stage;
      stage->etrace_tree->clone_tracker(property, parent_property);
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      // Initialize a new ExecutionTraceTree

      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      // Increment stat counter
      stats::stage_count += 1;

      if (!stages_.empty() && 
          stages_.count(parent_property)) {
          //stages_[parent_property]->etrace_tree->tracks(parent_property)) {

        CVDEBUG("NEW STAGE at state: " << *parent);

        new_stage->parent_stage = stages_[parent_property];

        // Set the SocketEvent of the previous stage
        Socket* socket = parent->network_manager()->socket();
        assert(socket);
        stages_[parent_property]->socket_event = 
            const_cast<SocketEvent*>(&socket->previous_event());

        //clear_execution_stage(property);
      }

      stages_[property] = new_stage;

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        ExecutionStateProperty* finished_property = parent_property;
        assert(stages_.count(finished_property));

        std::vector<ExecutionStage*> complete_stages;

        ExecutionStage* tmp_stage = stages_[finished_property];
        while (tmp_stage != NULL) {
          complete_stages.push_back(tmp_stage);
          tmp_stage = tmp_stage->parent_stage;
        }

        ExecutionStateProperty* tmp_property = finished_property;
        foreach(ExecutionStage* stage, complete_stages) {
          if (stage->etrace_tree->tracks(tmp_property)) {
            write_training_object(stage, tmp_property);
            tmp_property = stage->root_property;
          }
        }

        // XXX Only output one set of paths for now
        cv_->print_all_stats();
        cv_->executor()->setHaltExecution(true);
      }
    }
    break;

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

VerifyExecutionTraceManager::VerifyExecutionTraceManager(ClientVerifier* cv) 
  : ExecutionTraceManager(cv) {}

void VerifyExecutionTraceManager::initialize() {
  // Create similarity measure
  similarity_measure_ = SocketEventSimilarityFactory::create();

  // Parse the training data filenames
  if (!TrainingPathDir.empty())
    foreach (std::string path, TrainingPathDir)
      cv_->getFilesRecursive(path, ".tpath", TrainingPathFile);

  // Report error if no filenames found
  if (TrainingPathFile.empty())
    cv_error("Error parsing training data file names, exiting now.");

  CVMESSAGE("Loading " << TrainingPathFile.size() << " training data files.");

  // Read training data into memory
  TrainingManager::read_files(TrainingPathFile, training_data_); 
  if (training_data_.empty())
    cv_error("Error reading training data , exiting now.");

  CVMESSAGE("Finished loading " 
            << training_data_.size() << " unique training objects.");

  // ------------------------------------------------------------------------//

  // Parse the self training data filenames 
  if (!SelfTrainingPathDir.empty())
    foreach (std::string path, SelfTrainingPathDir)
      cv_->getFilesRecursive(path, ".tpath", SelfTrainingPathFile);

  // Check if we are reading self-training data
  if (SelfTrainingPathFile.size() > 0) {
    CVMESSAGE("Loading " << SelfTrainingPathFile.size() 
              << " self training data files for debugging.");

    // Read self training data into memory
    TrainingManager::read_files(SelfTrainingPathFile, self_training_data_); 
    if (self_training_data_.empty())
      cv_error("Error reading self, training data , exiting now.");

    CVMESSAGE("Finished loading " 
              << self_training_data_.size() << " unique self training objects.");
  }

  // ------------------------------------------------------------------------//

  initialize_training_data();
}

void VerifyExecutionTraceManager::initialize_training_data() {
  cluster_manager_ = new TrainingObjectManager();
  std::vector<TrainingObject*> tobj_vec(training_data_.begin(), training_data_.end());
  cluster_manager_->cluster(ClusterSize, tobj_vec);
}

void VerifyExecutionTraceManager::update_edit_distance(
    ExecutionStateProperty* property) {

  ExecutionStage* stage = stages_[property];
  assert(stage);

  if (property->edit_distance == INT_MAX) {
    return;
  }

  if (stage->ed_tree_map.count(property) == 0) {
    if (stage->root_ed_tree != NULL) {
      stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
    } else {
      return;
    }
  }

  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  CVDEBUG("Updated edit distance: " << property << ": " << *property << " etrace.size = " 
          << etrace.size() << ", row = " << stage->ed_tree_map[property]->row());

  stage->ed_tree_map[property]->update(etrace);

  //// XXX FIX ME -- make this operation O(1)
  //int trace_depth = stage->etrace_tree->tracker_depth(property);
  //
  //int row = stage->ed_tree_map[property]->row();

  //if (trace_depth - row == 1) {
  //  stage->ed_tree_map[property]->update_element(
  //      stage->etrace_tree->leaf_element(property));
  //} else {
  //  ExecutionTrace etrace;
  //  etrace.reserve(trace_depth);
  //  stage->etrace_tree->tracker_get(property, etrace);
  //  stage->ed_tree_map[property]->update(etrace);
  //}
  
  property->edit_distance = stage->ed_tree_map[property]->min_distance();
  CVDEBUG("Updated edit distance: " << property << ": " << *property << " " << etrace.size());
            
}

void VerifyExecutionTraceManager::create_ed_tree(CVExecutionState* state) {

  ExecutionStateProperty *property = state->property();
  ExecutionStage* stage = stages_[property];

  const SocketEvent* socket_event 
    = &(state->network_manager()->socket()->event());

  if (UseClusteringHint) {

    TrainingFilter tf(state);

    if (cluster_manager_->check_filter(tf)) {

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      // Find matching execution path
      TrainingObject* tobj = NULL;
      TrainingObject* matching_tobj = NULL;
      int match_count = 0;

      foreach (tobj, self_training_data_) {
        SocketEventSet se_set(tobj->socket_event_set.begin(),
                              tobj->socket_event_set.end());
        if (se_set.count(const_cast<SocketEvent*>(socket_event))) {
          match_count++;
          matching_tobj = tobj;
        }
      }

      //{
      //  TrainingObjectScoreList sorted_clusters;
      //  cluster_manager_->sorted_clusters(socket_event, tf,
      //                                    sorted_clusters, *similarity_measure_);

      //  std::stringstream ss;
      //  for (size_t i=0; i < sorted_clusters.size(); ++i) {
      //    if (match_count == 0) {
      //      stage->root_ed_tree->add_data(sorted_clusters[i].second->trace);
      //    }
      //    ss << sorted_clusters[i].first << ",";
      //  }
      //  CVMESSAGE("Original Cluster Distances: " << ss.str());
      //}

      assert(match_count != 0);

      if (match_count >= 1) {
        TrainingObjectScoreList sorted_clusters;

        cluster_manager_->all_clusters_distance(matching_tobj,
                                                sorted_clusters);

        std::stringstream ss;
        for (size_t i=0; i < sorted_clusters.size(); ++i) {
          ss << sorted_clusters[i].first << ",";
        }
        CVMESSAGE("All Cluster Distances: " << ss.str());

        // Add closest cluster
        stage->root_ed_tree->add_data(sorted_clusters[0].second->trace);
        stats::edit_distance_closest_medoid = sorted_clusters[0].first;
      }
    } else {
      stage->root_ed_tree = NULL;
      return;
    }
  } else {

    TrainingFilter tf(state);

    if (cluster_manager_->check_filter(tf)) {

      TrainingObjectScoreList sorted_clusters;
      cluster_manager_->sorted_clusters(socket_event, tf,
                                        sorted_clusters, *similarity_measure_);

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      //std::stringstream ss;
      size_t i = 0;
      stats::edit_distance_closest_medoid = sorted_clusters[0].first;
      do {
        stage->root_ed_tree->add_data(sorted_clusters[i].second->trace);
        stats::edit_distance_medoid_count += 1;
        //ss << sorted_clusters[i].first << ",";
        i++;
      } while (i < 1 && sorted_clusters[i].first <= (sorted_clusters[0].first * 2));
      //CVMESSAGE("Cluster SocketEvent Distances (" << i << ") " << ss.str());

    } else {
      stage->root_ed_tree = NULL;
      return;
    }
  }

  stage->root_ed_tree->init(stage->current_k);

  // Set initial values for edit distance
  property->edit_distance = INT_MAX-1;
  property->recompute = true;

  stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
}

void VerifyExecutionTraceManager::notify(ExecutionEvent ev) {
  if (cv_->executor()->replay_path())
    return;

  switch (ev.event_type) {
    case CV_SELECT_EVENT:
    case CV_BASICBLOCK_ENTRY:
    case CV_STATE_REMOVED:
    case CV_STATE_CLONE:
    case CV_SEARCHER_NEW_STAGE:
    case CV_CLEAR_CACHES:
      break;
    default:
      return;
  }

  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  ExecutionStateProperty *property = NULL, *parent_property = NULL;
  if (state)
    property = state->property();
  if (parent) 
    parent_property = parent->property();

  // Check if network is ready
  bool is_socket_active = false;

  if (state && state->network_manager() && 
      state->network_manager()->socket() &&
      state->network_manager()->socket()->end_of_log()) {
    is_socket_active = true;
  }

  switch (ev.event_type) {

    case CV_SELECT_EVENT: {
      CVDEBUG("SELECT_EVENT");
      property->is_recv_processing = false;
    }

    case CV_BASICBLOCK_ENTRY: {

      ExecutionStage* stage = stages_[property];

      if (is_socket_active && !property->is_recv_processing) {

        // Check if this is the first basic block of the stage
        if (!stage->etrace_tree->tracks(property)) {
          assert(stage->etrace_tree->element_count() == 0);
          CVDEBUG("First basic block entry (stage)");
          
          // Build the edit distance tree using training data
          klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);
          create_ed_tree(state);
        }

        // Check if we need to reclone the edit distance tree 
        if (stage->ed_tree_map.count(property) == 0 && stage->root_ed_tree) {
          stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
        }
      }

      if (!property->is_recv_processing && 
          (state->basic_block_tracking() || !BasicBlockDisabling)) {

        {
          klee::TimerStatIncrementer timer(stats::execution_tree_time);
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
        }

        if (is_socket_active && property->recompute) {
          if (EditDistanceAtCloneOnly)
            property->recompute = false;

          klee::TimerStatIncrementer timer(stats::edit_distance_time);
          update_edit_distance(property);
        }
      }
    }
    break;

    case CV_STATE_REMOVED: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];

      if (stage->etrace_tree->tracks(property))
        stage->etrace_tree->remove_tracker(property);

      if (is_socket_active) {
        if (stage->ed_tree_map.count(property)) {
          delete stage->ed_tree_map[property];
          stage->ed_tree_map.erase(property);
        }
      }

      stages_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      ExecutionStage* stage = stages_[parent_property];

      if (!property->is_recv_processing && EditDistanceAtCloneOnly) {
        klee::TimerStatIncrementer timer(stats::edit_distance_time);
        update_edit_distance(parent_property);
      }

      stages_[property] = stage;

      stage->etrace_tree->clone_tracker(property, parent_property);
      
      property->recompute=true;
      parent_property->recompute=true;

      property->edit_distance = parent_property->edit_distance;

      //if (is_socket_active && !property->is_recv_processing) {
      if (stage->ed_tree_map.count(parent_property) && stage->root_ed_tree) {
        stage->ed_tree_map[property] = 
            stage->ed_tree_map[parent_property]->clone_edit_distance_tree();

        //stage->ed_tree_map[property]->init(stage->current_k);
        CVDEBUG("Cloned EDTree "
                << " clone: " << property 
                << " parent: " << parent_property 
                << ", clone row = " << stage->ed_tree_map[property]->row()
                << ", parent row = " << stage->ed_tree_map[parent_property]->row());

        property->edit_distance = stage->ed_tree_map[property]->min_distance();
      }

      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      CVDEBUG("New Stage: " << property << ": " << *property);
      CVDEBUG("New Stage (parent): " << parent_property << ": " << *parent_property);

      // Increment stat counter
      stats::stage_count += 1;

      // Set edit distance stat
      if (stages_.count(parent_property) &&
          stages_[parent_property]->ed_tree_map.count(parent_property)) 
        stats::edit_distance =
          stages_[parent_property]->ed_tree_map[parent_property]->min_distance();

      // Set kprefix stat
      if (stages_.count(parent_property))
        stats::edit_distance_k = stages_[parent_property]->current_k;

      // Initialize a new ExecutionTraceTree
      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      if (!stages_.empty() && 
          stages_.count(property) && 
          stages_[property]->etrace_tree->tracks(property)) {

        //update_edit_distance(property);
        CVDEBUG("End state: " << *state);

        new_stage->parent_stage = stages_[parent_property];

      }

      stages_[property] = new_stage;

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        cv_->print_all_stats();
        cv_->executor()->setHaltExecution(true);
      }
    }
    break;

    case CV_CLEAR_CACHES: {
      clear_caches();
      break;
    }

    default:
    break;
  }
}

void VerifyExecutionTraceManager::clear_caches() {
  CVMESSAGE("ExecutionTraceManager::clear_caches() starting");
  StatePropertyStageMap::iterator stage_it = stages_.begin(), 
      stage_ie = stages_.end();

  for (;stage_it != stage_ie; ++stage_it) {
    ExecutionStage* stage = stage_it->second;

    if (stage->ed_tree_map.size()) {
      CVMESSAGE("Clearing EditDistanceTreeMap in ExecutionTraceStage of size: " 
                << stage->ed_tree_map.size());
      StatePropertyEditDistanceTreeMap::iterator it = stage->ed_tree_map.begin();
      StatePropertyEditDistanceTreeMap::iterator ie = stage->ed_tree_map.end();
      for (; it!=ie; ++it) {
        delete it->second;
      }
      stage->ed_tree_map.clear();
    }

    // We don't clear the root tree.
  }
  CVMESSAGE("ExecutionTraceManager::clear_caches() finished");
}

bool VerifyExecutionTraceManager::ready_process_all_states(
    ExecutionStateProperty* property) {

  assert(stages_.count(property));
  ExecutionStage* stage = stages_[property];

  return stage->root_ed_tree != NULL && (stage->current_k < MaxKExtension);
}

void VerifyExecutionTraceManager::recompute_property(
    ExecutionStateProperty *property) {

  assert(stages_.count(property));

  ExecutionStage* stage = stages_[property];

  if (stage->ed_tree_map.count(property) == 0)
    stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();

  stage->ed_tree_map[property]->init(stage->current_k);

  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  CVDEBUG("RC edit distance: " << property << ": " << *property << " etrace.size = " 
          << etrace.size() << ", row = " << stage->ed_tree_map[property]->row());

  stage->ed_tree_map[property]->update(etrace);
  property->edit_distance = stage->ed_tree_map[property]->min_distance();
}

void VerifyExecutionTraceManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {
  klee::TimerStatIncrementer edct(stats::edit_distance_time);

  assert(!states.empty());
  assert(stages_.count(states[0]));
  ExecutionStage* stage = stages_[states[0]];
  stage->current_k = stage->current_k * 2;
  stage->root_ed_tree->init(stage->current_k);

  for (unsigned i=0; i<states.size(); ++i) {
    if (states[i]->is_recv_processing) {
      CVMESSAGE("Not recomputing recv_processing state!");
    } else {
      int old_ed = states[i]->edit_distance;
      recompute_property(states[i]);
      CVDEBUG("K: " << stage->current_k 
              << " ed computed from: " << old_ed 
              << " to " << states[i]->edit_distance 
              << ", " << states[i] << " " << *(states[i]));
    }
  }

  CVMESSAGE("Recomputed kprefix edit distance trees with k=" 
            << stage->current_k << " in "
            << edct.check() / 1000000. << " secs");
           
}

// Delete the trees associated with each state in the edit distance map
// and clear the map itself
void VerifyExecutionTraceManager::clear_execution_stage(
    ExecutionStateProperty *property) {

  assert(stages_.count(property));

  ExecutionStage* stage = stages_[property];
  
  StatePropertyEditDistanceTreeMap::iterator it = stage->ed_tree_map.begin();
  StatePropertyEditDistanceTreeMap::iterator ie = stage->ed_tree_map.end();
  for (; it!=ie; ++it) {
    delete it->second;
  }
  stage->ed_tree_map.clear();

  if (stage->root_ed_tree != NULL) {
    stage->root_ed_tree->delete_shared_data();
    delete stage->root_ed_tree;
    stage->root_ed_tree = NULL;
  }
}

} // end namespace cliver
