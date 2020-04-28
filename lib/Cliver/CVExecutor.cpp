//===-- CVExecutor.cpp -====-------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "cliver/CVExecutor.h"

#include "cliver/ConstraintPruner.h"
#include "cliver/CVExecutionState.h"
#include "cliver/CVSearcher.h"
#include "cliver/CVStream.h"
#include "cliver/ExecutionObserver.h"
#include "cliver/NetworkManager.h"
#include "cliver/StateMerger.h"
#include "cliver/LockFreeVerifySearcher.h"
#include "cliver/ThreadBufferedSearcher.h"
#include "CVCommon.h"

#include "klee/Internal/Support/ErrorHandling.h"
#include "../Core/Context.h"
#include "../Core/CoreStats.h"
#include "../Core/ExternalDispatcher.h"
#include "../Core/ImpliedValue.h"
#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"
#include "../Core/PTree.h"
#include "../Core/ProfileTree.h"
#include "../Core/SeedInfo.h"
#include "../Core/StatsTracker.h"
#include "../Core/TimingSolver.h"
#include "../Core/UserSearcher.h"
#include "../Core/Searcher.h"
#include "../Core/SpecialFunctionHandler.h"
#include "klee/SolverStats.h"

#include "klee/CommandLine.h"
#include "klee/Common.h"
#include "klee/Constants.h"
#include "klee/Config/config.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/System/MemoryUsage.h"
#include "klee/Interpreter.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/GetElementPtrTypeIterator.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
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

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif
#include "llvm/IR/Instruction.h"

#include <cassert>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <set>

#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include <cxxabi.h>

#ifdef TCMALLOC
#include <google/malloc_extension.h>
#endif

namespace klee {
  // Defined in lib/Core/Executor.cpp
	extern RNG theRNG;

  // Command line options defined in lib/Core/Executor.cpp
  extern llvm::cl::opt<bool> DumpStatesOnHalt;
  extern llvm::cl::opt<bool> DebugPrintInstructions;
  extern llvm::cl::opt<bool> OnlyOutputStatesCoveringNew;
  extern llvm::cl::opt<bool> AlwaysOutputSeeds;
  extern llvm::cl::opt<unsigned int> StopAfterNInstructions;
  extern llvm::cl::opt<double> MaxInstructionTime;
  extern llvm::cl::opt<unsigned> MaxMemory;
  extern llvm::cl::opt<bool> MaxMemoryInhibit;

  // Command line options defined in lib/Core/UserSearcher.cpp
  extern llvm::cl::opt<unsigned> UseThreads;
}

namespace cliver {

llvm::cl::opt<bool> 
EnableCliver("cliver", 
             llvm::cl::desc("Enable cliver."), llvm::cl::init(false));

llvm::cl::opt<bool> 
DisableEnvironmentVariables("-disable-env-vars", 
                            llvm::cl::desc("Disable environment variables"), 
                            llvm::cl::init(true));

/// Give symbolic variables a name equal to the declared name + id
llvm::cl::opt<bool>
UseFullVariableNames("use-full-variable-names",
                     llvm::cl::init(false));

llvm::cl::opt<bool>
DebugExecutor("debug-executor",
              llvm::cl::init(false));
  
llvm::cl::opt<bool>
PrintFunctionCalls("print-function-calls",
                   llvm::cl::init(false));

llvm::cl::opt<bool>
DebugPrintInstructionAtCVFork("debug-print-instructions-at-cvfork",
    llvm::cl::desc("Print instruction when cliver forks an execution state."));

llvm::cl::opt<bool> 
NoXWindows("no-xwindows",
           llvm::cl::desc("Do not allow external XWindows function calls"),
           llvm::cl::init(false));

llvm::cl::opt<bool>
NativeAES("native-aes",
           llvm::cl::init(false));

llvm::cl::opt<bool>
SkipPrintf("skip-printf",
           llvm::cl::init(false));

llvm::cl::opt<bool>
EnableStateRebuilding("state-rebuilding",
                       llvm::cl::desc("Enable state rebuilding (default=0)"),
                       llvm::cl::init(false));

extern llvm::cl::opt<bool> EnableLockFreeSearcher;
extern llvm::cl::opt<unsigned> BufferedSearcherSize;

////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#undef CVDEBUG
#define CVDEBUG(x) \
	__CVDEBUG(DebugExecutor, x);

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x) \
	__CVDEBUG_S(DebugExecutor, __state_id, __x)

#else

#undef CVDEBUG
#define CVDEBUG(x)

#undef CVDEBUG_S
#define CVDEBUG_S(__state_id, __x)

#endif

////////////////////////////////////////////////////////////////////////////////

klee::Solver *createCanonicalSolver(klee::Solver *_solver);

////////////////////////////////////////////////////////////////////////////////

CVExecutor::CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ih)
: klee::Executor(opts, ih), 
  cv_(static_cast<ClientVerifier*>(ih)),
  memory_usage_mbs_(0),
  searcher_init_flag_(ONCE_FLAG_INIT) {
}

CVExecutor::~CVExecutor() {}

std::set <std::string> f_called_this_round;
void CVExecutor::print_round_functions(int round, int event_type){
  // printing set
  std::set<std::string>::iterator itr;
  std::cout << "\nThe set functions for this round " << round << " event type " << event_type <<" are: ";
  for (itr = f_called_this_round.begin(); itr != f_called_this_round.end(); ++itr) {
    std::cout << *itr << ", ";
  }
  std::cout << std::endl;
}

void CVExecutor::reset_round_functions(){
  f_called_this_round.clear();
}

void CVExecutor::executeCall(klee::ExecutionState &state, 
                             klee::KInstruction *ki,
                             llvm::Function *f,
                             std::vector< klee::ref<klee::Expr> > &arguments) {
  if (PrintFunctionCalls) {
    CVMESSAGE(std::setw(2) << std::right << klee::GetThreadID() << " "
              << std::string(state.stack.size(), '-') << f->getName().str());
  }
  std::string str(f->getName().data());
  f_called_this_round.insert(str);

  std::string XWidgetStr("Widget_");
  if (NoXWindows && f->getName().substr(0,XWidgetStr.size()) == XWidgetStr) {
    CVMESSAGE("Skipping function call to " << f->getName().str());
    return;
  }

  if (NativeAES && (f->getName() == "AES_encrypt"
      || f->getName() == "gcm_gmult_4bit"
      || f->getName() == "gcm_ghash_4bit")) {
    callExternalFunction(state, ki, f, arguments);
    return;
  }

  // Call base Executor implementation
  Executor::executeCall(state, ki, f, arguments);
}

