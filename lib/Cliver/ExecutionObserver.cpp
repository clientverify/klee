//===-- ExecutionObserver.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/ExecutionObserver.h"
#include "cliver/CVStream.h"
#include "cliver/CVExecutionState.h"
#include "cliver/ExecutionStateProperty.h"
#include "CVCommon.h"

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

ExecutionEvent::ExecutionEvent(ExecutionEventType t, 
                               CVExecutionState* s, 
                               CVExecutionState* p) 
  : event_type(t), state(s), parent(p) {}

ExecutionEvent::ExecutionEvent(ExecutionEventType t, 
                               klee::ExecutionState* s, 
                               klee::ExecutionState* p)
  : event_type(t), 
    state(static_cast<CVExecutionState*>(s)), 
    parent(static_cast<CVExecutionState*>(p)) {}

ExecutionEvent::ExecutionEvent(ExecutionEventType t)
  : event_type(t), state(NULL), parent(NULL) {}

ExecutionEvent::ExecutionEvent()
  : event_type(CV_NULL_EVENT), state(NULL), parent(NULL) {}

///////////////////////////////////////////////////////////////////////////////

void ExecutionObserverPrinter::notify(ExecutionEvent ev) {
#define X(x) #x,
	static std::string execution_event_types[] = { CV_EXECUTION_EVENT_TYPES };
#undef X

  // Skip common events..
  switch(ev.event_type) {
    case CV_BRANCH:
    case CV_BASICBLOCK_ENTRY:
    case CV_STATE_FORK_TRUE:
    case CV_STATE_FORK_FALSE:
      {
        return;
      }
  }

  switch(ev.event_type) {
#define X(x) case x : { \
  if (ev.state && ev.parent) {\
    CVMESSAGE( #x << " " << *ev.state << " parent: " << *ev.parent );\
  } else if (ev.state) {\
    CVMESSAGE( #x << " " << *ev.state );\
  } else {\
    CVMESSAGE( #x );\
  } break; }

    CV_EXECUTION_EVENT_TYPES

#undef X
    default:
      break;
  }
}

} // end namespace cliver
