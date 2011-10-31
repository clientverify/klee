//===-- Path.h --------------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_PATH_H
#define CLIVER_PATH_H

#include "ClientVerifier.h"

#include <iostream>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

namespace llvm {
 class Instruction;
}

namespace klee {
 class KInstruction;
}

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// PathRange holds the starting instruction and ending instruction (if closed)
/// for a single path of execution through the code. The member vars are ptrs
/// the corresponding KInstructions that enclose the range. When a PathRange is
/// serialized: on save, the KInstruction ids are written, on load the 
/// KInstruction pointers are looked up with the loaded ids.
class PathRange {
 public:
	typedef std::pair<klee::KInstruction*, klee::KInstruction*> kinst_pair_ty;
	typedef std::pair<llvm::Instruction*, llvm::Instruction*> inst_pair_ty;
	typedef std::pair<unsigned, unsigned> inst_id_pair_ty;

	PathRange(klee::KInstruction* s, klee::KInstruction* e);
	PathRange();
	bool less(const PathRange &b) const;
	bool equal(const PathRange &b) const;
	int compare(const PathRange &b) const;

	klee::KInstruction* start() const { return start_; }
	klee::KInstruction* end() const { return end_; }

	inst_id_pair_ty ids() const;
	inst_pair_ty 		insts() const;
	kinst_pair_ty 	kinsts() const;

	void print(std::ostream &os) const;

 private:
	klee::KInstruction* get_kinst(unsigned id);
	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void save(archive & ar, const unsigned version) const;
	template<class archive> 
		void load(archive & ar, const unsigned version);
	BOOST_SERIALIZATION_SPLIT_MEMBER()

	klee::KInstruction* start_;
	klee::KInstruction* end_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathRange &pr) {
  pr.print(os);
  return os;
}

template<class archive> 
void PathRange::save(archive & ar, const unsigned version) const {
	unsigned start_id = ids().first, end_id = ids().second;
	ar & start_id;
	ar & end_id;
}

template<class archive> 
void PathRange::load(archive & ar, const unsigned version) {
	unsigned start_id, end_id;
	ar & start_id;
	ar & end_id;
	
	if (start_id != 0)
		start_ = get_kinst(start_id);
	if (end_id != 0)
		end_ = get_kinst(end_id);
}

////////////////////////////////////////////////////////////////////////////////

/// A Path records the branch direction taken in a region of code. In order to 
/// save space, a Path may stored as a tree of Paths internally. 
class Path {
 public:
	Path();
	Path(Path* parent);
	~Path();
	unsigned length() const;
	void add(bool direction, klee::KInstruction* inst);
	bool less(const Path &b) const;
	bool equal(const Path &b) const;
	void inc_ref();
	void dec_ref();
	bool get_branch(int index);
	bool get_branch_id(int index);
	klee::KInstruction* get_branch_kinst(int index);
	void print(std::ostream &os) const;

 private: 
	explicit Path(const Path &p);

	// Helper functions
	void consolidate_branches(std::vector<bool> &branches) const;
	void consolidate_branch_ids(std::vector<unsigned> &branch_ids) const;
	klee::KInstruction* get_kinst(unsigned id);

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
		void save(archive & ar, const unsigned version) const;
	template<class archive> 
		void load(archive & ar, const unsigned version);
	BOOST_SERIALIZATION_SPLIT_MEMBER()

	// Member Variables
	const Path *parent_;
	mutable unsigned ref_count_;
	int length_;

	std::vector<bool> branches_;
	std::vector<unsigned>  branch_ids_;
};

inline std::ostream &operator<<(std::ostream &os, const Path &p) {
  p.print(os);
  return os;
}

template<class archive> 
void Path::save(archive & ar, const unsigned version) const {
	std::vector<bool> branches;
	consolidate_branches(branches);
	ar & branches;
	std::vector<unsigned> branch_ids;
	consolidate_branch_ids(branch_ids);
	ar & branch_ids;
}

template<class archive> 
void Path::load(archive & ar, const unsigned version) {
	ar & branches_;
	ar & branch_ids_;
}

} // end namespace cliver
#endif // CLIVER_PATH_H
