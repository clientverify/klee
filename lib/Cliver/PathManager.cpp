//===-- PathManager.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathManager.h"
#include "CVCommon.h"
#include "CVExecutor.h"
#include "CVExecutionState.h"
#include "ExecutionStateProperty.h"
#include "PathTree.h"

#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Function.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

llvm::cl::opt<bool>
DebugPathManager("debug-pathmanager",llvm::cl::init(false));

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugPathManager, x);

#ifdef DEBUG_CLIVER_STATE_LOG 

#undef CVDEBUG_S

#define CVDEBUG_S(__state, __x) \
	if (DebugPathManager) { \
	(__state)->debug_log() << __CVDEBUG_FILE << "State: " \
   << std::setw(4) << std::right << (__state)->id() << " - " << __x << "\n"; } 

#else

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugPathManager, __state_id, __x)

#endif

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

// Helper for debug output of instructions (also prints function name)
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	//ros << ki.info->id << ":" << *ki.inst;
	ros << ki.info->id << ":" << *ki.inst  << " (Function:"
		<< ki.inst->getParent()->getParent()->getName() << ")";
	str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() 
	: path_(new Path()) {}

PathManager::PathManager(Path *path, PathRange &range)
	: path_(path), range_(range) {}

PathManager* PathManager::clone() {
	Path* parent = path_;
	path_ = new Path(parent);
	return new PathManager(new Path(parent), range_);
}

PathManager::~PathManager() {
	if (path_)
		delete path_;
}

bool PathManager::merge(const PathManager &pm) {
	if (range_.equal(pm.range_) && path_->equal(*pm.path_))
		return true;

	return false;
}

bool PathManager::less(const PathManager &b) const {
	if (range_.less(b.range_))
		return true;
	return path_->less(*(b.path_));
}

bool PathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void PathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	path_->add(direction, inst, state->stack.size());
}

void PathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) { /* Not used */ }

void PathManager::set_range(const PathRange& range) {
	range_ = range;
}

void PathManager::print(std::ostream &os) const {
	os << "Path [" << path_->length() << "][" 
		<< range_.ids().first << ", " << range_.ids().second << "] " << *path_;
}

bool PathManagerLT::operator()(const PathManager* a, 
		const PathManager* b) const {
	return a->less(*b);
}

////////////////////////////////////////////////////////////////////////////////

TrainingPathManager::TrainingPathManager() {}

TrainingPathManager::TrainingPathManager(Path *path, PathRange &range)
	: PathManager(path, range) {}

TrainingPathManager::TrainingPathManager(Path *path, 
		PathRange &range, SocketEventDataSet &socket_events) 
	: PathManager(path, range), socket_events_(socket_events) {}

TrainingPathManager::~TrainingPathManager() {}

PathManager* TrainingPathManager::clone() {
	Path* parent = path_;
	path_ = new Path(parent);
	return new TrainingPathManager(new Path(parent), range_, socket_events_);
}

bool TrainingPathManager::merge(const PathManager &pm) {
	const TrainingPathManager *tpm 
		= static_cast<const TrainingPathManager*>(&pm);

	assert(range_.equal(tpm->range_) && "path range not equal");
	assert(path_->equal(*tpm->path_) && "paths not equal" );

	bool success = false;
	foreach(SocketEvent* se, tpm->socket_events_) {
		if (socket_events_.find(se) == socket_events_.end()) {
			success = true;
			socket_events_.insert(se);
		}
	}
	return success;
}

bool TrainingPathManager::less(const PathManager &pm) const {
	const TrainingPathManager *tpm = static_cast<const TrainingPathManager*>(&pm);
	if (range_.less(tpm->range_))
		return true;
	return path_->less(*(tpm->path_));
}

bool TrainingPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void TrainingPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	path_->add(direction, inst, state->stack.size());
}

