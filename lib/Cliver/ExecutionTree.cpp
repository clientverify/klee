//===-- ExecutionTree.cpp ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO: Method to merge ExecutionTrees
// TODO: Method to modify pre-existing ExecutionTree
// TODO: Method to split an ExecutionTree given a list of Leaf nodes
// TODO: Optimization: store BasicBlock entry id's in a vector rather than a 
//       path of nodes
// TODO: Unit tests for execution trees
// TODO: Remove static_casts in notify()
//===----------------------------------------------------------------------===//

#include "cliver/ExecutionTree.h"
#include "cliver/CVExecutor.h"
#include "cliver/CVStream.h"
#include "cliver/EditDistance.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/CVExecutionState.h"
#include "cliver/NetworkManager.h"
#include "cliver/Training.h"
#include "cliver/SocketEventMeasurement.h"
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
BasicBlockDisabling("basicblock-disabling",llvm::cl::init(true));

llvm::cl::opt<bool>
DebugExecutionTree("debug-execution-tree",llvm::cl::init(false));

llvm::cl::opt<bool>
DeleteOldTrees("delete-old-trees",llvm::cl::init(true));

llvm::cl::opt<bool>
AltEditDistance("alt-edit-distance",llvm::cl::init(false));

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

ExecutionTreeManager::ExecutionTreeManager(ClientVerifier* cv) : cv_(cv) {}

void ExecutionTreeManager::initialize() {
  tree_list_.push_back(new ExecutionTraceTree() );
}

void ExecutionTreeManager::notify(ExecutionEvent ev) {
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
    case CV_ROUND_START: {
      if (DeleteOldTrees && !tree_list_.empty()) {
        delete tree_list_.back();
        tree_list_.pop_back();
      }
      tree_list_.push_back(new ExecutionTraceTree() );
    }
    break;

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

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ:
    case CV_SOCKET_SHUTDOWN: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Successful socket event: " << *state);
      ExecutionTraceTree* tree = NULL;
      reverse_foreach (tree, tree_list_) {
        if (tree->tracks(property))
          break;
      }

      if (tree->tracks(property)) {
        ExecutionTrace etrace;
        tree->tracker_get(property, etrace);
        CVDEBUG("TRACE: length: " << etrace.size());
      }
    }
    break;

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

TrainingExecutionTreeManager::TrainingExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv) {}

void TrainingExecutionTreeManager::initialize() {
  tree_list_.push_back(new ExecutionTraceTree() );
}

void TrainingExecutionTreeManager::notify(ExecutionEvent ev) {
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
    case CV_ROUND_START: {
      if (DeleteOldTrees && !tree_list_.empty()) {
        delete tree_list_.back();
        tree_list_.pop_back();
      }
      tree_list_.push_back(new ExecutionTraceTree() );
      break;
    }
    case CV_BASICBLOCK_ENTRY: {
      if (state->basic_block_tracking() || !BasicBlockDisabling)
        tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
      break;
    }

    case CV_STATE_REMOVED: {
      CVDEBUG("Removing state: " << *state );
      tree_list_.back()->remove_tracker(property);
      break;
    }

    case CV_STATE_CLONE: {
      CVDEBUG("Cloned state: " << *state);
      tree_list_.back()->clone_tracker(property, parent_property);
      break;
    }

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      assert(tree_list_.back()->tracks(property));

      // On a successful socket read/write event, write this path's state
      // and associated socket event data to file

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
      break;
    }

    case CV_SOCKET_SHUTDOWN: {
      CVDEBUG("Successful socket shutdown");
      break;
    }

    default:
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////

VerifyExecutionTreeManager::VerifyExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv), root_tree_(NULL) {}

