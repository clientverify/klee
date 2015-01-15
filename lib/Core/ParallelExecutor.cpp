//===-- ParallelExecutor.cpp ----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "Common.h"

#include "Executor.h"
 
#include "Context.h"
#include "CoreStats.h"
#include "ExternalDispatcher.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "Searcher.h"
#include "SeedInfo.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"
#include "ExecutorTimerInfo.h"
#include "../Solver/SolverStats.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/CommandLine.h"
#include "klee/Common.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Config/Version.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/System/MemoryUsage.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/TypeBuilder.h"
#else
#include "llvm/Attributes.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 1)
#include "llvm/Target/TargetData.h"
#else
#include "llvm/DataLayout.h"
#include "llvm/TypeBuilder.h"
#endif
#endif
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Process.h"

#include "boost/date_time/posix_time/posix_time_types.hpp"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif

#include <cassert>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include <sys/mman.h>

#include <errno.h>
#include <cxxabi.h>

using namespace llvm;
using namespace klee;

#ifdef SUPPORT_METASMT

#include <metaSMT/frontend/Array.hpp>
#include <metaSMT/backend/Z3_Backend.hpp>
#include <metaSMT/backend/Boolector.hpp>
#include <metaSMT/backend/MiniSAT.hpp>
#include <metaSMT/DirectSolver_Context.hpp>
#include <metaSMT/support/run_algorithm.hpp>
#include <metaSMT/API/Stack.hpp>
#include <metaSMT/API/Group.hpp>

#define Expr VCExpr
#define Type VCType
#define STP STP_Backend
#include <metaSMT/backend/STP.hpp>
#undef Expr
#undef Type
#undef STP

using namespace metaSMT;
using namespace metaSMT::solver;

#endif /* SUPPORT_METASMT */

#ifdef TCMALLOC
#include <google/malloc_extension.h>
#endif

// Not used if USE_BOOST_THREAD_SPECIFIC_PTR
__thread Executor::ExecutorContext*  g_executor_context = NULL;

namespace klee {
  extern llvm::cl::opt<bool> DumpStatesOnHalt;
  extern llvm::cl::opt<bool> NoPreferCex;
  extern llvm::cl::opt<bool> RandomizeFork;
  extern llvm::cl::opt<bool> AllowExternalSymCalls;
  extern llvm::cl::opt<bool> DebugPrintInstructions;
  extern llvm::cl::opt<bool> DebugCheckForImpliedValues;
  extern llvm::cl::opt<bool> SimplifySymIndices;
  extern llvm::cl::opt<bool> EqualitySubstitution;
  extern llvm::cl::opt<unsigned> MaxSymArraySize;
  extern llvm::cl::opt<bool> SuppressExternalWarnings;
  extern llvm::cl::opt<bool> AllExternalWarnings;
  extern llvm::cl::opt<bool> OnlyOutputStatesCoveringNew;
  extern llvm::cl::opt<bool> AlwaysOutputSeeds;
  extern llvm::cl::opt<bool> EmitAllErrors;
  extern llvm::cl::opt<bool> UseQueryPCLog;
  extern llvm::cl::opt<bool> UseSTPQueryPCLog;
  extern llvm::cl::opt<bool> NoExternals;
  extern llvm::cl::opt<bool> OnlyReplaySeeds;
  extern llvm::cl::opt<bool> OnlySeed;
  extern llvm::cl::opt<bool> AllowSeedExtension;
  extern llvm::cl::opt<bool> ZeroSeedExtension;
  extern llvm::cl::opt<bool> AllowSeedTruncation;
  extern llvm::cl::opt<bool> NamedSeedMatching;
  extern llvm::cl::opt<double> MaxStaticForkPct;
  extern llvm::cl::opt<double> MaxStaticSolvePct;
  extern llvm::cl::opt<double> MaxStaticCPForkPct;
  extern llvm::cl::opt<double> MaxStaticCPSolvePct;
  extern llvm::cl::opt<double> MaxInstructionTime;
  extern llvm::cl::opt<double> SeedTime;
  extern llvm::cl::opt<double> MaxSTPTime;
  extern llvm::cl::opt<unsigned int> StopAfterNInstructions;
  extern llvm::cl::opt<unsigned> MaxForks;
  extern llvm::cl::opt<unsigned> MaxDepth;
  extern llvm::cl::opt<unsigned> MaxMemory;
  extern llvm::cl::opt<bool> MaxMemoryInhibit;

  extern llvm::cl::opt<unsigned> UseThreads;

  extern ThreadSpecificPointer<RNG>::type theRNG;
}

