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
//  At startup, load training paths and socket events.  
//
//  0. Filter the training paths by first instruction and by termination type
//  0.1 Hash the training objects into groups
//  1. Cluster the filtered training paths groups according to edit distance. 
//  2. Place each training path group into a TrainingObjectCluster
//  3. During execution, select cluster by choosing which cluster best matches 
//
// Alternate:
//  1. Add each training object to corresponding set in the filter map
//  2. For each set, cluster the execution traces into 2, 4, ... 32 clusters
//  3. (optional) For each cluster, cluster the corresponding socket events 
//  4. Create a new TrainingObject for each medoid and appropriate socketevents 
//  5. Store the TrainingObjects in the TrainingObjectManager
////////////////////////////////////////////////////////////////////////////////

#include <cliver/TrainingFilter.h>
#include <cliver/Cluster.h>
#include "cliver/EditDistance.h"
#include "CVCommon.h"

namespace cliver {

class TrainingObject;

////////////////////////////////////////////////////////////////////////////////

typedef Score<ExecutionTrace, BasicBlockID, int> ExecutionTraceScore;
typedef EditDistanceRow<ExecutionTraceScore,ExecutionTrace,int> ExecutionTraceEDR;

// Only used at start up
class TrainingObjectDistanceMetric : public cliver::DistanceMetric<TrainingObject> {
 public:
  typedef std::pair<const TrainingObject*, const TrainingObject*> TObjPair;
  typedef boost::unordered_map<TObjPair, double> DistanceMap;

  void init(std::vector<TrainingObject*> &datalist) {}

  double distance(const TrainingObject* t1, const TrainingObject* t2) {
    TObjPair tobj_pair(std::min(t1,t2), std::max(t1,t2));

    if (distance_map_.count(tobj_pair))
      return distance_map_[tobj_pair];

    ExecutionTraceEDR edr(t1->trace, t2->trace);
    double distance = (double)edr.compute_editdistance();
    distance_map_[tobj_pair] = distance;
    return distance;
    //return (double)edr.compute_editdistance();
  }
 private:
  DistanceMap distance_map_;
};

// Needs to be fast
class SocketEventDistanceMetric {
 public:
  virtual int distance(const SocketEvent* se1, const SocketEvent* se2) {
    return 0;
  }
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
  TrainingObjectClusterManager() {}

  void cluster(unsigned cluster_count, std::vector<TrainingObject*>& tobjs) {

    TrainingObjectListMap tf_map;
    // Filter the TrainingObjects
    foreach (TrainingObject* tobj, tobjs) {
      TrainingFilter tf(tobj);
      tf_map[tf].push_back(tobj);
    }

    foreach (TrainingObjectListMap::value_type &data_vec, tf_map) {
      std::cout << "TrainingObjectListMap: " << data_vec.second.size() << "\n";
    }


    foreach (TrainingObjectListMap::value_type &data_vec, tf_map) {

      if (data_vec.second.size() >= cluster_count) {
        TrainingObjectMetric metric;
        Clusterer<TrainingObject, TrainingObjectMetric>*
        clusterer_ = new Clusterer<TrainingObject, TrainingObjectMetric>();
        clusterer_->init(cluster_count, &metric);
        clusterer_->add_data(data_vec.second);

        std::cout << "Clustering started.\n";
        clusterer_->cluster();
        clusterer_->print_clusters();
        std::cout << "Clustering finished.\n";
        delete clusterer_;
      }

      /*
      for (unsigned i = 0; i< cluster_count; ++i) {
        std::vector<TrainingObject*> tobjs_vec;
        TrainingObject* tobj_medoid;
        TrainingObject* tobj_cluster;
        ExecutionTrace* et;

        clusterer_->get_cluster(i, tobjs_vec);
        tobj_medoid = clusterer_->get_medoid(i, tobjs);
        et = &(tobj_medoid->trace);

        // Extract all of the socket events from the cluster
        // Create a new TrainingObject that represents the medoid, and all of
        //   the associated socket event objects
        tobj_cluster = new TrainingObject(et);

        TrainingObject *tobj = NULL
        SocketEvent* se = NULL;

        foreach(tobj, tobjs_vec) {
          foreach(se, tobj->socket_event_set) {
            tobj_cluster->add_socket_event(se);
          }
        }
        cluster_.push_back(tobj_cluster);
      }
      */
    }
  }

  void clusters_within_distance(int distance, const SocketEvent* se, 
                                std::vector<TrainingObjectCluster*>& clusters);

  void n_closest_clusters(int count, const SocketEvent* se, 
                          std::vector<TrainingObjectCluster*>& clusters);

 private:
  std::vector<TrainingObject*> clusters_;
  //std::vector<TrainingObjectCluster*> clusters_;
  TrainingObjectMetric* training_object_metric_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_CLUSTER_H

