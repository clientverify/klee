//===-- JaccardTree.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_JACCARD_TREE_H
#define CLIVER_JACCARD_TREE_H

#include "cliver/RadixTree.h"
#include "cliver/EditDistanceTree.h"
#include <limits.h>

#include <vector>
#include <algorithm>
#include <memory>
#include <exception>
#include <iostream>
#include <cmath>

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

/// JaccardTree: Used to compute the Jaccard distance from a single
/// sequence s to a collection of sequences stored.
/// NOTA BENE: It's not actually a tree, but it implements the
/// EditDistanceTree interface.

template <class Sequence, class T>
class JaccardTree
: public EditDistanceTree<Sequence,T> {

 public:
  JaccardTree() :
    guide_sets(std::make_shared<std::vector<std::set<T> > >())
  {
  }

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int k) { }

  // Warning: this CANNOT be called after an update/update_suffix()
  virtual void add_data(Sequence &s) {
    if (guide_paths_immutable) {
      std::cerr << "Error: too late to add more guide paths.\n";
      return;
    }
    guide_sets->push_back(std::set<T>(s.begin(), s.end()));
    intersection_counts.push_back(0);
    union_counts.push_back(0);
    distances.push_back(MAX_JACCARD_DISTANCE);
  }

  /// Set current sequence to s and compute Jaccard distance to all guide paths.
  virtual void update(Sequence &s) {
    using namespace std;
    guide_paths_immutable = true;

    num_inserted = (int)s.size();
    current_set = std::set<T>(s.begin(), s.end());
    min_distance_ = MAX_JACCARD_DISTANCE;
    for (size_t i = 0; i < guide_sets->size(); ++i) {
      auto& guide_set = (*guide_sets)[i];
      vector<T> this_intersection;
      set_intersection(current_set.begin(), current_set.end(),
		       guide_set.begin(), guide_set.end(),
		       back_inserter(this_intersection));
      vector<T> this_union;
      set_union(current_set.begin(), current_set.end(),
		guide_set.begin(), guide_set.end(),
		back_inserter(this_union));
      intersection_counts[i] = (int)this_intersection.size();
      union_counts[i] = (int)this_union.size();
      distances[i] = compute_jaccard(intersection_counts[i], union_counts[i]);
      min_distance_ = min(min_distance_, distances[i]);
    }
  }

  /// Compute the minimum Jaccard distance from s' to all guide paths
  /// where s' is equal to the previously computed sequence + s
  virtual void update_suffix(Sequence &s) {
    using namespace std;
    guide_paths_immutable = true;
    if (s.size() == 0)
      return;
    num_inserted += (int)s.size();

    // For each element of the update sequence, ...
    for (auto it = s.begin(); it != s.end(); ++it) {
      auto elt = *it;

      // ... check if no change is necessary, and ...
      if (current_set.count(elt) == 1) {
	continue;
      }

      // ... update the counts and distance to each guide path.
      current_set.insert(elt);
      min_distance_ = MAX_JACCARD_DISTANCE;
      for (size_t i = 0; i < guide_sets->size(); ++i) {
        if ((*guide_sets)[i].count(elt) == 1) {
	  // new elt already in guide set: union unchanged, intersection++
	  intersection_counts[i] += 1;
	}
	else {
	  // new elt not in guide set: union++, intersection unchanged
	  union_counts[i] += 1;
	}
	distances[i] = compute_jaccard(intersection_counts[i], union_counts[i]);
	min_distance_ = min(min_distance_, distances[i]);
      }
    }
  }

  virtual void update_element(T t) {
    Sequence suffix;
    suffix.insert(suffix.end(), t);
    update_suffix(suffix);
  }

  // NOTE: since we must return an integer, we scale the actual Jaccard
  // distance from [0.0, 1.0] to a large range and embed into the integers.
  virtual int min_distance() { return (int)(min_distance_ * SCALE_FACTOR); }
  virtual double min_distance_noscale() { return min_distance_; }

  virtual int row() { return num_inserted; }

  virtual void delete_shared_data() {} // unnecessary

  virtual EditDistanceTree<Sequence,T>* clone_edit_distance_tree() {
    return new JaccardTree<Sequence,T>(*this); // use default copy constructor
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  static int test()
  {
    using namespace std;
    auto jt = JaccardTree<string,char>();
    if (jt.row() != 0)
      return 1;
    int ret = 0;
    double TOL = 1e-6; // floating point tolerance
    double d;

    string x, y;
    x = "hello"; jt.add_data(x); cout << "Added guide path: " << x << '\n';
    x = "world!"; jt.add_data(x); cout << "Added guide path: " << x << '\n';
    x = "foo"; jt.add_data(x); cout << "Added guide path: " << x << '\n';
    x = ""; jt.add_data(x); cout << "Added guide path: <empty string>\n";

    y = ""; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of <empty string>: " << d << '\n';
    if (d != 0.0)
      ret = 2;

    y = "hello"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (d != 0.0)
      ret = 2;

    y = "world"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 1.0/6.0) > TOL)
      ret = 3;

    y = "hello world!"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 1.0/3.0) > TOL)
      ret = 3;

    y = "foobar"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 3.0/5.0) > TOL)
      ret = 3;

    cout << "min_distance (with scaling) of " << y << ": " <<
      jt.min_distance() << '\n';

    return ret;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  double compute_jaccard(int intersection_count, int union_count) {
    if (union_count == 0)
      return 0.0;
    else
      return 1.0 - ((double)intersection_count/(double)union_count);
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  const double SCALE_FACTOR = 1e7; // since we must return an integer
  const double MAX_JACCARD_DISTANCE = 1.0;
  double min_distance_ = 1.0;
  int num_inserted = 0; // may exceed current_set.size() if there are repeats
  std::set<T> current_set;
  std::vector<int> intersection_counts;
  std::vector<int> union_counts;
  std::vector<double> distances;

  // The following is considered immutable after all add_data() calls
  // have been made.
  std::shared_ptr<std::vector<std::set<T> > > guide_sets;
  bool guide_paths_immutable = false;
};

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_JACCARD_TREE_H

