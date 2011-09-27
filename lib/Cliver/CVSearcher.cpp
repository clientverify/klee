//===-- CVSearcher.cpp ------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CVCommon.h"
#include "CVSearcher.h"
#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "StateMerger.h"
#include "ClientVerifier.h"
#include "NetworkManager.h"
#include "PathManager.h"
#include "PathSelector.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "llvm/Support/raw_ostream.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugSearcher, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugSearcher, __state_id, __x)

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

CVSearcher::CVSearcher(klee::Searcher* base_searcher, StateMerger* merger) 
	: base_searcher_(base_searcher), merger_(merger) {
}

klee::ExecutionState &CVSearcher::selectState() {
	return base_searcher_->selectState();
}

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {
	base_searcher_->update(current, addedStates, removedStates);
}

bool CVSearcher::empty() {
	return base_searcher_->empty();
}

void CVSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

void CVSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

////////////////////////////////////////////////////////////////////////////////

LogIndexSearcher::LogIndexSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger) {}

bool LogIndexSearcher::empty() {
	return state_count() == 0;
}

int LogIndexSearcher::state_count() {
	int count = 0;
	ExecutionStatePropertyMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;
		count += state_set.size();
	}
	return count;
}

klee::ExecutionState &LogIndexSearcher::selectState() {

	// Walk the ExecutionStatePropertyMap from the oldest to the newest,
	// or whatever ordering the ExecutionStateProperty induces
	// and select a state from the earliest grouping
	ExecutionStatePropertyMap::iterator it, ie;
	for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
		ExecutionStateSet &state_set = it->second;

		if (!state_set.empty()) {

			// Erase all of the empty sets up to the current one
			if (states_.begin() != it) {
				ExecutionStatePropertyMap::iterator erase_it=states_.begin();
				while (erase_it != it) {
					ExecutionStateProperty* p = erase_it->first;
					states_.erase(erase_it++);
					delete p;
				}
				//states_.erase(states_.begin(), it);

				// Merge each group of states
				ExecutionStatePropertyMap::iterator merge_it = it;
				for (; merge_it!=ie; ++merge_it) {
					ExecutionStateSet &merge_state_set = merge_it->second;
					ExecutionStateSet result;
					merger_->merge(merge_state_set, result);

					// Update stats
					stats::merged_states += merge_state_set.size() - result.size();
					stats::active_states += result.size();

					// Print new stats
					g_client_verifier->print_current_statistics();

					// swap state sets
					merge_state_set.swap(result);
				}
			}

			// Select the first state in this set, cast the CVExecutionState
			// pointer into a klee::ExecutionState ptr, then dereference
			return *(static_cast<klee::ExecutionState*>(*(state_set.begin())));
		}
	}
	cv_error("no states remaining");

	// This will never execute after cv_error
	klee::ExecutionState *null_state = NULL;
	return *null_state;
}

void LogIndexSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {

	std::set<klee::ExecutionState*> removed_states(removedStates);
	std::set<klee::ExecutionState*> added_states(addedStates);

	if (current && removedStates.count(current) == 0) {
		removed_states.insert(current);
		added_states.insert(current);
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		// Iterate over all the ExecutionState groups to determine
		// if they contain state, we can't look up the state via it's
		// ExecutionStateProperty because it might have changed since insertion
		ExecutionStatePropertyMap::iterator it, ie;
		for (it=states_.begin(), ie=states_.end(); it!=ie; ++it) {
			ExecutionStateSet &state_set = it->second;
			if (state_set.count(state)) {
				state_set.erase(state);
				break;
			}
		}
		if (it == ie) 
			cv_error("LogIndexSearcher could not erase state!");
	}

	// Insert the added states into the associated group
	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		ExecutionStatePropertyMap::iterator it = states_.find(state->property());
		if (it != states_.end()) {
			(it->second).insert(state);
		} else {
			ExecutionStateProperty* p = state->property()->clone();
			states_[p].insert(state);
		}
	}
}

void LogIndexSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

void LogIndexSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	LogIndexProperty* p = static_cast<LogIndexProperty*>(state->property());
	if (et == CliverEvent::NetworkSend || et == CliverEvent::NetworkRecv) {
		p->socket_log_index = state->network_manager()->socket_log_index();
	}
}

////////////////////////////////////////////////////////////////////////////////

TrainingSearcher::TrainingSearcher(klee::Searcher* base_searcher, 
		StateMerger* merger) 
	: CVSearcher(base_searcher, merger), paths_(new PathManagerSet()) {}

klee::ExecutionState &TrainingSearcher::selectState() {

	if (!phases_[PathProperty::Execute].empty()) {
		CVExecutionState* state = *(phases_[PathProperty::Execute].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::PrepareExecute].empty()) {
		// Print stats
		g_client_verifier->print_current_statistics();
		CVMESSAGE("Current Paths (" << paths_->size() << ")");
		foreach(PathManager* path, *paths_) {
			CVDEBUG(*path);
		}
		CVMESSAGE("Current States (" 
				<< phases_[PathProperty::PrepareExecute].size() << ")");

		ExecutionStateSet to_merge(phases_[PathProperty::PrepareExecute]);

		ExecutionStateSet result;
		if (!to_merge.empty())
			merger_->merge(to_merge, result);
		
		CVMESSAGE("Current states after mergin' (" << result.size() << ")");

		phases_[PathProperty::PrepareExecute].clear();

	  foreach (CVExecutionState* state, result) {
			PathProperty *p = static_cast<PathProperty*>(state->property());
			p->phase = PathProperty::Execute;
			p->path_range = PathRange(state->prevPC, NULL);
			state->reset_path_manager();

			CVDEBUG_S(state->id(), "Preparing Execution in " << *p 
					<< " in round " << p->round << " " << *state->prevPC);
		}

		foreach (CVExecutionState* state, result) {
			phases_[PathProperty::Execute].insert(state);
		}

		return selectState();
	}

	cv_error("no states remaining");

	// XXX better error handling
	// This will never execute after cv_error
	klee::ExecutionState *null_state = NULL;
	return *null_state;
}

void TrainingSearcher::update(klee::ExecutionState *current,
		const std::set<klee::ExecutionState*> &addedStates,
		const std::set<klee::ExecutionState*> &removedStates) {

	std::set<klee::ExecutionState*> removed_states(removedStates);
	std::set<klee::ExecutionState*> added_states(addedStates);

	if (current && removedStates.count(current) == 0) {
		removed_states.insert(current);
		added_states.insert(current);
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndState; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndState) {
				cv_error("state erase failed");
			}
		} else {
			state_set.erase(state);
		}
	}

	foreach (klee::ExecutionState* klee_state, added_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];
		state_set.insert(state);
	}
}

bool TrainingSearcher::empty() {
	if (phases_[PathProperty::NetworkClone].empty() &&
			phases_[PathProperty::Execute].empty() &&
			phases_[PathProperty::PrepareExecute].empty()) {
		return true;
	}
	return false;
}

