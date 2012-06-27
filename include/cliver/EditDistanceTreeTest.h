//===-- EditDistanceTreeTest.h ----------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CLIVER_EDIT_DISTANCE_TREE_TEST_H
#define CLIVER_EDIT_DISTANCE_TREE_TEST_H

#include "cliver/CVStream.h"
#include "cliver/EditDistanceTree.h"

// Implementations
#include "cliver/KExtensionTree.h"
#include "cliver/LevenshteinRadixTree.h"

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

template<class Sequence, class Element>
class EditDistanceTreeTest
: public EditDistanceTree<Sequence,Element> {

 public:

  EditDistanceTreeTest() {
    impl_.push_back(new KExtensionTree<Sequence, Element>());
    impl_name_.push_back(std::string("KExtensionTree"));
    impl_.push_back(new KLevenshteinRadixTree<Sequence, Element>());
    impl_name_.push_back(std::string("KLevenshteinRadixTree"));
  }

  EditDistanceTreeTest(std::vector<EditDistanceTree<Sequence,Element>*> &impl,
                       std::vector<std::string> impl_name) {
    impl_ = impl;
    impl_name_ = impl_name;
  }

  virtual ~EditDistanceTreeTest() {
    for (unsigned i=0; i<impl_.size(); ++i) {
      delete impl_[i];
    }
  }

  virtual void init(int k) {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->init(k);
    }
  }

  virtual void add_data(Sequence &s) {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->add_data(s);
    }
  }

  virtual void update(Sequence &s_update) {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->update(s_update);
    }
  }

  virtual void update_suffix(Sequence &s) {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->update_suffix(s);
    }
  }

  virtual void update_element(Element e) {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->update_element(e);
    }
  }

  virtual int  min_distance() {
    std::vector<int> results(impl_.size());
    bool all_equal = true;
    for (unsigned i=0; i<impl_.size(); ++i) {
      int res = impl_[i]->min_distance();
      if (i > 0 && res != results.back())
        all_equal = false;
      results.push_back(res);
    }
    if (!all_equal) {
      for (unsigned i=0; i<results.size(); ++i) {
        CVDEBUG(impl_name_[i] << "::min_distance() = " << results[i]);
      }
      cv_error("min_distance() implementations are not equivalent");
    }
    return results[0];
  }

  virtual int row() {
    std::vector<int> results(impl_.size());
    bool all_equal = true;
    for (unsigned i=0; i<impl_.size(); ++i) {
      int res = impl_[i]->row();
      if (i > 0 && res != results.back())
        all_equal = false;
      results.push_back(res);
    }
    if (!all_equal) {
      for (unsigned i=0; i<results.size(); ++i) {
        CVDEBUG(impl_name_[i] << "::row() = " << results[i]);
      }
      cv_error("row() implementations are not equivalent");
    }
    return results[0];
  }

  virtual void delete_shared_data() {
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_[i]->delete_shared_data();
    }
  }

  virtual EditDistanceTree<Sequence, Element>* clone_edit_distance_tree() {
    std::vector<EditDistanceTree<Sequence,Element>*> impl_clone(impl_.size());
    for (unsigned i=0; i<impl_.size(); ++i) {
      impl_clone[i] = impl_[i]->clone_edit_distance_tree();
    }
    return new EditDistanceTreeTest(impl_clone, impl_name_);
  }

 protected:
  std::vector<EditDistanceTree<Sequence,Element>*> impl_;
  std::vector<std::string> impl_name_;
};

} // end namespace cliver

////////////////////////////////////////////////////////////////////////////////

#endif // CLIVER_EDIT_DISTANCE_TREE_H

