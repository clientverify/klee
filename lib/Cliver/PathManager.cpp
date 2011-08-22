//===-- PathManager.cpp -----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "PathManager.h"
#include "llvm/Support/CommandLine.h"
#include "CVStream.h"
#include "CVExecutor.h"
#include "Socket.h"

#include <iostream>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

Path::Path() : parent_(NULL), ref_count_(0), length_(-1) {}

Path::Path(Path* parent) : parent_(parent), ref_count_(0), length_(-1) {
	parent->inc_ref();
}

void Path::add(bool direction, klee::KInstruction* inst) {
	assert(ref_count_ == 0);
	branches_.push_back(direction);
	//instructions_.push_back(inst);
}

Path::~Path() { 
	const_cast<Path*>(parent_)->dec_ref();
	if (parent_->ref_count_ <= 0) {
		CVDEBUG("deleting parent");
		delete parent_;
	}
}

unsigned Path::length() const { 
	if (ref_count_ == 0) {
		if (parent_)
			return branches_.size() + parent_->length();
		return branches_.size();
	}
	assert(length_ > 0);
	return (unsigned) length_;
}

bool Path::less(const Path &b) const {
	std::vector<bool> branches, branches_b;
	consolidate_branches(branches);
	b.consolidate_branches(branches_b);
	return branches < branches_b;
}

bool Path::equal(const Path &b) const {
	if (length() != b.length()) 
		return false;
	std::vector<bool> branches, branches_b;
	consolidate_branches(branches);
	b.consolidate_branches(branches_b);
	return branches == branches_b;
}

void Path::inc_ref() {
	// Path length can no longer change if it has a child
	if (ref_count_ == 0 && length_ == -1) {
		length_ = length();
	}
	ref_count_++;
}

void Path::dec_ref() {
	ref_count_--;
}

bool Path::get_branch(int index) {
	if (parent_ == NULL) {
		assert( index >= 0 && index < branches_.size());
		return branches_[index];
	}
	std::vector<bool> branches;
	consolidate_branches(branches);
	assert( index >= 0 && index < branches.size());
	return branches[index];
}

klee::KInstruction* Path::get_kinst(int index) {
	assert(0 && "get_kinst() not implemented");
	return NULL;
}

void Path::print(std::ostream &os) const {
	std::vector<bool> branches;
	consolidate_branches(branches);
	foreach (bool branch, branches) {
		if (branch) {
			os << '1';
		} else {
			os << '0';
		}
	}
}

void Path::consolidate_branches(std::vector<bool> &branches) const {
	branches.reserve(length());
	const Path* p = this;
	while (p != NULL) {
		branches.insert(branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		p = p->parent_;
	}
}

template<class archive> 
void Path::save(archive & ar, const unsigned version) const {
	std::vector<bool> branches;
	consolidate_branches(branches);
	ar & branches;
}

template<class archive> 
void Path::load(archive & ar, const unsigned version) {
	ar & branches_;
}

////////////////////////////////////////////////////////////////////////////////

PathRange::PathRange(klee::KInstruction* s, klee::KInstruction* e) 
	: start_(s), end_(e) {}

PathRange::PathRange()
	: start_(NULL), end_(NULL) {}

bool PathRange::less(const PathRange &b) const {
	return ids() < b.ids();
}

bool PathRange::equal(const PathRange &b) const {
	return ids() == b.ids();
}

int PathRange::compare(const PathRange &b) const {
	if (ids() < b.ids())
		return -1;
	if (ids() > b.ids())
		return 1;
	return 0;
}

PathRange::inst_id_pair_ty PathRange::ids() const {
	return std::make_pair(start_ ? start_->info->id : 0, 
			end_ ? end_->info->id : 0);
}

PathRange::inst_pair_ty PathRange::insts() const {
	return std::make_pair(start_ ? start_->inst : NULL, 
			end_ ? end_->inst : NULL);
}

PathRange::kinst_pair_ty PathRange::kinsts() const {
	return std::make_pair(start_, end_);
}

void PathRange::print(std::ostream &os) const {
	inst_id_pair_ty id_pair = ids();

	os << "(";
	if (id_pair.first)
		os << id_pair.first;
	else
		os << "...";

	os << " -> ";

	if (id_pair.second)
		os << id_pair.second;
	else
		os << "...";
	os << ")";
}

template<class archive> 
void PathRange::save(archive & ar, const unsigned version) const {
	unsigned start_id = ids().first, end_id = ids().second;
	ar & start_id;
	ar & end_id;
}

template<class archive> 
void PathRange::load(archive & ar, const unsigned version) {
	assert(g_executor && "CVExecutor not initialized");
	unsigned start_id, end_id;
	ar & start_id;
	ar & end_id;
	
	if (start_id != 0) {
		start_ = g_executor->get_instruction(start_id);
		assert(start_ != NULL && "invalid PathRange id");
	}
	if (end_id != 0) {
		end_ = g_executor->get_instruction(start_id);
		assert(end_ != NULL && "invalid PathRange id");
	}
}

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
	return path_->less(*b.path_);
}

void PathManager::add_false_branch(klee::KInstruction* inst) {
	add_branch(false, inst);
}

void PathManager::add_true_branch(klee::KInstruction* inst) {
	add_branch(true, inst);
}

void PathManager::add_branch(bool direction, klee::KInstruction* inst) {
	path_->add(direction, inst);
}

bool PathManager::add_message(const SocketEvent* se) {
	if (messages_.find(const_cast<SocketEvent*>(se)) == messages_.end()) {
		messages_.insert(const_cast<SocketEvent*>(se));
		return true;
	}
	return false;
}

void PathManager::set_range(const PathRange& range) {
	range_ = range;
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

VerifyPathManager::VerifyPathManager(Path* path)
	: index_(0), valid_(true) {
	path_ = path;
}

void VerifyPathManager::add_branch(bool direction, klee::KInstruction* inst) {
	bool path_direction = path_->get_branch(index_);
	//klee::KInstruction* path_inst = path->get_kinst(index);
	if (path_direction != direction) {
		valid_ = false;
	}
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
