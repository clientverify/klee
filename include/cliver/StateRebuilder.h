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

namespace cliver {

////////////////////////////////////////////////////////////////////////////////

class StateRebuilder : public ExecutionObserver {
 public:
  typedef std::vector<bool> BoolForkList;
  typedef std::vector<unsigned char> UCharForkList;
  typedef TrackingRadixTree< UCharForkList, unsigned char, ExecutionStateProperty> 
      ForkTree;

  StateRebuilder() : active_(false), root_(NULL), rebuild_state_(NULL) {}

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
    if (!active_) {
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

        default:
          break;
      }
    }
  }

  CVExecutionState* rebuild(ExecutionStateProperty* property) {
    assert(rebuild_state_ == NULL);

    // Clone root state with the given property
    rebuild_state_ = root_->clone(property);

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

    // Set Rebuilder to Active
    active_ = true;

    // Return the new state
    return rebuild_state_;
  }

  CVExecutionState* state() { return rebuild_state_; }

  bool active() { 
    // If we've executed all of the replay path
    if (active_ && 
        root_->cv()->executor()->replay_position() == replay_path_.size()) {

      // Deactivate
      active_ = false;

      // Reset state to null
      rebuild_state_ = NULL;

      // Clear replay path
      replay_path_.clear();

      // Set replay path to null in Executor
      root_->cv()->executor()->reset_replay_path();

    }
    return active_;
  }

 private:
  bool active_;
  CVExecutionState* root_;
  CVExecutionState* rebuild_state_;
  ForkTree fork_tree_;
  BoolForkList replay_path_;
};

} // end namespace cliver

#endif // CV_STATE_REBUILDER_H