void TrainingSearcher::record_path(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->path_range = PathRange(p->path_range.start(), state->prevPC);
	state->path_manager()->set_range(p->path_range);
	if (et == CliverEvent::NetworkSend ||
			et == CliverEvent::NetworkRecv) {
		if (Socket* s = state->network_manager()->socket()) {
			const SocketEvent* se = &s->previous_event();
			static_cast<TrainingPathManager*>(
					state->path_manager())->add_socket_event(se);
		} else {
			cv_error("No message in state");
		}
	} else {
		cv_error("invalid cliver event for recording path");
	}
 
	CVDEBUG_S(state->id(), "Recording path (length "
			<< state->path_manager()->length() << ") "<< *p 
			<< " [Start " << *p->path_range.kinsts().first << "]"
			<< " [End "   << *p->path_range.kinsts().second << "]");

	// Write to file (pathstart, pathend, path, message)...
	std::stringstream filename;
	filename << "state_" << state->id() 
		<< "-round_" << p->round 
		<< "-length_" << state->path_manager()->length() 
		<< ".tpath";
	std::ostream *file = g_client_verifier->openOutputFile(filename.str());
	static_cast<TrainingPathManager*>(state->path_manager())->write(*file);
	delete file;
		
	if (paths_->contains(state->path_manager())) {
		if (PathManager* mpath = paths_->merge(state->path_manager())) {
			CVDEBUG_S(state->id(), "Adding new message, mcount is now "
					<< static_cast<TrainingPathManager*>(mpath)->socket_events().size());
		} else {
			CVDEBUG_S(state->id(), "Path already contains message");
		}
	} else {
		if (paths_->insert(state->path_manager()->clone())) {
			CVDEBUG_S(state->id(), "Adding new path, pcount is now " 
					<< paths_->size());
		} else {
			cv_error("error adding new path");
		}
	}
	
	// update for next path
	p->phase = PathProperty::PrepareExecute;
	p->round++;
}


void TrainingSearcher::handle_post_event(CVExecutionState *state,
		CVExecutor *executor, CliverEvent::Type et) {

	PathProperty* p = static_cast<PathProperty*>(state->property());
	TrainingSearcher* searcher 
		= static_cast<TrainingSearcher*>(g_client_verifier->searcher());

	switch(et) {
		case CliverEvent::NetworkSend:
		case CliverEvent::NetworkRecv:
			searcher->record_path(state, executor, et);
			break;
		default:
			break;
	}
}

void TrainingSearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

////////////////////////////////////////////////////////////////////////////////

VerifyStage::VerifyStage(PathSelector *path_selector, 
		const SocketEvent* socket_event, VerifyStage* parent)
	: root_state_(NULL),
	  path_selector_(path_selector->clone()),
		socket_event_(socket_event), // XXX needed?
		network_event_index_(0), /// XXX needed?
		parent_(parent),
		search_mode(FullTraining) {

	if (parent_ == NULL) {
		network_event_index_ = 0;
	} else {
		network_event_index_ = parent_->network_event_index_+1;
	}
}

CVExecutionState* VerifyStage::next_state() {
	if (states_.size() == 0) {
		if (search_mode == Exhaustive) return NULL;
		assert(root_state_);
		PathRange range(root_state_->prevPC, NULL);
		CVExecutionState *state = root_state_->clone();
		g_executor->add_state(state);

		if (search_mode == FullTraining) {
			if (PathManager* path_manager = path_selector_->next_path(range)) {
				state->reset_path_manager();
				state->path_manager()->set_path(path_manager->path());
				state->path_manager()->set_range(path_manager->range());
			} else {
				search_mode = PrefixTraining;
				PathSelector *old_path_selector = path_selector_;
				path_selector_ = path_selector_->clone();
				delete old_path_selector;
			}
		}

		if (search_mode == PrefixTraining) {
			if (PathManager* path_manager = path_selector_->next_path(range)) {
				state->reset_path_manager(new VerifyPrefixPathManager());
				state->path_manager()->set_path(path_manager->path());
				state->path_manager()->set_range(path_manager->range());
			} else {
				search_mode = Exhaustive;
			}
		}

		if (search_mode == Exhaustive) {
			state->reset_path_manager(new PathManager(new Path));
		}

		states_.insert(state);
		return state;
	}
	return *(states_.begin());
}

void VerifyStage::add_state(CVExecutionState *state) {
	if (root_state_ == NULL) {
		assert(states_.size() == 0);
		root_state_ = state;
	} else {
		states_.insert(state);
	}
}