void VerifyExecutionTreeManager::initialize() {
  // Initialize a new ExecutionTraceTree
  tree_list_.push_back(new ExecutionTraceTree());

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

void VerifyExecutionTreeManager::notify(ExecutionEvent ev) {
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
    case CV_ROUND_START: {
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

    case CV_BASICBLOCK_ENTRY: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(property);

      // First BasicBlock Entry Event
      if (!tree_list_.back()->tracks(property)) {
        klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);

        TrainingObjectScoreList score_list;
        TrainingManager::init_score_list(training_data_, score_list);

        const SocketEvent* socket_event 
            = &(state->network_manager()->socket()->event());

        SocketEventSimilarityTetrinet measure;
        TrainingManager::sort_by_similarity_score(socket_event, score_list, measure);

        root_tree_ = new EditDistanceExecutionTree();

        size_t i, max_count = 5;
        bool zero_match = true;
        for (i=0; i < score_list.size() && (zero_match|| i < max_count); ++i) {
          root_tree_->insert(score_list[i].second->trace);
          double score = score_list[i].first;
          if (score > 0.0) zero_match = false;
        }

        // Set initial values for edit distance
        edp->edit_distance = INT_MAX;
        edp->recompute = true;

        // Store size of tree in stats
        size_t element_count = root_tree_->element_count();

        CVDEBUG("Training object tree for round: "
            << cv_->round() << " has " << element_count
            << " elements from " << i+1 << " training objects");
        stats::edit_distance_tree_size += element_count; 
      }

      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
        tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
      }

      if (edp->recompute) {

        if (edit_distance_map_.count(property) == 0) {
          edit_distance_map_[property] = 
              static_cast<EditDistanceExecutionTree*>(root_tree_->clone());
        }

        edp->recompute = false;

        ExecutionTrace etrace;
        {
          etrace.reserve(tree_list_.back()->tracker_depth(property));
          tree_list_.back()->tracker_get(property, etrace);
        }

        {
          klee::TimerStatIncrementer 
              compute_timer(stats::edit_distance_compute_time);

          edit_distance_map_[property]->update(etrace);
          edp->edit_distance = edit_distance_map_[property]->min_distance();
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
      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )

      tree_list_.back()->clone_tracker(property, parent_property);
      
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(property);
      EditDistanceProperty *edp_parent
        = static_cast<EditDistanceProperty*>(parent_property);

      edp->recompute=true;
      edp_parent->recompute=true;

      assert(edit_distance_map_.count(parent_property));

      edit_distance_map_[property] = 
          static_cast<EditDistanceExecutionTree*>(
              edit_distance_map_[parent_property]->clone());

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

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("End state: " << *state);

      ExecutionTrace etrace;
      tree_list_.back()->tracker_get(property, etrace);
      //EditDistanceProperty *edp = 
      //    static_cast<EditDistanceProperty*>(property);

      CVDEBUG("End of round, path length: " << etrace.size());

    }
    break;

    case CV_SOCKET_SHUTDOWN: {
      CVDEBUG("Successful socket shutdown. " << *state);
    }
    break;

    default: {
    }
    break;
  }
}

// Delete the trees associated with each state in the edit distance map
// and clear the map itself
void VerifyExecutionTreeManager::clear_edit_distance_map() {
  EditDistanceExecutionTreeMap::iterator it = edit_distance_map_.begin();
  EditDistanceExecutionTreeMap::iterator ie = edit_distance_map_.end();
  for (; it!=ie; ++it) {
    delete it->second;
  }
  edit_distance_map_.clear();
  if (root_tree_ != NULL)
    delete root_tree_;
}

////////////////////////////////////////////////////////////////////////////////

KExtensionVerifyExecutionTreeManager::KExtensionVerifyExecutionTreeManager(ClientVerifier* cv) 
  : ExecutionTreeManager(cv), root_tree_(NULL), current_k_(2) {}

