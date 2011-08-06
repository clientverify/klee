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
	void add(bool direction, klee::KInstruction* inst);
	virtual void write(std::ofstream &file);
 private: 
	std::vector<bool> branches_;
};

////////////////////////////////////////////////////////////////////////////////

class PathManager {
 public:
	PathManager();
	PathManager* clone();

	void add_false_branch(klee::KInstruction* inst);
	void add_true_branch(klee::KInstruction* inst);
	virtual void add_branch(bool direction, klee::KInstruction* inst);

	virtual void write(std::ofstream &file);
 private:

	Path path_;
};

class PathManagerFactory {
 public:
  static PathManager* create();
};


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // PATH_MANAGER_H