bool TrainingPathManager::add_socket_event(const SocketEvent* se) {
	if (socket_events_.find(const_cast<SocketEvent*>(se)) == socket_events_.end()) {
		socket_events_.insert(const_cast<SocketEvent*>(se));
		return true;
	}
	return false;
}

bool TrainingPathManager::contains_socket_event(const SocketEvent* se) {
	if (socket_events_.find(const_cast<SocketEvent*>(se)) == socket_events_.end()) {
		return false;
	}
	return true;
}

void TrainingPathManager::erase_socket_event(const SocketEvent* se) {
	socket_events_.erase(const_cast<SocketEvent*>(se));
}

template<class archive> 
void TrainingPathManager::serialize(archive & ar, const unsigned version) {
	ar & range_;
	ar & path_;
	ar & socket_events_;
}

void TrainingPathManager::write(std::ostream &os) {
	boost::archive::binary_oarchive oa(os);
	oa << *this;
}

void TrainingPathManager::read(std::ifstream &is) {
	boost::archive::binary_iarchive ia(is);
	ia >> *this;
}

void TrainingPathManager::print(std::ostream &os) const {
	os << "Path [" << path_->length() << "][" << socket_events_.size() << "] ["
		 << range_.ids().first << ", " << range_.ids().second << "] "
		 << *path_;
}

////////////////////////////////////////////////////////////////////////////////

VerifyPathManager::VerifyPathManager(const Path *vpath, PathRange &vrange)
	: vpath_(vpath), vrange_(vrange), index_(0) {}

PathManager* VerifyPathManager::clone() {
	VerifyPathManager *pm = new VerifyPathManager(vpath_, vrange_);
	pm->index_ = index_;
	return pm;
}

bool VerifyPathManager::less(const PathManager &b) const {
	const VerifyPathManager *vpm = static_cast<const VerifyPathManager*>(&b);
	if (vrange_.less(vpm->vrange_))
		return true;
	return vpath_->less(*(vpm->vpath_));
}

bool VerifyPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	assert(vpath_ && "path is null");
	if (index_ < vpath_->length()) {
    if (direction != vpath_->get_branch(index_)) {
			if (validity != klee::Solver::Unknown) {
				CVDEBUG("Failed after covering " << (float)index_/vpath_->length()
						<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
						<< *inst);
			}
		}
		return direction == vpath_->get_branch(index_);
	}

	CVDEBUG("End of vpath, Failed after covering " << (float)index_/vpath_->length()
			<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
			<< *inst);
	return false;
}

void VerifyPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	assert(vpath_ && "path is null");
	assert(index_ < vpath_->length());
	assert(direction == vpath_->get_branch(index_));
	index_++;
}

void VerifyPathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) { /* Not used */ }

void VerifyPathManager::print(std::ostream &os) const {
	os << "vPath [" << vpath_->length() << "][" 
		<< vrange_.ids().first << ", " << vrange_.ids().second << "] " << *vpath_;
}

////////////////////////////////////////////////////////////////////////////////

VerifyConcretePathManager::VerifyConcretePathManager(const Path *vpath, 
		PathRange &vrange)
  : VerifyPathManager(vpath, vrange), invalidated_(false) {}

PathManager* VerifyConcretePathManager::clone() {
	VerifyConcretePathManager *pm 
		= new VerifyConcretePathManager(vpath_, vrange_);
	pm->index_ = index_;
	pm->invalidated_ = invalidated_;
	return pm;
}

bool VerifyConcretePathManager::less(const PathManager &b) const {
	const VerifyConcretePathManager *vpm 
		= static_cast<const VerifyConcretePathManager*>(&b);
	if (vrange_.less(vpm->vrange_))
		return true;
	return vpath_->less(*(vpm->vpath_));
}

