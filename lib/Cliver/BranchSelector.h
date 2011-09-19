//===-- BranchSelector.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_BRANCH_SELECTOR_H
#define CLIVER_BRANCH_SELECTOR_H

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class CVExecutionState;

class BranchSelector {
public:
 BranchSelector(CVExecutionState* state);
 void add_path_pair(const Path* path, std::set<SocketEvent*> socket_events)

private:
 CVExecutionState* state_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_BRANCH_SELECTOR_H

