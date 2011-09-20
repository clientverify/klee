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
#include "Socket.h"

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
	: path_(new Path()) {}

PathManager* PathManager::clone() {
	// Don't split/clone a path that has already been finalized
	// with an endpoint
	PathManager *pm = new PathManager();
	Path* parent = path_;
	path_ = new Path(parent);
	pm->path_ = new Path(parent);
	pm->messages_ = messages_;
	pm->range_ = range_;
	return pm;
}

bool PathManager::merge(const PathManager &pm) {
	assert(range_.equal(pm.range_) && "path range not equal");
	assert(path_->equal(*pm.path_) && "paths not equal" );

	bool success = false;
	foreach(SocketEvent* se, pm.messages_) {
		if (messages_.find(se) == messages_.end()) {
			success = true;
			messages_.insert(se);
		}
	}
	return success;
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

bool PathManager::add_message(const SocketEvent* se) {
	if (messages_.find(const_cast<SocketEvent*>(se)) == messages_.end()) {
		messages_.insert(const_cast<SocketEvent*>(se));
		return true;
	}
	return false;
}

bool PathManager::contains_message(const SocketEvent* se) {
	if (messages_.find(const_cast<SocketEvent*>(se)) == messages_.end()) {
		return false;
	}
	return true;
}

void PathManager::set_range(const PathRange& range) {
	range_ = range;
}

void PathManager::set_path(Path* path) {
	assert(path_ == NULL && "path is already set");
	path_ = path;
}

bool PathManagerLT::operator()(const PathManager* a, 
		const PathManager* b) const {
	return a->less(*b);
}

template<class archive> 
void PathManager::serialize(archive & ar, const unsigned version) {
	ar & range_;
	ar & path_;
	ar & messages_;
}

void PathManager::write(std::ostream &os) {
	boost::archive::binary_oarchive oa(os);
	oa << *this;
}

void PathManager::read(std::ifstream &is) {
	boost::archive::binary_iarchive ia(is);
	ia >> *this;
}

void PathManager::print(std::ostream &os) const {
	os << "Path [" << path_->length() << "][" << messages_.size() << "] ["
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
	pm->messages_ = messages_;
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

////////////////////////////////////////////////////////////////////////////////

PathSet::PathSet() {}

bool PathSet::add(PathManager* path) {
	set_ty::iterator path_it = paths_.find(path);
	if (path_it  == paths_.end()) {
		paths_.insert(path);
		return true;
	}
	return false;
}

bool PathSet::contains(PathManager* path) {
	set_ty::iterator path_it = paths_.find(path);
	if (path_it  == paths_.end()) {
		return false;
	}
	return true;
}

PathManager* PathSet::merge(PathManager* path) {
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
		case VerifyWithTrainingPaths:
			return new VerifyPathManager();
		case DefaultMode:
			break;
  }
  return new PathManager();
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
