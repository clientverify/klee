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

#include "cliver/TrainingFilter.h"
#include "cliver/Cluster.h"
#include "cliver/SocketEventMeasurement.h"
#include "cliver/EditDistance.h"
#include "cliver/KExtensionTree.h"
#include "CVCommon.h"
#include "CVStream.h"

namespace cliver {

class TrainingObject;
class SocketEvent;

////////////////////////////////////////////////////////////////////////////////

extern llvm::cl::opt<unsigned> ClusterSize;
extern llvm::cl::opt<unsigned> MaxKExtension;

////////////////////////////////////////////////////////////////////////////////

typedef Score<ExecutionTrace, BasicBlockID, int> ExecutionTraceScore;
typedef EditDistanceRow<ExecutionTraceScore,ExecutionTrace,int> ExecutionTraceEDR;
typedef LevenshteinRadixTree<ExecutionTrace, BasicBlockID> LevEdTree;
typedef KExtensionTree<ExecutionTrace, BasicBlockID> KExtEdTree;

class TrainingObjectDistanceMetric : public cliver::DistanceMetric<TrainingObject> {
 public:
  typedef std::pair<const TrainingObject*, const TrainingObject*> TObjPair;
  typedef boost::unordered_map<TObjPair, double> DistanceMap;

  void init(std::vector<TrainingObject*> &datalist) {}

  double distance(const TrainingObject* t1, const TrainingObject* t2) {
    TObjPair tobj_pair(std::min(t1,t2), std::max(t1,t2));
    double dist = -1;

    #pragma omp critical 
    if (distance_map_.count(tobj_pair)) {
      dist = distance_map_[tobj_pair];
    }

    if (dist == -1) {
      // No need to compute distance if trace size difference is
      // greater than MaxKExtension
      if ((std::max(t1->trace.size(),t2->trace.size()) -
          std::min(t1->trace.size(),t2->trace.size())) > MaxKExtension) {
        dist = double(INT_MAX);
      } else {
          
        KExtEdTree ked;
        ked.add_data(*(const_cast<ExecutionTrace*>(&(t1->trace))));
        ked.init(MaxKExtension);
        ked.update_suffix(*(const_cast<ExecutionTrace*>(&(t2->trace))));
        dist = (double)ked.min_distance();
      }

      #pragma omp critical 
      { distance_map_[tobj_pair] = dist; }
    }

    return dist;
  }
 private:
  DistanceMap distance_map_;
};

class SimpleTrainingObjectDistanceMetric : public cliver::DistanceMetric<TrainingObject> {
 public:
  void init(std::vector<TrainingObject*> &datalist) {}

  double distance(const TrainingObject* t1, const TrainingObject* t2) {

    ExecutionTraceEDR edr(t1->trace, t2->trace);
    double distance = (double)edr.compute_editdistance();
    return distance;
  }
 private:

};

// Needs to be fast
class SocketEventDistanceMetric {
 public:
  typedef std::pair<const SocketEvent*, const SocketEvent*> SEPair;
  typedef boost::unordered_map<SEPair, double> DistanceMap;

  SocketEventDistanceMetric() {
    similarity_measure_ = new SocketEventSimilarityDataOnly();
  }

  ~SocketEventDistanceMetric() {
    delete similarity_measure_;
  }

  virtual void init(std::vector<SocketEvent*> &datalist) {}

  virtual double distance(const SocketEvent* se1, const SocketEvent* se2) {
    SEPair se_pair(std::min(se1,se2), std::max(se1,se2));

    if (distance_map_.count(se_pair))
      return distance_map_[se_pair];

    double distance = (double)similarity_measure_->similarity_score(se1, se2);
    distance_map_[se_pair] = distance;
    return distance;
  }

 private:
  SocketEventSimilarity* similarity_measure_;
  DistanceMap distance_map_;
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

template <class TrainingObjectClusterer, class SocketEventClusterer>
class TrainingObjectClusterManager {
 public:

  //typedef Clusterer<SocketEvent, SocketEventMetric> SocketEventClusterer;
  //typedef Clusterer<TrainingObject, TrainingObjectMetric> TrainingObjectClusterer;

  TrainingObjectClusterManager(size_t training_object_cluster_count,
                               size_t socket_event_cluster_count)
     : training_object_cluster_count_(training_object_cluster_count),
       socket_event_cluster_count_(socket_event_cluster_count) {}

  void cluster_socket_events(unsigned cluster_count, 
                             SocketEventDataSet &se_set_in,
                             SocketEventDataSet &se_set_out) {

    SocketEventClusterer *clusterer = new SocketEventClusterer();
    std::vector<SocketEvent*> se_vec(se_set_in.begin(), se_set_in.end());
    clusterer->add_data(se_vec);
    clusterer->cluster(cluster_count);

    for (unsigned i = 0; i< clusterer->count(); ++i) {
      std::vector<SocketEvent*> tmp_vec;
      SocketEvent* se = clusterer->get_medoid(i);
      se_set_out.insert(se);
    }
  }

