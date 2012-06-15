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
#include "cliver/SocketEventMeasurement.h"

#include <iostream>
#include <fstream>
#include <set>
#include <vector>

namespace boost {void throw_exception(std::exception const& e);}
#include <boost/serialization/access.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class CVExecutionState;
class ExecutionStateProperty;
class SocketEvent;
class ClientVerifier;

////////////////////////////////////////////////////////////////////////////////

/// Holds a single execution trace and the associated socket event data(s)
class TrainingObject {
 public:
  TrainingObject() {};
  TrainingObject(ExecutionTrace *et, SocketEvent *se)
      : trace(*et) { add_socket_event(se); }

  void add_socket_event(SocketEvent *se) {
    socket_event_set.insert(se);
  }

  void read(std::ifstream &is);
  void write(ExecutionStateProperty* property, ClientVerifier* cv);

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

typedef std::set<TrainingObject*, TrainingObjectTraceLT> TrainingObjectSet;
typedef std::vector<TrainingObject*> TrainingObjectList;
typedef std::vector<std::pair<double, TrainingObject*> > TrainingObjectScoreList;

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
    // to 
    for (int i=0; i<scorelist.size(); ++i) {
      double min = 1.0;
      std::set<SocketEvent*>::iterator 
          it = scorelist[i].second->socket_event_set.begin(),
          ie = scorelist[i].second->socket_event_set.end();
      for (; it != ie; ++it) {
        double result = measure.similarity_score(se, *it);
        if (result < min) min = result;
      }
      scorelist[i].first = min;
    }
    std::sort(scorelist.begin(), scorelist.end());
  }

  /// Return a set of TrainingObjects from a list of filenames, removing
  /// TrainingObjects with duplicate ExecutionTraces
  static void read_files(std::vector<std::string> &filename_list,
                         TrainingObjectSet &data) {
    
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

        // Assign TrainingObject a unique id
        tobj->id = ++current_id;

        // Check for duplicates with other TrainingObjects
        TrainingObjectSet::iterator it = data.find(tobj);
        if (data.end() != it) {
          // If duplicate found, insert associated SocketEvents into previously
          // create TrainingObject
          (*it)->socket_event_set.insert(tobj->socket_event_set.begin(),
                                         tobj->socket_event_set.end());

        // Otherwise add new TrainingObject to the data set
        } else {
          data.insert(tobj);
        }
      }

      is.close();
    }
  }

  // Store next TrainingObject id
  static int current_id;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_H