bool VerifyConcretePathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	assert(vpath_ && "path is null");

	bool result = false;

	if (index_ >= vpath_->length()) {
		invalidated_ = true;
	}

	if (!invalidated_) {
		result = direction == vpath_->get_branch(index_);
		if (validity != klee::Solver::Unknown) {
			if (!result) {
				// We are now diverging from the saved path
				invalidated_ = true;
			}
		}
	}

	if (invalidated_) {
		if (validity == klee::Solver::Unknown) {
			return false;
		} else {
			return true;
		}
	}

	return result;
}

void VerifyConcretePathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	if (!invalidated_) {
		assert(vpath_ && "path is null");
		assert(index_ < vpath_->length());
		assert(direction == vpath_->get_branch(index_));
	}
	index_++;
}

////////////////////////////////////////////////////////////////////////////////

VerifyPrefixPathManager::VerifyPrefixPathManager(const Path *vpath, 
		PathRange &vrange)
  : VerifyPathManager(vpath, vrange), invalidated_(false) {}

PathManager* VerifyPrefixPathManager::clone() {
	VerifyPrefixPathManager *pm 
		= new VerifyPrefixPathManager(vpath_, vrange_);
	pm->index_ = index_;
	pm->invalidated_ = invalidated_;
	return pm;
}

bool VerifyPrefixPathManager::less(const PathManager &b) const {
	const VerifyPrefixPathManager *vpm 
		= static_cast<const VerifyPrefixPathManager*>(&b);
	if (vrange_.less(vpm->vrange_))
		return true;
	return vpath_->less(*(vpm->vpath_));
}

/// Returns true if the direction matches the training path at the current
/// index, up until we take a branch that doesn't match and is only valid
/// in one direction (when validity == klee::Solver::True/False). At this
/// point, the path is invalidated and we allow the Executor the follow
/// any branch by always returning true.
bool VerifyPrefixPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	assert(vpath_ && "path is null");

	bool result = false;

	if (index_ >= vpath_->length()) {
		invalidated_ = true;
	}

	if (!invalidated_) {
		result = direction == vpath_->get_branch(index_);
		if (validity != klee::Solver::Unknown) {
			if (!result) {
				invalidated_ = true;
			}
		}
	}

	if (!invalidated_) {
		return result;
	}
	return true;
}

void VerifyPrefixPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	if (!invalidated_) {
		assert(vpath_ && "path is null");
		assert(index_ < vpath_->length());
		assert(direction == vpath_->get_branch(index_));
	}
	index_++;
}

////////////////////////////////////////////////////////////////////////////////

StackDepthVerifyPathManager::StackDepthVerifyPathManager(const Path *path, 
		PathRange &range) : VerifyPathManager(path, range) {}

PathManager* StackDepthVerifyPathManager::clone() {
	StackDepthVerifyPathManager *pm 
		= new StackDepthVerifyPathManager(vpath_, vrange_);
	pm->index_ = index_;
	return pm;
}

bool StackDepthVerifyPathManager::less(const PathManager &b) const {
	const StackDepthVerifyPathManager *pm 
		= static_cast<const StackDepthVerifyPathManager*>(&b);
	if (vrange_.less(pm->vrange_))
		return true;
	return vpath_->less(*(pm->vpath_));
}

bool StackDepthVerifyPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {}

void StackDepthVerifyPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {}

////////////////////////////////////////////////////////////////////////////////

HorizonPathManager::HorizonPathManager(const Path *vpath, PathRange &vrange,
		PathTree* path_tree)
  : VerifyPathManager(vpath, vrange), 
	  path_tree_(path_tree), 
	  is_horizon_(false) {}

PathManager* HorizonPathManager::clone() {
	HorizonPathManager *pm = new HorizonPathManager(vpath_, vrange_, path_tree_);
	pm->index_ 			= index_;
	pm->is_horizon_ = is_horizon_;
	return pm;
}

bool HorizonPathManager::less(const PathManager &b) const {
	const HorizonPathManager *pm = static_cast<const HorizonPathManager*>(&b);
	if (vrange_.less(pm->vrange_))
		return true;
	return vpath_->less(*(pm->vpath_));
}

