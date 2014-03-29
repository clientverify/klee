//===-- Training.h ----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// TODO: Remove unused functions
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRAINING_H
#define CLIVER_TRAINING_H

#include "cliver/ExecutionTrace.h"
#include "cliver/SocketEventMeasurement.h"

#include <iostream>
#include <fstream>
#include <set>
#include <vector>

#include <boost/serialization/access.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/unordered_map.hpp>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class CVExecutionState;
class ExecutionStateProperty;
class SocketEvent;
class ClientVerifier;

////////////////////////////////////////////////////////////////////////////////

//struct TrainingObjectFilter {
//  TrainingObjectFilter(unsigned _socket_event_type, 
//                       unsigned _initial_basic_block_id)
//    : socket_event_type(_socket_event_type), 
//      initial_basic_block_id(_initial_basic_block_id) {}
//
//  unsigned socket_event_type;
//  unsigned initial_basic_block_id;
//};
//
//// Comparator for TrainingObjecFilter 
//struct TrainingObjectFilterLT {
//	bool operator()(const TrainingObjectFilter *a, 
//                  const TrainingObjectFilter *b) const;
//};

typedef std::pair<unsigned, unsigned> TrainingObjectFilter;

////////////////////////////////////////////////////////////////////////////////

class TrainingObject;

////////////////////////////////////////////////////////////////////////////////

// Comparator for ExecutionTrace contents
struct TrainingObjectTraceLT {
	bool operator()(const TrainingObject* a, const TrainingObject* b) const;
};

// Comparator for ExecutionTrace lengths
struct TrainingObjectLengthLT {
	bool operator()(const TrainingObject* a, const TrainingObject* b) const;
};

typedef std::set<TrainingObject*, TrainingObjectTraceLT> TrainingObjectSet;
typedef std::vector<TrainingObject*> TrainingObjectList;
typedef std::vector<std::pair<int, TrainingObject*> > TrainingObjectScoreList;

////////////////////////////////////////////////////////////////////////////////

class TrainingObjectData {
 public:
  TrainingObjectData() : message_count(0), edit_distance_matrix(NULL) {}
  std::vector<TrainingObject*> training_objects;
  TrainingObjectSet training_object_set;
  unsigned message_count;
  //std::vector<int> *edit_distance_matrix;
  int *edit_distance_matrix;
  boost::unordered_map<SocketEvent*, TrainingObject*> reverse_socket_event_map;
  std::vector<SocketEvent*> socket_events_by_size;
  boost::unordered_map<SocketEvent*, unsigned> socket_event_indices;

  void select_training_paths_for_message(const SocketEvent *msg, int radius,
                                         SocketEventSimilarity *smeasure,
                                         std::vector<int> &scores,
                                         std::set<TrainingObject*> &selected);
};

//typedef std::map<TrainingObjectFilter*, TrainingObjectData*, TrainingObjectFilterLT> TrainingFilterMap;

typedef boost::unordered_map<TrainingObjectFilter, TrainingObjectData*> TrainingFilterMap;
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

/// Holds a single execution trace and the associated socket event data(s)
class TrainingObject {
 public:
  TrainingObject() {};
  TrainingObject(ExecutionTrace *et) 
      : trace(*et) {}
  TrainingObject(ExecutionTrace *et, SocketEvent *se)
      : trace(*et) { add_socket_event(se); }

  void add_socket_event(SocketEvent *se) {
    socket_event_set.insert(se);
  }

  void read(std::ifstream &is);
  void write(ExecutionStateProperty* property, ClientVerifier* cv);
  void parse_round();

 public:
  //std::set<SocketEvent*> socket_event_set; // std::set of SocketEvent ptrs
  SocketEventDataSet socket_event_set;
  ExecutionTrace trace; 
  std::string name; // Name created during seralization
  int round;

 protected:
  friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
    ar & socket_event_set;
    ar & trace;
    ar & name;
  }
};

std::ostream& operator<<(std::ostream& os, const TrainingObject &tobject);

////////////////////////////////////////////////////////////////////////////////

class TrainingObjectGroup {
 public:
  void insert(const TrainingObject* tobj);

 private:
  SocketEventDataSet socket_event_set_;
  std::vector<const TrainingObject*> training_objects_;
};

////////////////////////////////////////////////////////////////////////////////

class TrainingManager {
 public:

  static void init_score_list(TrainingObjectSet &tobjs,
                              TrainingObjectScoreList &scorelist) {
    TrainingObjectSet::iterator it=tobjs.begin(), ie=tobjs.end();
    for (; it!=ie; ++it) {
      scorelist.push_back(std::make_pair(1.0, *it));
    }
  }

  static void sort_by_similarity_score(const SocketEvent *se, 
                                       TrainingObjectScoreList &scorelist,
                                       SocketEventSimilarity &measure) {
    // Iterate over all of the TrainingObjects and compare their similarity
    for (int i=0; i<scorelist.size(); ++i) {
      int min = INT_MAX;
      std::set<SocketEvent*>::iterator 
          it = scorelist[i].second->socket_event_set.begin(),
          ie = scorelist[i].second->socket_event_set.end();
      for (; it != ie; ++it) {
        int result = measure.similarity_score(se, *it);
        if (result < min) min = result;
      }
      scorelist[i].first = min;
    }
    std::sort(scorelist.begin(), scorelist.end());
  }

  static TrainingObject* find_first_with_score(const SocketEvent *se, 
                                               TrainingObjectScoreList &scorelist,
                                               SocketEventSimilarity &measure,
                                               int first_score = 0) {
    for (int i=0; i<scorelist.size(); ++i) {
      std::set<SocketEvent*>::iterator 
          it = scorelist[i].second->socket_event_set.begin(),
          ie = scorelist[i].second->socket_event_set.end();
      for (; it != ie; ++it) {
        int result = measure.similarity_score(se, *it);
        if (result <= first_score)
          return scorelist[i].second;
      }
    }
    return NULL;
  }


  /// Return a set of TrainingObjects from a list of filenames, removing
  /// TrainingObjects with duplicate ExecutionTraces
  template <class TrainingObjectSetType>
  static void read_files(std::vector<std::string> &filename_list,
                         TrainingObjectSetType &data) {
    
    for (int i=0; i<filename_list.size(); ++i) {
      std::string filename = filename_list[i];

      // Construct input file stream from filename
      std::ifstream is(filename.c_str(), 
                       std::ifstream::in | std::ifstream::binary );

      // If opening the file was successful
      if (is.is_open() && is.good()) {
        TrainingObject* tobj = new TrainingObject();

        // Read serialized TrainingObject
        tobj->read(is);
        tobj->parse_round();

        if (tobj->round >= 1) {

          // Check for duplicates with other TrainingObjects
          typename TrainingObjectSetType::iterator it = data.find(tobj);
          if (data.end() != it) {
            // If duplicate found, insert associated SocketEvents into previously
            // create TrainingObject
            (*it)->socket_event_set.insert(tobj->socket_event_set.begin(),
                                          tobj->socket_event_set.end());

          // Otherwise add new TrainingObject to the data set
          } else {
            data.insert(tobj);
          }

        } else {
          delete tobj;
        }
      } else {
        CVMESSAGE("Error opening: " << filename);
      }

      is.close();
    }
  }

};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_H