  void cluster(std::vector<TrainingObject*>& tobjs) {

    TrainingObjectListMap tf_map;

    // Filter the TrainingObjects
    foreach (TrainingObject* tobj, tobjs) {
      TrainingFilter tf(tobj);
      CVMESSAGE("TF: " << tf);
      tf_map[tf].push_back(tobj);
    }

    int group_count = 0;
    foreach (TrainingObjectListMap::value_type &data_vec, tf_map) {

      if (data_vec.first.type == SocketEvent::SEND) {
        group_count++;

        TrainingObjectClusterer *clusterer = new TrainingObjectClusterer();
        CVMESSAGE("Adding " << data_vec.second.size() << " data objects into "
                  << std::min(training_object_cluster_count_,data_vec.second.size())
                  << " clusters.");
        size_t min_data_size=INT_MAX, max_data_size=0;
        for (unsigned i=0;i<data_vec.second.size();++i) {
          min_data_size = std::min(min_data_size, data_vec.second[i]->trace.size());
          max_data_size = std::max(max_data_size, data_vec.second[i]->trace.size());
        }
        CVMESSAGE("Min etrace size: " << min_data_size 
                  << ", max etrace size: " << max_data_size);

        clusterer->add_data(data_vec.second);
        CVMESSAGE("Starting clustering...");
        clusterer->cluster(training_object_cluster_count_);
        //clusterer->print_clusters();
        
        CVMESSAGE("Group " << group_count << " has " 
                  << data_vec.second.size() << " elements in "
                  << clusterer->count() << " clusters.");

        #pragma omp parallel for schedule(dynamic)
        for (unsigned i = 0; i< clusterer->count(); ++i) {
          std::vector<TrainingObject*> tobjs_vec;

          // Get all of the TrainingObjects assigned to this cluster
          clusterer->get_cluster(i, tobjs_vec);
          TrainingObject* tobj_medoid = clusterer->get_medoid(i);

          ExecutionTrace* et = &(tobj_medoid->trace);

          // Extract all of the socket events from the cluster
          // Create a new TrainingObject that represents the medoid, and all of
          //   the associated socket event objects
          TrainingObject* tobj_cluster = new TrainingObject(et);

          TrainingObject *tobj = NULL;
          SocketEvent *se = NULL;

          SocketEventDataSet se_set;
          SocketEventDataSet clustered_se_set;

          foreach(tobj, tobjs_vec) {
            foreach(se, tobj->socket_event_set) {
              se_set.insert(se);
            }
          }

          if (se_set.size() > socket_event_cluster_count_) {
            #pragma omp critical 
            CVMESSAGE("\tClustering " << se_set.size() << " socket events for medoid " << i);
            cluster_socket_events(socket_event_cluster_count_, se_set, clustered_se_set);

          } else {
            clustered_se_set = se_set;
          }

          foreach(se, clustered_se_set) {
            tobj_cluster->add_socket_event(se);
          }
        
          TrainingFilter tf(tobj_cluster);
          #pragma omp critical 
          { cluster_map_[tf].push_back(tobj_cluster); }
        }
        
        delete clusterer;
      }
    }

    //foreach (TrainingObjectListMap::value_type &data, cluster_map_) {
    //  std::cout << data.first << " " << data.second.size() << " ";
    //  for (size_t i=0; i < data.second.size(); ++i) {
    //    std::cout << "(" << data.second[i]->trace.size() << " "
    //        << data.second[i]->socket_event_set.size() << "), ";
    //  }
    //  std::cout << "\n";
    //}
  }

  void sorted_clusters(const SocketEvent* se, 
                       TrainingFilter &filter,
                       TrainingObjectScoreList& sorted_clusters,
                       SocketEventSimilarity &measure) {

    // XXX This case likely needs to be handled
    assert(cluster_map_.count(filter));

    std::vector<TrainingObject*>& clusters = cluster_map_[filter];

    for (size_t i=0; i<clusters.size(); ++i) {
      SocketEventDataSet::iterator it = clusters[i]->socket_event_set.begin(),
          ie = clusters[i]->socket_event_set.end();
      int min = INT_MAX;
      for (; it != ie; ++it) {
        // TODO change to double!
        int result = measure.similarity_score(se, *it);
        if (result < min) min = result;
      }
      sorted_clusters.push_back(std::make_pair(min, clusters[i]));
    } 
    std::sort(sorted_clusters.begin(), sorted_clusters.end());
  }

  void clusters_within_distance(double distance, const SocketEvent* se, 
                                std::vector<TrainingObject*>& clusters) {
  }

  void n_closest_clusters(unsigned n, const SocketEvent* se, 
                          std::vector<TrainingObject*>& clusters) {
  }

  bool check_filter(TrainingFilter &filter) {
    return cluster_map_.count(filter);
  }

  void all_clusters_distance(TrainingFilter &tf,
                             TrainingObject* tobj,
                             TrainingObjectScoreList& sorted_clusters) {
    SimpleTrainingObjectDistanceMetric metric;

    if (cluster_map_.count(tf) == 0) {
      return;
    }

    TrainingObjectListMap::iterator it = cluster_map_.find(tf);
    TrainingObjectListMap::value_type &data = *it;
    //foreach (TrainingObjectListMap::value_type &data, cluster_map_) {
      #pragma omp parallel for schedule(dynamic)
      for (unsigned i=0; i<data.second.size(); ++i) {
        int result = metric.distance(tobj, data.second[i]);
        #pragma omp critical 
        { sorted_clusters.push_back(std::make_pair(result, data.second[i]));}
      }
    //}
    std::sort(sorted_clusters.begin(), sorted_clusters.end());
  }

 private:
  TrainingObjectListMap cluster_map_;
  size_t training_object_cluster_count_;
  size_t socket_event_cluster_count_;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

#endif // CLIVER_TRAINING_CLUSTER_H