void CVExecutor::callExternalFunction(klee::ExecutionState &state,
                                      klee::KInstruction *target,
                                      llvm::Function *function,
                                      std::vector< klee::ref<klee::Expr> > &arguments) {


  if (SkipPrintf && function->getName().str() == "printf") {
    //CVMESSAGE("Skipping function call to " << function->getName().str());
    return;
  }

  if (NoXWindows && function->getName()[0] == 'X') { 
    if (function->getName().str() == "XParseGeometry" ||
        function->getName().str() == "XStringToKeysym") {
      CVDEBUG("Calling X function: " << function->getName().str());
    } else {
      CVDEBUG("Skipping function call to " << function->getName().str());
      return;
    }
  }

  // Call base Executor implementation
  Executor::callExternalFunction(state, target, function, arguments);
}

/// CVExecutor::parallelUpdateStates differs from
/// Executor::parallelUpdateStates and Executor::updatesStates by not
/// maintaining the global std::set of ExecutionStates
void CVExecutor::parallelUpdateStates(klee::ExecutionState *current,
                                      bool updateStateCount) {
  // Retrieve state counts for this thread
  int addedCount = getContext().addedStates.size();
  int removedCount = getContext().removedStates.size();

  // update atomic stateCount
  if (updateStateCount) {
    stateCount += (addedCount - removedCount);
  }

  // print diagnostic info if any change in stateCount
  if (DebugExecutor && (addedCount != 0 || removedCount != 0)) {
    std::string s;
    llvm::raw_string_ostream stack_info(s);
    current->dumpStack(stack_info);
    CVDEBUG("Thread " << klee::GetThreadID() << ": addedCount=" << addedCount
                      << ", removedCount=" << removedCount
                      << ", (new) stateCount=" << stateCount
                      << ", execution state stack:\n"
                      << stack_info.str());
  }

  if (!EnableLockFreeSearcher) {
    // Delete removed states 
    // TODO move all state delete into cvsearcher
    auto& removedStates = getContext().removedStates;
    for (auto es : removedStates) {
      delete es;
    }
  }

  // Clear thread local state sets
  getContext().removedStates.clear();
  getContext().addedStates.clear();

  if (!EnableLockFreeSearcher) {
  // Wake up sleeping threads
  if (addedCount == 1) {
    searcherCond.notify_one();
  } else if (addedCount > 1) {
    searcherCond.notify_all();
  }
  }
}

void CVExecutor::runFunctionAsMain(llvm::Function *f,
				 int argc,
				 char **argv,
				 char **envp) {
  using namespace klee;
  using namespace llvm;
  std::vector<ref<Expr> > arguments;

  // force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

	// Only difference from klee::Executor::runFunctionAsMain()
  cv_->initialize();
  CVExecutionState *state = new CVExecutionState(kmodule->functionMap[f]);
	state->initialize(cv_);
  
  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  for (envc=0; envp[envc]; ++envc) {
    CVDEBUG("Environment:" << std::string(envp[envc]) );
  }
  CVDEBUG("Environment count = " << envc);

  for (envc=0; envp[envc]; ++envc) ;

  if (DisableEnvironmentVariables)
    envc = 0;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];
  assert(kf);
  Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {
    arguments.push_back(klee::ConstantExpr::alloc(argc, Expr::Int32));

    if (++ai!=ae) {
      argvMO = memory->allocate((argc+1+envc+1+1) * NumPtrBytes, false, true,
                                f->begin()->begin());

      if (!argvMO)
        klee_error("Could not allocate memory for function arguments");

      arguments.push_back(argvMO->getBaseExpr());

      if (++ai!=ae) {
        uint64_t envp_start = argvMO->address + (argc+1)*NumPtrBytes;
        arguments.push_back(Expr::createPointer(envp_start));

        if (++ai!=ae)
          klee_error("invalid main function (expect 0-3 arguments)");
      }
    }
  }

  if (pathWriter) 
    state->pathOS = pathWriter->open();
  if (symPathWriter) 
    state->symPathOS = symPathWriter->open();


  if (statsTracker)
    statsTracker->framePushed(*state, 0);

  assert(arguments.size() == f->arg_size() && "wrong number of arguments");
  for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
    bindArgument(kf, i, *state, arguments[i]);

  if (argvMO) {
    ObjectState *argvOS = bindObjectInState(*state, argvMO, false);

    for (int i=0; i<argc+1+envc+1+1; i++) {
      if (i==argc || i>=argc+1+envc) {
        // Write NULL pointer
        argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
      } else {
        char *s = i<argc ? argv[i] : envp[i-(argc+1)];
        int j, len = strlen(s);
        
        MemoryObject *arg = memory->allocate(len+1, false, true, state->pc->inst);
        if (!arg)
          klee_error("Could not allocate memory for function arguments");
        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++)
          os->write8(j, s[j]);

        // Write pointer to newly allocated and initialised argv/envp c-string
        argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
      }
    }
  }
  
  initializeGlobals(*state);

  //Setting the initial execution state
  processTree = new PTree(state);
  state->ptreeNode = processTree->root;

  profileTree = new ProfileTree(state);
  state->profiletreeNode = profileTree->root;
  assert(state->profiletreeNode != NULL);  

  //Run things
  run(*state);

  //Record process tree info, and delete
  delete processTree;
  processTree = 0;


  //Record profile tree info, and delete
  int my_total_instructions = profileTree->get_total_ins_count();
  int my_total_node         = profileTree->get_total_node_count();
  int my_total_branches     = profileTree->get_total_branch_count();
  int my_clones             = profileTree->get_total_clone_count();
  int my_returns            = profileTree->get_total_ret_count();
  int my_calls              = profileTree->get_total_call_count();

  printf("my_total_instructions %d\n", my_total_instructions);
  printf("my_total_node %d\n", my_total_node);
  printf("my_total_branches %d\n", my_total_branches);
  printf("my_total_clones %d\n", my_clones);
  printf("my_total_returns %d\n", my_returns);
  printf("my_total_calls %d\n", my_calls);

  printf("postorder tree\n");
  int postorder_ins_count = profileTree->post_processing_dfs(profileTree->root);
  assert(postorder_ins_count == my_total_instructions);
  printf("my_total_instructions %d postorder_ins_count %d\n", my_total_instructions, postorder_ins_count);
