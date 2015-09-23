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
#include "klee/Internal/Support/Timer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Process.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#else
#include "llvm/Function.h"
#endif

#include <fstream>
#include <algorithm>

namespace cliver {

llvm::cl::opt<unsigned>
MaxKExtension("max-k-extension",llvm::cl::init(2));

llvm::cl::opt<unsigned>
MaxMedoids("max-medoids",llvm::cl::init(8));

llvm::cl::opt<double>
MedoidSelectRate("medoid-select-rate",llvm::cl::init(1.25));

llvm::cl::opt<bool>
EditDistanceAtCloneOnly("edit-distance-at-clone-only",llvm::cl::init(true));

llvm::cl::opt<bool>
BasicBlockDisabling("basicblock-disabling",llvm::cl::init(false));

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

llvm::cl::opt<bool>
FinalDistance("final-distance",llvm::cl::init(false));

llvm::cl::opt<unsigned>
ClusterSize("cluster-size",llvm::cl::init(256));

llvm::cl::opt<unsigned>
SocketEventClusterSize("socket-event-cluster-size",llvm::cl::init(10));

llvm::cl::opt<bool>
UseClustering("use-clustering",llvm::cl::init(false));

llvm::cl::opt<bool>
UseClusteringAll("use-clustering-all",llvm::cl::init(false));

llvm::cl::opt<bool>
UseClusteringHint("use-clustering-hint",llvm::cl::init(false));

llvm::cl::opt<bool>
UseSelfTraining("use-self-training",llvm::cl::init(false));

llvm::cl::opt<bool>
CheckSelfTraining("check-self-training",llvm::cl::init(false));

llvm::cl::opt<bool>
UseExternalStage("external-stage",llvm::cl::init(false));

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

llvm::cl::opt<bool>
UseHMM("use-hmm",llvm::cl::init(false));

llvm::cl::opt<std::string>
HMMTrainingFile("hmm-training-file",
                llvm::cl::desc("Specify a HMM training file)"),
                llvm::cl::init(""));

llvm::cl::opt<double>
HMMConfidence("hmm-confidence",
              llvm::cl::desc("Specify a HMM path prediction confidence level (default=0.9))"),
              llvm::cl::init(0.9));


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

ExecutionTraceManager::ExecutionTraceManager(ClientVerifier* cv) : cv_(cv) {}

ExecutionStage* ExecutionTraceManager::get_stage(ExecutionStateProperty* p) {
  if (UseExternalStage) {
    if (p) return p->execution_stage;
  } else {
    klee::LockGuard guard(lock_);
    if (p) return stages_[p];
  }
  return NULL;
}

void ExecutionTraceManager::set_stage(ExecutionStateProperty* p,
                                      ExecutionStage* s) {
  if (UseExternalStage) {
    if (p && s) p->execution_stage = s;
  } else {
    klee::LockGuard guard(lock_);
    if (p && s) stages_[p] = s;
  }
}

void ExecutionTraceManager::remove_from_stage(ExecutionStateProperty* p) {
  if (UseExternalStage) {
    if (p) p->execution_stage = NULL;
  } else {
    klee::LockGuard guard(lock_);
    if (p) stages_.erase(p);
  }
}

bool ExecutionTraceManager::remove_property(ExecutionStateProperty* p) {

  ExecutionStage* stage = get_stage(p);
  if (stage && stage->etrace_tree && stage->etrace_tree->tracks(p)) {
    stage->etrace_tree->remove_tracker_lazy(p);
    (*removed_trackers_)[stage].push_back(
        std::make_pair(p, p->tracker_node));
    remove_from_stage(p);
    return true;
  }
  return false;
}

void ExecutionTraceManager::lazy_property_removals() {
  klee::WallTimer cleanup_timer;
  int removed_count = 0;

  // Clean up ExecutionTree
  for (auto& removed_map: removed_trackers_.accessAllThreads()) {
    for (auto& removed_list : removed_map) {
      for (auto& e : removed_list.second) {
        removed_list.first->etrace_tree->remove_tracker(e.first, e.second);
        removed_count++;
      }
      removed_list.second.clear();
    }
  }
  CVDEBUG("Removed " << removed_count << " tracks from tree in "
          << cleanup_timer.check() / 1000000. << " secs");
}

void ExecutionTraceManager::notify(ExecutionEvent ev) {
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

  // Check if network is ready
  bool is_socket_active = false;

  if (state && state->network_manager() && 
      state->network_manager()->socket() &&
      state->network_manager()->socket()->end_of_log()) {
    is_socket_active = true;
  }

  switch (ev.event_type) {

    case CV_SELECT_EVENT: {
      CVDEBUG("SELECT EVENT: " << *state);
      property->is_recv_processing = false;
    }
    break;

    case CV_BASICBLOCK_ENTRY: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      ExecutionStage* stage = get_stage(property);

      if (!property->is_recv_processing) {
        if (stage->etrace_tree) {
          if (state->basic_block_tracking() || !BasicBlockDisabling) {
            stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
          }
        }
      }
    }
    break;

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << *state );
      remove_property(property);
      //klee::LockGuard guard(lock_);
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);
      //CVDEBUG("Removing state: " << *state );
      //ExecutionStage* stage = stages_[property];
      //if (stage->etrace_tree && stage->etrace_tree->tracks(property)) {
      //  stage->etrace_tree->remove_tracker(property);
      //}
      //stages_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Cloned state: " << *state);

      ExecutionStage* stage = get_stage(parent_property);
      set_stage(property, stage);

      if (stage->etrace_tree) {
        stage->etrace_tree->clone_tracker(property, parent_property);
      }
    }
    break;
    case CV_CLEAR_CACHES: {
      lazy_property_removals();
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      // Increment stat counter
      stats::stage_count += 1;

      // Network data statistics
      if (is_socket_active) {
        auto socket = state->network_manager()->socket();
        if (socket->index() > 0) {
          auto prev_se = socket->previous_event();
          stats::socket_event_size      = prev_se.length;
          stats::socket_event_timestamp = prev_se.timestamp;
          stats::socket_event_type      = prev_se.type;
        }
        CVDEBUG("Next Socket Event: " << socket->event());
      }

      // Symbolic variables
      stats::symbolic_variable_count += parent_property->symbolic_vars;

      //if (parent_stage &&
      //    parent_stage->etrace_tree->tracks(parent_property)) {

      ExecutionStage* parent_stage = get_stage(parent_property);
      if (parent_stage != NULL) {
        CVDEBUG("End state: " << *parent);
        new_stage->parent_stage = parent_stage;

        // Set the SocketEvent of the previous stage for our records
        Socket* socket = parent->network_manager()->socket();
        assert(socket);
        new_stage->parent_stage->socket_event = 
            const_cast<SocketEvent*>(&socket->previous_event());
      }

      set_stage(property, new_stage);

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        assert(get_stage(parent_property) != NULL);
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

// Write this state's path and associated socket event data to file
void TrainingExecutionTraceManager::write_training_object(
    ExecutionStage* stage, ExecutionStateProperty* property) {

  assert(stage->etrace_tree->tracks(property));

  // Get path from the execution tree
  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  // Create training object and write to file
  TrainingObject training_obj(&etrace, stage->socket_event);

  TrainingFilter tf(&training_obj);
  CVMESSAGE("Writing training object: (TF)" << tf << ", (property)" << *property);
  training_obj.write(property, cv_);
}

void TrainingExecutionTraceManager::notify(ExecutionEvent ev) {
  // Call base ExecutionTraceManager
  ExecutionTraceManager::notify(ev);

  switch (ev.event_type) {
    case CV_SEARCHER_NEW_STAGE: {
      ExecutionStateProperty *parent_property 
          = (ev.parent) ? ev.parent->property() : NULL;

      if (parent_property &&
          cv_->executor()->finished_states().count(parent_property)) {
        assert(get_stage(parent_property) != NULL);

        std::vector<ExecutionStage*> complete_stages;

        ExecutionStage* tmp_stage = get_stage(parent_property);
        while (tmp_stage != NULL) {
          complete_stages.push_back(tmp_stage);
          tmp_stage = tmp_stage->parent_stage;
        }

        ExecutionStateProperty* tmp_property = parent_property;
        foreach(ExecutionStage* stage, complete_stages) {
          if (!stage->etrace_tree->tracks(tmp_property)) {
            CVMESSAGE("Root property not tracked, can't write trace! "
                      << *(stage->root_property));
          } else {
						write_training_object(stage, tmp_property);
					}
          tmp_property = stage->root_property;
        }
      }
    }
    break;

    default:
      break;
  }
}

//void TrainingExecutionTraceManager::notify(ExecutionEvent ev) {
//  //klee::LockGuard guard(lock_);
//
//  // No Events if we are replaying a path that was expelled from cache
//  if (cv_->executor()->replay_path())
//    return;
//
//  CVExecutionState* state = ev.state;
//  CVExecutionState* parent = ev.parent;
//
//  ExecutionStateProperty *property = NULL, *parent_property = NULL;
//  if (state)
//    property = state->property();
//  if (parent) 
//    parent_property = parent->property();
//
//  // Check if network is ready
//  bool is_socket_active = false;
//
//  if (state && state->network_manager() && 
//      state->network_manager()->socket() &&
//      state->network_manager()->socket()->end_of_log()) {
//    is_socket_active = true;
//  }
//
//  switch (ev.event_type) {
//
//    case CV_SELECT_EVENT: {
//      CVDEBUG("SELECT EVENT: " << *state);
//      property->is_recv_processing = false;
//    }
//    break;
//
//    case CV_BASICBLOCK_ENTRY: {
//      //assert(stages_.count(property));
//      assert(NULL != get_stage(property));
//
//      if (!property->is_recv_processing) {
//        //ExecutionStage* stage = stages_[property];
//        ExecutionStage* stage = get_stage(property);
//
//        if (state->basic_block_tracking() || !BasicBlockDisabling)
//          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
//      }
//    }
//    break;
//
//    case CV_STATE_REMOVED: {
//      CVDEBUG("Removing state: " << *state );
//      remove_property(property);
//      //CVDEBUG("Removing state: " << *state );
//      //ExecutionStage* stage = stages_[property];
//      //if (stage->etrace_tree->tracks(property))
//      //  stage->etrace_tree->remove_tracker(property);
//      //stages_.erase(property);
//    }
//    break;
//
//    case CV_STATE_CLONE: {
//      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
//      //ExecutionStage* stage = stages_[parent_property];
//      //stages_[property] = stage;
//      ExecutionStage* stage = get_stage(parent_property);
//      assert(stage && stage->etrace_tree);
//      set_stage(property, stage);
//      stage->etrace_tree->clone_tracker(property, parent_property);
//    }
//    break;
//
//    case CV_CLEAR_CACHES: {
//      lazy_property_removals();
//    }
//    break;
//
//    case CV_SEARCHER_NEW_STAGE: {
//      // Initialize a new ExecutionTraceTree
//
//      ExecutionStage *new_stage = new ExecutionStage();
//      new_stage->etrace_tree = new ExecutionTraceTree();
//      new_stage->root_property = parent_property;
//
//      // Increment stat counter
//      stats::stage_count += 1;
//
//      if (is_socket_active) {
//        CVDEBUG("Next Socket Event: " << state->network_manager()->socket()->event());
//      }
//
//      ExecutionStage* parent_stage = get_stage(parent_property);
//      if (parent_stage &&
//          parent_stage->etrace_tree->tracks(parent_property)) {
//      //if (!stages_.empty() && 
//      //    stages_.count(parent_property)) {
//
//        CVDEBUG("New Stage: " << property << ": " << *property);
//        CVDEBUG("New Stage (parent): " << parent_property << ": " << *parent_property);
//
//        //new_stage->parent_stage = stages_[parent_property];
//        new_stage->parent_stage = parent_stage;
//        assert(new_stage->parent_stage != NULL);
//
//        // Set the SocketEvent of the previous stage 
//        // FIXME do we still need to do this?
//        Socket* socket = parent->network_manager()->socket();
//        assert(socket);
//        //stages_[parent_property]->socket_event = 
//        new_stage->parent_stage->socket_event = 
//            const_cast<SocketEvent*>(&socket->previous_event());
//      }
//
//      //stages_[property] = new_stage;
//      set_stage(property, new_stage);
//
//      if (cv_->executor()->finished_states().count(parent_property)) {
//        CVMESSAGE("Verification complete");
//        //ExecutionStateProperty* finished_property = parent_property;
//        //assert(stages_.count(finished_property));
//        assert(get_stage(parent_property) != NULL);
//
//        std::vector<ExecutionStage*> complete_stages;
//
//        //ExecutionStage* tmp_stage = stages_[finished_property];
//        ExecutionStage* tmp_stage = get_stage(parent_property);
//        while (tmp_stage != NULL) {
//          complete_stages.push_back(tmp_stage);
//          tmp_stage = tmp_stage->parent_stage;
//        }
//
//        //ExecutionStateProperty* tmp_property = finished_property;
//        ExecutionStateProperty* tmp_property = parent_property;
//        foreach(ExecutionStage* stage, complete_stages) {
//          if (!stage->etrace_tree->tracks(tmp_property)) {
//            CVMESSAGE("Root property not tracked, can't write trace! "
//                      << *(stage->root_property));
//          } else {
//						write_training_object(stage, tmp_property);
//					}
//          tmp_property = stage->root_property;
//        }
//
//        // Only output one set of paths for now
//        cv_->executor()->setHaltExecution(true);
//      }
//    }
//    break;
//
//    default:
//      break;
//  }
//}

////////////////////////////////////////////////////////////////////////////////

VerifyExecutionTraceManager::VerifyExecutionTraceManager(ClientVerifier* cv) 
  : ExecutionTraceManager(cv),
    last_round_cleared_(0),
    cluster_manager_(0),
    hmm_(0) {}

void VerifyExecutionTraceManager::initialize() {
  //klee::LockGuard guard(lock_);
  klee::WallTimer timer;
  // Create similarity measure
  similarity_measure_ = SocketEventSimilarityFactory::create();

  // Parse the training data filenames
  if (!TrainingPathDir.empty()) {
    foreach (std::string path, TrainingPathDir) {
      cv_->getFilesRecursive(path, ".tpath", TrainingPathFile);
    }
  }

  if (!TrainingPathFile.empty()) {
    CVMESSAGE("Loading " << TrainingPathFile.size() << " training data files.");

    // Read training data into memory
    TrainingManager::read_files(TrainingPathFile, training_data_); 

    if (training_data_.empty())
      cv_error("Error reading training data , exiting now.");

    CVMESSAGE("Finished loading " 
              << training_data_.size() << " unique training objects.");
  }

  // ------------------------------------------------------------------------//

  // Parse the self training data filenames 
  if (!SelfTrainingPathDir.empty()) {
    foreach (std::string path, SelfTrainingPathDir) {
      cv_->getFilesRecursive(path, ".tpath", SelfTrainingPathFile);
    }
  }

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

    // Assign tobjs to map according to round index
    foreach (TrainingObject *tobj, self_training_data_) {
      self_training_data_map_[tobj->round] = tobj;
    }
    if (DebugExecutionTree) {
      foreach (TrainingObject *tobj, self_training_data_) {
        std::stringstream ss; 
        foreach (SocketEvent* se, tobj->socket_event_set) {
          ss << " " << *se << " ";
        }
        foreach (BasicBlockID bbid, tobj->trace) {
          ss << bbid << ",";
        }
        CVDEBUG("Loaded SelfTraining (" << tobj->trace.size() << ") " 
                << tobj->name << " " << tobj->round << " " << ss.str());
      }
    }

  }
  CVMESSAGE("Finished reading training data in " 
            << timer.check() / 1000000. << "s");

  // ------------------------------------------------------------------------//

  if (UseHMM && (UseClustering || UseClusteringHint || UseClusteringAll)) {
    cv_error("-use-hmm and -use-clustering* are not compatible");
  }

  if (UseHMM && !llvm::sys::fs::exists(HMMTrainingFile)
      || (!UseHMM && HMMTrainingFile != "")) {
    cv_error("invalid usage of -use-hmm and -hmm-training-file");
  }

  if (UseClustering || UseClusteringHint || UseClusteringAll) {
    initialize_training_data();
  } else if (UseHMM) {
    hmm_ = new HMMPathPredictor();
    std::ifstream HMMTrainingFileIS(HMMTrainingFile.c_str(),
                                    std::ifstream::in | std::ifstream::binary );
    CVMESSAGE("HMM: loading " << HMMTrainingFile);
    HMMTrainingFileIS >> *hmm_;

    hmm_training_objs_ = hmm_->getAllTrainingObjects();
    CVMESSAGE("HMM: Retreived " << hmm_training_objs_.size() << " training objects");
  }
}

