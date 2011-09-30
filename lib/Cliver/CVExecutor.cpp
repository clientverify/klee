//===-- CVExecutor.cpp -====-------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "CVExecutionState.h"
#include "CVExecutor.h"
#include "CVCommon.h"
#include "CVSearcher.h"
#include "NetworkManager.h"
#include "PathManager.h"
#include "StateMerger.h"
#include "ConstraintPruner.h"

#include "../Core/Common.h"
#include "../Core/Executor.h"
#include "../Core/Context.h"
#include "../Core/CoreStats.h"
#include "../Core/ExternalDispatcher.h"
#include "../Core/ImpliedValue.h"
#include "../Core/Memory.h"
#include "../Core/MemoryManager.h"
#include "../Core/PTree.h"
#include "../Core/Searcher.h"
#include "../Core/SeedInfo.h"
#include "../Core/SpecialFunctionHandler.h"
#include "../Core/StatsTracker.h"
#include "../Core/TimingSolver.h"
#include "../Core/UserSearcher.h"

#include "../Solver/SolverStats.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Config/config.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Attributes.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Process.h"
#include "llvm/Target/TargetData.h"

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

namespace klee {
  // Command line options defined in lib/Core/Executor.cpp
  extern llvm::cl::opt<bool> DumpStatesOnHalt;
  extern llvm::cl::opt<bool> NoPreferCex;
  extern llvm::cl::opt<bool> UseAsmAddresses;
  extern llvm::cl::opt<bool> RandomizeFork;
  extern llvm::cl::opt<bool> AllowExternalSymCalls;
  extern llvm::cl::opt<bool> DebugPrintInstructions;
  extern llvm::cl::opt<bool> DebugCheckForImpliedValues;
  extern llvm::cl::opt<bool> SimplifySymIndices;
  extern llvm::cl::opt<unsigned> MaxSymArraySize;
  extern llvm::cl::opt<bool> DebugValidateSolver;
  extern llvm::cl::opt<bool> SuppressExternalWarnings;
  extern llvm::cl::opt<bool> AllExternalWarnings;
  extern llvm::cl::opt<bool> OnlyOutputStatesCoveringNew;
  extern llvm::cl::opt<bool> AlwaysOutputSeeds;
  extern llvm::cl::opt<bool> UseFastCexSolver;
  extern llvm::cl::opt<bool> UseIndependentSolver;
  extern llvm::cl::opt<bool> EmitAllErrors;
  extern llvm::cl::opt<bool> UseCexCache;
  extern llvm::cl::opt<bool> UseQueryPCLog;
  extern llvm::cl::opt<bool> UseSTPQueryPCLog;
  extern llvm::cl::opt<bool> NoExternals;
  extern llvm::cl::opt<bool> UseCache;
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
  extern llvm::cl::opt<bool> UseForkedSTP;
  extern llvm::cl::opt<bool> STPOptimizeDivides;

  // Command line options defined in lib/Core/Searcher.cpp
  extern llvm::cl::opt<bool> UseRandomSearch;
  extern llvm::cl::opt<bool> UseInterleavedRS;
  extern llvm::cl::opt<bool> UseInterleavedNURS;
  extern llvm::cl::opt<bool> UseInterleavedMD2UNURS;
  extern llvm::cl::opt<bool> UseInterleavedInstCountNURS;
  extern llvm::cl::opt<bool> UseInterleavedCPInstCountNURS;
  extern llvm::cl::opt<bool> UseInterleavedQueryCostNURS;
  extern llvm::cl::opt<bool> UseInterleavedCovNewNURS;
  extern llvm::cl::opt<bool> UseNonUniformRandomSearch;
  extern llvm::cl::opt<bool> UseRandomPathSearch;
	extern llvm::cl::opt<bool> UseMerge;
  extern llvm::cl::opt<bool> UseBumpMerge;
  extern llvm::cl::opt<bool> UseIterativeDeepeningTimeSearch;
  extern llvm::cl::opt<bool> UseBatchingSearch;

