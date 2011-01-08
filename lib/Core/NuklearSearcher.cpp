/* NUKLEAR KLEE begin (ENTIRE FILE) */
//===-- NuklearSearcher.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Common.h"

#include "Searcher.h"
#include "NuklearSearcher.h"

#include "CoreStats.h"
#include "Executor.h"
#include "PTree.h"
#include "StatsTracker.h"

#include "klee/TimerStatIncrementer.h"
#include "klee/ExecutionState.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <fstream>
#include <climits>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  DebugNuklearMerge("debug-nuklear-merge");
  cl::opt<bool>
  DebugNuklearRemainingMerge("debug-nuklear-remaining-merge");
  cl::opt<bool>
  NuklearMergeDigest("nuklear-merge-digest");
  cl::opt<bool>
  NuklearPruneStats("nuklear-prune-stats");
  cl::opt<bool>
  NuklearMergeCheckpointDigest("nuklear-merge-checkpoint-digest");
  cl::opt<bool>
  NuklearPruneHack2("nuklear-prune-hack-2");
}

namespace klee {
  extern RNG theRNG;
}

NuklearSearcher::NuklearSearcher(Executor &_executor, Searcher *_baseSearcher) 
  : executor(_executor),
    baseSearcher(_baseSearcher),
    mergeFn(executor.kmodule->nuklearMergeFn),
    functionPosition(0),
    merge_count(0),
    checkpoint_count(0),
    max_checkpoint_count(16) {
  llvm::errs() << "NuklearSearcher created. " << mergeFn.size() 
    << " merge fn's\n";

}

NuklearSearcher::~NuklearSearcher() {
  delete baseSearcher;
}

// TODO need method to set function vector klee_merge1, klee_merge2?
Instruction *NuklearSearcher::getMergePoint(ExecutionState &es,
                                            llvm::Function *function) {
  if (function) {
    Instruction *i = es.pc->inst;

    if (i->getOpcode()==Instruction::Call) {
      CallSite cs(cast<CallInst>(i));
      std::string fname;
      if (cs.getCalledFunction() )
        fname = cs.getCalledFunction()->getNameStr();
        //llvm::errs() << "getMergePoint( " << fname
        //  << ", " << function->getNameStr() << "\n";
      if (function==cs.getCalledFunction()) {
        //llvm::errs() << "returning: " << i << "\n";
        return i;
      }
    }
  }

  return 0;
}

static bool digest_compare(StateDigest* const& l, StateDigest* const& r)
{
      return memcmp(l->value, r->value, r->len) < 0;
}

typedef std::map<StateDigest*, ExecutionState*, 
                 bool(*)(StateDigest* const&,StateDigest* const&)> digest_map_t;