void VerifyExecutionTraceManager::set_ed_tree(
    ExecutionStage* stage,
    ExecutionStateProperty* property, 
    ExecutionTraceEditDistanceTree* ed_tree) {

  if (UseExternalStage) {
    property->ed_tree = ed_tree;
  } else {
    klee::LockGuard guard(lock_);
    stage->ed_tree_map[property] = ed_tree;
  }
}

ExecutionTraceEditDistanceTree* VerifyExecutionTraceManager::get_ed_tree(
    ExecutionStage* stage,
    ExecutionStateProperty *property) {

  if (UseExternalStage) {
    return property->ed_tree;
  } else {
    klee::LockGuard guard(lock_);
    if (stage->ed_tree_map.count(property))
      return stage->ed_tree_map[property];
  }
  return NULL;
}

void VerifyExecutionTraceManager::delete_ed_tree(
    ExecutionStage* stage,
    ExecutionStateProperty *property) {

  if (UseExternalStage) {
    if (property->ed_tree) {
      delete property->ed_tree;
      property->ed_tree = NULL;
    }
  } else {
    klee::LockGuard guard(lock_);
    if (stage->ed_tree_map.count(property)) {
      delete stage->ed_tree_map[property];
      stage->ed_tree_map.erase(property);
    }
  }
}

