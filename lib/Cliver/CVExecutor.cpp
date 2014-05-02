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
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Process.h"


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
  extern llvm::cl::opt<unsigned> MaxMemory;

  // Command line options defined in lib/Core/UserSearcher.cpp
  extern llvm::cl::opt<unsigned> UseThreads;
}

namespace cliver {

llvm::cl::opt<bool> 
EnableCliver("cliver", llvm::cl::desc("Enable cliver."), llvm::cl::init(false));

llvm::cl::opt<bool> 
DisableEnvironmentVariables("-disable-env-vars", 
                            llvm::cl::desc("Disable environment variables"), 
                            llvm::cl::init(true));

/// Give symbolic variables a name equal to the declared name + id
llvm::cl::opt<bool>
UseFullVariableNames("use-full-variable-names", llvm::cl::init(false));

llvm::cl::opt<bool>
DebugExecutor("debug-executor",llvm::cl::init(false));

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
  memory_usage_mbs_(0) {
  if (klee::UseThreads > 1) {
    cv_warning("Multi-threaded support is currently disabled");
    klee::UseThreads = 1;
  }
}

CVExecutor::~CVExecutor() {}

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

#if 0
void CVExecutor::runFunctionAsMain(llvm::Function *f,
				                   int argc, char **argv, char **envp) {
  std::vector< ref<Expr> > arguments;

  cv_->initialize();

	// force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

	// Only difference from klee::Executor::runFunctionAsMain()
  CVExecutionState *state = new CVExecutionState(kmodule->functionMap[f]);
	state->initialize(cv_);
  
  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  // Increment envc until envp[envc] == 0
  for (envc=0; envp[envc]; ++envc) ;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];
  assert(kf);
  llvm::Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {
    // XXX Should Expr::Int32 depend on arch?
    arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));

    if (++ai!=ae) {
      argvMO = memory->allocate((argc+1+envc+1+1) * NumPtrBytes, 
          false, true, f->begin()->begin());
      
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
      MemoryObject *arg;
      
      if (i==argc || i>=argc+1+envc) {
        arg = 0;
      } else {
        char *s = i<argc ? argv[i] : envp[i-(argc+1)];
        int j, len = strlen(s);
        
        arg = memory->allocate(len+1, false, true, state->pc->inst);

        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++)
          os->write8(j, s[j]);
      }

      if (arg) {
        argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
      } else {
        argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
      }
    }
  }
  
  initializeGlobals(*state);

  processTree = new klee::PTree(state);
  state->ptreeNode = processTree->root;
  run(*state);
  delete processTree;
  processTree = 0;

  //// hack to clear memory objects
  //delete memory;
  //memory = new klee::MemoryManager();
  
  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();

  // theMMap doesn't seem to be used anywhere
  //if (theMMap) {
  //  munmap(theMMap, theMMapSize);
  //  theMMap = 0;
  //}
}
#endif

void CVExecutor::run(klee::ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  states.insert(&initialState);

	searcher = cv_->searcher();

  std::set<klee::ExecutionState*> initial_state_set;
  initial_state_set.insert(&initialState);

  searcher->update(0, initial_state_set, std::set<klee::ExecutionState*>());

  klee::ExecutionState *prev_state = NULL;
  bool clear_caches = false;

  while (!searcher->empty() && !haltExecution) {
    cv_->set_execution_event_flag(false);

    if (clear_caches) {
      clear_caches = false;

      cv_message("Using %ld MB of memory (limit is %d MB). Clearing Caches\n", 
          memory_usage_mbs_, (unsigned)klee::MaxMemory);

      cv_->notify_all(ExecutionEvent(CV_CLEAR_CACHES));

      update_memory_usage();

      cv_message("Now using %ld MB of memory (limit is %d MB). \n", 
          memory_usage_mbs_, (unsigned)klee::MaxMemory);
    
      if (memory_usage_mbs_ > klee::MaxMemory) {
        goto dump;
      }
    }

    if (klee::MaxMemory) {
      // Only update the the memory usage here occasionally
      if ((klee::stats::instructions & 0xFFFFF) == 0) {
        update_memory_usage();
      }

      if (memory_usage_mbs_> ((double)klee::MaxMemory*(0.90))) {
        clear_caches = true;
      }
    }

    // Print usage stats during especially long rounds
    if ((klee::stats::instructions & 0xFFFFFF) == 0)
      cv_->print_current_statistics("UPDT");

    // Select the next state from the search if it was updated (prev_state is
    // null) or continue execution of the previous state
		klee::ExecutionState &state 
      = (prev_state ? *prev_state : searcher->selectState());

    prev_state = &state;

    if (haltExecution) goto dump;

    // XXX Not currently used
    //handle_pre_execution_events(state);

		klee::KInstruction *ki = state.pc;

    //cv_->notify_all(ExecutionEvent(CV_STEP_INSTRUCTION, &state));
    stepInstruction(state);
    executeInstruction(state, ki);
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

    foreach (klee::ExecutionState* rstate, getContext().removedStates) {
      cv_->notify_all(ExecutionEvent(CV_STATE_REMOVED, rstate));
    }

    // Update the searcher only if needed
    if (cv_->execution_event_flag() 
        || !getContext().removedStates.empty() 
        || !getContext().addedStates.empty()
        || clear_caches) {
      updateStates(&state);
      prev_state = NULL;
    }
  }

  if(searcher->empty())
    CVMESSAGE("No more states to search.");

 dump:
  if (klee::DumpStatesOnHalt && !states.empty()) {
    //std::cerr << "KLEE: halting execution, dumping remaining states\n";
		cv_warning("halting execution, dumping remaining states");
    for (std::set<klee::ExecutionState*>::iterator
           it = states.begin(), ie = states.end();
         it != ie; ++it) {
			klee::ExecutionState &state = **it;
      stepInstruction(state); // keep stats rolling
      terminateStateEarly(state, "execution halting");
    }
  }

  cv_->print_all_stats();
}

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

  // Create a new object state for the memory object (instead of a copy).
	uint64_t id = cv_->next_array_id();
  const klee::Array *array;

  if (UseFullVariableNames)
		array = new klee::Array(name + llvm::utostr(id), mo->size);
  else
		array = new klee::Array(std::string("mo") + llvm::utostr(id), mo->size);

  bindObjectInState(state, mo, false, array);
  state.addSymbolic(mo, array);

  CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);
  cvstate->property()->symbolic_vars++;
  CVDEBUG("Created symbolic: " << array->name << " in " << *cvstate);
}

void CVExecutor::updateStates(klee::ExecutionState *current) {
  if (searcher) {
    searcher->update(current, getContext().addedStates, getContext().removedStates);
  }
  
  states.insert(getContext().addedStates.begin(), getContext().addedStates.end());
  getContext().addedStates.clear();
  
  for (std::set<klee::ExecutionState*>::iterator
         it = getContext().removedStates.begin(), ie = getContext().removedStates.end();
       it != ie; ++it) {
		klee::ExecutionState *es = *it;
    std::set<klee::ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);
    delete es;
  }
  getContext().removedStates.clear();
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
	
	klee::Executor::ExactResolutionList rl;
  resolveExact(*state, address_expr, rl, "CVExecutor::resolve_one");
	assert(rl.size() == 1);
	//assert(rl[0].second == state);

  const klee::MemoryObject *mo = rl[0].first.first;
  const klee::ObjectState *os = rl[0].first.second;

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
  states.erase(state);
  delete state;
}

void CVExecutor::remove_state_internal_without_notify(CVExecutionState* state) {
  states.erase(state);
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
///

} // end namespace cliver

