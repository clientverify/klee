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
#include "CVCommon.h"

#include "../Core/Common.h"
#include "../Core/Context.h"
#include "../Core/CoreStats.h"
#include "../Core/ExternalDispatcher.h"
#include "../Core/ImpliedValue.h"
#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"
#include "../Core/PTree.h"
#include "../Core/SeedInfo.h"
#include "../Core/StatsTracker.h"
#include "../Core/TimingSolver.h"
#include "../Core/UserSearcher.h"
#include "../Core/Searcher.h"
#include "../Core/SpecialFunctionHandler.h"
#include "../Solver/SolverStats.h"

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
NoXWindows("no-xwindows", 
           llvm::cl::desc("Do not allow external XWindows function calls"), 
           llvm::cl::init(false));
 
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

void CVExecutor::executeCall(klee::ExecutionState &state, 
                             klee::KInstruction *ki,
                             llvm::Function *f,
                             std::vector< klee::ref<klee::Expr> > &arguments) {
  if (PrintFunctionCalls) {
    CVMESSAGE(std::setw(2) << std::right << klee::GetThreadID() << " "
              << std::string(state.stack.size(), '-') << f->getName().str());
  }

  std::string XWidgetStr("Widget_");
  if (NoXWindows && f->getName().substr(0,XWidgetStr.size()) == XWidgetStr) {
    CVMESSAGE("Skipping function call to " << f->getName().str());
    return;
  }

  // Call base Executor implementation
  Executor::executeCall(state, ki, f, arguments);
}