void NuklearSearcher::doMerge(std::set<ExecutionState*> &states,
                              bool pruning) {
  ++stats::nuklearMerges;
  unsigned init_size = states.size();
  bool digestMerge = false;
  if (NuklearMergeDigest) digestMerge = true;
  if (NuklearMergeCheckpointDigest 
      && statesAtCheckpointMerge.size() != 0) digestMerge = true;

  // build map of merge point -> state list
  std::map<Instruction*, std::vector<ExecutionState*> > merges;
  for (std::set<ExecutionState*>::const_iterator it = states.begin(),
         ie = states.end(); it != ie; ++it) {
    ExecutionState &state = **it;
    Instruction *mp = getMergePoint(state, mergeFn[mergeFn.size()-1]);
    
    merges[mp].push_back(&state);
  }
  
  unsigned state_erases = 0;
  merge_count++;
  if (DebugNuklearMerge)
    llvm::errs() << "-- all at merge --\n";

  TimerStatIncrementer mergeTimer(stats::nuklearMergeTime);

  for (std::map<Instruction*, std::vector<ExecutionState*> >::iterator
         it = merges.begin(), ie = merges.end(); it != ie; ++it) {

    if (DebugNuklearMerge) {
      Function *f = it->second.back()->stack.back().kf->function;
      llvm::errs() << "\tAt instruction " << it->first << " in "
        << f->getNameStr() << "()\n\tMerging "
        << it->second.size() << " of the total "
        << init_size << " states.\n";
    }

    if (pruning) {
      for (std::vector<ExecutionState*>::iterator it2 = it->second.begin(),
             ie2 = it->second.end(); it2 != ie2; ++it2) {
        ExecutionState *state = *it2;
				if (NuklearPruneHack2)
					state->prune_hack(executor.nuklearManager->getCurrentSymbolicNames());
				else
					state->prune();
      }
      if (NuklearPruneStats)
        executor.nuklearManager->printPruneStatistics();
    }


    if (DebugNuklearMerge && digestMerge)
      llvm::errs() << "\tComputing " << it->second.size() << " digests.\n";

    digest_map_t dmap(&digest_compare);
    std::vector<ExecutionState*> unique_states;

    if (digestMerge) {
      //#pragma omp parallel for
      for (unsigned i=0; i<it->second.size(); ++i) {
        it->second[i]->computeDigest();
      }
    }

    for (std::vector<ExecutionState*>::iterator it2 = it->second.begin(),
           ie2 = it->second.end(); it2 != ie2; ++it2) {
      ExecutionState *state = *it2;
      if (digestMerge) {
        digest_map_t::iterator dit = dmap.find(state->getDigest());
        if (dit == dmap.end()) {
          dmap[state->getDigest()] = state;
          unique_states.push_back(state);
        } else {
          mergedStates.insert(state);
          executor.terminateState(*state);
          ++stats::nuklearStateErases;
          ++state_erases;
        }
      } else {
        unique_states.push_back(state);
      }
    }

    if (DebugNuklearMerge && digestMerge) 
        llvm::errs() << "\tUnique digests: " << dmap.size() << "\n";

    //if (DebugNuklearRemainingMerge) {
    //  llvm::errs() << "-- begin post-digest remaining states (diff) --\n";
    //  std::vector<ExecutionState*>  ev(executor.states.begin(), executor.states.end());
    //  for (unsigned i=0;i<ev.size(); ++i) {
    //    ev[i]->print(ev);
    //  }
    //  llvm::errs() << "-- end post-digest remaining states (diff) --\n";
    //}

    if (DebugNuklearMerge && !digestMerge)
      llvm::errs() << "\tMerging " << unique_states.size() << " states.\n";

    // merge states
    std::set<ExecutionState*> toMerge(unique_states.begin(),
                                      unique_states.end());

    while (!toMerge.empty()) {
      ExecutionState *base = *toMerge.begin();
      toMerge.erase(toMerge.begin());
      
      std::set<ExecutionState*> toErase;
      for (std::set<ExecutionState*>::iterator it = toMerge.begin(),
             ie = toMerge.end(); it != ie; ++it) {

        ExecutionState *mergeWith = *it;
        if (!digestMerge && base->nuklear_merge(*mergeWith)) {
          toErase.insert(mergeWith);
        }
      }

      if (DebugNuklearMerge && !toErase.empty()) {
        llvm::errs() << "\t\tmerged: " << base << " with  "
          << toErase.size() << " states\n";
      }

      for (std::set<ExecutionState*>::iterator it = toErase.begin(),
             ie = toErase.end(); it != ie; ++it) {
        std::set<ExecutionState*>::iterator it2 = toMerge.find(*it);
        assert(it2!=toMerge.end());
        mergedStates.insert(*it);
        executor.terminateState(**it);
        toMerge.erase(it2);
        ++stats::nuklearStateErases;
        ++state_erases;
      }

      ++base->pc;
      baseSearcher->addState(base);
    }  
  }

  if (DebugNuklearMerge)
    llvm::errs() << "\tAfter Merging: " << init_size - state_erases << " states.\n";

}

