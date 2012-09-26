//===-- TrainingCluster.h ---------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_TRAINING_CLUSTER_H
#define CLIVER_TRAINING_CLUSTER_H

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class TrainingObject;

////////////////////////////////////////////////////////////////////////////////

// Only used at start up
class TrainingObjectDistanceMetric {
 public:
  virtual distance(const TrainingObject* t1, const TrainingObject* t2);
};

// Needs to be fast
class SocketEventDistanceMetric {
 public:
  virtual int distance(const SocketEvent* se1, const SocketEvent* se2);
};

////////////////////////////////////////////////////////////////////////////////

class TrainingObjectCluster {
 public:
  TrainingObjectCluster();

  TrainingObject* center() { return medoid_; }

 private:
  unsigned distance(TrainingObject* tobj) { return distance_map_[tobj]; }

  TrainingObject *medoid_;
  std::map<TrainingObject*, unsigned> distance_map_;
};

////////////////////////////////////////////////////////////////////////////////

template <class TrainingObjectMetric, class SocketEventMetric>
class TrainingObjectClusterManager {
 public:
  TrainingObjectClusterManager();

  void cluster(unsigned cluster_count, std::vector<TrainingObject*>& tobjs) {
    //clusters_.reserve(cluster_count);
    for (unsigned i = 0; i< cluster_count; ++i) {
      clusters_.push_back(new TrainingObjectCluster());
    }

    for (unsigned i = 0; i< tobjs.size(); ++i) {

    }
  }

  void clusters_within_distance(int distance, const SocketEvent* se, 
                                std::vector<TrainingObjectCluster*>& clusters);

  void n_closest_clusters(int count, const SocketEvent* se, 
                          std::vector<TrainingObjectCluster*>& clusters);

 private:
  std::vector<TrainingObjectCluster*> clusters_;
  TrainingObjectMetric* training_object_metric_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_CLUSTER_H

