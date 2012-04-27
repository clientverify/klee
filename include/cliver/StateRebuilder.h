//===-- StateRebuilder.h ----------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#ifndef CV_STATE_REBUILDER_H
#define CV_STATE_REBUILDER_H

#include "cliver/ExecutionObserver.h"
#include "cliver/TrackingRadixTree.h"

#include "cliver/CVStream.h"

#include <boost/bimap.hpp> 
#include <boost/bimap/list_of.hpp> 
#include <boost/bimap/set_of.hpp> 
#include <boost/bimap/unordered_set_of.hpp> 
#include <boost/function.hpp> 
#include <cassert> 
 
namespace cliver {

extern llvm::cl::opt<unsigned> StateCacheSize;

////////////////////////////////////////////////////////////////////////////////

class StateRebuilder : public ExecutionObserver {
 public:
  typedef std::vector<bool> BoolForkList;
  typedef std::vector<unsigned char> UCharForkList;
  typedef TrackingRadixTree< UCharForkList, unsigned char, ExecutionStateProperty> 
      ForkTree;

  StateRebuilder() : 
      root_(NULL), rebuild_state_(NULL), rebuild_property_(NULL) {}

  ~StateRebuilder() {
    if (root_)
      delete root_;
  }

  void set_root(CVExecutionState* root) {
    // root_ is never added to the CVExecutor state tracker because it is never
    // actually executed, only its clones in a rebuild
    root_ = root->clone(root->property()->clone());
  }

  void notify(ExecutionEvent ev) {
    if (rebuild_state_ == NULL) {
      CVExecutionState* state = ev.state;
      CVExecutionState* parent = ev.parent;

      switch (ev.event_type) {
        case CV_STATE_REMOVED: {
          fork_tree_.remove_tracker(state->property());
          break;
        }

        case CV_STATE_CLONE: {
          fork_tree_.clone_tracker(state->property(), parent->property());
          break;
        }

        case CV_STATE_FORK_TRUE: {
          fork_tree_.extend(true, state->property());
          break;
        }

        case CV_STATE_FORK_FALSE: {
          fork_tree_.extend(false, state->property());
          break;
        }

        //case CV_SOCKET_WRITE:
        //case CV_SOCKET_READ:
        //case CV_SOCKET_SHUTDOWN: {
        //  UCharForkList uchar_fork_list;
        //  fork_tree_.tracker_get(state->property(), uchar_fork_list);
        //  *cv_debug_stream << "FORKS: " << uchar_fork_list.size() << "\n";
        //  break;
        //}
        default:
          break;
      }
    }
  }

  CVExecutionState* rebuild(ExecutionStateProperty* property) {
    assert(rebuild_state_ == NULL);

    assert(fork_tree_.tracks(property));

    // Clone root state with the given property
    rebuild_state_ = root_->clone(property);
    rebuild_property_ = property;

    // Get the list of forks from the fork tree (uchar)
    UCharForkList uchar_fork_list;
    fork_tree_.tracker_get(property, uchar_fork_list);

    // Prepare the bool fork list
    replay_path_.reserve(uchar_fork_list.size());

    // Copy the uchar fork list to the bool fork list
    UCharForkList::iterator it=uchar_fork_list.begin(), 
        iend=uchar_fork_list.end();
    for (; it != iend; ++it)
      replay_path_.push_back(*it);

    // Set the replay path in Executor
    root_->cv()->executor()->reset_replay_path(&replay_path_);

    // Add new rebuild state to the executor
    root_->cv()->executor()->add_state_internal(rebuild_state_);

    // Create new timer object
    rebuild_timer_ = new klee::TimerStatIncrementer(stats::rebuild_time);

    //*cv_debug_stream << "Rebuilding State... with replay path of size: " << replay_path_.size() << "\n";

    // Return the new state
    return rebuild_state_;
  }

  CVExecutionState* state() { return rebuild_state_; }

  bool rebuilding() { 
    // If we've executed all of the replay path
    if (rebuild_state_ != NULL && 
        root_->cv()->executor()->replay_position() == replay_path_.size())
      finish_rebuild();

    return rebuild_state_ != NULL;
  }

