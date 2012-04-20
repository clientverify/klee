//===-- Training.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRAINING_H
#define CLIVER_TRAINING_H

#include "cliver/ExecutionTrace.h"

#include <iostream>
#include <set>
#include <vector>

namespace boost {void throw_exception(std::exception const& e);}
#include <boost/serialization/access.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class CVExecutionState;
class SocketEvent;
class ClientVerifier;

/// Holds a single execution trace and the associated message
class TrainingObject {
 public:
  TrainingObject() {};
  TrainingObject(ExecutionTrace *et, SocketEvent *se)
      : trace(*et) { add_socket_event(se); }

  void add_socket_event(SocketEvent *se) {
    socket_event_set.insert(se);
  }

  void read(std::ifstream &is);
  void write(CVExecutionState* state, ClientVerifier* cv);

 public:
  std::set<SocketEvent*> socket_event_set; // std::set of SocketEvent ptrs
  ExecutionTrace trace; 
  std::string name; // Name created during seralization
  int id;

 protected:
  friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
    ar & socket_event_set;
    ar & trace;
    ar & name;
  }
};

// Comparator for ExecutionTrace contents
struct TrainingObjectTraceLT {
	bool operator()(const TrainingObject* a, const TrainingObject* b) const;
};

// Comparator for ExecutionTrace lengths
struct TrainingObjectLengthLT {
	bool operator()(const TrainingObject* a, const TrainingObject* b) const;
};

std::ostream& operator<<(std::ostream& os, const TrainingObject &tobject);

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_H