void KExtensionVerifyExecutionTreeManager::initialize() {
  // Initialize a new ExecutionTraceTree
  tree_list_.push_back(new ExecutionTraceTree());

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

void KExtensionVerifyExecutionTreeManager::notify(ExecutionEvent ev) {
  if (cv_->executor()->replay_path())
    return;

  CVExecutionState* state = ev.state;
  CVExecutionState* parent = ev.parent;

  ExecutionStateProperty *property = NULL, *parent_property = NULL;
  if (state)
    property = state->property();
  if (parent) 
    parent_property = parent->property();

  if (state && !state->network_manager()->socket()->is_open()) {
    return;
  }

  switch (ev.event_type) {
    case CV_ROUND_START: {
      // Reset K
      current_k_ = 2;

      CVDEBUG("CV_ROUND_START");
      // Delete the ExecutionTraceTree from the previous round
      if (DeleteOldTrees && !tree_list_.empty()) {

        //tree_list_.back()->print(*cv_debug_stream);
        delete tree_list_.back();
        tree_list_.pop_back();
      }

      // Reset training data list
      current_training_list_.clear();

      // Delete old trees associated with states from previous rounds
      clear_edit_distance_map();

      // Initialize a new ExecutionTraceTree
      tree_list_.push_back(new ExecutionTraceTree() );
    }
    break;

    case CV_BASICBLOCK_ENTRY: {
      //CVDEBUG("CV_BASIC_BLOCK_ENTRY");
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(property);

      // First BasicBlock Entry Event
      if (!tree_list_.back()->tracks(property)) {
        klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);

        TrainingObjectScoreList score_list;
        TrainingManager::init_score_list(training_data_, score_list);

        const SocketEvent* socket_event 
            = &(state->network_manager()->socket()->event());

        SocketEventSimilarityTetrinet measure;
        TrainingManager::sort_by_similarity_score(socket_event, score_list, measure);

        root_tree_ = new KEditDistanceExecutionTree();

        size_t i, max_count = 5;
        bool zero_match = true;
        for (i=0; i < score_list.size() && (zero_match|| i < max_count); ++i) {
          root_tree_->insert(score_list[i].second->trace);
          current_training_list_.push_back(score_list[i].second);
          double score = score_list[i].first;
          if (score > 0.0) zero_match = false;
        }

        root_tree_->init(current_k_);

        // Set initial values for edit distance
        edp->edit_distance = 0;
        edp->recompute = true;

        // Store size of tree in stats
        size_t element_count = root_tree_->element_count();

        CVDEBUG("Training object tree for round: "
            << cv_->round() << " has " << element_count
            << " elements from " << i+1 << " training objects");
        stats::edit_distance_tree_size += element_count; 

        edit_distance_map_[property] = 
            static_cast<KEditDistanceExecutionTree*>(root_tree_->clone());
      }

      if (state->basic_block_tracking() || !BasicBlockDisabling) {
        //if (edp->edit_distance > current_k_) {
        //  CVDEBUG("edit distance > current_k_: " << *state);
        //}

        {
          klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
          tree_list_.back()->extend_element(state->prevPC->kbb->id, property);
        }

        if (edp->recompute) {
          //edp->recompute = false;

          {
            klee::TimerStatIncrementer compute_timer(stats::edit_distance_compute_time);
            edit_distance_map_[property]->update_element(state->prevPC->kbb->id);
            edp->edit_distance = edit_distance_map_[property]->min_prefix_distance();
          }
        }
      }

    }
    break;

    case CV_STATE_REMOVED: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);
      CVDEBUG("Removing state: " << *state );
      tree_list_.back()->remove_tracker(property);
      if (edit_distance_map_.count(property)) {
        delete edit_distance_map_[property];
        edit_distance_map_.erase(property);
      }
    }
    break;

    case CV_STATE_CLONE: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      tree_list_.back()->clone_tracker(property, parent_property);
      
      EditDistanceProperty *edp 
        = static_cast<EditDistanceProperty*>(property);
      EditDistanceProperty *edp_parent
        = static_cast<EditDistanceProperty*>(parent_property);

      edp->recompute=true;
      edp_parent->recompute=true;

      edit_distance_map_[property] = 
          static_cast<KEditDistanceExecutionTree*>(edit_distance_map_[parent_property]->clone());

      edp->edit_distance = edit_distance_map_[property]->min_prefix_distance();
      CVDEBUG("Cloned state: " << *state << ", parent: " << *parent )
    }
    break;

    case CV_SOCKET_WRITE:
    case CV_SOCKET_READ: {
      klee::TimerStatIncrementer timer(stats::execution_tree_time);

      EditDistanceProperty *edp = static_cast<EditDistanceProperty*>(property);

      //current_k_ = 2;
      //while (edp->edit_distance == INT_MAX) {
      //  recompute_property(property);
      //  current_k_ *= 2;
      //}

      CVDEBUG("End of round, path length: " << tree_list_.back()->tracker_depth(property));
      CVMESSAGE("End state: " << *state);
      //CVDEBUG("FINAL [ ][" << etrace.size() << "] " << etrace);
    }
    break;

    case CV_SOCKET_SHUTDOWN: {
      CVDEBUG("Successful socket shutdown. " << *state);
    }
    break;

    default: {
    }
    break;
  }
}

bool KExtensionVerifyExecutionTreeManager::ready_process_all_states() {
  return current_k_ < MaxKExtension;
}

void KExtensionVerifyExecutionTreeManager::recompute_property(
    ExecutionStateProperty *property) {
  klee::TimerStatIncrementer compute_timer(stats::edit_distance_compute_time);

  assert(edit_distance_map_.count(property));
  edit_distance_map_[property]->init(current_k_);

  EditDistanceProperty *edp = static_cast<EditDistanceProperty*>(property);

  ExecutionTrace etrace;
  etrace.reserve(tree_list_.back()->tracker_depth(property));
  tree_list_.back()->tracker_get(property, etrace); 

  edit_distance_map_[property]->update(etrace);
  edp->edit_distance = edit_distance_map_[property]->min_prefix_distance();
}


void KExtensionVerifyExecutionTreeManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {
  CVMESSAGE("Doubling K from: " << current_k_ << " to " << current_k_*2);
  current_k_ = current_k_ * 2;
  for (unsigned i=0; i<states.size(); ++i) {
    EditDistanceProperty *edp = static_cast<EditDistanceProperty*>(states[i]);
    assert(edp->edit_distance == INT_MAX);

    int old_ed = edp->edit_distance;
    recompute_property(states[i]);
    //CVMESSAGE("Edit distance computed from: " << old_ed << " to " << edp->edit_distance);
  }
}

// Delete the trees associated with each state in the edit distance map
// and clear the map itself
void KExtensionVerifyExecutionTreeManager::clear_edit_distance_map() {
  KEditDistanceExecutionTreeMap::iterator it = edit_distance_map_.begin();
  KEditDistanceExecutionTreeMap::iterator ie = edit_distance_map_.end();
  for (; it!=ie; ++it) {
    delete it->second;
  }
  edit_distance_map_.clear();
  if (root_tree_ != NULL) {
    root_tree_->destroy_root();
    delete root_tree_;
    root_tree_ = NULL;
  }
}

} // end namespace cliver
