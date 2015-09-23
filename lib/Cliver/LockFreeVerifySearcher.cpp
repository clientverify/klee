//===-- LockFreeVerifySearcher.cpp ------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/LockFreeVerifySearcher.h"

#include "cliver/CVSearcher.h"
#include "cliver/ClientVerifier.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVStream.h"

#include "CVCommon.h"

#include "llvm/Support/CommandLine.h"

////////////////////////////////////////////////////////////////////////////////

namespace klee {
extern llvm::cl::opt<unsigned> UseThreads;
}

////////////////////////////////////////////////////////////////////////////////

namespace cliver {


llvm::cl::opt<bool> 
EnableLockFreeSearcher("lock-free-searcher",
                       llvm::cl::desc("Enable lock free searcer."), 
                       llvm::cl::init(false));

LockFreeVerifySearcher::LockFreeVerifySearcher(CVSearcher *s)
  : searcher_(s), 
    ready_size_(0),
    halt_execution_(false),
    sleeping_(false) {
  searcher_->set_parent_searcher(this);
}

klee::ExecutionState &LockFreeVerifySearcher::selectState() {
  klee::ExecutionState *es = get_next_state(true);
  return *es;
}

klee::ExecutionState* LockFreeVerifySearcher::trySelectState() {
  return get_next_state(false);
}

void LockFreeVerifySearcher::update(klee::ExecutionState *current,
                    const std::set<klee::ExecutionState*> &addedStates,
                    const std::set<klee::ExecutionState*> &removedStates) {

  remove_states(removedStates);
  add_states(addedStates);

  if (current && removedStates.count(current) == 0) {
    add_state(current);
  }
}

klee::ExecutionState* LockFreeVerifySearcher::updateAndTrySelectState(
    klee::ExecutionState *current,
    const std::set<klee::ExecutionState*> &addedStates,
    const std::set<klee::ExecutionState*> &removedStates) {

  remove_states(removedStates);
  add_states(addedStates);

  if (current && removedStates.count(current) == 0) {
    add_state(current);
  }
  klee::ExecutionState *next = trySelectState();
  return next;
}

bool LockFreeVerifySearcher::empty() {
  if (ready_size_ > 0) return false;

  //// Wake up the worker thread if we are empty!
  //if (sleeping_)
  //  workerCond.notify_one();

  if (added_queue_.size()) {
    //CVMESSAGE("added_queue not empty");
    return false;
  }

  if (searcher_->empty()) {
    //CVMESSAGE("searcher empty");
    return true;
  }
  return true;
}

klee::ExecutionState* LockFreeVerifySearcher::get_next_state(bool blocking) {
  klee::ExecutionState *es;
  if (blocking) {
    while(!ready_stack_.pop(es))
      ;
    --ready_size_;
  } else {
    if (ready_stack_.pop(es)) {
      --ready_size_;
    } else {
      es = NULL;
    }
  }
  return es;
}

void LockFreeVerifySearcher::add_state(klee::ExecutionState* es) {
  added_queue_.push(es, true);
}

void LockFreeVerifySearcher::add_ready_state(klee::ExecutionState* es) {
  ready_stack_.push(es, true);
  ++ready_size_;
}

void LockFreeVerifySearcher::add_states(const std::set<klee::ExecutionState*> &states) {
  for (auto es : states)
    added_queue_.push(es, true);
}

void LockFreeVerifySearcher::remove_state(klee::ExecutionState* es) {
  removed_queue_.push(es, true);
}

void LockFreeVerifySearcher::remove_states(const std::set<klee::ExecutionState*> &states) {
  for (auto es : states)
    removed_queue_.push(es, true);
}

void LockFreeVerifySearcher::notify(ExecutionEvent ev) {
  if (ev.event_type == CV_HALT_EXECUTION)
    halt_execution_ = true;
  return searcher_->notify(ev);
}

void LockFreeVerifySearcher::flush_updates() {
  std::set<klee::ExecutionState*> removed_states;
  std::set<klee::ExecutionState*> added_states;
  klee::ExecutionState *es;

  if (removed_queue_.size()) {
    while (removed_queue_.pop(es)) {
      removed_states.insert(es);
    }
  }

  if (added_queue_.size()) {
    while (added_queue_.pop(es)) {
      added_states.insert(es);
    }
  }

  if (added_states.size() || removed_states.size()) {
    searcher_->update(NULL, added_states, removed_states);
    added_states.clear();
    removed_states.clear();
  }
}

void LockFreeVerifySearcher::flush() {
  while (!ready_stack_.empty()) {
    klee::ExecutionState *es;
    if (!ready_stack_.pop(es)) {
      cv_error("ready stack pop failed");
    }
    add_state(es);
  }
  flush_updates();
  ready_size_ = 0;
}

void LockFreeVerifySearcher::Worker() {
  threadBarrier->wait();
  threadBarrier->wait();

  while (!halt_execution_) {
    klee::ExecutionState *es;

    flush_updates();

    if (ready_size_ < (klee::UseThreads*4) && !searcher_->empty()) {
      klee::ExecutionState *es = searcher_->trySelectState();
      if (es) {
        ready_stack_.push(es);
        ++ready_size_;
        searcherCond->notify_one();
      }
    } else if (searcher_->empty()) {
      //CVMESSAGE("Searcher is empty! no ready states");
    }

    //if (ready_size_ == klee::UseThreads) {
    //  klee::WallTimer wt;
    //  //CVMESSAGE("Worker Thread " << klee::GetThreadID() << " sleeping.");
    //  klee::UniqueLock workerGuard(workerCondLock);
    //  sleeping_ = true;
    //  workerCond.wait(workerGuard);
    //  //CVMESSAGE("Worker Thread " << klee::GetThreadID() << " slept for " 
    //  //        << wt.check()/1000.0f << " ms" );
    //  sleeping_ = false;
    //}
  }
  CVMESSAGE("Stopping LockFreeVerifySearcher::Worker");
}

////////////////////////////////////////////////////////////////////////////////

} // end namespace cliver