//  profileTree->dump_branch_clone_graph("/playpen/cliver0/branch_clone_processtree.graph", cv_);

  delete profileTree;
  profileTree = 0;

  cv_->write_all_stats();

  //// hack to clear memory objects
  //delete memory;
  //memory = new MemoryManager();
  
  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();
}

void CVExecutor::execute(klee::ExecutionState *initialState,
                         klee::MemoryManager *memory) {
  // Initialize thread specific globals and objects
  initializePerThread(*initialState, memory);

  // Wait until all threads have initialized
  threadBarrier->wait();

  // Only one thread needs to add state to searcher after init
  thread_call_once(searcher_init_flag_,
            [](klee::Searcher *s, klee::ExecutionState *e) {
              std::vector<klee::ExecutionState*> one_state, empty_vec;
              one_state.push_back(e);
              s->update(0, one_state, empty_vec); // Add state
              s->update(0, empty_vec, empty_vec); // Force flush (ThreadBufferedSearcher)
              },
            searcher, initialState);

  // Now all states can execute
  threadBarrier->wait();

  klee::ExecutionState *statePtr = NULL;

  threadInstCount.reset(new unsigned());
  *threadInstCount = 0;
  ++live_threads_;

  Executor::ExecutorContext& context = getContext();
  unsigned instCountBeforeUpdate = 0;
  while (!empty() && !haltExecution) {


    if (statePtr == NULL) {
      statePtr = searcher->trySelectState();
    }

    if (statePtr != NULL && !haltExecution) {
      klee::ExecutionState &state = *statePtr;

      static_cast<CVExecutionState*>(&state)->set_event_flag(false);

      // This is the main execution loop where most of the time is spent.
      // Note that executeInstruction() might fork a new state, which is
      // appended to context.addedStates.
      klee::KInstruction *ki = state.pc;
      stepInstruction(state);
      executeInstruction(state, ki);
      ++(*threadInstCount);

      // XXX Process timers in cliver?
      //processTimers(&state, klee::MaxInstructionTime);

      // Increment instruction counters
      ++stats::round_instructions;
      static_cast<CVExecutionState*>(&state)->property()->inst_count++;
      if (static_cast<CVExecutionState*>(&state)->property()->is_recv_processing)
        ++stats::recv_round_instructions;

      // Handle post execution events if state wasn't removed
      if (std::find(context.removedStates.begin(), context.removedStates.end(),
                    &state) == context.removedStates.end()) {
        handle_post_execution_events(state);
      }

      // Handle post execution events for each newly added state
      foreach (klee::ExecutionState* astate, context.addedStates) {
        handle_post_execution_events(*astate);
      }

      // Notify all if a state was removed
      foreach (klee::ExecutionState* rstate, context.removedStates) {
        cv_->notify_all(ExecutionEvent(CV_STATE_REMOVED, rstate));
      }

      // Update searcher with new states and get next state to execute, if
      // anything interesting happened (network event, fork, etc.).
      // If nothing interesting happened (e.g., add instruction), we just
      // continue with the same state.
      if (static_cast<CVExecutionState *>(&state)->event_flag() ||
          !context.removedStates.empty() || !context.addedStates.empty()) {

        // Update stateCount *before* potentially passing any new states off to
        // the searcher. Why is this important? If a worker makes new states
        // available to other workers BEFORE it updates stateCount, then in
        // rare circumstances, the following could occur:
        //
        // 1. Initial conditions: stateCount = 1; state S1 assigned to Worker1.
        // 2. Worker1 encounters symbolic branch in S1.
        // 3. Worker1 clones S1 to create S2.
        // 4. Worker1 makes S2 available to other worker threads.
        // 5. Worker2 obtains S2 and executes it.
        // 6. Worker2 encounters a contradiction and state S2 dies.
        // 7. Worker2 executes stateCount+=(0-1), yielding stateCount = 0.
        // 8. Worker1 executes stateCount+=(1-0), yielding stateCount = 1.
        //
        // Between steps 7 and 8, we have a small amount of time when
        // stateCount = 0, so other worker threads may detect empty() == true,
        // and exit.
        int addedCount = context.addedStates.size();
        int removedCount = context.removedStates.size();
        stateCount += (addedCount - removedCount);

        // Update searcher with new states and get next state to execute
        // (if supported by searcher)
        statePtr = searcher->updateAndTrySelectState(&state,
                                                     context.addedStates,
                                                     context.removedStates);

        // Update Executor state tracking (stateCount already updated above)
        parallelUpdateStates(&state, false);
      }
    }

    // We hit this point after we execute an instruction, or if our
    // trySelectState failed and we have a null state pointer.

    // If I should go to sleep, go to sleep.
    //
    // Reasons for going to sleep comprise:
    // 1. pauseExecution (global), usually indicating the round is "finished"
    // 2. round is unfinished but no work is available for this thread
    //    - no state currently being executed (statePtr == NULL)
    //    - searcher has no states ready to be executed (searcher->empty())
    //    - somewhere there are still states to execute (!empty())
    //    - we have not fully finished behavioral verification (!haltExecution)
    while (pauseExecution ||
           (!statePtr && searcher->empty() && !empty() && !haltExecution)) {

      if (pauseExecution && statePtr) {
        searcher->update(statePtr,
                         std::vector<klee::ExecutionState*>(),
                         std::vector<klee::ExecutionState*>());
        statePtr = NULL;
      }

      // Call empty update to flush buffered searcher
      searcher->update(NULL,
                       std::vector<klee::ExecutionState*>(),
                       std::vector<klee::ExecutionState*>());

#if 1
      if (klee::UseThreads > 1) {
        klee::UniqueLock searcherCondGuard(searcherCondLock);
        if (!pauseExecution && live_threads_ > 1) {
          --live_threads_;
          searcherCond.wait(searcherCondGuard);
          ++live_threads_;
        }
      }

      if (pauseExecution) {
        // All threads except one (in PauseExecution) will wait here
        threadBarrier->wait();
        // All threads except one (in UnPauseExecution) will wait here
        threadBarrier->wait();
      }

#endif

#if 0
      {
        //if (!pauseExecution && live_threads_ > 1) {
          --live_threads_;
          {
          klee::UniqueLock searcherCondGuard(searcherCondLock);
          searcherCond.wait(searcherCondGuard);
          }
          ++live_threads_;
        //}
      }
#endif

    }

    // Check if we are running out of memory
    if (klee::MaxMemory) {
      // Note: Only one thread needs to check the memory situation
      if ((klee::stats::instructions & 0xFFFF) == 0 && memory_lock_.try_lock()) {
        klee::klee_message("checking memory (%d)", klee::GetThreadID());

        // We need to avoid calling GetMallocUsage() often because it
        // is O(elts on freelist). This is really bad since we start
        // to pummel the freelist once we hit the memory cap.
        unsigned mbs;
        if (klee::UseThreads > 1) {
          mbs = GetMemoryUsage() >> 20;
        } else {
          mbs = klee::util::GetTotalMallocUsage() >> 20;
        }

        if (mbs > klee::MaxMemory) {
          if (mbs > klee::MaxMemory + 100) {

            // Return statePtr to searcher
            if (statePtr) {
              searcher->update(statePtr, std::vector<klee::ExecutionState*>(),
                               std::vector<klee::ExecutionState*>());
              statePtr = NULL;
            }

            // Try to PauseExecution(), may fail
            if (PauseExecution()) {

              // just guess at how many to kill
              unsigned numStates = stateCount;
              unsigned toKill = std::max(1U, numStates - numStates*klee::MaxMemory/mbs);

              if (klee::MaxMemoryInhibit)
                klee::klee_warning("killing %d states (over memory cap)",
                            toKill);

              std::vector<klee::ExecutionState*> arr(states.begin(), states.end());
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

        memory_lock_.unlock();
      }
    }
  }

  // Update searcher with last state we executed if we are halting early
  if (statePtr) {
    searcher->update(statePtr, getContext().addedStates, getContext().removedStates);
  }

  // Alert threads to wake up if there are no more states to execute
  searcherCond.notify_all();

  cv_->notify_all(ExecutionEvent(CV_HALT_EXECUTION));

  // Release TSS memory (i.e., don't destroy with thread); the memory manager
  // for this thread may still be needed in dumpState
  this->memory.release();

  // Print per-thread stats
  CVMESSAGE("Thread " << klee::GetThreadID() << ": executed "
                      << *threadInstCount << " instructions and exited");
}

#if 1
void CVExecutor::run(klee::ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  states.insert(&initialState);
  ++stateCount;

  CVSearcher* cv_searcher = cv_->searcher();
  LockFreeVerifySearcher* lfvs = NULL;

  if (EnableLockFreeSearcher)
    cv_searcher = lfvs = new LockFreeVerifySearcher(cv_searcher, this);

  if (BufferedSearcherSize > 0)
    cv_searcher = new ThreadBufferedSearcher(cv_searcher);

	searcher = cv_searcher;

  if (!searcher) {
    klee::klee_error("failed to create searcher");
  }

  cv_->hook(cv_searcher);

  threadBarrier = new klee::Barrier(klee::UseThreads);

  if (klee::UseThreads > 1 && EnableLockFreeSearcher) {
    lfvs->threadBarrier = threadBarrier;
    lfvs->searcherCond = &searcherCond;
    lfvs->searcherCondLock = &searcherCondLock;
  }

  live_threads_ = 0;

  totalThreadCount = klee::UseThreads;
  if (!empty()) {
    if (klee::UseThreads <= 1) {
      // Execute state in this thread
      execute(&initialState, NULL);
    } else {
      klee::ThreadGroup threadGroup;
      int worker_count = klee::UseThreads;
      if (EnableLockFreeSearcher) {
        worker_count--;
        threadGroup.add_thread(
            new klee::Thread(&cliver::LockFreeVerifySearcher::Worker, lfvs));
      }
      for (unsigned i=0; i<worker_count; ++i) {
        memoryManagers.push_back(new klee::MemoryManager(&arrayCache));
        threadGroup.add_thread(new klee::Thread(&cliver::CVExecutor::execute,
                                          this, &initialState, memoryManagers.back()));
      }

      // Wait for all threads to finish
      threadGroup.join_all();
    }
  }
  totalThreadCount = 1;

  cv_->WriteSearcherStageGraph();

  if (klee::DumpStatesOnHalt && !empty()) {
    std::cerr << "KLEE: halting execution, dumping remaining states\n";
    while (klee::ExecutionState* state = searcher->trySelectState()) {
      stepInstruction(*state); // keep stats rolling
      terminateStateEarly(*state, "Execution halting.");
      searcher->update(state, getContext().addedStates, getContext().removedStates);
      parallelUpdateStates(state);
    }
  }

  // Delete all MemoryManagers used by threads
  memory.release();
  for (std::vector<klee::MemoryManager*>::iterator it = memoryManagers.begin(),
       ie = memoryManagers.end(); it != ie; ++it)
    delete *it;

}
#endif

void CVExecutor::handle_pre_execution_events(klee::ExecutionState &state) {
  klee::KInstruction* ki = state.pc;
  llvm::Instruction* inst = ki->inst;

  //switch(inst->getOpcode()) {
  //  // terminator instructions: 
  //  // 'ret', 'br', 'switch', 'indirectbr', 'invoke', 'unwind', 'resume', 'unreachable'
  //  case llvm::Instruction::Ret: {
  //    cv_->notify_all(ExecutionEvent(CV_RETURN, &state));
  //    break;
  //  }
  //  //case llvm::Instruction::Br: {
  //  //  llvm::BranchInst* bi = cast<llvm::BranchInst>(inst);
  //  //  if (bi->isUnconditional()) {
  //  //    cv_->notify_all(ExecutionEvent(CV_BRANCH_UNCONDITIONAL, &state));
  //  //  } else {
  //  //    cv_->notify_all(ExecutionEvent(CV_BRANCH, &state));
  //  //  }
  //  //  break;
  //  //}
  //  case llvm::Instruction::Call: {
  //    llvm::CallSite cs(inst);
  //    llvm::Function *f = getCalledFunction(cs, state);
  //    if (f && f->isDeclaration() 
  //        && f->getIntrinsicID() == llvm::Intrinsic::not_intrinsic)
  //      cv_->notify_all(ExecutionEvent(CV_CALL_EXTERNAL, &state));
  //    else
  //      cv_->notify_all(ExecutionEvent(CV_CALL, &state));
  //    break;
  //  }
  //}
}

void CVExecutor::handle_post_execution_events(klee::ExecutionState &state) {
  klee::KInstruction* ki = state.prevPC;
  llvm::Instruction* inst = ki->inst;

  switch(inst->getOpcode()) {
    case llvm::Instruction::Call: {
      llvm::CallSite cs(inst);
      llvm::Value *fp = cs.getCalledValue();
      llvm::Function *f = getTargetFunction(fp, state);
			if (!f) {
				// special case the call with a bitcast case
				llvm::ConstantExpr *ce = llvm::dyn_cast<llvm::ConstantExpr>(fp);
				if (ce && ce->getOpcode()==llvm::Instruction::BitCast) {
					f = dyn_cast<llvm::Function>(ce->getOperand(0));
				}
			}
			if (function_call_events_.find(f) != function_call_events_.end()) {
        cv_->notify_all(ExecutionEvent(function_call_events_[f], &state));
			}
      break;
    }
  }

  // Trigger event when a new BasicBlock is entered
  klee::KFunction *kf = state.stack.back().kf;
  llvm::BasicBlock *basic_block = ki->inst->getParent();
  //unsigned basic_block_id = kf->basicBlockEntry[basic_block];

  auto bb_entry_it = kf->basicBlockEntry.find(basic_block);
  if (bb_entry_it != kf->basicBlockEntry.end()) {
    unsigned basic_block_id = bb_entry_it->second;

    if (ki == kf->instructions[basic_block_id]) {
      cv_->notify_all(ExecutionEvent(CV_BASICBLOCK_ENTRY, &state));
    }
  }

}

void CVExecutor::stepInstruction(klee::ExecutionState &state) {

	if (klee::DebugPrintInstructions) {
    CVMESSAGE(*state.pc);
  }

  if (statsTracker)
    statsTracker->stepInstruction(state);

  ++klee::stats::instructions;
  state.prevPC = state.pc;
  ++state.pc;

  if (klee::StopAfterNInstructions &&
      klee::stats::instructions==klee::StopAfterNInstructions)
    haltExecution = true;
}

void CVExecutor::executeMakeSymbolic(klee::ExecutionState &state, 
                                     const klee::MemoryObject *mo,
                                     const std::string &name) {

  assert(!replayKTest && "replayOut not supported by cliver");

  CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);

  bool unnamed = (name == "unnamed") ? true : false;

  if (!unnamed && cvstate->multi_pass_clone_ == NULL) {
    CVDEBUG("cloning first state before symbolic event");
    cvstate->multi_pass_clone_ = cvstate->clone(cvstate->property()->clone());
    // Roll back one instruction
    cvstate->multi_pass_clone_->pc = cvstate->multi_pass_clone_->prevPC;
  }

  // Create a new object state for the memory object (instead of a copy).
  std::string array_name = cvstate->get_unique_array_name(name);

  const klee::Array *array = NULL;
  if (!unnamed)
    array = cvstate->multi_pass_assignment().getArray(array_name);

  bool multipass = false;
  if (array != NULL) {
    CVDEBUG("Multi-pass: Concretization found for " << array_name);
    multipass = true;
  } else {
    if (!unnamed) {
      CVDEBUG("Multi-pass: Concretization not found for " << array_name);
    }
    array = arrayCache.CreateArray(array_name, mo->size);
  }

  bindObjectInState(state, mo, false, array);

  std::vector<unsigned char> *bindings = NULL;
  if (cvstate->property()->pass_count > 0 && multipass) {
    bindings = cvstate->multi_pass_assignment().getBindings(array_name);

    CVDEBUG("Multi-pass: Binding variable:" << array->name
            << " with concrete assignment of length "
            << mo->size << " with bindings size " 
            << (bindings ? bindings->size() : 0));

    if (!bindings || bindings->size() != mo->size) {
      CVDEBUG("Multi-pass: Terminating state, bindings mismatch");
      terminateState(state);
    } else {
      const klee::ObjectState *os = state.addressSpace.findObject(mo);
      klee::ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      assert(wos && "Writeable object is NULL!");
      unsigned idx = 0;
      foreach (unsigned char b, *bindings) {
        wos->write8(idx, b);
        idx++;
      }
    }
  } else {
    CVDEBUG("Created symbolic: " << array->name << " in " << *cvstate);
    state.addSymbolic(mo, array);
    cvstate->property()->symbolic_vars++;
  }
}

klee::Executor::StatePair 
CVExecutor::fork(klee::ExecutionState &current, 
                 klee::ref<klee::Expr> condition, bool isInternal) {

  klee::Solver::Validity res;

  double timeout = coreSolverTimeout;

  solver->setTimeout(timeout);

  bool success = solver->evaluate(current, condition, res);
  solver->setTimeout(0);

  if (!success) {
    current.pc = current.prevPC;
    terminateStateEarly(current, "Query timed out (fork).");
    return StatePair(0, 0);
  }

  //if (replayPath) {
  //  assert(replayPosition < replayPath->size() &&
  //          "ran out of branches in replay path mode");
  //  bool branch = (*replayPath)[replayPosition++];
  //  
  //  if (res==klee::Solver::True) {
  //    assert(branch && "hit invalid branch in replay path mode");
  //  } else if (res==klee::Solver::False) {
  //    assert(!branch && "hit invalid branch in replay path mode");
  //  } else {
  //    // add constraints
  //    if(branch) {
  //      res = klee::Solver::True;
  //      addConstraint(current, condition);
  //    } else  {
  //      res = klee::Solver::False;
  //      addConstraint(current, klee::Expr::createIsZero(condition));
  //    }
  //  }
  //} 
 
  if (res == klee::Solver::True) {

    // if (DebugPrintInstructionAtCVFork) {
    //   CVMESSAGE("icount=" << klee::stats::instructions
    //                       << " [fork|always-true] @ " << *current.prevPC);
    // }

    if (EnableStateRebuilding && !replayPath)
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_TRUE, &current));

    return StatePair(&current, 0);

  } else if (res == klee::Solver::False) {

    // if (DebugPrintInstructionAtCVFork) {
    //   CVMESSAGE("icount=" << klee::stats::instructions
    //                       << " [fork|always-false] @ " << *current.prevPC);
    // }

    if (EnableStateRebuilding && !replayPath)
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_FALSE, &current));

    return StatePair(0, &current);
  } else {

    klee::ExecutionState *falseState, *trueState = &current;

    ++klee::stats::forks;

    if (DebugPrintInstructionAtCVFork) {
      CVMESSAGE("icount=" << klee::stats::instructions
                          << " [fork|both-possible] @ " << *current.prevPC);
    }

    falseState = trueState->branch();
    getContext().addedStates.push_back(falseState);

    addConstraint(*trueState, condition);
    addConstraint(*falseState, klee::Expr::createIsZero(condition));

    llvm::Instruction* current_inst = current.prevPC->inst;

    if (EnableStateRebuilding && !replayPath) {
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_FALSE, falseState));
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_TRUE, trueState));
    }

    assert(trueState->profiletreeNode->parent == falseState->profiletreeNode->parent);
    assert(trueState->profiletreeNode->parent->get_type() == klee::ProfileTreeNode::branch_parent ||
        trueState->profiletreeNode->parent->get_type() == klee::ProfileTreeNode::clone_parent);
    assert(trueState->profiletreeNode->get_type() == klee::ProfileTreeNode::leaf);
    assert(falseState->profiletreeNode->get_type() == klee::ProfileTreeNode::leaf);

    return StatePair(trueState, falseState);
  }
}