void VerifyExecutionTraceManager::initialize_training_data() {
  klee::WallTimer timer;
  cluster_manager_ = new TrainingObjectManager(ClusterSize,SocketEventClusterSize);
  std::vector<TrainingObject*> tobj_vec(training_data_.begin(), training_data_.end());
  cluster_manager_->cluster(tobj_vec);
  CVMESSAGE("Finished initialized training data in " 
            << timer.check() / 1000000. << "s");
}

void VerifyExecutionTraceManager::update_edit_distance(
    ExecutionStateProperty* property,
    CVExecutionState* state) {

  ExecutionStage* stage = get_stage(property);
  assert(stage);

  if (stage->root_ed_tree == NULL) {
    CVDEBUG("stage->root_ed_tree == NULL");
    return;
  }

  if (property->edit_distance == INT_MAX) {
    CVDEBUG("property->edit_distance == INT_MAX");
    return;
  }

  // FIXME do we need this?
  //if (stage->ed_tree_map.count(property) == 0) {
  //  if (stage->root_ed_tree != NULL) {
  //    CVMESSAGE("Cloning ed tree in update_edit_distance");
  //    stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
  //  } else {
  //    CVDEBUG("stage->root_ed_tree == NULL");
  //    return;
  //  }
  //}
  if (get_ed_tree(stage, property) == NULL) {
    CVMESSAGE("Cloning ed tree in update_edit_distance");
    set_ed_tree(stage, property, stage->root_ed_tree->clone_edit_distance_tree());
  }

  ExecutionTrace etrace;
  //klee::LockGuard guard(lock_);
  stage->etrace_tree->tracker_get(property, etrace);
  get_ed_tree(stage, property)->update(etrace);

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
  
  property->edit_distance = get_ed_tree(stage, property)->min_distance();
  CVDEBUG("Updated edit distance: " << property << ": " << *property << " etrace.size = " 
          << etrace.size() << ", row = " << get_ed_tree(stage, property)->row());

  // We should never differ by more than one basic block
  if (UseSelfTraining && CheckSelfTraining && property->edit_distance > 1) {
    TrainingObject* matching_tobj = self_training_data_map_[property->round];
    if (matching_tobj) {
      int curr_bb_id = state->pc->kbb->id;
      int self_bb_id = matching_tobj->trace[etrace.size()-1];
      if (curr_bb_id != self_bb_id) {
        CVMESSAGE("Self Training Data Mismatch!!");
        if (DebugExecutionTree) {
          klee::KBasicBlock *curr_kbb = cv_->LookupBasicBlockID(curr_bb_id);
          klee::KBasicBlock *self_kbb = cv_->LookupBasicBlockID(self_bb_id);
          CVDEBUG("Curr BasicBlock: [BB: " << curr_bb_id << "] " << *curr_kbb->kinst );
          CVDEBUG("Self BasicBlock: [BB: " << self_bb_id << "] " << *self_kbb->kinst );
        }
      }
    }
  }
}

