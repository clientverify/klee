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

#include "CVCommon.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <algorithm>

namespace cliver {

llvm::cl::opt<unsigned>
StateTreesMemoryLimit("state-trees-memory-limit",llvm::cl::init(0));

llvm::cl::opt<unsigned>
MaxKExtension("max-k-extension",llvm::cl::init(16));

llvm::cl::opt<bool>
EditDistanceAtCloneOnly("edit-distance-at-clone-only",llvm::cl::init(true));

llvm::cl::opt<bool>
BasicBlockDisabling("basicblock-disabling",llvm::cl::init(false));

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

llvm::cl::opt<bool>
DeleteOldTrees("delete-old-trees",llvm::cl::init(true));

llvm::cl::list<std::string> TrainingPathFile("training-path-file",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify a training path file (.tpath)"),
	llvm::cl::value_desc("tpath directory"));

llvm::cl::list<std::string> TrainingPathDir("training-path-dir",
	llvm::cl::ZeroOrMore,
	llvm::cl::ValueRequired,
	llvm::cl::desc("Specify directory containint .tpath files"),
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

    case CV_BASICBLOCK_ENTRY: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      ExecutionStage* stage = stages_[property];

      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        //klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
        stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
      }
    }
    break;

    case CV_STATE_REMOVED: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];
      stage->etrace_tree->remove_tracker(property);
      stages_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Cloned state: " << *state);
      ExecutionStage* stage = stages_[parent_property];
      stages_[property] = stage;
      stage->etrace_tree->clone_tracker(property, parent_property);
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      ExecutionStage *new_stage = new ExecutionStage();
      new_stage->etrace_tree = new ExecutionTraceTree();
      new_stage->root_property = parent_property;

      if (!stages_.empty() && 
          stages_.count(parent_property) && 
          stages_[parent_property]->etrace_tree->tracks(parent_property)) {

        CVDEBUG("End state: " << *parent);

        new_stage->parent_stage = stages_[parent_property];

        //clear_execution_stage(property);
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
    case CV_BASICBLOCK_ENTRY: {
      assert(stages_.count(property));

      ExecutionStage* stage = stages_[property];

      if (state->basic_block_tracking() || !BasicBlockDisabling)
        stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
    }
    break;

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];
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

      if (!stages_.empty() && 
          stages_.count(parent_property) && 
          stages_[parent_property]->etrace_tree->tracks(parent_property)) {

        CVDEBUG("End state: " << *parent);

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
          assert(stage->etrace_tree->tracks(tmp_property));
          write_training_object(stage, tmp_property);
          tmp_property = stage->root_property;
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
  similarity_measure_ = SocketEventSimilarityFactory::create(cv_);

  // Parse the training data filenames
  if (!TrainingPathDir.empty())
    foreach (std::string path, TrainingPathDir)
      cv_->getFilesRecursive(path, ".tpath", TrainingPathFile);

  // Report error if no filenames found
  if (TrainingPathFile.empty())
    cv_error("Error parsing training data file names, exiting now.");

  CVMESSAGE("Loading " 
            << TrainingPathFile.size() << " training data files.");

  // Read training data into memory
  TrainingManager::read_files(TrainingPathFile, training_data_); 
  if (training_data_.empty())
    cv_error("Error reading training data , exiting now.");

  CVMESSAGE("Finished loading " 
            << training_data_.size() << " unique training objects.");
}

void VerifyExecutionTraceManager::update_edit_distance(
    ExecutionStateProperty* property) {

  //klee::TimerStatIncrementer edct(stats::edit_distance_compute_time);

  ExecutionStage* stage = stages_[property];
  assert(stage);

  int row = stage->ed_tree_map[property]->row();

  // XXX FIX ME -- make this operation O(1)
  int trace_depth = stage->etrace_tree->tracker_depth(property);

  if (trace_depth - row == 1) {
    stage->ed_tree_map[property]->update_element(
        stage->etrace_tree->leaf_element(property));
  } else {
    ExecutionTrace etrace;
    etrace.reserve(trace_depth);
    stage->etrace_tree->tracker_get(property, etrace);
    stage->ed_tree_map[property]->update(etrace);
  }
  property->edit_distance = stage->ed_tree_map[property]->min_distance();
}

