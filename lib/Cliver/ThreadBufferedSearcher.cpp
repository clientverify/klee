//===-- ThreadBufferedSearcher.cpp ------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "cliver/ThreadBufferedSearcher.h"

#include "llvm/Support/CommandLine.h"

namespace cliver {

llvm::cl::opt<unsigned>
BufferedSearcherSize("buffered-searcher-size",llvm::cl::init(0));

////////////////////////////////////////////////////////////////////////////////

ThreadBufferedSearcher::ThreadBufferedSearcher(CVSearcher* searcher)
  : CVSearcher(NULL, NULL, NULL), searcher_(searcher) {}

SearcherStage* ThreadBufferedSearcher::get_local_states() {
  auto local_states = local_states_.get();
  if (__builtin_expect((local_states == NULL), 0)) {
    local_states = new PQSearcherStage(/*root state= */ NULL);
    local_states_.reset(local_states);
  }
  return local_states;
}

std::set<klee::ExecutionState*>* ThreadBufferedSearcher::get_shared_states() {
  auto shared_states = shared_states_.get();
  if (__builtin_expect((shared_states == NULL), 0)) {
    shared_states = new std::set<klee::ExecutionState*>();
    shared_states_.reset(shared_states);
  }
  return shared_states;
}

klee::ExecutionState &ThreadBufferedSearcher::selectState() {
  klee::ExecutionState *es = trySelectState();
  assert(es && "trySelectState returned NULL");
  return *es;
}

klee::ExecutionState* ThreadBufferedSearcher::trySelectState() {
  auto local_states = get_local_states();
  klee::ExecutionState *es = NULL;

  if (local_states->cache_size() > BufferedSearcherSize) {
    flush_states();
  }
 
  return get_next_state();
}

void ThreadBufferedSearcher::update(klee::ExecutionState *current,
                    const std::set<klee::ExecutionState*> &addedStates,
                    const std::set<klee::ExecutionState*> &removedStates) {

  // Called with no parameters, flush buffers
  if (!current && !addedStates.size() && !removedStates.size()) {
    flush_states();
    return;
  }

  auto local_states = get_local_states();

  for (auto es : removedStates)
    local_states->remove_state(static_cast<CVExecutionState*>(es));

  for (auto es : addedStates)
    local_states->add_state(static_cast<CVExecutionState*>(es));

  if (current && removedStates.count(current) == 0)
    local_states->add_state(static_cast<CVExecutionState*>(current));

  if ((local_states->cache_size() > BufferedSearcherSize) ||
      (current && static_cast<CVExecutionState*>(current)->event_flag())) {
    flush_states();
  }
}

klee::ExecutionState* ThreadBufferedSearcher::updateAndTrySelectState(
    klee::ExecutionState *current,
    const std::set<klee::ExecutionState*> &addedStates,
    const std::set<klee::ExecutionState*> &removedStates) {

  update(current, addedStates, removedStates);

  klee::ExecutionState *next = get_next_state();
  return next;
}

bool ThreadBufferedSearcher::empty() {
  auto local_states = get_local_states();
  if (!local_states->empty())
    return false;

  return searcher_->empty();
}

void ThreadBufferedSearcher::notify(ExecutionEvent ev) {
  searcher_->notify(ev);
}

klee::ExecutionState* ThreadBufferedSearcher::get_next_state() {
  auto local_states = get_local_states();
  klee::ExecutionState *es = NULL;

  if (local_states->empty()) {
    flush_states();
    es = searcher_->trySelectState();
    if (es) {
      local_states->add_state(static_cast<CVExecutionState*>(es));
      auto shared_states = get_shared_states();
      shared_states->insert(es);
    }
  }
  es = local_states->next_state();
  return es;
}

void ThreadBufferedSearcher::flush_states() {

  auto local_states = get_local_states();
  auto shared_states = get_shared_states();

  std::set<klee::ExecutionState*> added_states;

  while (local_states->cache_size()) {
    auto es = local_states->next_state();
    added_states.insert(es);
    local_states->remove_state(es);
    shared_states->erase(es);
  }

  searcher_->update(NULL, added_states, *shared_states);
  shared_states->clear();
}

} // end namespace cliver