/// Always returns true.
bool HorizonPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void HorizonPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {

	assert(vpath_ && "path is null");
	assert(false == is_horizon_ && "must stop execution at horizon");

	if (index_ >= vpath_->length() || direction != vpath_->get_branch(index_)) {
		is_horizon_ = true;
		VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
		assert(p->phase == VerifyProperty::Execute && "Wrong state property");
		p->phase = VerifyProperty::Horizon;

		if (validity == klee::Solver::Unknown) {
			CVDEBUG("Reached Solver::Unknown Horizon after covering " << (float)index_/(float)vpath_->length()
					<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
					<< *inst);
		} else {
			CVDEBUG("Reached Horizon after covering " << (float)index_/(float)vpath_->length()
					<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
					<< *inst);
		}
	}

	index_++;

	path_tree_->branch(direction, validity, inst, state);
}

void HorizonPathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) {
	path_tree_->add_branched_state(state, branched_state);
}

void HorizonPathManager::set_index(int index) {
	index_ = index;
}

////////////////////////////////////////////////////////////////////////////////

NthLevelPathManager::NthLevelPathManager(PathTree* path_tree)
	: path_tree_(path_tree), level_(0), symbolic_level_(0) {}

PathManager* NthLevelPathManager::clone() {
	NthLevelPathManager *pm = new NthLevelPathManager(path_tree_);
	pm->level_ = level_;
	pm->symbolic_level_ = symbolic_level_;
	return pm;
}

/// Always returns true.
bool NthLevelPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void NthLevelPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	level_++;
	if (validity == klee::Solver::Unknown)
		symbolic_level_++;
	path_tree_->branch(direction, validity, inst, state);
}

void NthLevelPathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) {
	path_tree_->add_branched_state(state, branched_state);
}

////////////////////////////////////////////////////////////////////////////////

KLookaheadPathManager::KLookaheadPathManager(const Path *vpath, 
		PathRange &vrange, PathTree* path_tree, unsigned max_k)
  : VerifyPathManager(vpath, vrange), 
	  path_tree_(path_tree), 
	  is_horizon_(false),
		max_k_(max_k),
		lookahead_count_(0),
		lookahead_index_(0) {}

PathManager* KLookaheadPathManager::clone() {
	KLookaheadPathManager *pm 
		= new KLookaheadPathManager(vpath_, vrange_, path_tree_, max_k_);
	pm->index_ 			= index_;
	pm->is_horizon_ = is_horizon_;
	pm->lookahead_count_ = lookahead_count_;
	pm->lookahead_index_ = lookahead_index_;
	return pm;
}

bool KLookaheadPathManager::less(const PathManager &b) const {
	const KLookaheadPathManager *pm = static_cast<const KLookaheadPathManager*>(&b);
	if (vrange_.less(pm->vrange_))
		return true;
	return vpath_->less(*(pm->vpath_));
}