void VerifyStage::remove_state(CVExecutionState *state) {
	assert(state != root_state_ && "unexpected state removal");
	assert(!(states_.count(state) && finished_states_.count(state)));
	states_.erase(state);
	if (finished_states_.count(state)) {
		CVDEBUG_S(state->id(), "VerifyStage::removing finished state");
		finished_states_.erase(state);
	}
}

void VerifyStage::finish(CVExecutionState *finished_state) {

	// XXX attempt to merge?
	finished_states_.insert(finished_state);

	CVExecutionState* state = finished_state->clone();
	g_executor->add_state(state);

	PathProperty *p = static_cast<PathProperty*>(state->property());
	p->path_range = PathRange(p->path_range.start(), state->prevPC);

	CVDEBUG("end_path: " << state->path_manager()->range()
			<< ", " << p->path_range);
 
	//CVDEBUG_S(state->id(), "A: End path (length "
	//		<< state->path_manager()->length() << ") "<< *p 
	//		<< " [Start " << *p->path_range.kinsts().first << "]"
	//		<< " [End "   << *p->path_range.kinsts().second << "]");
 
	//CVDEBUG_S(state->id(), "B: End path (length "
	//		<< state->path_manager()->length() << ") "<< *p 
	//		<< " [Start " << *state->path_manager()->range().kinsts().first << "]"
	//		<< " [End "   << *state->path_manager()->range().kinsts().second << "]");

	//assert(state->path_manager()->range().equal(p->path_range));

	p->phase = PathProperty::PrepareExecute;
	p->round++;
	const SocketEvent &se = state->network_manager()->socket()->event();

	VerifyStage *child_stage = new VerifyStage(path_selector_, &se, this);
	child_stage->root_state_ = state;
	children_.push_back(child_stage);
}

VerifySearcher::VerifySearcher(klee::Searcher* base_searcher, 
		StateMerger* merger, PathManagerSet *paths) 
	: CVSearcher(base_searcher, merger), 
	  path_selector_(PathSelectorFactory::create(paths)) {

	//const SocketEvent &se = state->network_manager()->socket()->event();
	root_stage_ = current_stage_ = new VerifyStage(path_selector_, NULL, NULL);
			
}

klee::ExecutionState &VerifySearcher::selectState() {

	if (current_stage_->children().size() > 0) {
		current_stage_ = current_stage_->children().back();
		g_client_verifier->print_current_statistics();
	}

	CVExecutionState *state = current_stage_->next_state();

	if (state == NULL)
		cv_error("Null State");

	return *(static_cast<klee::ExecutionState*>(state));

	//if (!phases_[PathProperty::PrepareExecute].empty()) {
	//	// Print stats
	//	g_client_verifier->print_current_statistics();
	//	CVMESSAGE("Current States (" 
	//			<< phases_[PathProperty::PrepareExecute].size() << ")");

	//	// Attempt to merge states
	//	ExecutionStateSet to_merge(phases_[PathProperty::PrepareExecute]);
	//	ExecutionStateSet merged_states;
	//	if (!to_merge.empty())
	//		merger_->merge(to_merge, merged_states);
	//	CVMESSAGE("Current states after mergin' (" << merged_states.size() << ")");
	//	phases_[PathProperty::PrepareExecute].clear();

	//	// Process merged states
	//	//PathProperty *map_property = new PathProperty();
	//  //CVExecutionState* state = NULL;
	//	//PathManager* pm = NULL;
	//  //foreach (state, merged_states) {
	//	//	PathProperty *p = static_cast<PathProperty*>(state->property());
	//	//	p->phase = PathProperty::Execute;
	//	//	p->path_range = PathRange(state->prevPC, NULL);
	//	//	state->reset_path_manager();
	//	//	const SocketEvent &se = state->network_manager()->socket()->event();
	//
	//	//	// Clone a new state for each path in our PathManagerSet (paths_) that
	//	//	// starts at the same current PC instruction. We naively clone 
	//	//	// a state for each message.
	//	//	foreach (pm, *paths_) {
	//	//		if (pm->range().start() == p->path_range.start()) {
	//	//			const SocketEvent* other_se = NULL;
	//	//			foreach (other_se, pm->messages()) {
	//	//				if (other_se->equal(se)) {
	//	//					CVDEBUG("Cloning state.");
	//	//					CVExecutionState* cloned_state = state->clone();
	//	//					cloned_state->path_manager()->set_range(pm->range());
	//	//					cloned_state->path_manager()->set_path(pm->path());
	//	//					phases_[PathProperty::Execute].insert(cloned_state);
	//	//					g_executor->add_state(cloned_state);
	//	//				}
	//	//			}
	//	//		}
	//	//	}
	//	//	pm = NULL;

	//	//	CVDEBUG_S(state->id(), "Preparing Execution in " << *p 
	//	//			<< " " << *state->prevPC);
	//	//}

	//	CVMESSAGE("Ready States (" 
	//			<< phases_[PathProperty::Execute].size() << ")");

	//	return selectState();
	//}

	//cv_error("no states remaining");

	//// XXX better error handling
	//// This will never execute after cv_error
	//klee::ExecutionState *null_state = NULL;
	//return *null_state;

}

