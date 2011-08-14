//===-- PathManager.h -------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef PATH_MANAGER_H
#define PATH_MANAGER_H

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "ClientVerifier.h"

#include <list>
#include <set>
#include <fstream>
#include <iostream>

#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

struct FunctionCallBranchEvent {
	uint64_t index;
	unsigned inst_id;
};

////////////////////////////////////////////////////////////////////////////////

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
	void print(std::ostream &os) const;

 private: 
	explicit Path(const Path &p);

	// Helper functions
	void consolidate_branches(std::vector<bool> &branches) const;

	// Serialization
	friend class boost::serialization::access;
	template<class archive> 
	void serialize(archive & ar, const unsigned version);

	// Member Variables
	const Path *parent_;
	mutable unsigned ref_count_;
  int length_;

	std::vector<bool> branches_;
	std::vector<klee::KInstruction*> instructions_;
};

inline std::ostream &operator<<(std::ostream &os, const Path &p) {
  p.print(os);
  return os;
}
 
////////////////////////////////////////////////////////////////////////////////

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

	klee::KInstruction* start() { return start_; }
	klee::KInstruction* end() { return end_; }

	inst_id_pair_ty ids() const;
	inst_pair_ty 		insts() const;
	kinst_pair_ty 	kinsts() const;

	void print(std::ostream &os) const;

 private:
	klee::KInstruction* start_;
	klee::KInstruction* end_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathRange &pr) {
  pr.print(os);
  return os;
}

////////////////////////////////////////////////////////////////////////////////

class SocketEvent;

class PathManager {
 public:
	typedef std::set<const SocketEvent*> message_set_ty;
	PathManager();
	PathManager* clone();
	unsigned length() { return path_->length(); }
	bool merge(const PathManager &pm);
	bool less(const PathManager &b) const;
	void add_false_branch(klee::KInstruction* inst);
	void add_true_branch(klee::KInstruction* inst);
	void add_branch(bool direction, klee::KInstruction* inst);
	void print(std::ostream &os) const;
	bool add_message(const SocketEvent* se);
	void set_range(const PathRange& range);
	const message_set_ty& messages() { return messages_; }

 private:
	explicit PathManager(const PathManager &pm);
	Path* path_;
	PathRange range_;
	message_set_ty messages_;
};

inline std::ostream &operator<<(std::ostream &os, 
		const PathManager &p) {
  p.print(os);
  return os;
}
 
class PathManagerFactory {
 public:
  static PathManager* create();
};

struct PathManagerLT {
	bool operator()(const PathManager* a, const PathManager* b) const;
};

typedef std::set<PathManager*, PathManagerLT> PathSet;

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // PATH_MANAGER_H