	extern RNG theRNG;
}

cliver::CVExecutor *g_executor = 0;

namespace cliver {

CVExecutor::CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ih)
: klee::Executor(opts, ih), cv_(static_cast<ClientVerifier*>(ih)) {

	// Check for incompatible or non-supported klee options.
#define INVALID_CL_OPT(name, val) \
	if ((int)name != (int)val) cv_error("Unsupported command line option: %s", #name);
	using namespace klee;
	INVALID_CL_OPT(ZeroSeedExtension,false);
	INVALID_CL_OPT(AllowSeedExtension,false);
	INVALID_CL_OPT(AlwaysOutputSeeds,true);
	INVALID_CL_OPT(OnlyReplaySeeds,false);
	INVALID_CL_OPT(OnlySeed,false);
	INVALID_CL_OPT(NamedSeedMatching,false);
	INVALID_CL_OPT(RandomizeFork,false);
	INVALID_CL_OPT(MaxDepth,false);
	INVALID_CL_OPT(UseRandomSearch,false);
	INVALID_CL_OPT(UseInterleavedRS,false);
	INVALID_CL_OPT(UseInterleavedNURS,false);
	INVALID_CL_OPT(UseInterleavedMD2UNURS,false);
	INVALID_CL_OPT(UseInterleavedInstCountNURS,false);
	INVALID_CL_OPT(UseInterleavedCPInstCountNURS,false);
	INVALID_CL_OPT(UseInterleavedQueryCostNURS,false);
	INVALID_CL_OPT(UseInterleavedCovNewNURS,false);
	INVALID_CL_OPT(UseNonUniformRandomSearch,false);
	INVALID_CL_OPT(UseRandomPathSearch,false);
	INVALID_CL_OPT(UseMerge,false);
	INVALID_CL_OPT(UseBumpMerge,false);
	INVALID_CL_OPT(UseIterativeDeepeningTimeSearch,false);
	INVALID_CL_OPT(UseBatchingSearch,false);
#undef INVALID_CL_OPT
}


CVExecutor::~CVExecutor() {}

void CVExecutor::runFunctionAsMain(llvm::Function *f,
				                   int argc, char **argv, char **envp) {
  using namespace klee;
  std::vector< ref<Expr> > arguments;

	// force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

	// Only difference from klee::Executor::runFunctionAsMain()
  CVExecutionState *state = new CVExecutionState(kmodule->functionMap[f], memory);
	state->initialize(this);
  
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
      argvMO = memory->allocate(*state, (argc+1+envc+1+1) * NumPtrBytes, 
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
        
        arg = memory->allocate(*state,len+1, false, true, state->pc->inst);

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

  // hack to clear memory objects
  delete memory;
  memory = new klee::MemoryManager();
  
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

void CVExecutor::run(klee::ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();

  states.insert(&initialState);

	searcher = cv_->searcher();

  searcher->update(0, states, std::set<klee::ExecutionState*>());

  while (!states.empty() && !haltExecution) {
		klee::ExecutionState &state = searcher->selectState();
		klee::KInstruction *ki = state.pc;

		// Handle pre execution events
		if (CliverEventInfo* ei = lookup_event(ki->inst)) {
			//cv_message("Function call pre event for %s",ei->function_name);
			cv_->pre_event(static_cast<CVExecutionState*>(&state), this, ei->type);
		}

    stepInstruction(state);

    executeInstruction(state, ki);
    processTimers(&state, klee::MaxInstructionTime);

    if (klee::MaxMemory) {
      if ((klee::stats::instructions & 0xFFFF) == 0) {
        // We need to avoid calling GetMallocUsage() often because it
        // is O(elts on freelist). This is really bad since we start
        // to pummel the freelist once we hit the memory cap.
        //unsigned mbs = llvm::sys::Process::GetTotalMemoryUsage() >> 20;
        unsigned mbs = check_memory_usage();
        
        if (mbs > klee::MaxMemory) {
					cv_message("Using %d MB of memory (limit is %d MB). Exiting.", 
							mbs, (unsigned)klee::MaxMemory);
					goto dump;
					
          if (mbs > klee::MaxMemory + 100) {
            // just guess at how many to kill
            unsigned numStates = states.size();
            unsigned toKill = std::max(1U, numStates - numStates*klee::MaxMemory/mbs);

            if (klee::MaxMemoryInhibit)
              cv_warning("killing %d states (over memory cap)",
                           toKill);

            std::vector<klee::ExecutionState*> arr(states.begin(), states.end());
            for (unsigned i=0,N=arr.size(); N && i<toKill; ++i,--N) {
              unsigned idx = rand() % N;

              // Make two pulls to try and not hit a state that
              // covered new code.
              if (arr[idx]->coveredNew)
                idx = rand() % N;

              std::swap(arr[idx], arr[N-1]);
              terminateStateEarly(*arr[N-1], "memory limit");
            }
          }
          atMemoryLimit = true;
        } else {
          atMemoryLimit = false;
        }
      }
    }

		// Handle post execution events
		if (removedStates.find(&state) == removedStates.end()) {
			// Don't create event if state was terminated
			assert(ki == state.prevPC && "instruction mismatch");
			if (CliverEventInfo* ei = lookup_event(ki->inst)) {
				//cv_message("Function call post event for %s",ei->function_name);
				cv_->post_event(static_cast<CVExecutionState*>(&state), this, ei->type);
			}
		}

    updateStates(&state);
  }

  delete searcher;
  searcher = 0;
  
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
    updateStates(0);
  }
}

void CVExecutor::stepInstruction(klee::ExecutionState &state) {

	if (klee::DebugPrintInstructions) {
		CVExecutionState *cvstate = static_cast<CVExecutionState*>(&state);
		std::string rstr;
		llvm::raw_string_ostream ros(rstr);
		ros << *(state.pc->inst);
		ros.flush();
		rstr.erase(std::remove(rstr.begin(), rstr.end(), '\n'), rstr.end());
    *cv_debug_stream << "sid: " << cvstate->id() 
			<< " " << std::setw(10) << state.pc->info->id << " : " << rstr << "\n";
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
                                     const klee::MemoryObject *mo) {
  // Create a new object state for the memory object (instead of a copy).
	unsigned id = cv_->next_array_id();
  const klee::Array *array 
		= new klee::Array(mo->name + llvm::utostr(id), mo->size);

  bindObjectInState(state, mo, false, array);
  state.addSymbolic(mo, array);
}

void CVExecutor::updateStates(klee::ExecutionState *current) {
  if (searcher) {
    searcher->update(current, addedStates, removedStates);
  }
  
  states.insert(addedStates.begin(), addedStates.end());
  addedStates.clear();
  
  for (std::set<klee::ExecutionState*>::iterator
         it = removedStates.begin(), ie = removedStates.end();
       it != ie; ++it) {
		klee::ExecutionState *es = *it;
    std::set<klee::ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);
    delete es;
  }
  removedStates.clear();
}

void CVExecutor::branch(klee::ExecutionState &state, 
		const std::vector< klee::ref<klee::Expr> > &conditions,
    std::vector<klee::ExecutionState*> &result) {

	klee::TimerStatIncrementer timer(klee::stats::forkTime);
  unsigned N = conditions.size();
  assert(N);

	klee::stats::forks += N-1;

  // XXX do proper balance or keep random?
  result.push_back(&state);
  for (unsigned i=1; i<N; ++i) {
		klee::ExecutionState *es = result[klee::theRNG.getInt32() % i];
		klee::ExecutionState *ns = es->branch();
    addedStates.insert(ns);
    result.push_back(ns);
    es->ptreeNode->data = 0;
    std::pair<klee::PTree::Node*,klee::PTree::Node*> res = 
      processTree->split(es->ptreeNode, ns, es);
    ns->ptreeNode = res.first;
    es->ptreeNode = res.second;
  }

  for (unsigned i=0; i<N; ++i)
    if (result[i])
      addConstraint(*result[i], conditions[i]);
}

klee::Executor::StatePair CVExecutor::fork(klee::ExecutionState &current, 
			klee::ref<klee::Expr> condition, bool isInternal) {
	klee::Solver::Validity res;
	PathManager *path_manager 
		= static_cast<CVExecutionState*>(&current)->path_manager();

	double timeout = stpTimeout;
	solver->setTimeout(timeout);
	bool success = solver->evaluate(current, condition, res);
	solver->setTimeout(0);
	if (!success) {
		current.pc = current.prevPC;
		terminateStateEarly(current, "query timed out");
		return klee::Executor::StatePair(0, 0);
	}

	if (isInternal) {
		if (res==klee::Solver::True) {
			return klee::Executor::StatePair(&current, 0);
		} else if (res==klee::Solver::False) {
			return klee::Executor::StatePair(0, &current);
		} else {
			klee::ExecutionState *falseState = NULL, *trueState = &current;
			falseState = trueState->branch();
			addConstraint(*trueState, condition);
			addConstraint(*falseState, klee::Expr::createIsZero(condition));
			addedStates.insert(falseState);
			return klee::Executor::StatePair(trueState, falseState);
		}
	} else {
		if (res==klee::Solver::True) {
			if (path_manager->try_branch(true, res, current.prevPC)) {
				if (pathWriter) {
					current.pathOS << "1";
				}
				path_manager->commit_branch(true, res, current.prevPC);
				return klee::Executor::StatePair(&current, 0);
			} else {
				terminateState(current);
				return klee::Executor::StatePair(0, 0);
			}

		} else if (res==klee::Solver::False) {
			if (path_manager->try_branch(false, res, current.prevPC)) {
				if (pathWriter) {
					current.pathOS << "0";
				}
				path_manager->commit_branch(false, res, current.prevPC);
				return klee::Executor::StatePair(0, &current);
			} else {
				terminateState(current);
				return klee::Executor::StatePair(0, 0);
			}

		} else { // res==klee::Solver::Unknown
			klee::TimerStatIncrementer timer(klee::stats::forkTime);
			klee::ExecutionState *falseState = NULL, *trueState = &current;

			++klee::stats::forks;

			if (path_manager->try_branch(false, res, current.prevPC)) {
				falseState = trueState->branch();
				if (pathWriter) {
					falseState->pathOS = pathWriter->open(current.pathOS);
					falseState->pathOS << "0";
				}   
				if (symPathWriter) {
					falseState->symPathOS = symPathWriter->open(current.symPathOS);
					falseState->symPathOS << "0";
				}

				addConstraint(*falseState, klee::Expr::createIsZero(condition));
				addedStates.insert(falseState);

				PathManager *false_path_manager 
					= static_cast<CVExecutionState*>(falseState)->path_manager();
				false_path_manager->commit_branch(false, res, current.prevPC);
			}

			if (path_manager->try_branch(true, res, current.prevPC)) {
				if (pathWriter) {
					trueState->pathOS << "1";
				}      
				if (symPathWriter) {
					trueState->symPathOS << "1";
				}

				addConstraint(*trueState, condition);
				path_manager->commit_branch(false, res, current.prevPC);
			} else {
				terminateState(*trueState);
				trueState = NULL;
			}

			return klee::Executor::StatePair(trueState, falseState);
		}
	}
}

void CVExecutor::terminateState(klee::ExecutionState &state) {

  cv_->incPathsExplored();

  std::set<klee::ExecutionState*>::iterator it 
		= addedStates.find(&state);

  if (it==addedStates.end()) {
    state.pc = state.prevPC;

    removedStates.insert(&state);

  } else {

    addedStates.erase(it);
    //processTree->remove(state.ptreeNode);
    delete &state;

  }
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
		klee::ref<klee::Expr> address_expr, klee::ObjectPair &result) {
	
	klee::Executor::ExactResolutionList rl;
  resolveExact(*state, address_expr, rl, "CVExecutor::resolve_one");
	assert(rl.size() == 1);
	//assert(rl[0].second == state);
	result.first = rl[0].first.first;
	result.second = rl[0].first.second;
}

void CVExecutor::terminate_state(CVExecutionState* state) {
	terminateState(*state);
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

void CVExecutor::add_constraint(CVExecutionState *state, 
		klee::ref<klee::Expr> condition) {
	addConstraint(*state, condition);
}

void CVExecutor::register_event(const CliverEventInfo& event_info) {
	if (event_info.opcode == llvm::Instruction::Call) {
		llvm::Function* f = kmodule->module->getFunction(event_info.function_name);
		if (f) {
			function_call_events_[f] = event_info;
			cv_message("Registering function call event for %s (%x)",
					event_info.function_name, f);
		} else {
			cv_warning("Not registering function call event for %s",
					event_info.function_name);
		}
	} else {
		instruction_events_[event_info.opcode] = event_info;
	}
}

CliverEventInfo* CVExecutor::lookup_event(llvm::Instruction *i) {
	if (!function_call_events_.empty()) {
		if (i->getOpcode() == llvm::Instruction::Call) {
			llvm::CallSite cs(llvm::cast<llvm::CallInst>(i));
			llvm::Function *f = cs.getCalledFunction();
			if (!f) {
				// special case the call with a bitcast case
				llvm::Value *fp = cs.getCalledValue();
				llvm::ConstantExpr *ce = llvm::dyn_cast<llvm::ConstantExpr>(fp);
				if (ce && ce->getOpcode()==llvm::Instruction::BitCast) {
					f = dyn_cast<llvm::Function>(ce->getOperand(0));
				}
			}
			if (function_call_events_.find(f) != function_call_events_.end()) {
				return &function_call_events_[f];
			}
		}
	}
	return NULL;
}

void CVExecutor::add_state(CVExecutionState* state) {
	addedStates.insert(state);
}

void CVExecutor::rebuild_solvers() {
  delete solver;                                                                                                                                                                                            
  klee::STPSolver *stpSolver = new klee::STPSolver(false);                                                                                                                                                                       
  klee::Solver *new_solver = klee::createCexCachingSolver(stpSolver);                                                                                                                                                                
  new_solver = klee::createCachingSolver(new_solver);                                                                                                                                                                              
  new_solver = klee::createIndependentSolver(new_solver);                                                                                                                                                                          
  //klee::TimingSolver *timing_solver 
  //  = new klee::TimingSolver(solver, stpSolver);                                                                                                                                                             
  solver = new klee::TimingSolver(new_solver, stpSolver);                                                                                                                                                             
}

uint64_t CVExecutor::check_memory_usage() {
	pid_t myPid = getpid();
	std::stringstream ss;
	ss << "/proc/" << myPid << "/status";

	FILE *fp = fopen(ss.str().c_str(), "r"); 
	if (!fp) { 
		return 0;
	}

	uint64_t peakMem=0;

	char buffer[512];
	while(!peakMem && fgets(buffer, sizeof(buffer), fp)) { 
		if (sscanf(buffer, "VmSize: %llu", &peakMem)) {
			break; 
		}
	}

	fclose(fp);

	return peakMem / 1024; 
}

klee::KInstruction* CVExecutor::get_instruction(unsigned id) {
	if (kmodule->kinsts.find(id) != kmodule->kinsts.end()) {
		return kmodule->kinsts[id];
	}
	return NULL;
}

} // end namespace cliver

