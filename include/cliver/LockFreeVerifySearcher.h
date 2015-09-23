//===-- LockFreeVerifySearcher.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CV_LOCK_FREE_VERIFY_SEARCHER_H
#define CV_LOCK_FREE_VERIFY_SEARCHER_H

#include "cliver/CVSearcher.h"
#include "klee/Internal/ADT/LockFreeQueue.h"

////////////////////////////////////////////////////////////////////////////////

namespace cliver {

class LockFreeVerifySearcher : public CVSearcher {
 public:
  LockFreeVerifySearcher(CVSearcher *s);

  virtual klee::ExecutionState &selectState();

  virtual void update(klee::ExecutionState *current,
                      const std::set<klee::ExecutionState*> &addedStates,
                      const std::set<klee::ExecutionState*> &removedStates);

  virtual klee::ExecutionState* updateAndTrySelectState(
      klee::ExecutionState *current,
      const std::set<klee::ExecutionState*> &addedStates,
      const std::set<klee::ExecutionState*> &removedStates);

  virtual bool empty();
  virtual klee::ExecutionState* trySelectState();
  virtual void printName(std::ostream &os) { os << "LockFreeVerifySearcher\n"; }

  virtual void notify(ExecutionEvent ev);
  virtual void flush();
  virtual void set_parent_searcher(CVSearcher* s) {}

  void flush_updates();

  void Worker();

  klee::Barrier* threadBarrier;
  klee::ConditionVariable* searcherCond;
  klee::Mutex* searcherCondLock;

 private:

  klee::ExecutionState* get_next_state(bool blocking=false);

  inline void add_state(klee::ExecutionState* es);
  inline void add_ready_state(klee::ExecutionState* es);
  inline void add_states(const std::set<klee::ExecutionState*> &states);
  inline void remove_state(klee::ExecutionState* es);
  inline void remove_states(const std::set<klee::ExecutionState*> &states);

  klee::LockFreeQueue<klee::ExecutionState*> added_queue_;
  klee::LockFreeQueue<klee::ExecutionState*> removed_queue_;
  klee::LockFreeQueue<klee::ExecutionState*> ready_stack_;

  klee::Atomic<bool>::type halt_execution_;
  klee::Atomic<bool>::type pause_execution_;
  klee::Atomic<bool>::type sleeping_;

  klee::Atomic<size_t>::type ready_size_;

  klee::ConditionVariable workerCond;
  klee::Mutex workerCondLock;

  CVSearcher* searcher_;
}; 

} // end namespace cliver

#endif // CV_LOCK_FREE_VERIFY_SEARCHER
