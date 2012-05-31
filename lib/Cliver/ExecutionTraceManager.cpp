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
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
        tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
      }
    }
    break;

    case CV_STATE_REMOVED: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      tree_list_.back()->remove_tracker(property);
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Cloned state: " << *state);
      tree_list_.back()->clone_tracker(property, parent_property);
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      if (DebugExecutionTree) {
        if (!tree_list_.empty() && tree_list_.back()->tracks(property)) {
          ExecutionTrace etrace;
          tree_list_.back()->tracker_get(property, etrace);
          CVDEBUG("TRACE: length: " << etrace.size());
        }
      }

      if (DeleteOldTrees && !tree_list_.empty()) {
        delete tree_list_.back();
        tree_list_.pop_back();
      }
      tree_list_.push_back(new ExecutionTraceTree());
    }
    break;

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingExecutionTraceManager::TrainingExecutionTraceManager(ClientVerifier* cv) 
  : ExecutionTraceManager(cv) {}

void TrainingExecutionTraceManager::initialize() {
  tree_list_.push_back(new ExecutionTraceTree() );
}

// Write this state's path and associated socket event data to file
void TrainingExecutionTraceManager::write_training_object(
    CVExecutionState* state) {

  ExecutionStateProperty *property = state->property();

  assert(tree_list_.back()->tracks(property));

  // Get socket event for this successful path
  Socket* socket = state->network_manager()->socket();
  assert(socket);
  SocketEvent* socket_event 
      = const_cast<SocketEvent*>(&socket->previous_event());

  // Get path from the execution tree
  ExecutionTrace etrace;
  tree_list_.back()->tracker_get(property, etrace);

  // Create training object and write to file
  TrainingObject training_obj(&etrace, socket_event);
  training_obj.write(state, cv_);
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
      if (state->basic_block_tracking() || !BasicBlockDisabling)
        tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
    }
    break;

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << *state );
      tree_list_.back()->remove_tracker(property);
    }
    break;

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << *state);
      tree_list_.back()->clone_tracker(property, parent_property);
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      if (!tree_list_.empty() && tree_list_.back()->tracks(property)) {
          write_training_object(state);
      }

      if (DeleteOldTrees && !tree_list_.empty()) {
        delete tree_list_.back();
        tree_list_.pop_back();
      }
      tree_list_.push_back(new ExecutionTraceTree());
    }
    break;

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