/// Always returns true.
bool KLookaheadPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void KLookaheadPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {

	assert(vpath_ && "path is null");
	assert(false == is_horizon_ && "must stop execution at horizon");

	if (index_ < vpath_->length() && direction != vpath_->get_branch(index_)) {

		if (llvm::BranchInst *bi = cast<llvm::BranchInst>(inst->inst)) {
			if (bi->isUnconditional()) {
				assert(direction && "False direction on unconditional branch");
			}
		}

		llvm::BasicBlock *bb = Path::lookup_successor(direction, inst);
		llvm::BasicBlock *bb_verify = Path::lookup_successor(!direction, inst);

		int k = 0;
		std::vector<klee::KInstruction*> skipped_insts;
		while (k < max_k_ && (index_ + k) < vpath_->length()) {
			llvm::BasicBlock *lookahead_bb 
				= vpath_->get_successor(index_ + k);

			skipped_insts.push_back(Path::lookup_kinst(vpath_->get_branch_id(index_ + k)));
			if (lookahead_bb == bb) {
				lookahead_count_++;
				index_ += (k + 1);
				lookahead_index_ = index_;

				foreach(klee::KInstruction* ki, skipped_insts) {
					CVDEBUG("Skipped : " << *ki);
				}
				CVDEBUG("Lookahead " << k << " branches at "
						<< "(" << index_ <<"/"<< vpath_->length() << ") "
						<< *inst);


				path_tree_->branch(direction, validity, inst, state);
				return;
			}
			k++;
		}
	}

	if (index_ >= vpath_->length() || direction != vpath_->get_branch(index_)) {
		is_horizon_ = true;
		VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
		assert(p->phase == VerifyProperty::Execute && "Wrong state property");
		p->phase = VerifyProperty::Horizon;

		if (validity == klee::Solver::Unknown) {
			CVDEBUG("Reached Solver::Unknown Horizon after covering " << (float)index_/(float)vpath_->length()
					<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
					<< *inst);
		} else {
			CVDEBUG("Reached Horizon after covering " << (float)index_/(float)vpath_->length()
					<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
					<< *inst);
		}
	}

	index_++;

	path_tree_->branch(direction, validity, inst, state);
}

void KLookaheadPathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) {
	path_tree_->add_branched_state(state, branched_state);
}

void KLookaheadPathManager::set_index(int index) {
	index_ = index;
}

////////////////////////////////////////////////////////////////////////////////

KLookPathManager::KLookPathManager(const Path *vpath, 
		PathRange &vrange, PathTree* path_tree, unsigned max_k)
  : VerifyPathManager(vpath, vrange), 
	  path_tree_(path_tree), 
	  is_horizon_(false),
		max_k_(max_k),
		lookahead_count_(0),
		lookahead_index_(0) {}

PathManager* KLookPathManager::clone() {
	KLookPathManager *pm 
		= new KLookPathManager(vpath_, vrange_, path_tree_, max_k_);
	pm->index_ 			= index_;
	pm->is_horizon_ = is_horizon_;
	pm->lookahead_count_ = lookahead_count_;
	pm->lookahead_index_ = lookahead_index_;
	pm->basicblock_list_ = basicblock_list_;
	pm->kinst_list_ = kinst_list_;
	return pm;
}

bool KLookPathManager::less(const PathManager &b) const {
	const KLookPathManager *pm = static_cast<const KLookPathManager*>(&b);
	if (vrange_.less(pm->vrange_))
		return true;
	return vpath_->less(*(pm->vpath_));
}

/// Always returns true.
bool KLookPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {
	return true;
}