ExecutionState &NuklearSearcher::selectState() {
  // remove states from the basesearcher that are at a call instruction to the
  // checkpoint function. When all states are at the checkpoint, do the same
  // until all states are at the merge point where we can then attempt to prune
  // and merge states. 
  
  while (!baseSearcher->empty()) {
    ExecutionState &es = baseSearcher->selectState();
    unsigned max = (checkpoint_count < max_checkpoint_count) ? mergeFn.size() : 1;
    bool removed = false;
    for (unsigned i=0;i<max;++i) {
      if (!removed && getMergePoint(es, mergeFn[i])) {
        removed = true;
        baseSearcher->removeState(&es, &es);
        if (i==0) {
          //if (DebugNuklearMerge)
          //  llvm::errs() << "\tat final merge: " << 
          //    mergeFn[i]->getNameStr() << "(" << es.id << ")\n";
          statesAtMerge.insert(&es);
        } else {
          //if (DebugNuklearMerge)
          //  llvm::errs() << "\tat merge: "  << 
          //    mergeFn[i]->getNameStr() << "(" << es.id << ")\n";
          statesAtCheckpointMerge.insert(&es);
        }
      }
    }
    if (!removed)
      return es;
  }

  if (statesAtCheckpointMerge.size()) {
    stats::nuklearPreMergeStates += statesAtCheckpointMerge.size();
    checkpoint_count++;
    doMerge(statesAtCheckpointMerge, true /* no pruning */);
    statesAtCheckpointMerge.clear();
    executor.nuklearManager->checkpointMerge();
    return selectState();
  }

  checkpoint_count = 0;
#if 1
  executor.updateStates(NULL);

  if (DebugNuklearMerge)
    llvm::errs() << "NUKLEAR RNCHECK: Checking round numbers in "
      << statesAtMerge.size() << " states.\n";

  unsigned state_erases = 0;
  stats::nuklearPreMergeStates += statesAtMerge.size();

  std::set<ExecutionState*> toErase;
  for (std::set<ExecutionState*>::const_iterator it = statesAtMerge.begin(),
         ie = statesAtMerge.end(); it != ie; ++it) {
    if (!executor.nuklearManager->checkValidSocketRound(**it)) {
      toErase.insert(*it);
    }
  }

  for (std::set<ExecutionState*>::iterator it = toErase.begin(),
         ie = toErase.end(); it != ie; ++it) {
    mergedStates.insert(*it);
    statesAtMerge.erase(*it);
    executor.terminateState(**it);
    ++stats::nuklearStateErases;
    ++state_erases;
  }
#endif

  // Reset searcher states
  executor.updateStates(NULL);

  if (DebugNuklearMerge)
    llvm::errs() << "NUKLEAR RNCHECK: Removed " << state_erases 
      << " states with invalid round numbers.\n";

  // All states have reached each of the checkpoint checkpointFn (in order).
  doMerge(statesAtMerge, true);

  // Reset searcher states
  executor.updateStates(NULL);
  
  // Compute stats and increment round number.
  executor.nuklearManager->merge();

  statesAtMerge.clear();
  
  if (DebugNuklearRemainingMerge) {
    llvm::errs() << "== begin remaining states (diff) =========================\n";
    std::vector<ExecutionState*>  ev(executor.states.begin(), 
                                     executor.states.end());
    ev[0]->print_diff(ev);

    llvm::errs() << "==========================================================\n";
    llvm::errs() << "== end remaining states (diff) ===========================\n";
  }

  if (DebugNuklearMerge)
    llvm::errs() << "-- merge complete, continuing --\n";


  return selectState();
}

void NuklearSearcher::update(ExecutionState *current,
                             const std::set<ExecutionState*> &addedStates,
                             const std::set<ExecutionState*> &removedStates) {
  if (!removedStates.empty()) {
    std::set<ExecutionState *> alt = removedStates;
    for (std::set<ExecutionState*>::const_iterator it = removedStates.begin(),
           ie = removedStates.end(); it != ie; ++it) {
      ExecutionState *es = *it;

      std::set<ExecutionState*>::const_iterator mit 
        = mergedStates.find(es);
      if (mit!=mergedStates.end()) {
        mergedStates.erase(mit);
        alt.erase(alt.find(es));
      } else {
        std::set<ExecutionState*>::const_iterator sit 
          = statesAtMerge.find(es);
        if (sit!=statesAtMerge.end()) {
          statesAtMerge.erase(sit);
          alt.erase(alt.find(es));
        } else {
          std::set<ExecutionState*>::const_iterator cit
            = statesAtCheckpointMerge.find(es);
          if (cit!=statesAtCheckpointMerge.end()) {
            statesAtCheckpointMerge.erase(cit);
            alt.erase(alt.find(es));
          }
        }
      }
    }    

    baseSearcher->update(current, addedStates, alt);
  } else {
    baseSearcher->update(current, addedStates, removedStates);
  }
}

/* NUKLEAR KLEE end (ENTIRE FILE) */
