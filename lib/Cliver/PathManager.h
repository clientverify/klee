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

namespace cliver {

struct BranchEvent {
	BranchEvent(bool dir, klee::KInstruction* i) : direction(dir), inst(i) {}
	bool direction;
	klee::KInstruction *inst;
};

class PathManager {
 public:
	PathManager();
	PathManager* clone();

	void add_false_branch(klee::KInstruction* inst);
	void add_true_branch(klee::KInstruction* inst);
	virtual void add_branch(bool direction, klee::KInstruction* inst);
 private:

	std::list<BranchEvent> branches_;
};

class PathManagerFactory {
 public:
  static PathManager* create();
};


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // PATH_MANAGER_H