// NOTE: this function seems to never be called during cliver execution.
void CVExecutor::branch(klee::ExecutionState &state, 
		const std::vector< klee::ref<klee::Expr> > &conditions,
    std::vector<klee::ExecutionState*> &result) {

  unsigned N = conditions.size();
  assert(N);

  klee::stats::forks += N-1;

  // Cliver assumes each state branches from a single other state
  assert(N <= 1); // FIXME: This seems wrong, given the loop bound below

  result.push_back(&state);
  for (unsigned i=1; i<N; ++i) {
		klee::ExecutionState *es = result[klee::theRNG.getInt32() % i];
		klee::ExecutionState *ns = es->branch();
    getContext().addedStates.push_back(ns);
    result.push_back(ns);
    es->ptreeNode->data = 0;
    std::pair<klee::PTree::Node*,klee::PTree::Node*> res = 
      processTree->split(es->ptreeNode, ns, es);
    ns->ptreeNode = res.first;
    es->ptreeNode = res.second;
 
    // TODO do we still need this event? can we just use Executor::branch()?
    cv_->notify_all(ExecutionEvent(CV_BRANCH, ns, es));
  }

  for (unsigned i=0; i<N; ++i)
    if (result[i])
      addConstraint(*result[i], conditions[i]);
}

void CVExecutor::add_external_handler(std::string name, 
		klee::SpecialFunctionHandler::ExternalHandler external_handler,
		bool has_return_value) {
	
	llvm::Function *function = kmodule->module->getFunction(name);

	if (function == NULL) {
		cv_message("External Handler %s not added: Usage not found",
				name.c_str());
	} else {
		specialFunctionHandler->addExternalHandler(function, 
				external_handler, has_return_value);
	}
}