void VerifyExecutionTraceManager::compute_self_training_stats(CVExecutionState* state,
                                                              std::vector<TrainingObject*> &selected) {
  ExecutionStateProperty *property = state->property();

  stats::edit_distance_medoid_count = selected.size();
  stats::edit_distance_self_first_medoid = 3003;
  stats::edit_distance_self_last_medoid = 2002;
  stats::edit_distance_self_socket_event = 1001;

  if (!self_training_data_.empty() && self_training_data_map_.count(property->round)) {
    klee::WallTimer stat_timer;
    int ed = 0;
    TrainingObject* self_tobj = self_training_data_map_[property->round];

    if (self_tobj->trace.size() < 5000) {
      TrainingObjectDistanceMetric metric;
      stats::edit_distance_self_first_medoid 
          = metric.distance(self_tobj, selected[0]);
      stats::edit_distance_self_last_medoid 
          = metric.distance(self_tobj, selected[selected.size()-1]);

      const SocketEvent* se      = &(state->network_manager()->socket()->event());
      const SocketEvent* self_se = *(self_tobj->socket_event_set.begin());

      ed = similarity_measure_->similarity_score(self_se, se);
    } else {
      CVMESSAGE("Not computing self training stats on trace, length: " << self_tobj->trace.size());
    }

    stats::edit_distance_self_socket_event = ed;
    stats::edit_distance_stat_time += stat_timer.check();
  }
}