void CVExecutor::callExternalFunction(klee::ExecutionState &state,
                                      klee::KInstruction *target,
                                      llvm::Function *function,
                                      std::vector< klee::ref<klee::Expr> > &arguments) {
  
  if (NoXWindows && function->getName()[0] == 'X') { 
    if (function->getName().str() == "XParseGeometry" ||
        function->getName().str() == "XStringToKeysym") {
      CVMESSAGE("Calling X function: " << function->getName().str());
    } else {
      CVMESSAGE("Skipping function call to " << function->getName().str());
      return;
    }
  }

  // Call base Executor implementation
  Executor::callExternalFunction(state, target, function, arguments);
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
        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++)
          os->write8(j, s[j]);

        // Write pointer to newly allocated and initialised argv/envp c-string
        argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
      }
    }
  }
  
  initializeGlobals(*state);

  processTree = new PTree(state);
  state->ptreeNode = processTree->root;
  run(*state);
  delete processTree;
  processTree = 0;

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
  call_once(searcher_init_flag_,
            [](klee::Searcher *s, klee::ExecutionState *e) {
              std::set<klee::ExecutionState*> sset;
              sset.insert(e);
              s->update(0, sset, std::set<klee::ExecutionState*>()); },
            searcher, initialState);

  // Now all states can execute
  threadBarrier->wait();

  klee::ExecutionState *statePtr = NULL;

  threadInstCount.reset(new unsigned());
  *threadInstCount = 0;

  unsigned instCountBeforeUpdate = 0;
  while (!empty() && !haltExecution) {


    if (statePtr == NULL) {
      statePtr = searcher->trySelectState();
    }

    if (statePtr != NULL && !haltExecution) {
      klee::ExecutionState &state = *statePtr;

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
      if (getContext().removedStates.find(&state) == getContext().removedStates.end()) {
        handle_post_execution_events(state);
      }

      // Handle post execution events for each newly added state
      foreach (klee::ExecutionState* astate, getContext().addedStates) {
        handle_post_execution_events(*astate);
      }

      // Notify all if a state was removed
      foreach (klee::ExecutionState* rstate, getContext().removedStates) {
        cv_->notify_all(ExecutionEvent(CV_STATE_REMOVED, rstate));
      }

      // Update searcher with new states and get next state to execute
      if (static_cast<CVExecutionState*>(&state)->event_flag()
          || !getContext().removedStates.empty()
          || !getContext().addedStates.empty()) {
        statePtr = searcher->updateAndTrySelectState(&state,
                                                     getContext().addedStates,
                                                     getContext().removedStates);
        // Update Executor state tracking
        parallelUpdateStates(&state);
      }
    }

    while (pauseExecution
           || (!statePtr && searcher->empty() && !empty() && !haltExecution)) {

      if (pauseExecution && statePtr) {
        searcher->update(statePtr,
                         std::set<klee::ExecutionState*>(),
                         std::set<klee::ExecutionState*>());
        statePtr = NULL;
      }

      if (klee::UseThreads >= 1) {
        klee::UniqueLock searcherCondGuard(searcherCondLock);
        if (!pauseExecution) {
          searcherCond.wait(searcherCondGuard);
        }
      }

      if (pauseExecution) {
        threadBarrier->wait();
        threadBarrier->wait();
      }
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
              searcher->update(statePtr, std::set<klee::ExecutionState*>(),
                               std::set<klee::ExecutionState*>());
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

  // Release TSS memory (i.e., don't destroy with thread); the memory manager
  // for this thread may still be needed in dumpState
  this->memory.release();

  // Print per-thread stats
  klee::LockGuard initializationLockGuard(initializationLock);
  CVMESSAGE("Thread: " << klee::GetThreadID() << " executed " << *threadInstCount << " instructions");
}

#if 1
void CVExecutor::run(klee::ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  states.insert(&initialState);
  ++stateCount;

	searcher = cv_->searcher();

  if (!searcher) {
    klee::klee_error("failed to create searcher");
  }

  threadBarrier = new klee::Barrier(klee::UseThreads);

  totalThreadCount = klee::UseThreads;
  if (!empty()) {
    if (klee::UseThreads <= 1) {
      // Execute state in this thread
      execute(&initialState, NULL);
    } else {
      klee::ThreadGroup threadGroup;
      for (unsigned i=0; i<klee::UseThreads; ++i) {
        // Initialize MemoryManager outside of thread
        memoryManagers.push_back(new klee::MemoryManager());
        threadGroup.add_thread(new klee::Thread(&cliver::CVExecutor::execute,
                                          this, &initialState, memoryManagers.back()));
      }

      // Wait for all threads to finish
      threadGroup.join_all();
    }
  }
  totalThreadCount = 1;

 dump:
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
  unsigned basic_block_id = kf->basicBlockEntry[basic_block];
  if (ki == kf->instructions[basic_block_id]) {
    cv_->notify_all(ExecutionEvent(CV_BASICBLOCK_ENTRY, &state));
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

  if (klee::stats::instructions==klee::StopAfterNInstructions)
    haltExecution = true;
}

void CVExecutor::executeMakeSymbolic(klee::ExecutionState &state, 
                                     const klee::MemoryObject *mo,
                                     const std::string &name) {

  assert(!replayOut && "replayOut not supported by cliver");

  CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);

  // Create a new object state for the memory object (instead of a copy).
  std::string array_name = cvstate->get_unique_array_name(name);

  const klee::Array *array
    = cvstate->multi_pass_assignment().getArray(array_name);

  bool multipass = false;
  if (array != NULL) {
    CVDEBUG("Multi-pass: Concretization found for " << array_name);
    multipass = true;
  } else {
    CVDEBUG("Multi-pass: Concretization not found for " << array_name);
    array = new klee::Array(array_name, mo->size);
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

    if (!replayPath)
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_TRUE, &current));

    return StatePair(&current, 0);

  } else if (res == klee::Solver::False) {

    if (!replayPath)
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_FALSE, &current));

    return StatePair(0, &current);
  } else {

    klee::ExecutionState *falseState, *trueState = &current;

    ++klee::stats::forks;

    falseState = trueState->branch();
    getContext().addedStates.insert(falseState);

    addConstraint(*trueState, condition);
    addConstraint(*falseState, klee::Expr::createIsZero(condition));

    if (!replayPath) {
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_FALSE, falseState));
      cv_->notify_all(ExecutionEvent(CV_STATE_FORK_TRUE, trueState));
    }

    return StatePair(trueState, falseState);
  }
}

void CVExecutor::branch(klee::ExecutionState &state, 
		const std::vector< klee::ref<klee::Expr> > &conditions,
    std::vector<klee::ExecutionState*> &result) {

  unsigned N = conditions.size();
  assert(N);

	klee::stats::forks += N-1;

  // Cliver assumes each state branches from a single other state
  assert(N <= 1);

  result.push_back(&state);
  for (unsigned i=1; i<N; ++i) {
		klee::ExecutionState *es = result[klee::theRNG.getInt32() % i];
		klee::ExecutionState *ns = es->branch();
    getContext().addedStates.insert(ns);
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
	getContext().addedStates.insert(state);
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

