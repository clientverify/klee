//===-- Path.cpp ------------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
// TODO some function calls make unnecessary consolidate_branches, either make
// this a commandline option (i.e., consolidate all branches when a path is
// created with a parent or some other optimization
//
//===----------------------------------------------------------------------===//

#include "Path.h"
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

klee::KInstruction* PathRange::get_kinst(unsigned id) {
	assert(g_executor && "CVExecutor not initialized");
	klee::KInstruction* kinst = g_executor->get_instruction(id);
	assert(kinst != NULL && "invalid PathRange id");
	return kinst;
}

////////////////////////////////////////////////////////////////////////////////

Path::Path() : parent_(NULL), ref_count_(0), length_(-1) {}

Path::Path(Path* parent) : parent_(parent), ref_count_(0), length_(-1) {
	parent->inc_ref();
}

void Path::add(bool direction, klee::KInstruction* inst) {
	assert(ref_count_ == 0);
	branches_.push_back(direction);
	branch_ids_.push_back(inst->info->id);
}

Path::~Path() { 
	if (parent_) {
		const_cast<Path*>(parent_)->dec_ref();
		if (parent_->ref_count_ <= 0) {
			//CVDEBUG("deleting parent");
			delete parent_;
		}
	}
}

unsigned Path::length() const { 
	if (ref_count_ == 0) {
		if (parent_)
			return branches_.size() + parent_->length();
		return branches_.size();
	}
	assert(length_ > -1);
	return (unsigned) length_;
}

bool Path::less(const Path &b) const {
	if (length() < b.length()) 
		return true;
	std::vector<bool> branches, branches_b;
	consolidate_branches(branches);
	b.consolidate_branches(branches_b);
	return branches < branches_b;
}

bool Path::equal(const Path &b) const {
	// XXX Possible? branches_ == b.branches && branch_ids_ != b.branch_ids_
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

bool Path::get_branch(int index) const {
	if (parent_ == NULL) {
		assert( index >= 0 && index < branches_.size());
		return branches_[index];
	}
	std::vector<bool> branches;
	consolidate_branches(branches);
	assert( index >= 0 && index < branches.size());
	return branches[index];
}

bool Path::get_branch_id(int index) const {
	if (parent_ == NULL) {
		assert( index >= 0 && index < branch_ids_.size());
		return branch_ids_[index];
	}
	std::vector<unsigned> branch_ids;
	consolidate_branch_ids(branch_ids);
	assert( index >= 0 && index < branch_ids.size());
	return branch_ids[index];
}

klee::KInstruction* Path::get_branch_kinst(int index) {
	return get_kinst(get_branch_id(index));
}

klee::KInstruction* Path::get_kinst(unsigned id) {
	assert(g_executor && "CVExecutor not initialized");
	klee::KInstruction* kinst = g_executor->get_instruction(id);
	assert(kinst != NULL && "invalid KInstruction id");
	return kinst;
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

void Path::consolidate_branch_ids(std::vector<unsigned> &branch_ids) const {
	branch_ids.reserve(length());
	const Path* p = this;
	while (p != NULL) {
		branch_ids.insert(branch_ids.begin(), 
				p->branch_ids_.begin(), p->branch_ids_.end());
		p = p->parent_;
	}
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
