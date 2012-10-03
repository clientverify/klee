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
FilterTrainingUsage("filter-training-usage",llvm::cl::init(0));

llvm::cl::opt<unsigned>
MedoidCount("medoid-count",llvm::cl::init(1));

llvm::cl::opt<bool>
AggressiveNaive("aggressive-naive",llvm::cl::init(false));

llvm::cl::opt<bool>
DisableExecutionTraceTree("disable-et-tree",llvm::cl::init(false));

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
        if (state->basic_block_tracking() || !BasicBlockDisabling) {
          //klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
          stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
        }
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
      if (!DisableExecutionTraceTree) {
        new_stage->etrace_tree = new ExecutionTraceTree();
      }
      new_stage->root_property = parent_property;

      // Increment stat counter
      stats::stage_count += 1;

      if (!stages_.empty() && 
          stages_.count(parent_property) && 
          (DisableExecutionTraceTree || stages_[parent_property]->etrace_tree->tracks(parent_property))) {

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

    case CV_SELECT_EVENT: {
      CVDEBUG("SELECT_EVENT");
      property->is_recv_processing = false;
    }

    case CV_BASICBLOCK_ENTRY: {
      assert(stages_.count(property));

      if (FilterTrainingUsage > 0 && !property->is_recv_processing) {
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
  // 1 Loop over all training data, creating a TrainingFilter for each
  // and adding it to the vector of training objects in the TrainingObjectData

  // For each TrainingObjectData, initialize the editdistance matrix and fill
  // in the values by computing NxN edit distances

  // Later (randomly?) choose a training path, compute the edit distance,
  // then walk that msgs entries and eliminate those msgs that do not need
  // to be computed, marked via a sentinal value, chose another msg that
  // needs to be computed and continue

  //TrainingObject* tobj = NULL;
  //foreach (tobj, training_data_) {
  //  SocketEvent *se = NULL;
  //  TrainingObjectFilter *send_filter = NULL;
  //  TrainingObjectFilter *recv_filter = NULL;

  //  foreach (se, tobj->socket_event_set) {
  //    if (send_filter == NULL && se->type == SocketEvent::SEND)
  //      send_filter = new TrainingObjectFilter(se->type, tobj->trace[0]);
  //    if (recv_filter == NULL && se->type == SocketEvent::RECV)
  //      recv_filter = new TrainingObjectFilter(se->type, tobj->trace[0]);
  //  }
  //  assert(send_filter || recv_filter);

  //  if (send_filter) {
  //    if (filter_map_.count(send_filter) == 0) {
  //      TrainingObjectData* tobj_data = new TrainingObjectData();
  //      filter_map_[send_filter] = tobj_data;
  //    }
  //    filter_map_[send_filter]->training_objects.push_back(tobj);
  //    filter_map_[send_filter]->training_object_set.insert(tobj);
  //    delete send_filter;
  //  }

  //  if (recv_filter) {
  //    if (filter_map_.count(recv_filter) == 0) {
  //      filter_map_[recv_filter] = new TrainingObjectData();
  //    }
  //    filter_map_[recv_filter]->training_objects.push_back(tobj);
  //    filter_map_[recv_filter]->training_object_set.insert(tobj);
  //    delete recv_filter;
  //  }
  //}

  //foreach (TrainingFilterMap::value_type &d, filter_map_) {
  //  TrainingObjectData *tod = d.second;
  //  size_t matrix_size = tod->training_objects.size()*tod->training_objects.size();
  //  tod->edit_distance_matrix = new std::vector<int>(matrix_size, INT_MAX);

  //}

  cluster_manager_ = new TrainingObjectManager();
  std::vector<TrainingObject*> tobj_vec(training_data_.begin(), training_data_.end());
  cluster_manager_->cluster(8, tobj_vec);

  TrainingObject* tobj = NULL;
  foreach (tobj, training_data_) {
    SocketEvent *se = NULL;
    TrainingObjectFilter *send_filter = NULL;
    TrainingObjectFilter *recv_filter = NULL;

    foreach (se, tobj->socket_event_set) {
      if (send_filter == NULL && se->type == SocketEvent::SEND) {

        if (ClientModelFlag == XPilot) 
          send_filter = new TrainingObjectFilter(se->type, tobj->trace[0]);
        else
          send_filter = new TrainingObjectFilter(se->type, 0);
      }

      if (recv_filter == NULL && se->type == SocketEvent::RECV) {
        if (ClientModelFlag == XPilot) 
          recv_filter = new TrainingObjectFilter(se->type, tobj->trace[0]);
        else
          send_filter = new TrainingObjectFilter(se->type, 0);
      }
    }
    assert(send_filter || recv_filter);

    if (send_filter) {
      TrainingObjectData* tobj_data = NULL;

      if (filter_map_.count(*send_filter) == 0) {
        tobj_data = new TrainingObjectData();
        filter_map_[*send_filter] = tobj_data;
      } else {
        tobj_data = filter_map_[*send_filter];
      }

      tobj_data->training_objects.push_back(tobj);
      tobj_data->training_object_set.insert(tobj);
      tobj_data->message_count += tobj->socket_event_set.size();

      foreach (se, tobj->socket_event_set) {
        tobj_data->socket_events_by_size.push_back(se); 
        tobj_data->reverse_socket_event_map[se] = tobj;
      }

      delete send_filter;
    }

    if (recv_filter) {
      TrainingObjectData* tobj_data = NULL;

      if (filter_map_.count(*recv_filter) == 0) {
        tobj_data = new TrainingObjectData();
        filter_map_[*recv_filter] = tobj_data;
      } else {
        tobj_data = filter_map_[*recv_filter];
      }

      tobj_data->training_objects.push_back(tobj);
      tobj_data->training_object_set.insert(tobj);
      tobj_data->message_count += tobj->socket_event_set.size();

      foreach (se, tobj->socket_event_set) {
        tobj_data->socket_events_by_size.push_back(se); 
        tobj_data->reverse_socket_event_map[se] = tobj;
      }

      delete recv_filter;
    }
  }

  size_t total_count = 0;
  size_t count = 0;
  foreach (TrainingFilterMap::value_type &d, filter_map_) {
    TrainingObjectData *tod = d.second;
    total_count += ((tod->message_count*tod->message_count)/2);
  }
  llvm::sys::TimeValue start_now(0,0),start_user(0,0),sys(0,0);
  llvm::sys::Process::GetTimeUsage(start_now,start_user,sys);

  CVMESSAGE("Message edit distance matrix computation with "
            << omp_get_max_threads() << " threads"
            << ", matrix has " << total_count << " elements.");

  int tod_count = 0;
  foreach (TrainingFilterMap::value_type &d, filter_map_) {
    TrainingObjectData *tod = d.second;
    tod_count++;

    SocketEventSizeLT comp;
    std::sort(tod->socket_events_by_size.begin(), tod->socket_events_by_size.end(), comp);
    size_t matrix_size = tod->message_count*tod->message_count;

    //if (tod->training_objects.size() <= 3 || ClientModelFlag != XPilot) {
    if (tod->training_objects.size() <= 3) {

      CVMESSAGE("Not computing " << matrix_size/2
                << " scores between training messages, because " << tod->training_objects.size()
                << " paths is <= 3");
      count += (tod->message_count*tod->message_count)/2;
    } else {

      //tod->edit_distance_matrix 
      //    = new std::vector<int>(tod->message_count*tod->message_count, -1);
      tod->edit_distance_matrix = (int*)malloc(sizeof(int)*matrix_size);

      CVMESSAGE("Computing " << matrix_size/2
                << " scores between training messages for " << tod->training_objects.size()
                << " paths.");

      for (unsigned i = 0; i < tod->message_count; ++i) {
        SocketEvent *se_i = tod->socket_events_by_size[i];
        tod->socket_event_indices[se_i] = i;
      }

      double previous_percent_done = 0.0;
      #pragma omp parallel for schedule(dynamic)
      for (unsigned i = 0; i < tod->message_count; ++i) {
        SocketEvent *se_i = tod->socket_events_by_size[i];

        unsigned row = i*tod->message_count;
        for (unsigned j = 0; j < i; ++j) {
          SocketEvent *se_j = tod->socket_events_by_size[j];
          int score = similarity_measure_->similarity_score(se_i, se_j);
          tod->edit_distance_matrix[row + j] = score;
        }
      }

      count += (tod->message_count*tod->message_count)/2;
      //count += i;

      double percent_done = 
          ((double)(count))/((double)(total_count));
          //((double)(count))/((double)(tod->edit_distance_matrix->size()));
      
      if ((percent_done - previous_percent_done) > 0.001f) {
        llvm::sys::TimeValue curr_now(0,0),curr_user(0,0);
        llvm::sys::Process::GetTimeUsage(curr_now,curr_user,sys);
        llvm::sys::TimeValue delta = curr_user - start_user;
        CVMESSAGE(percent_done * 100 
                  << "% completed in " << delta.usec() / 1000000 << " (s), "
                  << "est. time remaining is " 
                  << ((delta.usec() / 1000000)/percent_done)-(delta.usec() / 1000000) 
                  << " (s)");
        previous_percent_done = percent_done;
      }

      unsigned j_max = tod->message_count;
      #pragma omp parallel for schedule(dynamic)
      for (unsigned i = 0; i < tod->message_count; ++i) {
        unsigned row = i*tod->message_count;
        for (unsigned j = i; j < j_max; ++j) {
          tod->edit_distance_matrix[row + j]
            = tod->edit_distance_matrix[j*tod->message_count + i];
        }
      }
      //count += (tod->message_count*tod->message_count)/2;


      std::stringstream name_ss;
      name_ss << "training_ed_matrix_" << tod_count << "_" << matrix_size << ".mat";
      std::string name = std::string(name_ss.str());
      std::ostream *file = cv_->openOutputFile(name);
      file->write( reinterpret_cast<char*>(&tod_count), sizeof(tod_count) );
      file->write( reinterpret_cast<char*>(&matrix_size), sizeof(matrix_size) );
      file->write( reinterpret_cast<char*>(tod->edit_distance_matrix), sizeof(int)*matrix_size );
      static_cast<std::ofstream*>(file)->close();
    }
  }
}

void VerifyExecutionTraceManager::update_edit_distance(
    ExecutionStateProperty* property) {

  //klee::TimerStatIncrementer edct(stats::edit_distance_compute_time);

  ExecutionStage* stage = stages_[property];
  assert(stage);

  //if (stage->current_k >= MaxKExtension) {
  if (property->edit_distance == INT_MAX) {
    return;
  }

  //if (!EditDistanceAtCloneOnly) {
  //  stage->ed_tree_map[property]->update_element(
  //      stage->etrace_tree->leaf_element(property));
  //} else {
  //  ExecutionTrace etrace;
  //  stage->etrace_tree->tracker_get(property, etrace);
  //  stage->ed_tree_map[property]->update(etrace);
  //}


  if (stage->ed_tree_map.count(property) == 0) {
    if (stage->root_ed_tree != NULL) {
      stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
    } else {
      return;
    }
  }

  ExecutionTrace etrace;
  stage->etrace_tree->tracker_get(property, etrace);

  stage->ed_tree_map[property]->update(etrace);

  //int row = stage->ed_tree_map[property]->row();

  //// XXX FIX ME -- make this operation O(1)
  //int trace_depth = stage->etrace_tree->tracker_depth(property);

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

      klee::TimerStatIncrementer speds(stats::self_path_edit_distance);

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
      if (match_count > 1) {
        CVMESSAGE("match_count > 1 : " << match_count);
      }

      {
        TrainingObjectScoreList sorted_clusters;
        cluster_manager_->sorted_clusters(socket_event, tf,
                                          sorted_clusters, *similarity_measure_);

        // Store size of tree in stats
        stats::edit_distance_tree_size = sorted_clusters.size(); 

        std::stringstream ss;
        for (size_t i=0; i < sorted_clusters.size(); ++i) {
          if (match_count == 0) {
            stage->root_ed_tree->add_data(sorted_clusters[i].second->trace);
          }
          ss << sorted_clusters[i].first << ",";
        }
        CVMESSAGE("Original Cluster Distances: " << ss.str());
      }


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
      }
    } else {
      stage->root_ed_tree = NULL;
      return;
    }
  }
  else if (UseClustering) {

    TrainingFilter tf(state);

    if (cluster_manager_->check_filter(tf)) {

      TrainingObjectScoreList sorted_clusters;
      cluster_manager_->sorted_clusters(socket_event, tf,
                                        sorted_clusters, *similarity_measure_);

      // Store size of tree in stats
      stats::edit_distance_tree_size = sorted_clusters.size(); 

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      std::stringstream ss;
      for (size_t i=0; i < sorted_clusters.size(); ++i) {
        stage->root_ed_tree->add_data(sorted_clusters[i].second->trace);
        ss << sorted_clusters[i].first << ",";
      }
      CVMESSAGE("Cluster Distances: " << ss.str());

    } else {
      stage->root_ed_tree = NULL;
      return;
    }

  } else {
  
    TrainingObjectFilter filter;

    if (ClientModelFlag == XPilot) {
      filter = TrainingObjectFilter(socket_event->type, state->prevPC->kbb->id);
    } else {
      filter = TrainingObjectFilter(socket_event->type, 0);
    }

    if ((AggressiveNaive && socket_event->type == SocketEvent::RECV) 
        || filter_map_.count(filter) == 0) {
    //if (filter_map_.count(filter) == 0) {
      CVMESSAGE("Filter not found! naive search");
      stage->root_ed_tree = NULL;
      return;

    } else {

      CVMESSAGE("Filter found with " 
                << filter_map_[filter]->message_count << " messages, and "
                << filter_map_[filter]->training_objects.size() << " paths");

      std::set<TrainingObject*> selected;
      std::vector<int> scores;

      int radius = 0;
      if (ClientModelFlag == XPilot)
        radius = 1;

      while (selected.empty()) {
        filter_map_[filter]->select_training_paths_for_message(socket_event, radius,
                                                              similarity_measure_,
                                                              scores, selected);
        CVMESSAGE("Selected " << selected.size() << " paths with radius " << radius);
        if (selected.empty()) {
          if (ClientModelFlag == XPilot) {
            radius *= 2;
          } else {
            radius += 1;
          }
        }
      }

      if (ClientModelFlag == XPilot) {
        if (selected.size() > 5) {
          radius /= 2;
          scores = std::vector<int>();
          selected = std::set<TrainingObject*>();
          while (selected.empty()) {
            filter_map_[filter]->select_training_paths_for_message(socket_event, radius,
                                                                  similarity_measure_,
                                                                  scores, selected);
            CVMESSAGE("Re-selected " << selected.size() << " paths with radius " << radius);
            if (selected.empty())
              radius++;
          }
        }
      }

      // Store size of tree in stats
      stats::edit_distance_tree_size = selected.size(); 

      // Create a new root edit distance
      stage->root_ed_tree = EditDistanceTreeFactory::create();

      foreach (TrainingObject *tobj, selected) {
        stage->root_ed_tree->add_data(tobj->trace);
      }
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

      if (is_socket_active) {

        if (FilterTrainingUsage > 0 && !property->is_recv_processing) {

          // Check if this is the first basic block of the stage
          if (!stage->etrace_tree->tracks(property)) {
            assert(stage->etrace_tree->element_count() == 0);
            CVDEBUG("First basic block entry (stage)");
            //klee::TimerStatIncrementer build_timer(stats::edit_distance_build_time);
            //klee::TimerStatIncrementer training_timer(stats::training_time);
            
            // Build the edit distance tree using training data
            create_ed_tree(state);
          }

          // Check if we need to reclone the edit distance tree 
          if (stage->ed_tree_map.count(property) == 0 && stage->root_ed_tree) {
            stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
          }
        }
      }


      if (FilterTrainingUsage > 0 && !property->is_recv_processing) {
        if (state->basic_block_tracking() || !BasicBlockDisabling) {
          klee::TimerStatIncrementer timer(stats::execution_tree_time);
          {
            //klee::TimerStatIncrementer extend_timer(stats::execution_tree_extend_time);
            stage->etrace_tree->extend_element(state->prevPC->kbb->id, property);
          }

          if (is_socket_active) {
            if (property->recompute) {
              if (EditDistanceAtCloneOnly || property->round < 5) {
                property->recompute = false;
              }
              update_edit_distance(property);
            }
          }
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
        //assert(stage->ed_tree_map.count(property));
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

      if (FilterTrainingUsage > 0 && !property->is_recv_processing) {
        if (EditDistanceAtCloneOnly || property->round < 5)
          update_edit_distance(parent_property);
      }

      stages_[property] = stage;

      stage->etrace_tree->clone_tracker(property, parent_property);
      
      property->recompute=true;
      parent_property->recompute=true;

      if (is_socket_active) {
        if (FilterTrainingUsage > 0 && !property->is_recv_processing) {
          //assert(stage->ed_tree_map.count(parent_property));

          if (stage->ed_tree_map.count(property) && stage->root_ed_tree) {
            stage->ed_tree_map[property] = 
                stage->ed_tree_map[parent_property]->clone_edit_distance_tree();
            property->edit_distance = stage->ed_tree_map[property]->min_distance();
          } else {
            property->edit_distance = parent_property->edit_distance;
          }

        } else {
          property->edit_distance = parent_property->edit_distance;

        }
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

  if (stage->ed_tree_map.count(property) == 0) {
    stage->ed_tree_map[property] = stage->root_ed_tree->clone_edit_distance_tree();
  }

  stage->ed_tree_map[property]->init(stage->current_k);

  ExecutionTrace etrace;
  etrace.reserve(stage->etrace_tree->tracker_depth(property));
  stage->etrace_tree->tracker_get(property, etrace);
  stage->ed_tree_map[property]->update(etrace);
  property->edit_distance = stage->ed_tree_map[property]->min_distance();
}

void VerifyExecutionTraceManager::process_all_states(
    std::vector<ExecutionStateProperty*> &states) {
  klee::TimerStatIncrementer timer(stats::execution_tree_time);
  klee::TimerStatIncrementer edct(stats::edit_distance_compute_time);

  {
    assert(!states.empty());
    assert(stages_.count(states[0]));
    ExecutionStage* stage = stages_[states[0]];
    CVMESSAGE("Doubling K from: " << stage->current_k 
              << " to " << stage->current_k*2);

    stage->current_k = stage->current_k * 2;

    stats::edit_distance_final_k = stage->current_k;
  }

  CVDEBUG("All states should have INT_MAX=" << INT_MAX << " edit distance.");
  for (unsigned i=0; i<states.size(); ++i) {
    if (states[i]->is_recv_processing) {
      CVMESSAGE("Not recompting recv_processing state!");
    } else {
      //assert(states[i]->edit_distance == INT_MAX);
      int old_ed = states[i]->edit_distance;
      recompute_property(states[i]);
      CVDEBUG("Edit distance computed from: " << old_ed 
                << " to " << states[i]->edit_distance);
    }
  }
  CVMESSAGE("Done recomputing.");
            
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