VerifyExecutionTraceManager::VerifyExecutionTraceManager(ClientVerifier* cv) 
  : ExecutionTraceManager(cv), root_tree_(NULL) {}

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

  klee::TimerStatIncrementer edct(stats::edit_distance_compute_time);

  int row = edit_distance_map_[property]->row();
  int trace_depth = tree_list_.back()->tracker_depth(property);

  if (trace_depth - row == 1) {
    edit_distance_map_[property]->update_element(
        tree_list_.back()->leaf_element(property));
  } else {
    ExecutionTrace etrace;
    etrace.reserve(trace_depth);
    tree_list_.back()->tracker_get(property, etrace);
    edit_distance_map_[property]->update(etrace);
  }
  property->edit_distance = edit_distance_map_[property]->min_distance();
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
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      // First BasicBlock Entry Event
      if (!tree_list_.back()->tracks(property)) {
        klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);

        TrainingObjectScoreList score_list;
        TrainingManager::init_score_list(training_data_, score_list);

        const SocketEvent* socket_event = NULL;

        // XXX TEMP HACK
        if (ClientModelFlag == XPilot)
          socket_event = &(state->network_manager()->socket()->last_event());
        else
          socket_event = &(state->network_manager()->socket()->event());


        TrainingManager::sort_by_similarity_score(socket_event, score_list, 
                                                  *similarity_measure_);

        // Create a new root edit distance
        //ExecutionTraceEditDistanceTree *root_ed_tree = EditDistanceTreeFactory::create();
        root_tree_ = EditDistanceTreeFactory::create();

        // If exact match exists, only add exact match, otherwise
        // add 5 closest matches 
        size_t i, max_count = 5;
        bool zero_match = false;
        for (i=0; (i < score_list.size()) && (i < max_count); ++i) {
          double score = score_list[i].first;
					if (zero_match && score > 0.0f) {
            i--;
            break;
          }
          CVDEBUG("Score: " << score);
          if (score == 0.0f)
            zero_match = true;
					root_tree_->add_data(score_list[i].second->trace);
        }

        root_tree_->init(current_k_);

        // Set initial values for edit distance
        property->edit_distance = INT_MAX;
        property->recompute = true;

        // Store size of tree in stats
        CVDEBUG("Training object tree for round: "
            << state->property()->round << " used " << i+1 << " training objects");
        stats::edit_distance_tree_size += (i+1); 

        edit_distance_map_[property] = root_tree_->clone_edit_distance_tree();
      }

      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        {
          klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
          tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
        }

        if (property->recompute) {
          if (EditDistanceAtCloneOnly)
            property->recompute = false;

          update_edit_distance(property);
        }
      }
    }
    break;

    case CV_STATE_REMOVED: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      tree_list_.back()->remove_tracker(property);
      assert(edit_distance_map_.count(property));
      delete edit_distance_map_[property];
      edit_distance_map_.erase(property);
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      if (EditDistanceAtCloneOnly)
        update_edit_distance(parent_property);

      tree_list_.back()->clone_tracker(property, parent_property);
      
      property->recompute=true;
      parent_property->recompute=true;

      assert(edit_distance_map_.count(parent_property));

      edit_distance_map_[property] = 
          edit_distance_map_[parent_property]->clone_edit_distance_tree();

      /// XXX not necessesary?
      property->edit_distance = edit_distance_map_[property]->min_distance();
      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )

      //if (StateTreesMemoryLimit > 0 
      //    && cv_->executor()->memory_usage() >= StateTreesMemoryLimit) {
      //  ExecutionStateEDTreeMap::iterator it = state_tree_map_.begin(),
      //      ie = state_tree_map_.end();
      //  for (; it!=ie; ++it) {
      //    delete it->second;
      //  }
      //  state_tree_map_.clear();
      //}
    }
    break;

    case CV_SEARCHER_NEW_STAGE: {
      //klee::TimerStatIncrementer timer(stats::execution_tree_time);

      if (!tree_list_.empty() && tree_list_.back()->tracks(property)) {
        //update_edit_distance(property);
        CVMESSAGE("End state: " << *state);
        //ExecutionTrace etrace;
        //tree_list_.back()->tracker_get(property, etrace);
        //CVDEBUG("End of round, path length: " << etrace.size());
      }

      // Delete the ExecutionTraceTree from the previous round
      if (DeleteOldTrees && !tree_list_.empty()) {
        delete tree_list_.back();
        tree_list_.pop_back();
      }

      // Delete old trees associated with states from previous rounds
      clear_edit_distance_map();

      // Initialize a new ExecutionTraceTree
      tree_list_.push_back(new ExecutionTraceTree() );
    }
    break;

    //case CV_SOCKET_SHUTDOWN: {
    //  CVDEBUG("Successful socket shutdown. " << *state);
    //}
    break;

    default: {
    }
    break;
  }
}

bool VerifyExecutionTraceManager::ready_process_all_states() {
  return current_k_ < MaxKExtension;
}

void VerifyExecutionTraceManager::recompute_property(
    ExecutionStateProperty *property) {
  klee::TimerStatIncrementer compute_timer(stats::edit_distance_compute_time);

  assert(edit_distance_map_.count(property));
  edit_distance_map_[property]->init(current_k_);

  ExecutionTrace etrace;
  etrace.reserve(tree_list_.back()->tracker_depth(property));
  tree_list_.back()->tracker_get(property, etrace); 

  edit_distance_map_[property]->update(etrace);
  property->edit_distance = edit_distance_map_[property]->min_distance();
}

void VerifyExecutionTraceManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {

  CVMESSAGE("Doubling K from: " << current_k_ << " to " << current_k_*2);
  current_k_ = current_k_ * 2;

  for (unsigned i=0; i<states.size(); ++i) {
    assert(states[i]->edit_distance == INT_MAX);
    int old_ed = states[i]->edit_distance;
    recompute_property(states[i]);
    CVMESSAGE("Edit distance computed from: " << old_ed 
              << " to " << states[i]->edit_distance);
  }
}

// Delete the trees associated with each state in the edit distance map
// and clear the map itself
void VerifyExecutionTraceManager::clear_edit_distance_map() {
  StatePropertyEditDistanceTreeMap::iterator it = edit_distance_map_.begin();
  StatePropertyEditDistanceTreeMap::iterator ie = edit_distance_map_.end();
  for (; it!=ie; ++it) {
    delete it->second;
  }
  edit_distance_map_.clear();

  if (root_tree_ != NULL) {
    root_tree_->delete_shared_data();
    delete root_tree_;
  }
}

} // end namespace cliver
