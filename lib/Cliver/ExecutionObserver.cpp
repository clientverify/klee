//===-- ExecutionObserver.cpp -----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/ExecutionObserver.h"
#include "CVCommon.h"
#include "cliver/CVStream.h"
#include "cliver/CVExecutionState.h"

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

///////////////////////////////////////////////////////////////////////////////

void ExecutionObserverPrinter::notify(ExecutionEvent ev) {
#define X(x) #x,
	static std::string execution_event_types[] = { CV_EXECUTION_EVENT_TYPES };
#undef X

  switch(ev.event_type) {
#define X(x) case x : { \
  CVDEBUG( #x << " " << ev.state->id() << " " \
  << (ev.parent ? ev.parent->id() : 0 ) ); break; }
    CV_EXECUTION_EVENT_TYPES
#undef X
    default:
      break;
  }
}

} // end namespace cliver