void VerifySearcher::update(klee::ExecutionState *current,
		const std::set<klee::ExecutionState*> &addedStates,
		const std::set<klee::ExecutionState*> &removedStates) {

	// add any added states via current_stage_->add_state()
	foreach (klee::ExecutionState* klee_state, addedStates) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		current_stage_->add_state(state);
	}
	// remove any removed states via current_stage_->remove_state()
	foreach (klee::ExecutionState* klee_state, removedStates) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		current_stage_->remove_state(state);
	}
	//std::set<klee::ExecutionState*> removed_states(removedStates);
	//std::set<klee::ExecutionState*> added_states(addedStates);

	//if (current && removedStates.count(current) == 0) {
	//	removed_states.insert(current);
	//	added_states.insert(current);
	//}

	//foreach (klee::ExecutionState* klee_state, removed_states) {
	//	CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
	//	PathProperty *p = static_cast<PathProperty*>(state->property());
	//	ExecutionStateSet &state_set = phases_[p->phase];

	//	if (state_set.count(state) == 0) {
	//		unsigned i;
	//		for (i=0; i < PathProperty::EndState; i++) {
	//			if (phases_[i].count(state) != 0) {
	//				phases_[i].erase(state);
	//				break;
	//			}
	//		}
	//		if (i == PathProperty::EndState) {
	//			cv_error("state erase failed");
	//		}
	//	} else {
	//		state_set.erase(state);
	//	}
	//}

	//foreach (klee::ExecutionState* klee_state, added_states) {
	//	CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
	//	PathProperty *p = static_cast<PathProperty*>(state->property());
	//	ExecutionStateSet &state_set = phases_[p->phase];
	//	state_set.insert(state);
	//}

}

bool VerifySearcher::empty() {
	//if (phases_[PathProperty::Execute].empty() &&
	//		phases_[PathProperty::PrepareExecute].empty()) {
	//	return true;
	//}
	// XXX fix me!
	return false;
}

void VerifySearcher::end_path(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {
	current_stage_->finish(state);
}

void VerifySearcher::handle_post_event(CVExecutionState *state,
		CVExecutor *executor, CliverEvent::Type et) {

	PathProperty* p = static_cast<PathProperty*>(state->property());
	VerifySearcher* searcher 
		= static_cast<VerifySearcher*>(g_client_verifier->searcher());

	switch(et) {
		case CliverEvent::NetworkSend:
		case CliverEvent::NetworkRecv:
			searcher->end_path(state, executor, et);
			break;
		default:
			break;
	}
}

void VerifySearcher::handle_pre_event(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {}

} // end namespace cliver
