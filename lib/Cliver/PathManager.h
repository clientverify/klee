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

#include "CVExecutionState.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"

#include <list>
#include <fstream>

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
	typedef std::vector<bool> path_ty;
	typedef path_ty::const_iterator path_iterator;

	Path();
	~Path();
	path_iterator begin() const;
	path_iterator end() const;
	void add(bool direction, klee::KInstruction* inst);
	void write_file(std::ofstream &file);
	void read_file(std::ifstream &file);
	bool less(const Path &b) const;
	void inc_ref();
	void dec_ref();
	unsigned ref() { return ref_count_; }
	void consolidate();
	void set_parent(Path *path);

 private: 
	friend class boost::serialization::access;
	template<class archive> 
	void serialize(archive & ar, const unsigned version);

	Path *parent_;
	unsigned ref_count_;

	std::vector<unsigned> instructions_;
	std::vector<bool> branches_;
};

////////////////////////////////////////////////////////////////////////////////

class PathManager {
 public:
	PathManager();
	PathManager(const PathManager &pm);
	PathManager* clone();
	bool less(const PathManager &b) const;
	void print_diff(const PathManager &b, std::ostream &os) const;

	void add_false_branch(klee::KInstruction* inst);
	void add_true_branch(klee::KInstruction* inst);
	virtual void add_branch(bool direction, klee::KInstruction* inst);

	virtual void write(std::ofstream &file);
 private:
	void consolidate_path();

	Path* path_;
};

class PathManagerFactory {
 public:
  static PathManager* create();
};


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // PATH_MANAGER_H
