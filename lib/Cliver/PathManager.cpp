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

// Helper for debug output
inline std::ostream &operator<<(std::ostream &os, 
		const klee::KInstruction &ki) {
	std::string str;
	llvm::raw_string_ostream ros(str);
	//ros << ki.info->id << ":" << *ki.inst;
	ros << ki.info->id << ":" << *ki.inst 
		<< ki.inst->getParent()->getParent()->getName();
	str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
	return os << ros.str();
}

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() 
	: path_(NULL) {}

PathManager::PathManager(Path *path) 
	: path_(path) {}

PathManager* PathManager::clone() {
	PathManager *pm = new PathManager();
	Path* parent = path_;
	path_ = new Path(parent);
	pm->path_ = new Path(parent);
	pm->range_ = range_;
	return pm;
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
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	return true;
}

void PathManager::commit_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	path_->add(direction, inst);
}

void PathManager::set_range(const PathRange& range) {
	range_ = range;
}

void PathManager::set_path(Path* path) {
	assert(path_ == NULL && "path is already set");
	path_ = path;
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

TrainingPathManager::TrainingPathManager() {
	set_path(new Path());
}

PathManager* TrainingPathManager::clone() {
	// Don't split/clone a path that has already been finalized
	// with an endpoint
	TrainingPathManager *pm = new TrainingPathManager();
	Path* parent = path_;
	path_ = new Path(parent);
	pm->path_ = new Path(parent);
	pm->socket_events_ = socket_events_;
	pm->range_ = range_;
	return pm;
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
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	return true;
}

void TrainingPathManager::commit_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	path_->add(direction, inst);
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

VerifyPathManager::VerifyPathManager()
	: index_(0) {
	path_ = NULL;
}

PathManager* VerifyPathManager::clone() {
	VerifyPathManager *pm = new VerifyPathManager();
	pm->path_ = path_;
	pm->index_ = index_;
	pm->socket_events_ = socket_events_;
	pm->range_ = range_;
	return pm;
}

bool VerifyPathManager::merge(const PathManager &pm) {
	assert(0);
	return false;
}

bool VerifyPathManager::less(const PathManager &b) const {
	const VerifyPathManager *vpm = static_cast<const VerifyPathManager*>(&b);
	if (range_.less(vpm->range_))
		return true;
	return path_->less(*(vpm->path_));
}

bool VerifyPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	assert(path_ && "path is null");
	if (index_ < path_->length()) {
    if (direction != path_->get_branch(index_)) {
			CVDEBUG("Failed after covering " << (float)index_/path_->length()
					<< " of branches (" << index_ <<"/"<< path_->length() << ") "
					<< *inst);
		}
		return direction == path_->get_branch(index_);
	}
	return false;
}

void VerifyPathManager::commit_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	assert(path_ && "path is null");
	assert(index_ < path_->length());
	assert(direction == path_->get_branch(index_));
	index_++;
}

void VerifyPathManager::print(std::ostream &os) const {
	os << "Path [" << path_->length() << "][" 
		<< range_.ids().first << ", " << range_.ids().second << "] " << *path_;
}
////////////////////////////////////////////////////////////////////////////////

VerifyPrefixPathManager::VerifyPrefixPathManager()
	: VerifyPathManager(), invalidated_(false) {}

PathManager* VerifyPrefixPathManager::clone() {
	VerifyPrefixPathManager *pm = new VerifyPrefixPathManager();
	pm->path_ = path_;
	pm->index_ = index_;
	pm->socket_events_ = socket_events_;
	pm->range_ = range_;
	pm->invalidated_ = invalidated_;
	return pm;
}

bool VerifyPrefixPathManager::merge(const PathManager &pm) {
	assert(0);
	return false;
}

bool VerifyPrefixPathManager::less(const PathManager &b) const {
	const VerifyPrefixPathManager *vpm 
		= static_cast<const VerifyPrefixPathManager*>(&b);
	if (range_.less(vpm->range_))
		return true;
	return path_->less(*(vpm->path_));
}

/// Returns true if the direction matches the training path at the current
/// index, up until we take a branch that doesn't match and is only valid
/// in one direction (when validity == klee::Solver::True/False). At this
/// point, the path is invalidated and we allow the Executor the follow
/// any branch by always returning true.
bool VerifyPrefixPathManager::try_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	assert(path_ && "path is null");

	bool result = false;

	if (index_ >= path_->length()) {
		invalidated_ = true;
	}

	if (!invalidated_) {
		result = direction == path_->get_branch(index_);
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

void VerifyPrefixPathManager::commit_branch(bool direction, 
		klee::Solver::Validity validity, klee::KInstruction* inst) {
	if (!invalidated_) {
		assert(path_ && "path is null");
		assert(index_ < path_->length());
		assert(direction == path_->get_branch(index_));
	}
	index_++;
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
			return new VerifyPathManager();
		case DefaultMode:
			break;
  }
  return new PathManager();
}

} // end namespace cliver