void VerifyExecutionTraceManager::create_ed_tree(CVExecutionState* state) {

  ExecutionStateProperty *property = state->property();
  ExecutionStage* stage = get_stage(property);

  stage->root_ed_tree = NULL;

  if (!state->network_manager()->socket()->end_of_log()) {
    CVDEBUG("End of log, not building edit distance tree");
    return;
  }

  const SocketEvent* socket_event 
    = &(state->network_manager()->socket()->event());

  TrainingFilter tf(state);

  if (UseHMM) {
    property->hmm_round++;
    if (hmm_->rounds() < property->hmm_round) {
      hmm_->addMessage(*socket_event, tf.initial_basic_block_id);
    }

    auto guidePaths = hmm_->predictPath(property->hmm_round,
                                        tf.initial_basic_block_id,
                                        HMMConfidence);

    stage->root_ed_tree = EditDistanceTreeFactory::create();
    for (auto it : guidePaths) {
      auto training_object = hmm_training_objs_[it.second];
      CVMESSAGE("HMM: Adding path: " << training_object->name);
      stage->root_ed_tree->add_data(training_object->trace);
    }
    stats::edit_distance_medoid_count = guidePaths.size();

  } else if (UseSelfTraining) {
    if (self_training_data_map_.count(property->round) == 0) {
      CVDEBUG("No path in self training data for round " << property->round);
      return;
    } else {
      TrainingObject* matching_tobj = self_training_data_map_[property->round];

      CVDEBUG("Curr Socket Event: " << *socket_event);
      CVDEBUG("Self Socket Event: " << *(*(matching_tobj->socket_event_set.begin())));
      assert(matching_tobj->socket_event_set.size() == 1);
      assert(socket_event->equal(*(*(matching_tobj->socket_event_set.begin()))));

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      if (DebugExecutionTree) {
        std::stringstream ss; 
        foreach (BasicBlockID bbid, matching_tobj->trace) {
          ss << bbid << ",";
        }
        CVDEBUG("SelfTraining (" << matching_tobj->trace.size()
                << ") " << ss.str());
      }
      // Add to edit distance tree
      stage->root_ed_tree->add_data(matching_tobj->trace);

      stats::edit_distance_medoid_count = 1;
    }

  } else if (cluster_manager_->check_filter(tf)) {

    TrainingObjectScoreList sorted_clusters;
    std::vector<TrainingObject*> selected_training_objs;

    if (UseClusteringHint) {
      // Select matching execution path
      if (self_training_data_map_.count(property->round) == 0) {
        CVMESSAGE("No path in self training data for round " << property->round);
        return;
      }
      TrainingObject* matching_tobj = self_training_data_map_[property->round];

      // Compute hint
      klee::WallTimer hint_timer;
      cluster_manager_->all_clusters_distance(tf, matching_tobj, sorted_clusters);
      stats::edit_distance_hint_time += hint_timer.check();

      if (sorted_clusters.size() == 0) {
        CVMESSAGE("No hint found for round " << property->round);
        return;
      }

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      // Compute medoid distance stats
      selected_training_objs.push_back(sorted_clusters[0].second);
      compute_self_training_stats(state, selected_training_objs);

      // Add closest object to 'hint'
      stage->root_ed_tree->add_data(sorted_clusters[0].second->trace);

    } else if (UseClustering || UseClusteringAll) {

      TrainingObjectScoreList sorted_clusters;
      CVDEBUG("Selecting clusters...");
      cluster_manager_->sorted_clusters(socket_event, tf,
                                        sorted_clusters, *similarity_measure_);
      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      // Select the training paths 
      size_t i = 0;
      do {
        // Add path to edit distance tree
        stage->root_ed_tree->add_data(sorted_clusters[i].second->trace);
        selected_training_objs.push_back(sorted_clusters[i].second);
        i++;
      } while (i < sorted_clusters.size() && i < MaxMedoids 
          && sorted_clusters[i].first <= (sorted_clusters[0].first * MedoidSelectRate));

      // Stats
      stats::edit_distance_socket_event_first_medoid = sorted_clusters[0].first;
      stats::edit_distance_socket_event_last_medoid = sorted_clusters[i-1].first;

      CVDEBUG("Medoids: " << sorted_clusters[0].first
              << ", " << sorted_clusters[i-1].first);

      if (UseClusteringAll && self_training_data_map_.count(property->round)) {

        TrainingObject* matching_tobj = self_training_data_map_[property->round];

        // Compute hint
        klee::WallTimer hint_timer;
        TrainingObjectScoreList hint_sorted_clusters;
        cluster_manager_->all_clusters_distance(tf, matching_tobj, hint_sorted_clusters);
        stats::edit_distance_hint_time += hint_timer.check();

        if (sorted_clusters.size() == 0) {
          CVMESSAGE("No hint found for round " << property->round);
        } else {
          // Add closest object to the 'hint' to the ed tree
          stage->root_ed_tree->add_data(hint_sorted_clusters[0].second->trace);
        }
      }

      // Compute medoid distance stats
      compute_self_training_stats(state, selected_training_objs);
    } else {
      CVDEBUG("Not using Edit Distance Tree");
      return;
    }
  } else {
    CVDEBUG("No match for filter in clusters: " << tf << " " << *socket_event);
    return;
  }

  stage->current_k = MaxKExtension;
  stage->root_ed_tree->init(stage->current_k);

  // Set initial values for edit distance
  property->edit_distance = INT_MAX-1;
  property->recompute = true;

  set_ed_tree(stage, property, stage->root_ed_tree->clone_edit_distance_tree());
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
      CVDEBUG("SELECT EVENT: " << *state);
      property->is_recv_processing = false;
    }
    break;

    case CV_BASICBLOCK_ENTRY: {
      //klee::LockGuard guard(lock_);

      ExecutionStage* stage = get_stage(property);

      if (!property->is_recv_processing) {

        // Check if this is the first basic block of the stage
        // that needs an edit distance tree
        //if (stage->etrace_tree->empty()) {
        {
          klee::LockGuard ed_tree_guard(ed_tree_lock_);
          if (stage->root_ed_tree == NULL) {
            CVDEBUG("First basic block entry (stage)");
            
            // Build the edit distance tree using training data
            klee::WallTimer build_timer;
            create_ed_tree(state);
            stats::edit_distance_build_time += build_timer.check();

            CVDEBUG("Constructed edit distance tree in "
                    << build_timer.check() / 1000000. << " secs");
          }
        }

        // FIXME do we need this?
        //// Check if we need to reclone the edit distance tree 
        //if (stage->ed_tree_map.count(property) == 0 && stage->root_ed_tree) {
        //  CVMESSAGE("Cloning ed tree in BasicblockEntry");
        //  stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
        //}
        if (get_ed_tree(stage, property) == NULL && stage->root_ed_tree) {
          CVDEBUG("Cloning ed tree in BasicblockEntry");
          set_ed_tree(stage, property, stage->root_ed_tree->clone_edit_distance_tree());
        }
      }

      if (!property->is_recv_processing && 
          (state->basic_block_tracking() || !BasicBlockDisabling)) {

        {
          klee::TimerStatIncrementer timer(stats::execution_tree_time);
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
          //if (property->edit_distance != INT_MAX) {
          //  property->ed_tree->update_element(state->prevPC->kbb->id);
          //}
        }

        if (property->recompute) {
          if (EditDistanceAtCloneOnly) {
            CVDEBUG("Setting recompute to false");
            property->recompute = false;
          }

          klee::TimerStatIncrementer timer(stats::edit_distance_time);
          update_edit_distance(property, state);
        }
      }
    }
    break;

    case CV_STATE_REMOVED: {
      //klee::LockGuard guard(lock_);
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      //ExecutionStage* stage = stages_[property];
      ExecutionStage* stage = get_stage(property);

      //if (stage->etrace_tree->tracks(property))
      //  stage->etrace_tree->remove_tracker(property);
      remove_property(property);

      if (is_socket_active) {
        if (get_ed_tree(stage, property) != NULL) {
          delete_ed_tree(stage, property);
          //delete stage->ed_tree_map[property];
          //stage->ed_tree_map.erase(property);
        }
      }

      //stages_.erase(property);
      //remove_from_stage(property);
    }
    break;

    case CV_STATE_CLONE: {
      //klee::LockGuard guard(lock_);
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      //ExecutionStage* stage = stages_[parent_property];
      ExecutionStage* stage = get_stage(parent_property);

      if (!property->is_recv_processing && EditDistanceAtCloneOnly) {
        klee::TimerStatIncrementer timer(stats::edit_distance_time);
        update_edit_distance(parent_property, parent);
      }

      //stages_[property] = stage;
      set_stage(property, stage);

      stage->etrace_tree->clone_tracker(property, parent_property);
      
      property->recompute=true;
      parent_property->recompute=true;

      property->edit_distance = parent_property->edit_distance;

      //if (stage->ed_tree_map.count(parent_property) && stage->root_ed_tree) {
      //  stage->ed_tree_map[property] = 
      //      stage->ed_tree_map[parent_property]->clone_edit_distance_tree();
      
      auto ed_tree = get_ed_tree(stage, parent_property);
      if (ed_tree != NULL && stage->root_ed_tree != NULL) {

        set_ed_tree(stage, property, ed_tree->clone_edit_distance_tree());

        CVDEBUG("Cloned EDTree "
                << " clone: " << property 
                << " parent: " << parent_property 
                << ", clone row = " << get_ed_tree(stage,property)->row()
                << ", parent row = " << get_ed_tree(stage,parent_property)->row());

        //property->edit_distance = stage->ed_tree_map[property]->min_distance();
        property->edit_distance = get_ed_tree(stage,property)->min_distance();
      }

      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      //klee::LockGuard guard(lock_);
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      CVDEBUG("New Stage: " << property << ": " << *property);
      CVDEBUG("New Stage (parent): " << parent_property << ": " << *parent_property);

      // Stats ------------------------------------------------------
      // Increment stage count
      stats::stage_count += 1;

      ExecutionStage* stage = get_stage(parent_property);

      // Final edit distance 
      //if (FinalDistance &&
      //    stages_.count(parent_property) &&
      //    stages_[parent_property]->ed_tree_map.count(parent_property)) {
      if (FinalDistance && stage != NULL &&
          get_ed_tree(stage, parent_property) != NULL) {
        klee::WallTimer stat_timer;

        //ExecutionStage* stage = stages_[parent_property];
        //ExecutionTraceEditDistanceTree *ed_tree 
        //    = stage->ed_tree_map[parent_property];
        ExecutionTraceEditDistanceTree *ed_tree = get_ed_tree(stage, parent_property);

        ExecutionTrace etrace;
        stage->etrace_tree->tracker_get(parent_property, etrace);
        ed_tree->update(etrace);

        int ed = ed_tree->min_distance();

        if (ed == INT_MAX) {
          ed_tree->init(10000000);
          ed_tree->update(etrace);
          ed = ed_tree->min_distance();
        }

        assert(ed != INT_MAX);
        
        stats::edit_distance = ed;
        stats::edit_distance_stat_time += stat_timer.check();
      }

      // Final kprefix
      //if (stages_.count(parent_property))
      //  stats::edit_distance_k = stages_[parent_property]->current_k;
      if (stage != NULL)
        stats::edit_distance_k = stage->current_k;

      // Socket event size
      if (is_socket_active) {
        if (state->network_manager()->socket()->index() > 0) {
          stats::socket_event_size
              = state->network_manager()->socket()->previous_event().length;
          stats::socket_event_timestamp
              = state->network_manager()->socket()->previous_event().timestamp;
          stats::socket_event_type
              = state->network_manager()->socket()->previous_event().type;
        }
        CVDEBUG("Next Socket Event: " << state->network_manager()->socket()->event());
      }

      // Symbolic variables
      stats::symbolic_variable_count += parent_property->symbolic_vars;

      // ------------------------------------------------------------
      
      // Initialize a new ExecutionTraceTree
      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      //if (!stages_.empty() && 
      //    stages_.count(parent_property) && 
      //    stages_[parent_property]->etrace_tree->tracks(parent_property)) {
      if (stage && stage->etrace_tree->tracks(parent_property)) {

        //update_edit_distance(property);
        CVDEBUG("End state: " << *parent);
        //new_stage->parent_stage = stages_[parent_property];
        new_stage->parent_stage = stage;
      }

      //stages_[property] = new_stage;
      set_stage(property, new_stage);

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        cv_->executor()->setHaltExecution(true);
      }
    }
    break;

    case CV_CLEAR_CACHES: {
      cv_error("CLEAR_CACHES not currently supported!");
      clear_caches();
      break;
    }

    default:
    break;
  }
}

