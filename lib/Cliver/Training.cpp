//===-- Training.cpp --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//

#include "cliver/Training.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/Socket.h"
#include "CVCommon.h"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// Write Training object to file in cliver's output directory
void TrainingObject::write(CVExecutionState* state, 
                           ClientVerifier* cv) {

  // Create an unique identifing name for this path
  std::stringstream name_ss;
  name_ss << "round_" << std::setw(4) << std::setfill('0') << cv->round();
  name_ss << "_length_" << std::setw(6) << std::setfill('0') << trace.size();
  name_ss << "_state_" <<  state->id() << ".tpath";

  // Set member var
  name = std::string(name_ss.str());

  // Write object to a sub dir so that # files is not greater than the FS limit
  std::stringstream subdir_ss;
  subdir_ss << "round_" << std::setw(4) << std::setfill('0') << cv->round();
  std::string subdir = subdir_ss.str();

  // Open file ../output_directory/subdir/name
  std::ostream *file = cv->openOutputFileInSubDirectory(name, subdir);

  // Write to file using boost::serialization
	boost::archive::binary_oarchive oa(*file);
  oa << *this;

  // Close file
  static_cast<std::ofstream*>(file)->close();
}

/// Read file using boost::serialization
void TrainingObject::read(std::ifstream &is) {
	boost::archive::binary_iarchive ia(is);
  ia >> *this;
}

////////////////////////////////////////////////////////////////////////////////

bool TrainingObjectTraceLT::operator()(const TrainingObject* a, 
                                       const TrainingObject* b) const {
  return (a->trace) < (b->trace);
}


bool TrainingObjectLengthLT::operator()(const TrainingObject* a, 
                                        const TrainingObject* b) const {
  return a->trace.size() < b->trace.size();
}

////////////////////////////////////////////////////////////////////////////////

/// Print TrainingObject info
std::ostream& operator<<(std::ostream& os, const TrainingObject &tobject) {
  os << "(trace id:" << tobject.id << ") "
     << "(length:" << tobject.trace.size() << ") "
     << "(" << tobject.name << ") ";
  os << "[socket_events: ";
  foreach (const SocketEvent* socket_event, tobject.socket_event_set) {
    os << *socket_event << ", ";
  }
  os << "]";
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

