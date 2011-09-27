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

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() 
	: path_(NULL) {}

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

bool PathManager::query_branch(bool direction, klee::KInstruction* inst) {
	return true;
}

bool PathManager::commit_branch(bool direction, klee::KInstruction* inst) {
	path_->add(direction, inst);
	return true;
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

bool TrainingPathManager::query_branch(bool direction, klee::KInstruction* inst) {
	return true;
}

bool TrainingPathManager::commit_branch(bool direction, klee::KInstruction* inst) {
	path_->add(direction, inst);
	return true;
}

void TrainingPathManager::set_range(const PathRange& range) {
	range_ = range;
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

bool VerifyPathManager::query_branch(bool direction, klee::KInstruction* inst) {
	assert(path_ && "path is null");
	return direction == path_->get_branch(index_);
}

bool VerifyPathManager::commit_branch(bool direction, klee::KInstruction* inst) {
	assert(path_ && "path is null");
	assert(direction == path_->get_branch(index_));
	index_++;
	return true;
}

void VerifyPathManager::print(std::ostream &os) const {
	os << "Path [" << path_->length() << "][" 
		<< range_.ids().first << ", " << range_.ids().second << "] " << *path_;
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