void VerifyExecutionTraceManager::clear_caches() {
  CVMESSAGE("ExecutionTraceManager::clear_caches() starting");
  cv_error("clear_caches: not supported!");
  //StatePropertyStageMap::iterator stage_it = stages_.begin(), 
  //    stage_ie = stages_.end();

  //for (;stage_it != stage_ie; ++stage_it) {
  //  ExecutionStateProperty *property = stage_it->first;
  //  ExecutionStage* stage = stage_it->second;

  //  size_t size = stage->ed_tree_map.size();
  //  if (last_round_cleared_ == cv_->round() 
	//			|| property->round < cv_->round()) {
  //    if (stage->ed_tree_map.size()) {
  //      CVMESSAGE("Clearing EditDistanceTree of size: " << size);
  //      StatePropertyEditDistanceTreeMap::iterator it = stage->ed_tree_map.begin();
  //      StatePropertyEditDistanceTreeMap::iterator ie = stage->ed_tree_map.end();
  //      for (; it!=ie; ++it) {
  //        delete it->second;
  //      }
  //      stage->ed_tree_map.clear();
  //    }
  //  }

  //  // We don't clear the root tree.
  //}
  CVMESSAGE("ExecutionTraceManager::clear_caches() finished");
	last_round_cleared_ = cv_->round();
}

