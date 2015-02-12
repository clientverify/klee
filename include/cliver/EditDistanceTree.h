//===-- EditDistanceTree.h --------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EDIT_DISTANCE_TREE_H
#define CLIVER_EDIT_DISTANCE_TREE_H

////////////////////////////////////////////////////////////////////////////////
// Abstract edit distance tree interface
////////////////////////////////////////////////////////////////////////////////

namespace cliver {

template<class Sequence, class Element>
class EditDistanceTree {
 public:
  virtual ~EditDistanceTree() {}
  virtual void init(int k) = 0; // Ukkonen k (max edit distance)
  virtual void add_data(Sequence &s) = 0; // add guide paths (immutable)
  virtual void update(Sequence &s_update) = 0; // set the "growing" path
  virtual void update_suffix(Sequence &s) = 0; // add suffix to "growing" path
  virtual void update_element(Element e) = 0; // add one elt to "growing" path
  virtual double min_distance() = 0; // minimum distance to any guide path
  virtual int  row() = 0; // internal dynamic programming table "size"
  virtual void delete_shared_data() = 0;
  // The following is a semi-deep copy; prefix will be shared between clones.
  virtual EditDistanceTree* clone_edit_distance_tree() = 0;
};

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_EDIT_DISTANCE_TREE_H

