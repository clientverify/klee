//===-- TrainingFilter.cpp --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/TrainingFilter.h"
#include "cliver/Training.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/NetworkManager.h"
#include "cliver/Socket.h"

#include "CVCommon.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

TrainingFilter::TrainingFilter() : type(0), initial_basic_block_id(0) {}

TrainingFilter::TrainingFilter(CVExecutionState* state) {
  type = extract_socket_event_type(state);
  initial_basic_block_id = extract_initial_basic_block_id(state);
}

TrainingFilter::TrainingFilter(TrainingObject* tobj) {
  type = extract_socket_event_type(tobj);
  initial_basic_block_id = extract_initial_basic_block_id(tobj);
}

unsigned TrainingFilter::extract_socket_event_type(CVExecutionState* state) {
  assert(state && state->network_manager());
  return state->network_manager()->socket()->event().type;
}

unsigned TrainingFilter::extract_socket_event_type(const TrainingObject*tobj) {
  SocketEvent::Type se_type;
  assert(tobj->socket_event_set.size() > 0);

  foreach (SocketEvent* se, tobj->socket_event_set) {
    if (se != *(tobj->socket_event_set.begin()))
      assert(se_type == se->type);
    else
      se_type = se->type;
  }
  return se_type;
}

unsigned TrainingFilter::extract_initial_basic_block_id(CVExecutionState* state) {
  if (ClientModelFlag == XPilot) 
    return state->get_current_basic_block();
  return 0;
}

unsigned TrainingFilter::extract_initial_basic_block_id(const TrainingObject* tobj) {
  if (ClientModelFlag == XPilot) 
    return tobj->trace[0];
  return 0;
}

////////////////////////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream& os, const TrainingFilter &tf) {
  os << "(type: " << tf.type << ") (IBB: " << tf.initial_basic_block_id << ")";
  return os;
}

////////////////////////////////////////////////////////////////////////////////

//TrainingFilter* TrainingFilterFactory::create(const TrainingObject *tobj) {
//
//  TrainingFilter* tf = new TrainingFilter();
//  tf->type = tf->extract_socket_event_type(tobj);
//
//  if (ClientModelFlag == XPilot) 
//    tf->initial_basic_block_id = tf->extract_initial_basic_block_id(tobj);
//
//  return tf;
//}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