  void finish_rebuild() {

    // Reset state to null
    rebuild_state_ = NULL;
    rebuild_property_ = NULL;

    // Clear replay path
    replay_path_.clear();

    // Set replay path to null in Executor
    root_->cv()->executor()->reset_replay_path();

    // End the timer
    delete rebuild_timer_;
    rebuild_timer_ = 0;
    
    //*cv_debug_stream << "Finished rebuilding State.\n";
  }

 protected:
  CVExecutionState* root_;
  CVExecutionState* rebuild_state_;
  ExecutionStateProperty* rebuild_property_;
  ForkTree fork_tree_;
  BoolForkList replay_path_;
  klee::TimerStatIncrementer* rebuild_timer_;
};

////////////////////////////////////////////////////////////////////////////////

class BasicStateCache
 : public boost::unordered_map<ExecutionStateProperty*, CVExecutionState*> {

 public:
  void notify(ExecutionEvent ev) {}
  ExecutionStateProperty* rebuild_property() { return NULL; }
  void set_capacity(size_t c) {}
};

////////////////////////////////////////////////////////////////////////////////

class RebuildingStateCache : public StateRebuilder {
 
  typedef ExecutionStateProperty* key_type; 
  typedef CVExecutionState* value_type; 
 
  typedef boost::bimaps::bimap< 
    boost::bimaps::unordered_set_of<key_type>, 
    boost::bimaps::list_of<value_type> 
    > container_type; 
 
 public:
  RebuildingStateCache() : capacity_(StateCacheSize) {}

  void set_capacity(size_t c) { capacity_ = c; }

  ExecutionStateProperty* rebuild_property() {
    if (this->rebuilding())
      return this->rebuild_property_;
    return NULL;
  }

  //bool empty() {
  //  return (this->rebuild_state_ == NULL) && cache_.empty();
  //}

  value_type operator[](const key_type& k) { 

    if (this->rebuild_property_)
      assert(this->rebuild_property_ == k);
 
    // Attempt to find existing record 
    const container_type::left_iterator it = cache_.left.find(k); 
 
    if (it==cache_.left.end()) { 
 
      // We don't have it: 
 
      // Evaluate function and create new record 
      value_type v = this->rebuild(k);
      insert(k,v);
 
      // Return the freshly computed value 
      return v; 
 
    } else { 
 
      // We do have it: 
 
      // Update the access record view. 
      cache_.right.relocate( 
        cache_.right.end(), 
        cache_.project_right(it) 
      ); 
 
      // Return the retrieved value 
      return it->second; 
    } 
  } 
 
  size_t count(const key_type& k) {
    if (cache_.left.find(k) == cache_.left.end())
      return 0;
    return 1;
  }
 
  void erase(const key_type& k) {
    if (this->rebuild_property_ != NULL) {
      //*cv_debug_stream << "aborting rebuild.\n";
      assert(k == this->rebuild_property_);
      this->finish_rebuild();
    }
    cache_.left.erase(k);
  }
 
  void clear() {
    cache_.left.clear();
  }

  void insert(std::pair<key_type, value_type> p) {
    insert(p.first, p.second);
  }

 private:
 
  void insert(const key_type& k,const value_type& v) {

    if (this->root_ == NULL) {
      this->set_root(v);
    }
 
    //assert(cache_.size() <= capacity_); 
 
    // If necessary, make space 
    while (cache_.size() > capacity_) { 
      //*cv_debug_stream << "reached capacity, removing state from cache, id: "
      //    << cache_.right.begin()->first->id() << "\n";
      
      // by purging the least-recently-used element 
      cache_.right.begin()->first->erase_self();
      cache_.right.erase(cache_.right.begin()); 
    } 
 
    // Create a new record from the key and the value 
    // bimap's list_view defaults to inserting this at 
    // the list tail (considered most-recently-used). 
    cache_.insert(container_type::value_type(k,v)); 
  } 
 
  size_t capacity_;
  container_type cache_;

};

} // end namespace cliver

#endif // CV_STATE_REBUILDER_H
