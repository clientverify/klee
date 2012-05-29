//===-- EditDistanceTree.h ----------------------------------*- C++ -*-===//
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
  virtual void init(int k) = 0;
  virtual void add_data(Sequence &s) = 0;
  virtual void update(Sequence &s_update) = 0;
  virtual void update_suffix(Sequence &s) = 0;
  virtual void update_element(Element e) = 0;
  virtual int  min_distance() = 0;
  virtual void delete_shared_data() = 0;
  virtual EditDistanceTree* clone_edit_distance_tree() = 0;
};

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_EDIT_DISTANCE_TREE_H

