//===-- PathSelector.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/PathSelector.h"
#include "cliver/CVStream.h"
#include "cliver/Path.h"
#include "cliver/PathManager.h"
#include "CVCommon.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

std::vector<PathManager*> g_ordered_training_paths;

OrderedSetPathSelector::OrderedSetPathSelector() : index_(0) {}

PathManager* OrderedSetPathSelector::next_path(const PathRange &range) {
	while (index_ < g_ordered_training_paths.size()) {
		PathManager *path_manager = g_ordered_training_paths[index_++];
		if (path_manager->range().start() == range.start()) {
			//CVDEBUG("selected next path");
			return path_manager;
		}
		//CVDEBUG("start instructions do not match");
	}
	CVDEBUG("no remaining paths");
	return NULL;
}

PathSelector* OrderedSetPathSelector::clone() {
	return new OrderedSetPathSelector();
}

////////////////////////////////////////////////////////////////////////////////

PathSelector* PathSelectorFactory::create(PathManagerSet* training_paths) {
  SocketEventDataSet socket_events;
  std::set<PathManager*> worklist(training_paths->begin(),
      training_paths->end());

  // Recursively select path that covers the largest number of messages
  // and add to g_ordered_trainin_paths, then remove covered messages from
  // the rest of the paths and repeat.
  while (worklist.size() > 0) {
    TrainingPathManager* max_size_tpm = NULL;
    PathManager *pm;
    foreach (pm, worklist) {
      TrainingPathManager* tpm = static_cast<TrainingPathManager*>(pm);
      if (max_size_tpm == NULL || 
          tpm->socket_events().size() > max_size_tpm->socket_events().size()) {
        max_size_tpm = tpm;
      }
    }
    worklist.erase(static_cast<PathManager*>(max_size_tpm));

    // HACK don't clone so that Path will not have a parent...
    TrainingPathManager *new_tpm 
      = new TrainingPathManager(const_cast<Path*>(max_size_tpm->path()), max_size_tpm->range());
    foreach (SocketEvent *se, max_size_tpm->socket_events()) {
      new_tpm->add_socket_event(se);
    }
    g_ordered_training_paths.push_back(new_tpm);

    foreach (pm, worklist) {
      TrainingPathManager* tpm = static_cast<TrainingPathManager*>(pm);
      foreach (SocketEvent *se, new_tpm->socket_events()) {
        tpm->erase_socket_event(se);
      }
    }
  }
}

} // end namespace cliver