void VerifyExecutionTraceManager::notify(ExecutionEvent ev) {
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
    case CV_BASICBLOCK_ENTRY: {

      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      ExecutionStage* stage = stages_[property];

      if (state->network_manager() && state->network_manager()->socket() &&
          state->network_manager()->socket()->end_of_log()) {

        // Check if this is the first basic block of the stage
        if (!stage->etrace_tree->tracks(property)) {
          assert(stage->etrace_tree->element_count() == 0);
          CVDEBUG("First basic block entry (stage)");
          //klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);

          TrainingObjectScoreList score_list;
          TrainingManager::init_score_list(training_data_, score_list);

          const SocketEvent* socket_event 
            = &(state->network_manager()->socket()->event());

          TrainingManager::sort_by_similarity_score(socket_event, score_list, 
                                                    *similarity_measure_);
          // Create a new root edit distance
          stage->root_ed_tree = EditDistanceTreeFactory::create();

          // If exact match exists, only add exact match, otherwise
          // add 5 closest matches 
          size_t i, max_count = 5;
          bool zero_match = false;
          for (i=0; (i < score_list.size()) && (i < max_count); ++i) {
            double score = score_list[i].first;
            if (i == 0) {
              stats::edit_distance_min_score = (int)(100 * score);
            }
            CVDEBUG("Score: " << score);
            if (zero_match && score > 0.0f) {
              i--;
              break;
            }
            if (score == 0.0f)
              zero_match = true;
            stage->root_ed_tree->add_data(score_list[i].second->trace);
          }

          stage->root_ed_tree->init(stage->current_k);

          // Set initial values for edit distance
          property->edit_distance = INT_MAX;
          property->recompute = true;

          // Store size of tree in stats
          CVDEBUG("Training object tree for round: "
              << state->property()->round << " used " << i+1 << " training objects");
          stats::edit_distance_tree_size = (i+1); 

          stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
        }
      }

      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        {
          //klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
        }

        if (state->network_manager() && state->network_manager()->socket() &&
            state->network_manager()->socket()->end_of_log()) {
          
          if (property->recompute) {
            if (EditDistanceAtCloneOnly) {
              property->recompute = false;
            }
            update_edit_distance(property);
          }
        }
      }
    }
    break;

    case CV_STATE_REMOVED: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      ExecutionStage* stage = stages_[property];
      stage->etrace_tree->remove_tracker(property);

      if (state->network_manager() && state->network_manager()->socket() &&
          state->network_manager()->socket()->end_of_log()) {
        assert(stage->ed_tree_map.count(property));
        delete stage->ed_tree_map[property];
        stage->ed_tree_map.erase(property);
      }

      stages_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      if (EditDistanceAtCloneOnly)
        update_edit_distance(parent_property);

      ExecutionStage* stage = stages_[parent_property];
      stages_[property] = stage;

      stage->etrace_tree->clone_tracker(property, parent_property);
      
      property->recompute=true;
      parent_property->recompute=true;

      if (state->network_manager() && state->network_manager()->socket() &&
          state->network_manager()->socket()->end_of_log()) {
        assert(stage->ed_tree_map.count(parent_property));

        stage->ed_tree_map[property] = 
            stage->ed_tree_map[parent_property]->clone_edit_distance_tree();

        property->edit_distance = stage->ed_tree_map[property]->min_distance();
      }

      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      // Increment stat counter
      stats::stage_count += 1;

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

        //clear_execution_stage(property);
      }

      stages_[property] = new_stage;

      if (cv_->executor()->finished_states().count(parent_property)) {
        CVMESSAGE("Verification complete");
        cv_->print_all_stats();
        cv_->executor()->setHaltExecution(true);
      }
    }
    break;

    default:
    break;
  }
}

bool VerifyExecutionTraceManager::ready_process_all_states(
    ExecutionStateProperty* property) {

  assert(stages_.count(property));
  ExecutionStage* stage = stages_[property];

  return stage->current_k < MaxKExtension;
}

void VerifyExecutionTraceManager::recompute_property(
    ExecutionStateProperty *property) {
  //klee::TimerStatIncrementer compute_timer(stats::edit_distance_compute_time);

  assert(stages_.count(property));

  ExecutionStage* stage = stages_[property];

  assert(stage->ed_tree_map.count(property));
  
  stage->ed_tree_map[property]->init(stage->current_k);

  ExecutionTrace etrace;
  etrace.reserve(stage->etrace_tree->tracker_depth(property));
  stage->etrace_tree->tracker_get(property, etrace);
  stage->ed_tree_map[property]->update(etrace);
  property->edit_distance = stage->ed_tree_map[property]->min_distance();
}

void VerifyExecutionTraceManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {

  {
    assert(!states.empty());
    assert(stages_.count(states[0]));
    ExecutionStage* stage = stages_[states[0]];
    CVMESSAGE("Doubling K from: " << stage->current_k 
              << " to " << stage->current_k*2);

    stage->current_k = stage->current_k * 2;

    stats::edit_distance_final_k = stage->current_k;
  }

  CVMESSAGE("All states should have INT_MAX=" << INT_MAX << " edit distance.");
  for (unsigned i=0; i<states.size(); ++i) {
    //assert(states[i]->edit_distance == INT_MAX);
    int old_ed = states[i]->edit_distance;
    recompute_property(states[i]);
    CVMESSAGE("Edit distance computed from: " << old_ed 
              << " to " << states[i]->edit_distance);
  }
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
