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
#include "PathTree.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "llvm/Support/raw_ostream.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
DebugSearcher("debug-searcher",llvm::cl::init(false));

llvm::cl::opt<bool>
PrintTrainingPaths("print-training-paths",llvm::cl::init(false));

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
	//klee::TimerStatIncrementer timer(stats::searcher_time);
	return base_searcher_->selectState();
}

void CVSearcher::update(klee::ExecutionState *current,
						const std::set<klee::ExecutionState*> &addedStates,
						const std::set<klee::ExecutionState*> &removedStates) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);
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
	//klee::TimerStatIncrementer timer(stats::searcher_time);

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
	//klee::TimerStatIncrementer timer(stats::searcher_time);

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
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	if (!phases_[PathProperty::Execute].empty()) {
		CVExecutionState* state = *(phases_[PathProperty::Execute].begin());
		return *(static_cast<klee::ExecutionState*>(state));
	}

	if (!phases_[PathProperty::PrepareExecute].empty()) {
		// Print stats
		g_client_verifier->print_current_statistics();
		CVMESSAGE("Current Paths (" << paths_->size() << ")");
		if (PrintTrainingPaths) {
			foreach(PathManager* path, *paths_) {
				CVDEBUG(*path);
			}
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
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	const std::set<klee::ExecutionState*>& removed_states = removedStates;
	const std::set<klee::ExecutionState*>& added_states = addedStates;

	if (current && removed_states.count(current) == 0) {
		CVExecutionState *state = static_cast<CVExecutionState*>(current);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];
		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndPhase; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndPhase) {
				cv_error("state erase failed");
			}
			state_set.insert(state);
		}
	}

	foreach (klee::ExecutionState* klee_state, removed_states) {
		CVExecutionState *state = static_cast<CVExecutionState*>(klee_state);
		PathProperty *p = static_cast<PathProperty*>(state->property());
		ExecutionStateSet &state_set = phases_[p->phase];

		if (state_set.count(state) == 0) {
			unsigned i;
			for (i=0; i < PathProperty::EndPhase; i++) {
				if (phases_[i].count(state) != 0) {
					phases_[i].erase(state);
					break;
				}
			}
			if (i == PathProperty::EndPhase) {
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
	if (phases_[PathProperty::Execute].empty() &&
		  phases_[PathProperty::PrepareExecute].empty()) {
		return true;
	}
	return false;
}

void TrainingSearcher::record_path(CVExecutionState *state,
		CVExecutor* executor, CliverEvent::Type et) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

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
		path_tree_(NULL),
		socket_event_(socket_event), // XXX needed?
		network_event_index_(0), /// XXX needed?
		parent_(parent),
		search_strategy_(FullTraining),
    exhaustive_search_level_(1),
    training_paths_used_(0) {

	if (parent_ == NULL) {
		network_event_index_ = 0;
	} else {
		network_event_index_ = parent_->network_event_index_+1;
	}
}

/// XXX Needs explanatory comment!
CVExecutionState* VerifyStage::next_state() {
	if (states_.empty()) {

		assert(root_state_);

		PathRange range(root_state_->prevPC, NULL);

		if (search_strategy_ == FullTraining) {
			PathManager* training = NULL;
			do {
				if (training = path_selector_->next_path(range)) {
					const Path* tpath     = training->path();
					PathRange trange      = training->range();
					int index;
					ExecutionStateSet tree_states;

					if (path_tree_->get_states(tpath, trange, tree_states, index)) {
						training_paths_used_++;
						//CVDEBUG("VerifyStage::next_state(): " << tree_states.size() 
						//		<< " states found in path_tree starting at inst " << index); 
						foreach (CVExecutionState* s, tree_states) {
							HorizonPathManager* pm 
								= new HorizonPathManager(tpath, trange, path_tree_);
							pm->set_index(index);
							s->reset_path_manager(pm);
							VerifyProperty* p = static_cast<VerifyProperty*>(s->property());
							p->phase = VerifyProperty::Execute;
							states_.insert(s);
						}
					}
				}
			} while (training && states_.empty());

			if (states_.empty()) {
				CVDEBUG("Switching to KLook search mode");
				search_strategy_ = KLookSearch;
			}
		}
	
		if (search_strategy_ == KLookSearch) {
			PathManager* training = NULL;
			do {
				if (training = path_selector_->next_path(range)) {
					const Path* tpath     = training->path();
					PathRange trange      = training->range();
					int index;
					ExecutionStateSet tree_states;

					if (path_tree_->get_states(tpath, trange, tree_states, index)) {
						training_paths_used_++;
						//CVDEBUG("VerifyStage::next_state(): " << tree_states.size() 
						//		<< " states found in path_tree starting at inst " << index); 
						foreach (CVExecutionState* s, tree_states) {
							KLookPathManager* pm 
								= new KLookPathManager(tpath, trange, path_tree_, 4096);
							pm->set_index(index);
							s->reset_path_manager(pm);
							VerifyProperty* p = static_cast<VerifyProperty*>(s->property());
							p->phase = VerifyProperty::Execute;
							states_.insert(s);
						}
					}
				}
			} while (training && states_.empty());

			if (states_.empty()) {
				CVDEBUG("Switching to Exhaustive search mode");
				search_strategy_ = Exhaustive;
			}
		}	

		// For the Exhaustive strategy we collect all of the remaining states
		// from the PathTree and begin a complete search for a valid path from
		// all possible paths
		if (search_strategy_ == Exhaustive) {
			assert(states_.empty());
			exhaustive_search_level_ *= 2;
			ExecutionStateSet &path_tree_states = path_tree_->states();

			// XXX need better handling of this event
			assert(!path_tree_states.empty());

			foreach (CVExecutionState* s, path_tree_states) {
				// XXX We only need to reset the PathManager once...
				NthLevelPathManager* pm = new NthLevelPathManager(path_tree_);
				s->reset_path_manager(pm);
				states_.insert(s);
				g_executor->add_state_internal(s);
			}
		}
	}

	// Find a state that is not at the search "horizon"
	if (search_strategy_ == KLookSearch || search_strategy_ == FullTraining) {
		while (!states_.empty()) {
			CVExecutionState* state = *(states_.begin());
			VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
			if (VerifyProperty::Horizon == p->phase) {
				states_.erase(state);
			} else {
				return state;
			}
		}
		return next_state();
	}

	if (search_strategy_ == Exhaustive) {
		while (!states_.empty()) {
			CVExecutionState* state = *(states_.begin());
			NthLevelPathManager* pm 
				= static_cast<NthLevelPathManager*>(state->path_manager());
			if (pm->symbolic_level() >= exhaustive_search_level_) {
				states_.erase(state);
			} else {
				return state;
			}
		}
		// All states are at exhaustive_search_level_;
		CVDEBUG("All states have reached " << exhaustive_search_level_
				<< ", raising level to " << exhaustive_search_level_*2);
		return next_state();
	}

	cv_error("no states remaining");
	return *(states_.begin());
}

bool VerifyStage::contains_state(CVExecutionState *state) {
	if (states_.count(state) || path_tree_->contains_state(state))
		return true;
	return false;
}

void VerifyStage::add_state(CVExecutionState *state) {
	if (root_state_ == NULL) {
		assert(states_.empty());
		assert(path_tree_ == NULL && "PathTree already created");
		root_state_ = state;
		CVExecutionState* cloned_state = root_state_->clone();
		path_tree_ = new PathTree(cloned_state);
		g_executor->add_state_internal(cloned_state);
	} else {
		states_.insert(state);
	}
}

void VerifyStage::remove_state(CVExecutionState *state) {
	if (states_.count(state)) {
		//CVDEBUG_S(state->id(), "removing state from VerifyStage");
		states_.erase(state);
	}

	if (path_tree_->contains_state(state)) {
		//CVDEBUG_S(state->id(), "removing state from PathTree");
		path_tree_->remove_state(state);
	}

	if (finished_states_.count(state)) {
		//CVDEBUG_S(state->id(), "removing finished state from VerifyStage");
		finished_states_.erase(state);
	}
}

void VerifyStage::finish(CVExecutionState *finished_state) {

	CVDEBUG("Used " << training_paths_used_ << " training paths.");

#ifdef DEBUG_CLIVER_STATE_LOG
	CVDEBUG("\n" << finished_state->debug_log().str());
	finished_state->reset_debug_log();
	finished_state->debug_log() << "---------------------------------------------\n";
	finished_state->debug_log() << finished_state->network_manager()->socket()->event() << "\n";
	finished_state->debug_log() << "---------------------------------------------\n";
#endif

	// XXX attempt to merge?
	finished_states_.insert(finished_state);

	CVExecutionState* state = finished_state->clone();
	g_executor->add_state(state);

	VerifyProperty *p = static_cast<VerifyProperty*>(state->property());
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

	p->phase = VerifyProperty::Execute;
	p->round++;
	const SocketEvent &se = state->network_manager()->socket()->event();

	VerifyStage *child_stage = new VerifyStage(path_selector_, &se, this);
	child_stage->add_state(state);
	children_.push_back(child_stage);
}

VerifySearcher::VerifySearcher(klee::Searcher* base_searcher, 
		StateMerger* merger, PathManagerSet *paths) 
	: CVSearcher(base_searcher, merger), 
	  path_selector_(PathSelectorFactory::create(paths)) {

	root_stage_ = current_stage_ 
		= new VerifyStage(path_selector_, NULL, NULL);
}

klee::ExecutionState &VerifySearcher::selectState() {
	//klee::TimerStatIncrementer timer(stats::searcher_time);

	if (current_stage_->children().size() > 0) {
		current_stage_ = current_stage_->children().back();
		g_client_verifier->print_current_statistics();
	}

	CVExecutionState *state = current_stage_->next_state();

	if (state == NULL)
		cv_error("Null State");

	return *(static_cast<klee::ExecutionState*>(state));
}

void VerifySearcher::update(klee::ExecutionState *current,
		const std::set<klee::ExecutionState*> &addedStates,
		const std::set<klee::ExecutionState*> &removedStates) {
	//klee::TimerStatIncrementer timer(stats::searcher_time);
	
	// Check and add current if needed
	if (current != NULL) {
		CVExecutionState *cvcurrent = static_cast<CVExecutionState*>(current);
		if (false == current_stage_->contains_state(cvcurrent))
			current_stage_->add_state(cvcurrent);
	}

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
}

bool VerifySearcher::empty() {
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
