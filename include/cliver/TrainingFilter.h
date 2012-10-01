//===-- TrainingFilter.h ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRAINING_FILTER_H
#define CLIVER_TRAINING_FILTER_H

#include <boost/unordered_map.hpp>
#include <vector>

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class TrainingObject;

////////////////////////////////////////////////////////////////////////////////

struct TrainingFilter {
  TrainingFilter();
  TrainingFilter(TrainingObject* tobj);

  // Extraction methods used to create different filters
  unsigned extract_socket_event_type(const TrainingObject *tobj);
  unsigned extract_initial_basic_block_id(const TrainingObject *tobj);

  // Filter variables 
  unsigned type; // socket event type
  unsigned initial_basic_block_id; // initial instruction id
};

std::ostream& operator<<(std::ostream& os, const TrainingFilter &tf);

////////////////////////////////////////////////////////////////////////////////

class TrainingFilterFactory {
 public:
  static TrainingFilter* create(const TrainingObject* tobj);
};

////////////////////////////////////////////////////////////////////////////////

struct tf_equal : std::binary_function<TrainingFilter, TrainingFilter, bool> {

  bool operator()(cliver::TrainingFilter const& f1, 
                  cliver::TrainingFilter const& f2) const {
    return f1.type == f2.type && 
          f1.initial_basic_block_id == f2.initial_basic_block_id;
  }

};

struct tf_hash : std::unary_function<TrainingFilter, std::size_t> {

  std::size_t operator()(cliver::TrainingFilter const& f) const {
    std::size_t seed = 0;
    boost::hash_combine(seed, f.type);
    boost::hash_combine(seed, f.initial_basic_block_id);
    return seed;
  }

};

typedef boost::unordered_map<TrainingFilter, 
        TrainingObject*, tf_hash, tf_equal > TrainingObjectMap;

typedef boost::unordered_map<TrainingFilter,  
        std::vector<TrainingObject*>, tf_hash, tf_equal > TrainingObjectListMap;

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_FILTER_H