bool VerifyExecutionTraceManager::ready_process_all_states(
    ExecutionStateProperty* property) {
  //klee::LockGuard guard(lock_);

  //assert(stages_.count(property));
  //ExecutionStage* stage = stages_[property];
  assert(get_stage(property) != NULL);
  ExecutionStage* stage = get_stage(property);

  return stage->root_ed_tree != NULL && (stage->current_k < MaxKExtension);
}

void VerifyExecutionTraceManager::recompute_property(
    ExecutionStateProperty *property) {
  //assert(stages_.count(property));
  assert(get_stage(property) != NULL);

  //ExecutionStage* stage = stages_[property];
  ExecutionStage* stage = get_stage(property);

  //if (stage->ed_tree_map.count(property) == 0) {
  //  CVMESSAGE("Cloning ed tree in recompute property");
  //  stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
  //}
  
  if (get_ed_tree(stage, property) == NULL) {
    set_ed_tree(stage, property, stage->root_ed_tree->clone_edit_distance_tree());
  }

  //stage->current_k = MaxKExtension;
  //stage->ed_tree_map[property]->init(stage->current_k);
  get_ed_tree(stage, property)->init(stage->current_k);

  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  CVMESSAGE("RC edit distance: " << property << ": " << *property << " etrace.size = " 
          << etrace.size() << ", row = " << get_ed_tree(stage, property)->row());
          //<< etrace.size() << ", row = " << stage->ed_tree_map[property]->row());

  //stage->ed_tree_map[property]->update(etrace);
  //property->edit_distance = stage->ed_tree_map[property]->min_distance();
  get_ed_tree(stage, property)->update(etrace);
  property->edit_distance = get_ed_tree(stage, property)->min_distance();
}

void VerifyExecutionTraceManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {
  //klee::LockGuard guard(lock_);
  klee::WallTimer timer;
  klee::TimerStatIncrementer edct(stats::edit_distance_time);

  assert(!states.empty());
  //assert(stages_.count(states[0]));
  assert(get_stage(states[0]) != NULL);

  //ExecutionStage* stage = stages_[states[0]];
  ExecutionStage* stage = get_stage(states[0]);

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
            << timer.check() / 1000000. << " secs");
}

// Delete the trees associated with each state in the edit distance map
// and clear the map itself
void VerifyExecutionTraceManager::clear_execution_stage(
    ExecutionStateProperty *property) {

  cv_error("clear_execution_stages() not supported");
  //assert(stages_.count(property));

  //ExecutionStage* stage = stages_[property];
  //
  //StatePropertyEditDistanceTreeMap::iterator it = stage->ed_tree_map.begin();
  //StatePropertyEditDistanceTreeMap::iterator ie = stage->ed_tree_map.end();
  //for (; it!=ie; ++it) {
  //  delete it->second;
  //}
  //stage->ed_tree_map.clear();

  //if (stage->root_ed_tree != NULL) {
  //  stage->root_ed_tree->delete_shared_data();
  //  delete stage->root_ed_tree;
  //  stage->root_ed_tree = NULL;
  //}
}

} // end namespace cliver
