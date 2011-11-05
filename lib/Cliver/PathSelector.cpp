//===-- PathSelector.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathSelector.h"
#include "Path.h"
#include "PathManager.h"
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
  switch (g_cliver_mode) {
		case VerifyWithTrainingPaths: 
		{
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


				//VerifyPathManager *new_vpm = new VerifyPathManager();
				//new_vpm->set_path(max_size_tpm->path());
				//new_vpm->set_range(max_size_tpm->range());
				//foreach (SocketEvent *se, max_size_tpm->socket_events()) {
				//	new_vpm->add_socket_event(se);
				//}
				//g_ordered_training_paths.push_back(new_vpm);
				
				TrainingPathManager *new_tpm 
					= static_cast<TrainingPathManager*>(max_size_tpm->clone());
				g_ordered_training_paths.push_back(new_tpm);

				foreach (pm, worklist) {
					TrainingPathManager* tpm = static_cast<TrainingPathManager*>(pm);
					//foreach (SocketEvent *se, new_vpm->socket_events()) {
					foreach (SocketEvent *se, new_tpm->socket_events()) {
						tpm->erase_socket_event(se);
					}
				}
			}

			return new OrderedSetPathSelector();
		}
		case DefaultMode:
		case DefaultTrainingMode:
		default:
			break;
  }
	cv_error("PathSelector only used in verification modes.");
}

} // end namespace cliver
