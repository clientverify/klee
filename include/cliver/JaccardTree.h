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
    using namespace std;
    if (guide_paths_immutable) {
      std::cerr << "Error: too late to add more guide paths.\n";
      return;
    }
    guide_sets->push_back(std::set<T>(s.begin(), s.end()));
    intersection_counts.push_back(0);
    union_counts.push_back((int)guide_sets->back().size());
    double d = compute_jaccard(intersection_counts.back(), union_counts.back());
    distances.push_back(d);
    min_distance_ = min(min_distance_, d);
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

    y = "hello"; jt.update_suffix(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (d != 0.0)
      ret = 2;

    y = ""; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of <empty string>: " << d << '\n';
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

    y = "foofood"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 1.0/3.0) > TOL)
      ret = 3;

    cout << "min_distance (with scaling) of " << y << ": " <<
      jt.min_distance() << '\n';

    return ret;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  double compute_jaccard(int intersection_count, int union_count) const
  {
    if (union_count == 0)
      return 0.0;
    else
      return 1.0 - ((double)intersection_count/(double)union_count);
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  const double SCALE_FACTOR = 1e9; // since we must return an integer
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

///////////////////////////////////////////////////////////////////////////////

/// MultiSetJaccardTree: Used to compute the Multi-set Jaccard
/// distance from a single sequence s to a collection of sequences
/// stored.  NOTA BENE: It's not actually a tree, but it implements
/// the EditDistanceTree interface.

template <class Sequence, class T>
class MultiSetJaccardTree
: public EditDistanceTree<Sequence,T> {

 public:
  MultiSetJaccardTree() :
    guide_multisets(std::make_shared<std::vector<std::map<T,int> > >())
  {}

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int k) { }

  // Warning: this CANNOT be called after an update/update_suffix()
  virtual void add_data(Sequence &s) {
    using namespace std;
    if (guide_paths_immutable) {
      std::cerr << "Error: too late to add more guide paths.\n";
      return;
    }
    auto h = build_histogram(s);
    guide_multisets->push_back(h);
    intersection_counts.push_back(0);
    union_counts.push_back(histogram_area(h));
    double d = compute_jaccard(intersection_counts.back(), union_counts.back());
    distances.push_back(d);
    min_distance_ = min(min_distance_, d);
  }

  /// Set current sequence to s and compute Jaccard distance to all guide paths.
  virtual void update(Sequence &s) {
    using namespace std;
    guide_paths_immutable = true;
    // reset
    current_multiset.clear();
    num_inserted = 0;
    min_distance_ = MAX_JACCARD_DISTANCE;
    for (size_t i = 0; i < intersection_counts.size(); ++i) {
      intersection_counts[i] = 0;
      union_counts[i] = histogram_area((*guide_multisets)[i]);
      distances[i] = compute_jaccard(intersection_counts[i], union_counts[i]);
      min_distance_ = min(min_distance_, distances[i]);
    }
    // compute new
    update_suffix(s);
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

      // ... update the counts and distance to each guide path.
      add_to_histogram(current_multiset, elt);
      min_distance_ = MAX_JACCARD_DISTANCE;
      for (size_t i = 0; i < guide_multisets->size(); ++i) {
        if (histogram_getdefault((*guide_multisets)[i], elt, 0) >=
            current_multiset[elt]) {
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
    // use default copy constructor
    return new MultiSetJaccardTree<Sequence,T>(*this);
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  static int test()
  {
    using namespace std;
    auto jt = MultiSetJaccardTree<string,char>();
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
    if (abs(d - 1.0/2.0) > TOL)
      ret = 3;

    y = "fobarf"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 5.0/7.0) > TOL)
      ret = 3;

    y = "foofood"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 4.0/7.0) > TOL)
      ret = 3;

    cout << "min_distance (with scaling) of " << y << ": " <<
      jt.min_distance() << '\n';

    return ret;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  void add_to_histogram(std::map<T,int>& hist, T k) const
  {
    if (hist.find(k) == hist.end()) {
      hist[k] = 1;
    }
    else {
      hist[k] += 1;
    }
  }

  std::map<T,int> build_histogram(const Sequence& s) const
  {
    std::map<T,int> hist;
    for (auto it = s.begin(); it != s.end(); ++it) {
      add_to_histogram(hist, *it);
    }
    return hist;
  }

  T histogram_getdefault(std::map<T,int>& hist, T k, int defval) const
  {
    if (hist.find(k) == hist.end()) {
      return defval;
    }
    else {
      return hist[k];
    }
  }

  int histogram_area(std::map<T,int>& hist) const
  {
    int area = 0;
    for (const auto &p : hist) {
      area += p.second;
    }
    return area;
  }

  double compute_jaccard(int intersection_count, int union_count) const
  {
    if (union_count == 0)
      return 0.0;
    else
      return 1.0 - ((double)intersection_count/(double)union_count);
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  const double SCALE_FACTOR = 1e9; // since we must return an integer
  const double MAX_JACCARD_DISTANCE = 1.0;
  double min_distance_ = 1.0;
  int num_inserted = 0; // may exceed current_multiset.size() if repeats
  std::map<T,int> current_multiset;
  std::vector<int> intersection_counts;
  std::vector<int> union_counts;
  std::vector<double> distances;

  // The following is considered immutable after all add_data() calls
  // have been made.
  std::shared_ptr<std::vector<std::map<T,int> > > guide_multisets;
  bool guide_paths_immutable = false;
};

///////////////////////////////////////////////////////////////////////////////

/// MultiSetJaccardPrefixTree: Used to compute the Multi-set Jaccard
/// distance from a single sequence s to a collection of sequences
/// stored, but only the prefix.  NOTA BENE: It's not actually a tree,
/// but it implements the EditDistanceTree interface.

template <class Sequence, class T>
class MultiSetJaccardPrefixTree
: public EditDistanceTree<Sequence,T> {

 public:
  MultiSetJaccardPrefixTree() :
    guide_paths(std::make_shared<std::vector<Sequence> >()),
    guide_multisets(std::make_shared<std::vector<std::map<T,int> > >())
  {}

  //===-------------------------------------------------------------------===//
  // EditDistanceTree Interface Methods
  //===-------------------------------------------------------------------===//

  virtual void init(int k) { }

  // Warning: this CANNOT be called after an update/update_suffix()
  virtual void add_data(Sequence &s) {
    using namespace std;
    if (guide_paths_immutable) {
      std::cerr << "Error: too late to add more guide paths.\n";
      return;
    }
    guide_multisets->push_back(std::map<T,int>());
    guide_paths->push_back(s);
    intersection_counts.push_back(0);
    union_counts.push_back(0);
    double d = compute_jaccard(intersection_counts.back(), union_counts.back());
    distances.push_back(d);
    min_distance_ = min(min_distance_, d);
  }

  /// Set current sequence to s and compute Jaccard distance to all guide paths.
  virtual void update(Sequence &s) {
    using namespace std;
    guide_paths_immutable = true;
    // reset
    current_multiset.clear();
    num_inserted = 0;
    min_distance_ = MAX_JACCARD_DISTANCE;
    for (size_t i = 0; i < intersection_counts.size(); ++i) {
      (*guide_multisets)[i].clear();
      intersection_counts[i] = 0;
      union_counts[i] = 0;
      distances[i] = compute_jaccard(intersection_counts[i], union_counts[i]);
      min_distance_ = min(min_distance_, distances[i]);
    }
    // compute new
    update_suffix(s);
  }

  /// Compute the minimum Jaccard distance from s' to all guide paths
  /// where s' is equal to the previously computed sequence + s
  virtual void update_suffix(Sequence &s) {
    using namespace std;
    guide_paths_immutable = true;
    if (s.size() == 0)
      return;

    // For each element of the update sequence, ...
    for (auto it = s.begin(); it != s.end(); ++it) {
      auto elt = *it;

      // ... add one more element to each guide path prefix (if possible) ...
      for (size_t i = 0; i < guide_multisets->size(); ++i) {
        std::map<T,int>& guide_multiset = (*guide_multisets)[i];
        Sequence& guide_path = (*guide_paths)[i];
        if (num_inserted < (int)guide_path.size()) {
          add_and_update_counts(guide_multiset,
                                current_multiset,
                                intersection_counts[i],
                                union_counts[i],
                                guide_path[num_inserted]);
        }
      }

      // ... and update the counts and distance to each guide path.
      add_to_histogram(current_multiset, elt);
      min_distance_ = MAX_JACCARD_DISTANCE;
      for (size_t i = 0; i < guide_multisets->size(); ++i) {
        if (histogram_getdefault((*guide_multisets)[i], elt, 0) >=
            current_multiset[elt]) {
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

      num_inserted++;
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
    // use default copy constructor
    return new MultiSetJaccardPrefixTree<Sequence,T>(*this);
  }

  //===-------------------------------------------------------------------===//
  // Extra methods, testing, utility
  //===-------------------------------------------------------------------===//

  static int test()
  {
    using namespace std;
    auto jt = MultiSetJaccardPrefixTree<string,char>();
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

    y = "olleh"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (d != 0.0)
      ret = 2;

    y = "world"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 0.0) > TOL)
      ret = 3;

    y = "hello world!"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 1.0/2.0) > TOL)
      ret = 3;

    y = "fobarf"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 5.0/7.0) > TOL)
      ret = 3;

    y = "foofood"; jt.update(y); d = jt.min_distance_noscale();
    cout << "min_distance of " << y << ": " << d << '\n';
    if (abs(d - 4.0/7.0) > TOL)
      ret = 3;

    cout << "min_distance (with scaling) of " << y << ": " <<
      jt.min_distance() << '\n';

    return ret;
  }

 private:

  //===-------------------------------------------------------------------===//
  // Internal Methods
  //===-------------------------------------------------------------------===//

  void add_to_histogram(std::map<T,int>& hist, T k) const
  {
    if (hist.find(k) == hist.end()) {
      hist[k] = 1;
    }
    else {
      hist[k] += 1;
    }
  }

  void add_and_update_counts(std::map<T,int>& hist_target,
                             std::map<T,int>& hist_other,
                             int& intersection_count,
                             int& union_count,
                             T k) const
  {
    add_to_histogram(hist_target, k);
    if (hist_target[k] <= histogram_getdefault(hist_other, k, 0)) {
      intersection_count++;
    }
    else {
      union_count++;
    }
  }

  std::map<T,int> build_histogram(const Sequence& s) const
  {
    std::map<T,int> hist;
    for (auto it = s.begin(); it != s.end(); ++it) {
      add_to_histogram(hist, *it);
    }
    return hist;
  }

  T histogram_getdefault(std::map<T,int>& hist, T k, int defval) const
  {
    if (hist.find(k) == hist.end()) {
      return defval;
    }
    else {
      return hist[k];
    }
  }

  int histogram_area(std::map<T,int>& hist) const
  {
    int area = 0;
    for (const auto &p : hist) {
      area += p.second;
    }
    return area;
  }

  double compute_jaccard(int intersection_count, int union_count) const
  {
    if (union_count == 0)
      return 0.0;
    else
      return 1.0 - ((double)intersection_count/(double)union_count);
  }

  //===-------------------------------------------------------------------===//
  // Member variables
  //===-------------------------------------------------------------------===//

  const double SCALE_FACTOR = 1e9; // since we must return an integer
  const double MAX_JACCARD_DISTANCE = 1.0;
  double min_distance_ = 1.0;
  int num_inserted = 0; // may exceed current_multiset.size() if repeats
  std::map<T,int> current_multiset;
  std::vector<int> intersection_counts;
  std::vector<int> union_counts;
  std::vector<double> distances;

  // considered immutable after all add_data() calls complete
  std::shared_ptr<std::vector<Sequence> > guide_paths;

  // multisets corresponding to  prefixes of length 'num_inserted'
  std::shared_ptr<std::vector<std::map<T,int> > > guide_multisets;

  bool guide_paths_immutable = false;
};


////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_JACCARD_TREE_H