void Executor::initializePerThread(ExecutionState &state, MemoryManager* memory) {
  static Mutex initializePerThreadLock;
  LockGuard guard(initializePerThreadLock);

  if (!this->solver.get()) {
    // Construct new Solver for this thread
    this->solver.reset(new TimingSolver(initializeSolver(), EqualitySubstitution));
  }

  if (!this->memory.get()) {
    // Set the MemoryManager for this thread
    this->memory.reset(memory);
  }

  if (!theRNG.get()) {
    theRNG.reset(new RNG());
  }

#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
  unsigned width = Context::get().getPointerWidth();

  /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
  ObjectPair errno_addr_obj;
  int *errno_addr = __errno_location();
  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)errno_addr, width), errno_addr_obj)) {
    addExternalObject(state, (void *)errno_addr, sizeof(*errno_addr), false);
  }

  /* from /usr/include/ctype.h:
       These point into arrays of 384, so they can be indexed by any `unsigned
       char' value [0,255]; by EOF (-1); or by any `signed char' value
       [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
  ObjectPair addr_obj;
  const uint16_t **addr = __ctype_b_loc();

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)(*addr-128), width), addr_obj)) {
    addExternalObject(state, const_cast<uint16_t*>(*addr-128), 384 * sizeof **addr, true);
  }

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)addr, width), addr_obj)) {
    addExternalObject(state, addr, sizeof(*addr), true);

    for (unsigned i=0; i<width/8; ++i)
      const_cast<ObjectState*>(addr_obj.second)->markBytePointer(i);
  }
    
  ObjectPair lower_addr_obj;
  const int32_t **lower_addr = __ctype_tolower_loc();

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)(*lower_addr-128), width), lower_addr_obj)) {
    addExternalObject(state, const_cast<int32_t*>(*lower_addr-128), 384 * sizeof **lower_addr, true);
  }

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)lower_addr, width), lower_addr_obj)) {
    addExternalObject(state, lower_addr, sizeof(*lower_addr), true);

    for (unsigned i=0; i<width/8; ++i)
      const_cast<ObjectState*>(lower_addr_obj.second)->markBytePointer(i);
  }
  
  ObjectPair upper_addr_obj;
  const int32_t **upper_addr = __ctype_toupper_loc();

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)(*upper_addr-128), width), upper_addr_obj)) {
    addExternalObject(state, const_cast<int32_t*>(*upper_addr-128), 384 * sizeof **upper_addr, true);
  }

  if (!state.addressSpace.resolveOne(ConstantExpr::alloc((uint64_t)upper_addr, width), upper_addr_obj)) {
    addExternalObject(state, upper_addr, sizeof(*upper_addr), true);

    for (unsigned i=0; i<width/8; ++i)
      const_cast<ObjectState*>(upper_addr_obj.second)->markBytePointer(i);
  }

#endif
#endif
#endif
}

/// Caller will force all other threads to finish execution current instruction
/// and wait for UnPauseExecution()
bool Executor::PauseExecution() {
  if (totalThreadCount > 1) {
    if (!haltExecution && pauseExecutionMutex.try_lock()) {
      Mutex lock;
      UniqueLock guard(lock);

      // Set pauseExecution condition
      pauseExecution = true;

      // Wake up all threads and wait at first barrier
      searcherCond.notify_all();
      threadBarrier->wait();

      return true;
    }
    return false;
  }
  // Pausing always "succeeds" when there is just 1 thread
  return true;
}

void Executor::UnPauseExecution() {
  if (totalThreadCount > 1) {
    pauseExecution = false;
    threadBarrier->wait();
    pauseExecutionMutex.unlock();
  }
}

void Executor::parallelUpdateStates(ExecutionState *current) {
  unsigned addedCount = getContext().addedStates.size();
  stateCount += addedCount;

  if (getContext().addedStates.size() > 0) {
    LockGuard guard(statesMutex);
    states.insert(getContext().addedStates.begin(),
                  getContext().addedStates.end());
  }

  if (getContext().removedStates.size() > 0) {
    LockGuard guard(statesMutex);
    for (std::set<ExecutionState*>::iterator
          it = getContext().removedStates.begin(), ie = getContext().removedStates.end();
        it != ie; ++it) {
      ExecutionState *es = *it;
      --stateCount;

      std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
        seedMap.find(es);
      if (it3 != seedMap.end())
        seedMap.erase(it3);
      processTree->remove(es->ptreeNode);
      states.erase(es);
      delete es;
    }
  }

  getContext().removedStates.clear();
  getContext().addedStates.clear();

  if (addedCount == 1) {
    searcherCond.notify_one();
  } else if (addedCount > 1) {
    searcherCond.notify_all();
  }
}

void Executor::execute(ExecutionState *initialState, MemoryManager *memory) {
  Mutex lock;
  Mutex memoryMutex;

  UniqueLock guard(lock);

  // Initialize thread specific globals and objects
  initializePerThread(*initialState, memory);

  // Wait until all threads have initialized
  threadBarrier->wait();

  ExecutionState *statePtr = NULL;
  while (!empty() && !haltExecution) {

    if (statePtr == NULL)
      statePtr = searcher->trySelectState();

    if (statePtr != NULL) {
      ExecutionState &state = *statePtr;
      
      KInstruction *ki = state.pc;
      stepInstruction(state);
      executeInstruction(state, ki);
      processTimers(&state, MaxInstructionTime);

      // Update searcher with new states and get next state to execute
      // (if supported by searcher)
      statePtr = searcher->updateAndTrySelectState(&state, 
                                                   getContext().addedStates, 
                                                   getContext().removedStates);
      // Update Executor state tracking
      parallelUpdateStates(&state);
    }

    while (pauseExecution
           || (searcher->empty() && !empty() && !haltExecution)) {

      if (statePtr) {
        searcher->update(statePtr,
                         std::set<ExecutionState*>(),
                         std::set<ExecutionState*>());
        statePtr = NULL;
      }

      searcherCond.timed_wait(guard, boost::posix_time::milliseconds(500));

      if (pauseExecution) {
        // 1st Barrier:
        // All threads execept one (in PauseExecution) will wait here
        threadBarrier->wait();
        // 2nd Barrier:
        // All threads execept one (in UnPauseExecution) will wait here
        threadBarrier->wait();
      }
    }

    // Check if we are running out of memory
    if (MaxMemory) {
      // Note: Only one thread needs to check the memory situation
      if ((stats::instructions & 0xFFFF) == 0 && memoryMutex.try_lock()) {
        klee_message("checking memory (%d)", GetThreadID());

        // We need to avoid calling GetMallocUsage() often because it
        // is O(elts on freelist). This is really bad since we start
        // to pummel the freelist once we hit the memory cap.
        unsigned mbs; 
        if (UseThreads > 1) {
          mbs = GetMemoryUsage() >> 20;
        } else {
          mbs = util::GetTotalMallocUsage() >> 20;
        }

        if (mbs > MaxMemory) {
          if (mbs > MaxMemory + 100) {

            // Return statePtr to searcher
            if (statePtr) {
              searcher->update(statePtr, std::set<ExecutionState*>(), 
                               std::set<ExecutionState*>());
              statePtr = NULL;
            }

            // Try to PauseExecution(), may fail
            if (PauseExecution()) {

              // just guess at how many to kill
              unsigned numStates = stateCount;
              unsigned toKill = std::max(1U, numStates - numStates*MaxMemory/mbs);

              if (MaxMemoryInhibit)
                klee_warning("killing %d states (over memory cap)",
                            toKill);

              std::vector<ExecutionState*> arr(states.begin(), states.end());
              for (unsigned i=0,N=arr.size(); N && i<toKill; ++i,--N) {
                unsigned idx = rand() % N;

                // Make two pulls to try and not hit a state that
                // covered new code.
                if (arr[idx]->coveredNew)
                  idx = rand() % N;

                std::swap(arr[idx], arr[N-1]);
                terminateStateEarly(*arr[N-1], "Memory limit exceeded.");
              }
              UnPauseExecution();
            }
          }
          atMemoryLimit = true;
        } else {
          atMemoryLimit = false;
        }

        memoryMutex.unlock();
      }
    }
  }

  // Update searcher with last state we executed if we are halting early
  if (statePtr) 
    searcher->update(statePtr, getContext().addedStates, getContext().removedStates);

  // Alert threads to wake up if there are no more states to execute
  searcherCond.notify_one();

  // Release TSS memory (i.e., don't destroy with thread); the memory manager
  // for this thread may still be needed in dumpState
  this->memory.release();
}

void Executor::parallelRun(ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  states.insert(&initialState);
  ++stateCount;

  if (usingSeeds) {
    std::vector<SeedInfo> &v = seedMap[&initialState];
    
    for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(), 
           ie = usingSeeds->end(); it != ie; ++it)
      v.push_back(SeedInfo(*it));

    int lastNumSeeds = usingSeeds->size()+10;
    double lastTime, startTime = lastTime = util::getWallTime();
    ExecutionState *lastState = 0;
    while (!seedMap.empty()) {
      if (haltExecution) goto dump;

      std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = 
        seedMap.upper_bound(lastState);
      if (it == seedMap.end())
        it = seedMap.begin();
      lastState = it->first;
      unsigned numSeeds = it->second.size();
      ExecutionState &state = *lastState;
      KInstruction *ki = state.pc;
      stepInstruction(state);

      executeInstruction(state, ki);
      processTimers(&state, MaxInstructionTime * numSeeds);

      states.insert(getContext().addedStates.begin(),getContext().addedStates.end());

      for (std::set<ExecutionState*>::iterator
            it = getContext().removedStates.begin(), ie = getContext().removedStates.end();
          it != ie; ++it) {
        ExecutionState *es = *it;
        states.erase(es);
      }

      parallelUpdateStates(&state);

      if ((stats::instructions % 1000) == 0) {
        int numSeeds = 0, numStates = 0;
        for (std::map<ExecutionState*, std::vector<SeedInfo> >::iterator
               it = seedMap.begin(), ie = seedMap.end();
             it != ie; ++it) {
          numSeeds += it->second.size();
          numStates++;
        }
        double time = util::getWallTime();
        if (SeedTime>0. && time > startTime + SeedTime) {
          klee_warning("seed time expired, %d seeds remain over %d states",
                       numSeeds, numStates);
          break;
        } else if (numSeeds<=lastNumSeeds-10 ||
                   time >= lastTime+10) {
          lastTime = time;
          lastNumSeeds = numSeeds;          
          klee_message("%d seeds remaining over: %d states", 
                       numSeeds, numStates);
        }
      }
    }

    klee_message("seeding done (%d states remain)", (int) states.size());

    // XXX total hack, just because I like non uniform better but want
    // seed results to be equally weighted.
    for (std::set<ExecutionState*>::iterator
           it = states.begin(), ie = states.end();
         it != ie; ++it) {
      (*it)->weight = 1.;
    }

    if (OnlySeed)
      goto dump;
  }

  searcher = constructUserSearcher(*this);

  if (!searcher) {
    klee_error("failed to create searcher");
  }

  searcher->update(0, states, std::set<ExecutionState*>());

  threadBarrier = new Barrier(totalThreadCount);

  if (!empty()) {
    if (totalThreadCount == 1) {
      execute(&initialState, NULL);
    } else {
      ThreadGroup threadGroup;
      for (unsigned i=0; i<totalThreadCount; ++i) {
        // Initialize MemoryManager outside of thread
        memoryManagers.push_back(new MemoryManager());
        threadGroup.add_thread(new Thread(&klee::Executor::execute, 
                                          this, &initialState, memoryManagers.back()));
      }
      // Wait for all threads to finish
      threadGroup.join_all();
    }
  }
  totalThreadCount = 1;
  
 dump:
  if (DumpStatesOnHalt && !empty()) {
    std::cerr << "KLEE: halting execution, dumping remaining states\n";
    while (ExecutionState* state = searcher->trySelectState()) {
      stepInstruction(*state); // keep stats rolling
      terminateStateEarly(*state, "Execution halting.");
      searcher->update(state, getContext().addedStates, getContext().removedStates);
      parallelUpdateStates(state);
    }
  }

  // Delete all MemoryManagers used by threads
  memory.release();
  for (std::vector<MemoryManager*>::iterator it = memoryManagers.begin(), 
       ie = memoryManagers.end(); it != ie; ++it)
    delete *it;

  delete threadBarrier;
  delete searcher;
  searcher = 0;
}

Executor::ExecutorContext& Executor::getContext() { 
#if USE_BOOST_THREAD_SPECIFIC_PTR
  ExecutorContext* context_ptr = context.get();
  if(!context_ptr) {
    context.reset(new Executor::ExecutorContext());
    context_ptr = context.get();
  }
  return *context_ptr;
#else
  if(!g_executor_context)
    g_executor_context = new Executor::ExecutorContext();
  return *g_executor_context;
#endif
}

bool Executor::empty() {
  return stateCount == 0;
}

/// Don't use mallinfo, overflows if usage is > 4GB and also doesn't work with
/// threads
size_t Executor::GetMemoryUsage() {
  size_t bytes_used = 0;
#ifdef TCMALLOC
  MallocExtension::instance()->GetNumericProperty(
      "generic.current_allocated_bytes", &bytes_used);
  return bytes_used;
#else
  pid_t myPid = getpid();
  std::stringstream ss;
  ss << "/proc/" << myPid << "/status";

  FILE *fp = fopen(ss.str().c_str(), "r"); 
  if (!fp) { 
    return bytes_used;
  }

  uint64_t peakMem=0;

  char buffer[512];
  while(!peakMem && fgets(buffer, sizeof(buffer), fp)) { 
    if (sscanf(buffer, "VmSize: %llu", (long long unsigned int*)&peakMem)) {
      break; 
    }
  }

  fclose(fp);

  return peakMem * 1024;
#endif
}

