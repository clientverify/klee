//===-- Training.cpp --------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/Training.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionTrace.h"
#include "cliver/ExecutionStateProperty.h"
#include "cliver/Socket.h"
#include "../CVCommon.h"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <string>
#include <vector>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// Write Training object to file in cliver's output directory
void TrainingObject::write(ExecutionStateProperty* property, 
                           ClientVerifier* cv) {

  // Create an unique identifing name for this path
  std::stringstream name_ss;
  name_ss << "round_" << std::setw(4) << std::setfill('0') << property->round;
  name_ss << "_length_" << std::setw(6) << std::setfill('0') << trace.size();
  name_ss << "_sp_" << property << ".tpath";

  // Set member var
  name = std::string(name_ss.str());

  // Write object to a sub dir so that # files is not greater than the FS limit
  std::stringstream subdir_ss;
  subdir_ss << "round_" << std::setw(4) << std::setfill('0') << property->round;
  std::string subdir = subdir_ss.str();

  // Open file ../output_directory/subdir/name
  std::ostream *file = cv->openOutputFileInSubDirectory(name, subdir);

  // Write to file using boost::serialization
  CVMESSAGE("Writing " << name << " to " << subdir);

  // Write out compressed file
  boost::iostreams::filtering_streambuf<boost::iostreams::output> out;
  out.push(boost::iostreams::gzip_compressor());
  out.push(*file);
  boost::archive::binary_oarchive oa(out);
  oa << *this;

  // Without compression
  //boost::archive::binary_oarchive oa(*file);
  //oa << *this;

  // Close file
  static_cast<std::ofstream*>(file)->close();
}

/// Read file using boost::serialization
void TrainingObject::read(std::ifstream &is) {
  // Read in compressed file
  boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
  in.push(boost::iostreams::gzip_decompressor());
  in.push(is);
  boost::archive::binary_iarchive ia(in);
  ia >> *this;

  // Without compression
  //boost::archive::binary_iarchive ia(is);
  //ia >> *this;
}

// Extract the round index from string name
void TrainingObject::parse_round() {
  std::vector<std::string> fields;
  boost::split(fields, this->name, boost::is_any_of("_"));
  round = boost::lexical_cast<int>(fields[1]);
  round--;
}

////////////////////////////////////////////////////////////////////////////////

//bool TrainingObjectFilterLT::operator()(const TrainingObjectFilter *a, 
//                                        const TrainingObjectFilter *b) const {
//  if (a->socket_event_type < b->socket_event_type)
//    return true;
//
//  return a->initial_basic_block_id < b->initial_basic_block_id;
//}

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

void TrainingObjectData::select_training_paths_for_message(
    const SocketEvent *msg, int radius, 
    SocketEventSimilarity* smeasure,
    std::vector<int> &scores,
    std::set<TrainingObject*> &selected) {
  if (edit_distance_matrix == NULL) {
    selected.insert(training_objects.begin(), training_objects.end());
  }

  std::set<SocketEvent*> worklist(socket_events_by_size.begin(),
                                  socket_events_by_size.end());

  //std::vector<int> scores(message_count, -1);
  if (scores.empty()) {
    scores = std::vector<int>(message_count, -1);
  }

  unsigned tri_ineq_count = 0, overlap_count = 0;
  while (!worklist.empty()) {
    // Select any socket event (the smallest)?
    SocketEvent *se = *(worklist.begin());
    unsigned se_index = socket_event_indices[se];
    // remove se from the worklist
    worklist.erase(se);

    int score = scores[se_index];
    if (score == -1 || score == INT_MAX) {
      // Compute the distance score
      score = smeasure->similarity_score(msg, se);
      scores[se_index] = score;
      //CVMESSAGE("Computed msg/msg distance measure: " << score);
    }

    // If score is less than radius, we will select the paths it corresponds to
    if (score < radius) {
      selected.insert(reverse_socket_event_map[se]);
      for (unsigned i=0; i<message_count; ++i) {
        SocketEvent *se_i = socket_events_by_size[i];
        // If we haven't already computed distance, or eliminated this se
        if (scores[i] == -1) {
          TrainingObject* tobj = reverse_socket_event_map[se_i];
          if (selected.count(tobj) != 0) {
            scores[i] = INT_MAX;
            worklist.erase(se_i);
            overlap_count++;
          } else if (i != se_index && edit_distance_matrix != NULL) {
            unsigned row = i*message_count;
            int ed = edit_distance_matrix[row + se_index];
            if (ed > score + radius) {
              scores[i] = INT_MAX;
              worklist.erase(se_i);
              tri_ineq_count++;
            }
          }
        }
      }
    }
  }
  CVMESSAGE("Eliminated " << tri_ineq_count << " via triangle ineq. and eliminated "
            << overlap_count << " via path overlap.");
}

////////////////////////////////////////////////////////////////////////////////

/// Print TrainingObject info
std::ostream& operator<<(std::ostream& os, const TrainingObject &tobject) {
  os << "(length:" << tobject.trace.size() << ") "
     << "(" << tobject.name << ") ";
  os << "[socket_events (" << tobject.socket_event_set.size() << "): ";
  foreach (const SocketEvent* socket_event, tobject.socket_event_set) {
    os << *socket_event << ", ";
  }
  os << "]";
  return os;
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

