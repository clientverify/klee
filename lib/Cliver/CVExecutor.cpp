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
#include "CVMemoryManager.h"
#include "CVStream.h"
#include "CVSearcher.h"
#include "NetworkManager.h"
#include "StateMerger.h"
#include "ConstraintPruner.h"

//#include "../Core/ExternalDispatcher.h"
//#include "../Core/SpecialFunctionHandler.h"
//#include "../Core/TimingSolver.h"

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

}

namespace cliver {

CVExecutor::CVExecutor(const InterpreterOptions &opts, klee::InterpreterHandler *ih)
: klee::Executor(opts, ih), cv_(static_cast<ClientVerifier*>(ih)) {
	memory = new CVMemoryManager();

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

const llvm::Module *CVExecutor::setModule(llvm::Module *module, 
                                  const ModuleOptions &opts) {
  assert(!kmodule && module && "can only register one module"); // XXX gross
  
  kmodule = new klee::KModule(module);

  // Initialize the context.
	llvm::TargetData *TD = kmodule->targetData;
	klee::Context::initialize(TD->isLittleEndian(),
                      (klee::Expr::Width) TD->getPointerSizeInBits());

  specialFunctionHandler = new klee::SpecialFunctionHandler(*this);

  specialFunctionHandler->prepare();
  kmodule->prepare(opts, interpreterHandler);
  specialFunctionHandler->bind();

	cv_->initialize_external_handlers(this);

  return module;
}


void CVExecutor::runFunctionAsMain(llvm::Function *f,
				                   int argc, char **argv, char **envp) {
  using namespace klee;
  std::vector< ref<Expr> > arguments;

	// force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

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

  processTree = new PTree(state);
  state->ptreeNode = processTree->root;
  run(*state);
  delete processTree;
  processTree = 0;

  // hack to clear memory objects
  delete memory;
  memory = new CVMemoryManager();
  
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

	searcher = new klee::DFSSearcher();
	pruner_ = new ConstraintPruner();
	merger_ = new StateMerger(pruner_);
	searcher = new CVSearcher(searcher, merger_);
  //searcher = constructUserSearcher(*this);

  searcher->update(0, states, std::set<klee::ExecutionState*>());

  while (!states.empty() && !haltExecution) {
		klee::ExecutionState &state = searcher->selectState();
		klee::KInstruction *ki = state.pc;
    stepInstruction(state);

    executeInstruction(state, ki);
    processTimers(&state, klee::MaxInstructionTime);

    if (klee::MaxMemory) {
      if ((klee::stats::instructions & 0xFFFF) == 0) {
        // We need to avoid calling GetMallocUsage() often because it
        // is O(elts on freelist). This is really bad since we start
        // to pummel the freelist once we hit the memory cap.
        unsigned mbs = llvm::sys::Process::GetTotalMemoryUsage() >> 20;
        
        if (mbs > klee::MaxMemory) {
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

void CVExecutor::executeMakeSymbolic(klee::ExecutionState &state, 
                                     const klee::MemoryObject *mo) {
  using namespace klee;

  // Create a new object state for the memory object (instead of a copy).
	CVExecutionState* cv_state = static_cast<CVExecutionState*>(&state);
	unsigned id = cv_state->get_symbolic_name_id(mo->name);

  const Array *array = new Array(mo->name + llvm::utostr(id), mo->size);
  bindObjectInState(state, mo, false, array);
  state.addSymbolic(mo, array);

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

} // end namespace cliver

