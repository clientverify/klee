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
	instructions_.push_back(inst);
}

Path::~Path() { 
	parent_->dec_ref();
	if (parent_->ref() <= 0) {
		delete parent_;
	}
}

unsigned Path::size() const { 
	unsigned branch_count = branches_.size();

	Path* p = parent_;
	while (p != NULL) {
		branch_count += p->branches_.size();
		p = p->parent_;
	}
	return branch_count;
}

bool Path::less(const Path &b) const {
	std::vector<bool> branches;
	std::vector<bool> b_branches;

	branches.reserve(size());
	b_branches.reserve(b.size());

	const Path* p = this;
	while (p != NULL) {
		branches.insert(branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		p = p->parent_;
	}

	p = &b;
	while (p != NULL) {
		b_branches.insert(b_branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		p = p->parent_;
	}

	return branches < b_branches;
}

bool Path::equal(const Path &b) const {
	//if (size() != b.size()) 
	//	return false;

	std::vector<bool> branches;
	std::vector<bool> b_branches;

	branches.reserve(size());
	b_branches.reserve(b.size());

	const Path* p = this;
	while (p != NULL) {
		branches.insert(branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		p = p->parent_;
	}

	p = &b;
	while (p != NULL) {
		b_branches.insert(b_branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		p = p->parent_;
	}

	return branches == b_branches;
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

////////////////////////////////////////////////////////////////////////////////

PathManager::PathManager() 
	: path_(new Path()) {}

PathManager::PathManager(const PathManager &pm)
	: path_(new Path()),
	  range_(pm.range_),
		messages_(pm.messages_) {
	path_->branches_ = pm.path_->branches_;
	path_->instructions_ = pm.path_->instructions_;
	//path_->set_parent(pm.path_);
}

PathManager* PathManager::clone() {
	PathManager *pm = new PathManager(*this);
	assert(pm->path_->size() == path_->size());
	return pm;
}

bool PathManager::merge(const PathManager &pm) {
	assert(range_.equal(pm.range_) && "path range not equal");
	assert(path_->equal(*pm.path_) && "paths not equal" );

	bool success = false;
	foreach(const SocketEvent* se, pm.messages_) {
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

const Path& PathManager::get_consolidated_path() {
	if (path_->get_parent() != NULL) {
		path_->consolidate();
	}
	return *path_;
}

void PathManager::print(std::ostream &os) const {

	std::vector<bool> branches;
	std::vector<klee::KInstruction*> instructions;

	branches.reserve(path_->size());
	//instructions.reserve(path_->size());

	const Path* p = path_;
	while (p != NULL) {
		branches.insert(branches.begin(), 
				p->branches_.begin(), p->branches_.end());
		//instructions.insert(instructions.begin(), 
		//		p->instructions_.begin(), p->instructions_.end());
		p = p->parent_;
	}
	os << "Path [" << branches.size() << "][" << messages_.size() << "] [";
	os << range_.ids().first << ", " << range_.ids().second << "] ";
	foreach (bool branch, branches) {
	//for (unsigned i=0; i<branches.size(); ++i) {
		//std::string str;
		//util_kinst_string(instructions[i], str);
		//if (branches[i]) {
		if (branch) {
			os << '1';
			//os << "1: " << str << "\n";
		} else {
			os << '0';
			//os << "0: " << str << "\n";
		}
	}
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

bool PathManager::add_message(const SocketEvent* se) {
	if (messages_.find(se) == messages_.end()) {
		messages_.insert(se);
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
