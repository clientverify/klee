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

#include <iostream>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

Path::Path() : parent_(NULL), ref_count_(0) {}

void Path::add(bool direction, klee::KInstruction* inst) {
	branches_.push_back(direction);
	instructions_.push_back(inst->info->id);
}

Path::~Path() { 
	parent_->dec_ref();
	if (parent_->ref() <= 0) {
		delete parent_;
	}
}

bool Path::less(const Path &b) const {
	return branches_ < b.branches_;
}

Path::path_iterator Path::begin() const {
	return branches_.begin();
}

Path::path_iterator Path::end() const {
	return branches_.end();
}

void Path::inc_ref() {
	ref_count_++;
}

void Path::dec_ref() {
	ref_count_--;
}

void Path::consolidate() {
	Path* p = parent_;
	while (p != NULL) {
		branches_.insert(branches_.begin(), 
				p->branches_.begin(), p->branches_.end());
		instructions_.insert(instructions_.begin(), 
				p->instructions_.begin(), p->instructions_.end());
		p = p->parent_;
	}
	parent_->dec_ref();
	parent_ = NULL;
}

void Path::set_parent(Path *path) {
	assert(parent_ == NULL && branches_.empty() && instructions_.empty());
	parent_ = path;
	parent_->inc_ref();
}

const Path* Path::get_parent() {
	return parent_;
}


template<class archive> 
void Path::serialize(archive & ar, const unsigned version) {
	ar & branches_;
	ar & instructions_;
}

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() : path_(new Path()) {}

PathManager::PathManager(const PathManager &pm)
	: path_(new Path()) {
	path_->set_parent(pm.path_);
}

PathManager* PathManager::clone() {
	return new PathManager(*this);
}

bool PathManager::less(const PathManager &b) const {
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

const Path& PathManager::get_consolidated_path() {
	if (path_->get_parent() != NULL) {
		path_->consolidate();
	}
	return *path_;
}

void PathManager::print_diff(const PathManager &b, std::ostream &os) const {
	if (less(b) || b.less(*this)) {
		Path::path_iterator it_a=path_->begin(), it_b=b.path_->begin(), 
			ie_a=path_->end(), ie_b=b.path_->end();
		bool equal = false;
		std::stringstream ss_a, ss_b;
		while (it_a != ie_a || it_b != ie_b) {
			if (it_a != ie_a && it_b != ie_b) {
				if (*it_a == *it_b) {
					equal = true;
				} else {
					if (equal) {
						ss_a << "-";
						ss_b << "-";
						equal = false;
					}
					ss_a << *it_a;
					ss_b << *it_b;
				}
				it_a++; it_b++;
			} else if (it_a != ie_a) {
				ss_a << *it_a;
				ss_b << "-";
				it_a++;
			}	else if (it_b != ie_b) {
				ss_a << "-";
				ss_b << *it_b;
				it_b++;
			}
		}
		os << "Paths:\n" << ss_a << "\n" << ss_b;
	}
}

////////////////////////////////////////////////////////////////////////////////

PathManager* PathManagerFactory::create() {
  switch (g_cliver_mode) {
		case DefaultMode:
    break;
  }
  return new PathManager();
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