void KLookPathManager::branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst, 
		CVExecutionState *state) {

	assert(vpath_ && "path is null");
	assert(false == is_horizon_ && "must stop execution at horizon");


	// Build a new bb list if this direction doesn't match the verify path
	if (index_ < vpath_->length() && direction != vpath_->get_branch(index_)) {
		// If we are not already tracking a BasicBlock list
		if (basicblock_list_.empty()) {

			if (llvm::BranchInst *bi = cast<llvm::BranchInst>(inst->inst)) {
				if (bi->isUnconditional()) {
					assert(direction && "False direction on unconditional branch");
				}
			}

			int k = 0;
			while ((index_ + k) < vpath_->length() && k < max_k_) {
				basicblock_list_.push_back(vpath_->get_successor(index_ + k));
				kinst_list_.push_back(Path::lookup_kinst(vpath_->get_branch_id(index_ + k)));
				k++;
			}
		}
	}

	if (!basicblock_list_.empty()) {

		if (validity == klee::Solver::Unknown) {
			is_horizon_ = true;
			VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
			assert(p->phase == VerifyProperty::Execute && "Wrong state property");
			p->phase = VerifyProperty::Horizon;
			CVDEBUG("Reached Solver::Unknown Horizon after covering " 
			    << (float)index_/(float)vpath_->length()
					<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
					<< " and deviating " << lookahead_count_ << " branches to "
					<< *inst);
		} else {
			CVDEBUG_S(state, "KLook branch " << lookahead_count_ << " () " << *inst);


			lookahead_count_++;
			llvm::BasicBlock *successor_bb = Path::lookup_successor(direction, inst);
			for (int k=0; k < basicblock_list_.size(); ++k) {
				llvm::BasicBlock *bb = basicblock_list_[k];
				if (bb == successor_bb) {
					for (int i=0; i < basicblock_list_.size(); ++i) {
						CVDEBUG_S(state, "Skipped " << i << " () " << *(kinst_list_[i]));

					}
					// Path Match!
					CVDEBUG_S(state, "KLook success. Found match in "
							<< k << " training path skips and "
							<< lookahead_count_ << " lookaheads at (" 
							<< index_ + k + 1 <<"/"<< vpath_->length() << ") " << *inst);
					
					index_ += (k + 1);
					lookahead_count_ = 0;
					basicblock_list_.clear();
					kinst_list_.clear();
				}
			}

			if (lookahead_count_ >= max_k_) {
				// We've reached the branch threshold
				is_horizon_ = true;
				VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
				assert(p->phase == VerifyProperty::Execute && "Wrong state property");
				p->phase = VerifyProperty::Horizon;

				CVDEBUG_S(state, "KLook failed at (" 
						<< index_ <<"/"<< vpath_->length() << ") " << *inst);
			}
		}

	} else {

		if (index_ >= vpath_->length() || direction != vpath_->get_branch(index_)) {
			is_horizon_ = true;
			VerifyProperty* p = static_cast<VerifyProperty*>(state->property());
			assert(p->phase == VerifyProperty::Execute && "Wrong state property");
			p->phase = VerifyProperty::Horizon;

			if (validity == klee::Solver::Unknown) {
				CVDEBUG("Reached Solver::Unknown Horizon after covering " 
				    << (float)index_/(float)vpath_->length()
						<< " of branches (" << index_ <<"/"<< vpath_->length() << ") "
						<< *inst);
			} else {
				CVDEBUG_S(state, "Reached Horizon after covering " 
						<< (float)index_/(float)vpath_->length() << " of branches (" 
						<< index_ <<"/"<< vpath_->length() << ") "
						<< *inst);
			}
		}

		index_++;
	}

	path_tree_->branch(direction, validity, inst, state);
}

void KLookPathManager::state_branch(CVExecutionState* state, 
		CVExecutionState* branched_state) {
	path_tree_->add_branched_state(state, branched_state);
}

void KLookPathManager::set_index(int index) {
	index_ = index;
}

////////////////////////////////////////////////////////////////////////////////

PathManagerSet::PathManagerSet() {}

bool PathManagerSet::insert(PathManager* path) {
	set_ty::iterator path_it = paths_.find(path);
	if (path_it  == paths_.end()) {
		paths_.insert(path);
		return true;
	}
	return false;
}

bool PathManagerSet::contains(PathManager* path) {
	set_ty::iterator path_it = paths_.find(path);
	if (path_it  == paths_.end()) {
		return false;
	}
	return true;
}

void PathManagerSet::erase(PathManager* path) {
	paths_.erase(path);
}

PathManager* PathManagerSet::merge(PathManager* path) {
	assert(contains(path) && "trying to merge with non-existant path");
	set_ty::iterator path_it = paths_.find(path);
	if (path_it  == paths_.end()) {
		cv_error("path doesn't exist");
	} else if ((*path_it)->merge(*path)) {
		return *path_it;
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////

PathManager* PathManagerFactory::create() {
  switch (g_cliver_mode) {
		case DefaultTrainingMode:
			return new TrainingPathManager();
		case VerifyWithTrainingPaths:
		case DefaultMode:
			break;
  }
  return new PathManager();
}

} // end namespace cliver
