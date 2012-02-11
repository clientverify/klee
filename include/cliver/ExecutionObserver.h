//===-- ExecutionObserver.h -------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EXECUTION_OBSERVER_H
#define CLIVER_EXECUTION_OBSERVER_H

#include <list>

namespace klee {
class ExecutionState;
}

namespace cliver {

class CVExecutionState;

////////////////////////////////////////////////////////////////////////////////

#define CV_EXECUTION_EVENT_TYPES \
  X(CV_ROUND_START)              \
  X(CV_BRANCH)                   \
  X(CV_BRANCH_UNCONDITIONAL)     \
  X(CV_BRANCH_INTERNAL_TRUE)     \
  X(CV_BRANCH_INTERNAL_FALSE)    \
  X(CV_BRANCH_TRUE)              \
  X(CV_BRANCH_FALSE)             \
  X(CV_STATE_FORK)               \
  X(CV_STATE_CLONE)              \
  X(CV_STATE_REMOVED)            \
  X(CV_RETURN)                   \
  X(CV_STEP_INSTRUCTION)         \
  X(CV_CALL)                     \
  X(CV_CALL_EXTERNAL)            \
  X(CV_TRANSFER_TO_BASICBLOCK)   \
  X(CV_TRANSFER_TO_BASICBLOCK_LOOP)   \
  X(CV_BASICBLOCK_CHANGED)       \
  X(CV_BASICBLOCK_ENTRY)         \
  X(CV_SOCKET_CREATE)            \
  X(CV_SOCKET_SHUTDOWN)          \
  X(CV_SOCKET_WRITE)             \
  X(CV_SOCKET_READ)

#define X(x) x,
enum ExecutionEventType {
  CV_EXECUTION_EVENT_TYPES CV_NULL_EVENT
};
#undef X

class ExecutionEvent {
 public:
  ExecutionEventType event_type;
  CVExecutionState* state;
  CVExecutionState* parent;

  ExecutionEvent(ExecutionEventType t, 
                 CVExecutionState* s, CVExecutionState* p = NULL);

  ExecutionEvent(ExecutionEventType t, 
                 klee::ExecutionState* s, klee::ExecutionState* p = NULL);

  ExecutionEvent(ExecutionEventType t);
};

////////////////////////////////////////////////////////////////////////////////

class ExecutionObserver {
 public:
  ExecutionObserver() {}
  virtual ~ExecutionObserver() {}
  virtual void notify(ExecutionEvent ev) = 0;
};

class ExecutionObserverPrinter : public ExecutionObserver {
 public:
  ExecutionObserverPrinter() {}
  virtual ~ExecutionObserverPrinter() {}
  virtual void notify(ExecutionEvent ev);
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
#endif // CLIVER_EXECUTION_OBSERVER_H
