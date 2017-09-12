//===-- ThreadBufferedSearcher.h --------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef CV_THREAD_BUFFERED_SEARCHER_H
#define CV_THREAD_BUFFERED_SEARCHER_H

#include "cliver/CVSearcher.h"
#include "cliver/SearcherStage.h"
#include "klee/util/Thread.h"

namespace cliver {

class ThreadBufferedSearcher : public CVSearcher {
 public:
  ThreadBufferedSearcher(CVSearcher* searcher);

  virtual klee::ExecutionState &selectState();

  virtual void update(klee::ExecutionState *current,
                      const std::set<klee::ExecutionState*> &addedStates,
                      const std::set<klee::ExecutionState*> &removedStates);

  virtual klee::ExecutionState* updateAndTrySelectState(
      klee::ExecutionState *current,
      const std::set<klee::ExecutionState*> &addedStates,
      const std::set<klee::ExecutionState*> &removedStates);

  virtual bool empty();
  virtual void notify(ExecutionEvent ev);
  virtual klee::ExecutionState* trySelectState();
  virtual void printName(std::ostream &os) { os << "ThreadBufferedSearcher\n"; }
  virtual inline void flush();
  virtual void set_parent_searcher(CVSearcher* s) {}

 private:
  inline SearcherStage* get_local_states();
  inline std::set<klee::ExecutionState*>* get_shared_states();
  inline klee::ExecutionState* get_next_state();

  CVSearcher* searcher_;
  klee::ThreadSpecificPointer<SearcherStage>::type local_states_;
  klee::ThreadSpecificPointer<std::set<klee::ExecutionState*> >::type shared_states_;
}; 

}

////////////////////////////////////////////////////////////////////////////////

#endif