void CVExecutor::resolve_one(klee::ExecutionState *state, 
		klee::ref<klee::Expr> address_expr, klee::ObjectPair &result,
    bool writeable = false) {

  bool success;
  state->addressSpace.resolveOne(*state, solver.get(), address_expr,
                                 result, success);
  assert(success && "resolveOne failed");
  const klee::MemoryObject *mo = result.first;
  const klee::ObjectState *os = result.second;

  result.first = mo;

  if (writeable)
    result.second = state->addressSpace.getWriteable(mo, os);
  else
    result.second = os;
}

void CVExecutor::terminateStateOnExit(klee::ExecutionState &state) {
  if (!klee::OnlyOutputStatesCoveringNew || state.coveredNew || 
      (klee::AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(state, 0, 0);
  
  // Check if finished:
  CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);
  if (cvstate->network_manager() &&
      cvstate->network_manager()->socket() &&
      !cvstate->network_manager()->socket()->is_open()) {
    // TODO process finished state here instead of Searcher

    cv_->notify_all(ExecutionEvent(CV_FINISH, cvstate));
  } else {
    terminateState(state);
  }
}

void CVExecutor::terminate_state(CVExecutionState* state) {
	terminateState(*state);
}

void CVExecutor::remove_state_internal(CVExecutionState* state) {
  cv_->notify_all(ExecutionEvent(CV_STATE_REMOVED, state));
  {
    klee::LockGuard guard(statesMutex);
    states.erase(state);
  }
  delete state;
}

void CVExecutor::remove_state_internal_without_notify(CVExecutionState* state) {
  {
    klee::LockGuard guard(statesMutex);
    states.erase(state);
  }
  delete state;
}

void CVExecutor::bind_local(klee::KInstruction *target, 
		CVExecutionState *state, unsigned i) {
	bindLocal(target, *state, klee::ConstantExpr::alloc(i, klee::Expr::Int32));
}

bool CVExecutor::compute_truth(CVExecutionState* state, 
		klee::ref<klee::Expr> query, bool &result) {
	solver->mustBeTrue(*state, query, result);
	return result;
}

bool CVExecutor::compute_false(CVExecutionState* state, 
		klee::ref<klee::Expr> query, bool &result) {
	solver->mustBeFalse(*state, query, result);
	return result;
}

void CVExecutor::add_constraint(CVExecutionState *state, 
		klee::ref<klee::Expr> condition) {
	addConstraint(*state, condition);
}

klee::TimingSolver* CVExecutor::get_solver() {
  return solver.get();
}

void CVExecutor::register_function_call_event(const char **fname, 
                                              ExecutionEventType event_type) {
  llvm::Function* f = kmodule->module->getFunction(*fname);
  if (f) {
    function_call_events_[f] = event_type;
  } else {
    cv_message("Not registering function call event for %s", *fname);
  }
}

//void CVExecutor::register_event(const CliverEventInfo& event_info) {
//	if (event_info.opcode == llvm::Instruction::Call) {
//		llvm::Function* f = kmodule->module->getFunction(event_info.function_name);
//		if (f) {
//			function_call_events_[f] = event_info;
//			cv_message("Registering function call event for %s (%x)",
//					event_info.function_name, f);
//		} else {
//			cv_warning("Not registering function call event for %s",
//					event_info.function_name);
//		}
//	} else {
//		instruction_events_[event_info.opcode] = event_info;
//	}
//}
//
//CliverEventInfo* CVExecutor::lookup_event(llvm::Instruction *i) {
//	if (!function_call_events_.empty()) {
//		if (i->getOpcode() == llvm::Instruction::Call) {
//			llvm::CallSite cs(llvm::cast<llvm::CallInst>(i));
//			llvm::Function *f = cs.getCalledFunction();
//			if (!f) {
//				// special case the call with a bitcast case
//				llvm::Value *fp = cs.getCalledValue();
//				llvm::ConstantExpr *ce = llvm::dyn_cast<llvm::ConstantExpr>(fp);
//				if (ce && ce->getOpcode()==llvm::Instruction::BitCast) {
//					f = dyn_cast<llvm::Function>(ce->getOperand(0));
//				}
//			}
//			if (function_call_events_.find(f) != function_call_events_.end()) {
//				return &function_call_events_[f];
//			}
//		}
//	}
//	return NULL;
//}

void CVExecutor::add_state(CVExecutionState* state) {
	getContext().addedStates.push_back(state);
}

void CVExecutor::add_state_internal(CVExecutionState* state) {
  klee::LockGuard guard(statesMutex);
	states.insert(state);
}

// FIXME incorporate CanonicalSolver
void CVExecutor::rebuild_solvers() {

#if 0
  FIXME
  delete solver;
  klee::Solver* coreSolver = new klee::STPSolver(klee::UseForkedCoreSolver, klee::CoreSolverOptimizeDivides);

  klee::Solver *solver = 
    constructSolverChain(coreSolver,
                         interpreterHandler->getOutputFilename(klee::ALL_QUERIES_SMT2_FILE_NAME),
                         interpreterHandler->getOutputFilename(klee::SOLVER_QUERIES_SMT2_FILE_NAME),
                         interpreterHandler->getOutputFilename(klee::ALL_QUERIES_PC_FILE_NAME),
                         interpreterHandler->getOutputFilename(klee::SOLVER_QUERIES_PC_FILE_NAME));
  
  this->solver = new klee::TimingSolver(solver);
#endif
}

// Don't use mallinfo, overflows if usage is > 4GB
void CVExecutor::update_memory_usage() {
#ifdef TCMALLOC
  size_t bytes_used;
  MallocExtension::instance()->GetNumericProperty(
      "generic.current_allocated_bytes", &bytes_used);
  memory_usage_mbs_ = (bytes_used / (1024 * 1024) );
#else
	pid_t myPid = getpid();
	std::stringstream ss;
	ss << "/proc/" << myPid << "/status";

	FILE *fp = fopen(ss.str().c_str(), "r"); 
	if (!fp) { 
		return;
	}

	uint64_t peakMem=0;

	char buffer[512];
	while(!peakMem && fgets(buffer, sizeof(buffer), fp)) { 
		if (sscanf(buffer, "VmSize: %llu", (long long unsigned int*)&peakMem)) {
			break; 
		}
	}

	fclose(fp);

	memory_usage_mbs_ = (peakMem / 1024);
#endif
}

klee::KInstruction* CVExecutor::get_instruction(unsigned id) {
	if (kmodule->kinsts.find(id) != kmodule->kinsts.end()) {
		return kmodule->kinsts[id];
	}
	return NULL;
}

std::string CVExecutor::get_string_at_address(CVExecutionState* state, 
                                               klee::ref<klee::Expr> address_expr) {
    klee::ExecutionState* kstate = static_cast<klee::ExecutionState*>(state);
		return specialFunctionHandler->readStringAtAddress(*kstate, address_expr);
}

void CVExecutor::reset_replay_path(std::vector<bool>* replay_path) {
  this->replayPath = replay_path;
  this->replayPosition = 0;
}

void CVExecutor::add_finished_state(CVExecutionState* state) {
  assert(finished_states_.count(state->property()) == 0);
  finished_states_.insert(state->property());
}

void CVExecutor::ktest_copy(CVExecutionState* state,
                            klee::KInstruction *target,
                            std::string &name,
                            int ktest_index,
                            klee::ObjectState* os,
                            unsigned os_offset,
                            unsigned len) {
  KTest* replay_objs = cv_->get_replay_objs();
  KTestObject* ktest_obj = NULL;

  // HACK: Negative index values lookup the ith "name" ktest object
  if (ktest_index < 0) {
    int tmp_index = ktest_index;
    if (replay_objs && replay_objs->numObjects > 0) {
      for (unsigned i=0; i<replay_objs->numObjects; ++i) {
        KTestObject* kto = NULL;
        kto = &(replay_objs->objects[i]);
        if (std::string(kto->name) == name) {
          tmp_index++;
          if (tmp_index == 0) {
            ktest_obj = kto;
            break;
          }
        }
      }
    }
  }

  if (!ktest_obj && replay_objs && replay_objs->numObjects > ktest_index) {
    ktest_obj = &(replay_objs->objects[ktest_index]);
  }

  if (ktest_obj &&
      std::string(ktest_obj->name) == name &&
      ktest_obj->numBytes <= len) {

    // Enforce rule that a stdin event can only be processed in a round that
    // ends in a SEND event
    // In bssl, 0-len stdin is followed by a shutdown, so there is no more recorded
    // network activity--hence exception for 0length stdin read.
    if (name == "stdin" && ktest_obj->numBytes > 0 &&
        state->network_manager()->socket()->event().type == SocketEvent::RECV ) {
      CVDEBUG("Early stdin read, terminating state.");
      terminate_state(state);
      bindLocal(target, *state,
                klee::ConstantExpr::alloc(0,
                                          klee::Expr::Int32));
    }

    for (unsigned i=0; i<ktest_obj->numBytes; i++) {
      os->write8(os_offset+i, ktest_obj->bytes[i]);
    }

    CVDEBUG("ktest_copy logname: " <<
              ktest_obj->name << ", argname: " <<
              name << ", index: " << ktest_index << ", loglen: " <<
              ktest_obj->numBytes << ", arglen: " << len << ", offset: " <<
              os_offset << " State: " << *state );

    bindLocal(target, *state,
              klee::ConstantExpr::alloc(ktest_obj->numBytes,
                                        klee::Expr::Int32));
  } else {
    if (replay_objs && ktest_obj) {
      CVDEBUG("ktest_copy failed: logname: " <<
                ktest_obj->name << ", argname: " <<
                name << ", index: " << ktest_index << ", loglen: " <<
                ktest_obj->numBytes << ", arglen: " << len
                << ", State: " << *state);
    } else {
      CVDEBUG("ktest_copy failed: null ktest obj, argname: " <<
                name << ", index: " << ktest_index << ",  arglen: " << len
                << ", State: " << *state);
    }
    terminate_state(state);
  }
}

// Predict size of stdin read based on the size of the next
// client-to-server TLS record, assuming the negotiated symmetric
// ciphersuite is AES128-GCM.
//
// Case 1: (OpenSSL and BoringSSL) The next c2s TLS application data
//         record [byte#1 = 23] is 29 bytes longer than stdin.
//
// Case 2: (OpenSSL only) The stdin read is 0, i.e., Control-D on a
//         blank line, thereby closing the connection.  In this case,
//         the subsequent c2s TLS alert record [byte#1 = 21] has
//         length 31.
//
// Case 3: (BoringSSL only) The stdin read is 0, i.e., Control-D on a
//         blank line, thereby closing the connection.  There is no
//         subsequent c2s TLS alert record in this case; instead we
//         simply see the connection close.
//
// Any other situation terminates the state (stdin read disallowed).
//
// The ultimate read() call attempts to read up to 'count' bytes from
// a given file descriptor. If for some reason the stdin length
// prediction exceeds 'count', do not return a number greater than
// 'count'.
void CVExecutor::tls_predict_stdin_size(CVExecutionState *state,
                                        klee::KInstruction *target,
                                        size_t count) {
  const uint8_t TLS_ALERT = 21;
  const uint8_t TLS_APPDATA = 23;
  size_t stdin_len;
  Socket *socket = state->network_manager()->socket();

  if (socket->end_of_log()) { // Case 3

    stdin_len = 0;

  } else if (socket->event().type == SocketEvent::SEND &&
             socket->event().data[0] == TLS_ALERT &&
             socket->event().data.size() == 31) { // Case 2

    stdin_len = 0;

  } else if (socket->event().type == SocketEvent::SEND &&
             socket->event().data[0] == TLS_APPDATA &&
             socket->event().data.size() > 29) { // Case 1

    stdin_len = socket->event().data.size() - 29;

  } else {
    CVDEBUG(
        "Terminating state: stdin disallowed in current TLS network state.");
    stdin_len = 0;
    terminate_state(state);
  }

  if (stdin_len > count) {
    // Is this sufficient grounds for terminating the state?
    CVMESSAGE("tls_predict_stdin_size predicts stdin_len = "
              << stdin_len << ", but read() max count = " << count);
    stdin_len = count;
  }

  // write return value (stdin length) into bitcode
  bindLocal(target, *state,
            klee::ConstantExpr::alloc(stdin_len, klee::Expr::Int32));
}

// Write the 48-byte TLS master secret into the designated buffer
// location.  If no master secret file has been designated, this will
// silently return failure, in which case the caller must try to
// obtain the master secret some other way (e.g., binary ktest file).
void CVExecutor::tls_master_secret(CVExecutionState *state,
                                   klee::KInstruction *target,
                                   klee::ObjectState *os, unsigned os_offset) {

  uint8_t master_secret[TLS_MASTER_SECRET_SIZE];
  bool master_secret_obtained = false;
  unsigned ret = 0; // failure

  // Read master secret from file (or in-memory cache)
  master_secret_obtained = cv_->load_tls_master_secret(master_secret);
  if (master_secret_obtained) {
    // Master secret obtained. Write to the designated location if
    // there is enough space there.
    if (os && os_offset + TLS_MASTER_SECRET_SIZE <= os->size) {
      for (size_t i = 0; i < TLS_MASTER_SECRET_SIZE; i++) {
        os->write8(os_offset + i, master_secret[i]);
      }
      ret = 1; // success
    } else {
      cv_warning(
          "Terminating state: TLS master secret too big for target buffer.");
      terminate_state(state);
    }
  }

  // Return success (1) or failure (0)
  bindLocal(target, *state, klee::ConstantExpr::alloc(ret, klee::Expr::Int32));
}

void CVExecutor::executeEvent(klee::ExecutionState &state, unsigned int type,
                            long int value) {
  CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);
  if (type == KLEE_EVENT_SYMBOLIC_MODEL) {
    CVMESSAGE("SYMBOLIC MODEL EVENT");
    cvstate->property()->symbolic_model = true;
  }
}

///

} // end namespace cliver

