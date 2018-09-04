//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

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


#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/CommandLine.h"
#include "klee/Common.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Config/Version.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/System/MemoryUsage.h"
#include "klee/SolverStats.h"

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
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif

#ifdef HAVE_ZLIB_H
#include "klee/Internal/Support/CompressionStream.h"
#endif

#include <cassert>
#include <algorithm>
#include <iomanip>
#include <iosfwd>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include <sys/mman.h>

#include <errno.h>
#include <cxxabi.h>

using namespace llvm;
using namespace klee;

//AH: Our additions below. --------------------------------------
#include <ucontext.h>
#include <iostream>

extern std::stringstream globalLogStream;
extern int worker2managerFD [2];
#include "klee/tase_constants.h"
enum runType : int {PURE_INTERP, TSX_NATIVE, VERIFICATION};
extern enum runType exec_mode;
extern void * StackBase;

extern char target_stack[STACK_SIZE +1];

extern Module * interpModule;
extern char * target_stack_begin_ptr;
extern char * interp_stack_begin_ptr;
extern uint32_t springboard_flags;
extern KTestObjectVector ktov;

extern "C" void * sb_entertran();
extern "C" void * sb_exittran();
extern "C" void * sb_reopen();
//AH: Todo -- see if we need to change prototype of enter_modeled
extern "C" void * sb_enter_modeled();
extern gregset_t target_ctx_gregs;
extern gregset_t prev_ctx_gregs;

MemoryObject * target_ctx_gregs_MO;
ObjectState * target_ctx_gregs_OS;
MemoryObject * prev_ctx_gregs_MO;
ObjectState * prev_ctx_gregs_OS;
bool hasForked = false;
//std::ofstream debugFile;


void printCtx(gregset_t ctx );


ExecutionState * GlobalExecutionStatePtr;
extern klee::Interpreter * GlobalInterpreter;
extern bool enableMultipass;
//Temp hack
//TO-DO:  Remove!!

//TODO Be careful, as BUFFER_SIZE is a macro we're defining twice now.
#define BUFFER_SIZE 256
extern char message_test_buffer[BUFFER_SIZE];
//TODO Test verification with automatic message_buf_length stuff now..
extern uint64_t message_buf_length;
uint64_t bytes_printed = 0;

extern int MESSAGE_COUNT;
extern int BASKET_SIZE;
char basket[BUFFER_SIZE];
ObjectState * basket_OS;
MemoryObject * basket_MO;

extern std::stringstream workerIDStream;

#include "klee/CVAssignment.h"
#include "klee/util/ExprUtil.h"
//Todo: Need to move this out into its own cpp file.

CVAssignment::CVAssignment(std::vector<const klee::Array*> &objects, 
                           std::vector< std::vector<unsigned char> > &values) {
  addBindings(objects, values);
}

void CVAssignment::addBindings(std::vector<const klee::Array*> &objects, 
                                std::vector< std::vector<unsigned char> > &values) {

  std::vector< std::vector<unsigned char> >::iterator valIt = 
    values.begin();
  for (std::vector<const klee::Array*>::iterator it = objects.begin(),
          ie = objects.end(); it != ie; ++it) {
    const klee::Array *os = *it;
    std::vector<unsigned char> &arr = *valIt;
    bindings.insert(std::make_pair(os, arr));
    name_bindings.insert(std::make_pair(os->name, os));
    ++valIt;
  }
}

//Extended for TASE to include ExecStatePtr
void CVAssignment::solveForBindings(klee::Solver* solver, 
                                    klee::ref<klee::Expr> &expr,
				    klee::ExecutionState * ExecStatePtr) {
  std::vector<const klee::Array*> arrays;
  std::vector< std::vector<unsigned char> > initial_values;

  klee::findSymbolicObjects(expr, arrays);
  //ABH: It needs to be the case that the write condition was added to
  //exec state's constraints before solveForBindings was called.
  //Todo:  Make this simpler and less prone to misuse.
  klee::ConstraintManager cm(ExecStatePtr->constraints);
  

  klee::Query query(cm, klee::ConstantExpr::alloc(0, klee::Expr::Bool));

  solver->getInitialValues(query, arrays, initial_values);

  klee::ref<klee::Expr> value_disjunction
		= klee::ConstantExpr::alloc(0, klee::Expr::Bool);

  for (unsigned i=0; i<arrays.size(); ++i) {
    for (unsigned j=0; j<initial_values[i].size(); ++j) {

      klee::ref<klee::Expr> read = 
        klee::ReadExpr::create(klee::UpdateList(arrays[i], 0),
          klee::ConstantExpr::create(j, klee::Expr::Int32));

      klee::ref<klee::Expr> neq_expr = 
        klee::NotExpr::create(
          klee::EqExpr::create(read,
            klee::ConstantExpr::create(initial_values[i][j], klee::Expr::Int8)));

      value_disjunction = klee::OrExpr::create(value_disjunction, neq_expr);
    }
  }

  // This may be a null-op how this interaction works needs to be better
  // understood
  value_disjunction = cm.simplifyExpr(value_disjunction);

  if (value_disjunction->getKind() == klee::Expr::Constant
      && cast<klee::ConstantExpr>(value_disjunction)->isFalse()) {
    addBindings(arrays, initial_values);
  } else {
    cm.addConstraint(value_disjunction);

    bool result;
    solver->mayBeTrue(klee::Query(cm,
      klee::ConstantExpr::alloc(0, klee::Expr::Bool)), result);

    if (result) {
      printf("INVALID solver concretization!");
      std::exit(EXIT_FAILURE);
    } else {
      //TODO Test this path
      addBindings(arrays, initial_values);
    }
  }
}

extern multipassRecord multipassInfo;

//AH: End of our additions. -----------------------------------

namespace {
  cl::opt<bool>
  DumpStatesOnHalt("dump-states-on-halt",
                   cl::init(true),
		   cl::desc("Dump test cases for all active states on exit (default=on)"));
  
  cl::opt<bool>
  AllowExternalSymCalls("allow-external-sym-calls",
                        cl::init(false),
			cl::desc("Allow calls with symbolic arguments to external functions.  This concretizes the symbolic arguments.  (default=off)"));

  /// The different query logging solvers that can switched on/off
  enum PrintDebugInstructionsType {
    STDERR_ALL, ///
    STDERR_SRC,
    STDERR_COMPACT,
    FILE_ALL,    ///
    FILE_SRC,    ///
    FILE_COMPACT ///
  };

  llvm::cl::bits<PrintDebugInstructionsType> DebugPrintInstructions(
      "debug-print-instructions",
      llvm::cl::desc("Log instructions during execution."),
      llvm::cl::values(
          clEnumValN(STDERR_ALL, "all:stderr", "Log all instructions to stderr "
                                               "in format [src, inst_id, "
                                               "llvm_inst]"),
          clEnumValN(STDERR_SRC, "src:stderr",
                     "Log all instructions to stderr in format [src, inst_id]"),
          clEnumValN(STDERR_COMPACT, "compact:stderr",
                     "Log all instructions to stderr in format [inst_id]"),
          clEnumValN(FILE_ALL, "all:file", "Log all instructions to file "
                                           "instructions.txt in format [src, "
                                           "inst_id, llvm_inst]"),
          clEnumValN(FILE_SRC, "src:file", "Log all instructions to file "
                                           "instructions.txt in format [src, "
                                           "inst_id]"),
          clEnumValN(FILE_COMPACT, "compact:file",
                     "Log all instructions to file instructions.txt in format "
                     "[inst_id]")
          KLEE_LLVM_CL_VAL_END),
      llvm::cl::CommaSeparated);
#ifdef HAVE_ZLIB_H
  cl::opt<bool> DebugCompressInstructions(
      "debug-compress-instructions", cl::init(false),
      cl::desc("Compress the logged instructions in gzip format."));
#endif

  cl::opt<bool>
  DebugCheckForImpliedValues("debug-check-for-implied-values");


  cl::opt<bool>
  SimplifySymIndices("simplify-sym-indices",
                     cl::init(false),
		     cl::desc("Simplify symbolic accesses using equalities from other constraints (default=off)"));

  cl::opt<bool>
  EqualitySubstitution("equality-substitution",
		       cl::init(true),
		       cl::desc("Simplify equality expressions before querying the solver (default=on)."));
 
  cl::opt<unsigned>
  MaxSymArraySize("max-sym-array-size",
                  cl::init(0));

  cl::opt<bool>
  SuppressExternalWarnings("suppress-external-warnings",
			   cl::init(false),
			   cl::desc("Supress warnings about calling external functions."));

  cl::opt<bool>
  AllExternalWarnings("all-external-warnings",
		      cl::init(false),
		      cl::desc("Issue an warning everytime an external call is made," 
			       "as opposed to once per function (default=off)"));

  cl::opt<bool>
  OnlyOutputStatesCoveringNew("only-output-states-covering-new",
                              cl::init(false),
			      cl::desc("Only output test cases covering new code (default=off)."));

  cl::opt<bool>
  EmitAllErrors("emit-all-errors",
                cl::init(false),
                cl::desc("Generate tests cases for all errors "
                         "(default=off, i.e. one per (error,instruction) pair)"));
  
  cl::opt<bool>
  NoExternals("no-externals", 
           cl::desc("Do not allow external function calls (default=off)"));

  cl::opt<bool>
  AlwaysOutputSeeds("always-output-seeds",
		    cl::init(true));

  cl::opt<bool>
  OnlyReplaySeeds("only-replay-seeds",
		  cl::init(false),
                  cl::desc("Discard states that do not have a seed (default=off)."));
 
  cl::opt<bool>
  OnlySeed("only-seed",
	   cl::init(false),
           cl::desc("Stop execution after seeding is done without doing regular search (default=off)."));
 
  cl::opt<bool>
  AllowSeedExtension("allow-seed-extension",
		     cl::init(false),
                     cl::desc("Allow extra (unbound) values to become symbolic during seeding (default=false)."));
 
  cl::opt<bool>
  ZeroSeedExtension("zero-seed-extension",
		    cl::init(false),
		    cl::desc("(default=off)"));
 
  cl::opt<bool>
  AllowSeedTruncation("allow-seed-truncation",
		      cl::init(false),
                      cl::desc("Allow smaller buffers than in seeds (default=off)."));
 
  cl::opt<bool>
  NamedSeedMatching("named-seed-matching",
		    cl::init(false),
                    cl::desc("Use names to match symbolic objects to inputs (default=off)."));

  cl::opt<double>
  MaxStaticForkPct("max-static-fork-pct", 
		   cl::init(1.),
		   cl::desc("(default=1.0)"));

  cl::opt<double>
  MaxStaticSolvePct("max-static-solve-pct",
		    cl::init(1.),
		    cl::desc("(default=1.0)"));

  cl::opt<double>
  MaxStaticCPForkPct("max-static-cpfork-pct", 
		     cl::init(1.),
		     cl::desc("(default=1.0)"));

  cl::opt<double>
  MaxStaticCPSolvePct("max-static-cpsolve-pct",
		      cl::init(1.),
		      cl::desc("(default=1.0)"));

  cl::opt<double>
  MaxInstructionTime("max-instruction-time",
                     cl::desc("Only allow a single instruction to take this much time (default=0s (off)). Enables --use-forked-solver"),
                     cl::init(0));
  
  cl::opt<double>
  SeedTime("seed-time",
           cl::desc("Amount of time to dedicate to seeds, before normal search (default=0 (off))"),
           cl::init(0));
  
  cl::list<Executor::TerminateReason>
  ExitOnErrorType("exit-on-error-type",
		  cl::desc("Stop execution after reaching a specified condition.  (default=off)"),
		  cl::values(
		    clEnumValN(Executor::Abort, "Abort", "The program crashed"),
		    clEnumValN(Executor::Assert, "Assert", "An assertion was hit"),
		    clEnumValN(Executor::BadVectorAccess, "BadVectorAccess", "Vector accessed out of bounds"),
		    clEnumValN(Executor::Exec, "Exec", "Trying to execute an unexpected instruction"),
		    clEnumValN(Executor::External, "External", "External objects referenced"),
		    clEnumValN(Executor::Free, "Free", "Freeing invalid memory"),
		    clEnumValN(Executor::Model, "Model", "Memory model limit hit"),
		    clEnumValN(Executor::Overflow, "Overflow", "An overflow occurred"),
		    clEnumValN(Executor::Ptr, "Ptr", "Pointer error"),
		    clEnumValN(Executor::ReadOnly, "ReadOnly", "Write to read-only memory"),
		    clEnumValN(Executor::ReportError, "ReportError", "klee_report_error called"),
		    clEnumValN(Executor::User, "User", "Wrong klee_* functions invocation"),
		    clEnumValN(Executor::Unhandled, "Unhandled", "Unhandled instruction hit")
		    KLEE_LLVM_CL_VAL_END),
		  cl::ZeroOrMore);

  cl::opt<unsigned long long>
  StopAfterNInstructions("stop-after-n-instructions",
                         cl::desc("Stop execution after specified number of instructions (default=0 (off))"),
                         cl::init(0));
  
  cl::opt<unsigned>
  MaxForks("max-forks",
           cl::desc("Only fork this many times (default=-1 (off))"),
           cl::init(~0u));
  
  cl::opt<unsigned>
  MaxDepth("max-depth",
           cl::desc("Only allow this many symbolic branches (default=0 (off))"),
           cl::init(0));
  
  cl::opt<unsigned>
  MaxMemory("max-memory",
            cl::desc("Refuse to fork when above this amount of memory (in MB, default=2000)"),
            cl::init(2000));

  cl::opt<bool>
  MaxMemoryInhibit("max-memory-inhibit",
            cl::desc("Inhibit forking at memory cap (vs. random terminate) (default=on)"),
            cl::init(true));
}


namespace klee {
  RNG theRNG;
}

const char *Executor::TerminateReasonNames[] = {
  [ Abort ] = "abort",
  [ Assert ] = "assert",
  [ BadVectorAccess ] = "bad_vector_access",
  [ Exec ] = "exec",
  [ External ] = "external",
  [ Free ] = "free",
  [ Model ] = "model",
  [ Overflow ] = "overflow",
  [ Ptr ] = "ptr",
  [ ReadOnly ] = "readonly",
  [ ReportError ] = "reporterror",
  [ User ] = "user",
  [ Unhandled ] = "xxx",
};

Executor::Executor(LLVMContext &ctx, const InterpreterOptions &opts,
    InterpreterHandler *ih)
    : Interpreter(opts), kmodule(0), interpreterHandler(ih), searcher(0),
      externalDispatcher(new ExternalDispatcher(ctx)), statsTracker(0),
      pathWriter(0), symPathWriter(0), specialFunctionHandler(0),
      processTree(0), replayKTest(0), replayPath(0), usingSeeds(0),
      atMemoryLimit(false), inhibitForking(false), haltExecution(false),
      ivcEnabled(false),
      coreSolverTimeout(MaxCoreSolverTime != 0 && MaxInstructionTime != 0
                            ? std::min(MaxCoreSolverTime, MaxInstructionTime)
                            : std::max(MaxCoreSolverTime, MaxInstructionTime)),
      debugInstFile(0), debugLogBuffer(debugBufferString) {

  if (coreSolverTimeout) UseForkedCoreSolver = true;
  Solver *coreSolver = klee::createCoreSolver(CoreSolverToUse);
  if (!coreSolver) {
    klee_error("Failed to create core solver\n");
  }

  Solver *solver = constructSolverChain(
      coreSolver,
      interpreterHandler->getOutputFilename(ALL_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(ALL_QUERIES_KQUERY_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_KQUERY_FILE_NAME));

  this->solver = new TimingSolver(solver, EqualitySubstitution);
  memory = new MemoryManager(&arrayCache);

  initializeSearchOptions();

  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    std::string debug_file_name =
        interpreterHandler->getOutputFilename("instructions.txt");
    std::string ErrorInfo;
#ifdef HAVE_ZLIB_H
    if (!DebugCompressInstructions) {
#endif

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
    std::error_code ec;
    debugInstFile = new llvm::raw_fd_ostream(debug_file_name.c_str(), ec,
                                             llvm::sys::fs::OpenFlags::F_Text);
    if (ec)
	    ErrorInfo = ec.message();
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
    debugInstFile = new llvm::raw_fd_ostream(debug_file_name.c_str(), ErrorInfo,
                                             llvm::sys::fs::OpenFlags::F_Text);
#else
    debugInstFile =
        new llvm::raw_fd_ostream(debug_file_name.c_str(), ErrorInfo);
#endif
#ifdef HAVE_ZLIB_H
    } else {
      debugInstFile = new compressed_fd_ostream(
          (debug_file_name + ".gz").c_str(), ErrorInfo);
    }
#endif
    if (ErrorInfo != "") {
      klee_error("Could not open file %s : %s", debug_file_name.c_str(),
                 ErrorInfo.c_str());
    }
  }
}


const Module *Executor::setModule(llvm::Module *module, 
                                  const ModuleOptions &opts) {
  
  assert (!kmodule && "kmodule fail \n");
  assert (module && "module fail \n");
  assert(!kmodule && module && "can only register one module"); // XXX gross
  
  kmodule = new KModule(module);

  // Initialize the context.
  DataLayout *TD = kmodule->targetData;
  Context::initialize(TD->isLittleEndian(),
                      (Expr::Width) TD->getPointerSizeInBits());

  specialFunctionHandler = new SpecialFunctionHandler(*this);

  specialFunctionHandler->prepare();
  kmodule->prepare(opts, interpreterHandler);
  specialFunctionHandler->bind();

  if (StatsTracker::useStatistics() || userSearcherRequiresMD2U()) {
    statsTracker = 
      new StatsTracker(*this,
                       interpreterHandler->getOutputFilename("assembly.ll"),
                       userSearcherRequiresMD2U());
  }
  
  return module;
}

Executor::~Executor() {
  delete memory;
  delete externalDispatcher;
  delete processTree;
  delete specialFunctionHandler;
  delete statsTracker;
  delete solver;
  delete kmodule;
  while(!timers.empty()) {
    delete timers.back();
    timers.pop_back();
  }
  delete debugInstFile;
}

/***/

void Executor::initializeGlobalObject(ExecutionState &state, ObjectState *os,
                                      const Constant *c, 
                                      unsigned offset) {
  DataLayout *targetData = kmodule->targetData;
  if (const ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cp->getType()->getElementType());
    for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cp->getOperand(i), 
			     offset + i*elementSize);
  } else if (isa<ConstantAggregateZero>(c)) {
    unsigned i, size = targetData->getTypeStoreSize(c->getType());
    for (i=0; i<size; i++)
      os->write8(offset+i, (uint8_t) 0);
  } else if (const ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(ca->getType()->getElementType());
    for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, ca->getOperand(i), 
			     offset + i*elementSize);
  } else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
    const StructLayout *sl =
      targetData->getStructLayout(cast<StructType>(cs->getType()));
    for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cs->getOperand(i), 
			     offset + sl->getElementOffset(i));
  } else if (const ConstantDataSequential *cds =
               dyn_cast<ConstantDataSequential>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cds->getElementType());
    for (unsigned i=0, e=cds->getNumElements(); i != e; ++i)
      initializeGlobalObject(state, os, cds->getElementAsConstant(i),
                             offset + i*elementSize);
  } else if (!isa<UndefValue>(c)) {
    unsigned StoreBits = targetData->getTypeStoreSizeInBits(c->getType());
    ref<ConstantExpr> C = evalConstant(c);

    // Extend the constant if necessary;
    assert(StoreBits >= C->getWidth() && "Invalid store size!");
    if (StoreBits > C->getWidth())
      C = C->ZExt(StoreBits);

    os->write(offset, C);
  }
}

MemoryObject * Executor::addExternalObject(ExecutionState &state, 
                                           void *addr, unsigned size, 
                                           bool isReadOnly) {
  MemoryObject *mo = memory->allocateFixed((uint64_t) (unsigned long) addr, 
                                           size, 0);
  ObjectState *os = bindObjectInState(state, mo, false);
  
  printf("mo->address is %lu \n", mo->address);
  os->concreteStore = (uint8_t *) mo->address;
  //for(unsigned i = 0; i < size; i++)
  // os->write8(i, ((uint8_t*)addr)[i]);
  if(isReadOnly)
    os->setReadOnly(true);  
  return mo;
}


extern void *__dso_handle __attribute__ ((__weak__));

void Executor::initializeGlobals(ExecutionState &state) {
  Module *m = kmodule->module;

  if (m->getModuleInlineAsm() != "")
    klee_warning("executable has module level assembly (ignoring)");
  // represent function globals using the address of the actual llvm function
  // object. given that we use malloc to allocate memory in states this also
  // ensures that we won't conflict. we don't need to allocate a memory object
  // since reading/writing via a function pointer is unsupported anyway.
  for (Module::iterator i = m->begin(), ie = m->end(); i != ie; ++i) {
    Function *f = &*i;
    ref<ConstantExpr> addr(0);

    // If the symbol has external weak linkage then it is implicitly
    // not defined in this module; if it isn't resolvable then it
    // should be null.
    if (f->hasExternalWeakLinkage() && 
        !externalDispatcher->resolveSymbol(f->getName())) {
      addr = Expr::createPointer(0);
    } else {
      addr = Expr::createPointer((unsigned long) (void*) f);
      legalFunctions.insert((uint64_t) (unsigned long) (void*) f);
    }
    
    globalAddresses.insert(std::make_pair(f, addr));
  }

  // Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
  /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
  int *errno_addr = __errno_location();
  addExternalObject(state, (void *)errno_addr, sizeof *errno_addr, false);

  /* from /usr/include/ctype.h:
       These point into arrays of 384, so they can be indexed by any `unsigned
       char' value [0,255]; by EOF (-1); or by any `signed char' value
       [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
  const uint16_t **addr = __ctype_b_loc();
  addExternalObject(state, const_cast<uint16_t*>(*addr-128),
                    384 * sizeof **addr, true);
  addExternalObject(state, addr, sizeof(*addr), true);
    
  const int32_t **lower_addr = __ctype_tolower_loc();
  addExternalObject(state, const_cast<int32_t*>(*lower_addr-128),
                    384 * sizeof **lower_addr, true);
  addExternalObject(state, lower_addr, sizeof(*lower_addr), true);
  
  const int32_t **upper_addr = __ctype_toupper_loc();
  addExternalObject(state, const_cast<int32_t*>(*upper_addr-128),
                    384 * sizeof **upper_addr, true);
  addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

  // allocate and initialize globals, done in two passes since we may
  // need address of a global in order to initialize some other one.

  // allocate memory objects for all globals
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    const GlobalVariable *v = &*i;
    size_t globalObjectAlignment = getAllocationAlignment(v);
    if (i->isDeclaration()) {
      // FIXME: We have no general way of handling unknown external
      // symbols. If we really cared about making external stuff work
      // better we could support user definition, or use the EXE style
      // hack where we check the object file information.

      Type *ty = i->getType()->getElementType();
      uint64_t size = 0;
      if (ty->isSized()) {
	size = kmodule->targetData->getTypeStoreSize(ty);
      } else {
        klee_warning("Type for %.*s is not sized", (int)i->getName().size(),
			i->getName().data());
      }

      // XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
      if (i->getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
        size = 0x2C;
      }
#endif

      if (size == 0) {
        klee_warning("Unable to find size for global variable: %.*s (use will result in out of bounds access)",
			(int)i->getName().size(), i->getName().data());
      }

      MemoryObject *mo = memory->allocate(size, /*isLocal=*/false,
                                          /*isGlobal=*/true, /*allocSite=*/v,
                                          /*alignment=*/globalObjectAlignment);
      ObjectState *os = bindObjectInState(state, mo, false);
      globalObjects.insert(std::make_pair(v, mo));
      globalAddresses.insert(std::make_pair(v, mo->getBaseExpr()));

      // Program already running = object already initialized.  Read
      // concrete value and write it to our copy.
      if (size) {
        void *addr;
        if (i->getName() == "__dso_handle") {
          addr = &__dso_handle; // wtf ?
        } else {
          addr = externalDispatcher->resolveSymbol(i->getName());
        }
        if (!addr)
          klee_error("unable to load symbol(%s) while initializing globals.", 
                     i->getName().data());

        for (unsigned offset=0; offset<mo->size; offset++){
	  //printf("Calling os->write8 from initializeGlobals()\n ");
          os->write8(offset, ((unsigned char*)addr)[offset]);
	}
      }
    } else {
      Type *ty = i->getType()->getElementType();
      uint64_t size = kmodule->targetData->getTypeStoreSize(ty);
      MemoryObject *mo = memory->allocate(size, /*isLocal=*/false,
                                          /*isGlobal=*/true, /*allocSite=*/v,
                                          /*alignment=*/globalObjectAlignment);
      if (!mo)
        llvm::report_fatal_error("out of memory");
      ObjectState *os = bindObjectInState(state, mo, false);
      globalObjects.insert(std::make_pair(v, mo));
      globalAddresses.insert(std::make_pair(v, mo->getBaseExpr()));

      if (!i->hasInitializer())
          os->initializeToRandom();
    }
  }
  
  // link aliases to their definitions (if bound)
  for (Module::alias_iterator i = m->alias_begin(), ie = m->alias_end(); 
       i != ie; ++i) {
    // Map the alias to its aliasee's address. This works because we have
    // addresses for everything, even undefined functions. 
    globalAddresses.insert(std::make_pair(&*i, evalConstant(i->getAliasee())));
  }

  // once all objects are allocated, do the actual initialization
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    if (i->hasInitializer()) {
      const GlobalVariable *v = &*i;
      MemoryObject *mo = globalObjects.find(v)->second;
      const ObjectState *os = state.addressSpace.findObject(mo);
      assert(os);
      ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      
      initializeGlobalObject(state, wos, i->getInitializer(), 0);
      // if(i->isConstant()) os->setReadOnly(true);
    }
  }
}

void Executor::branch(ExecutionState &state, 
                      const std::vector< ref<Expr> > &conditions,
                      std::vector<ExecutionState*> &result) {
  TimerStatIncrementer timer(stats::forkTime);
  unsigned N = conditions.size();
  assert(N);

  if (MaxForks!=~0u && stats::forks >= MaxForks) {
    unsigned next = theRNG.getInt32() % N;
    for (unsigned i=0; i<N; ++i) {
      if (i == next) {
        result.push_back(&state);
      } else {
        result.push_back(NULL);
      }
    }
  } else {
    stats::forks += N-1;

    // XXX do proper balance or keep random?
    result.push_back(&state);
    for (unsigned i=1; i<N; ++i) {
      ExecutionState *es = result[theRNG.getInt32() % i];
      ExecutionState *ns = es->branch();
      addedStates.push_back(ns);
      result.push_back(ns);
      es->ptreeNode->data = 0;
      std::pair<PTree::Node*,PTree::Node*> res = 
        processTree->split(es->ptreeNode, ns, es);
      ns->ptreeNode = res.first;
      es->ptreeNode = res.second;
    }
  }

  // If necessary redistribute seeds to match conditions, killing
  // states if necessary due to OnlyReplaySeeds (inefficient but
  // simple).
  
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it != seedMap.end()) {
    std::vector<SeedInfo> seeds = it->second;
    seedMap.erase(it);

    // Assume each seed only satisfies one condition (necessarily true
    // when conditions are mutually exclusive and their conjunction is
    // a tautology).
    for (std::vector<SeedInfo>::iterator siit = seeds.begin(), 
           siie = seeds.end(); siit != siie; ++siit) {
      unsigned i;
      for (i=0; i<N; ++i) {
        ref<ConstantExpr> res;
        bool success = 
          solver->getValue(state, siit->assignment.evaluate(conditions[i]), 
                           res);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (res->isTrue())
          break;
      }
      
      // If we didn't find a satisfying condition randomly pick one
      // (the seed will be patched).
      if (i==N)
        i = theRNG.getInt32() % N;

      // Extra check in case we're replaying seeds with a max-fork
      if (result[i])
        seedMap[result[i]].push_back(*siit);
    }

    if (OnlyReplaySeeds) {
      for (unsigned i=0; i<N; ++i) {
        if (result[i] && !seedMap.count(result[i])) {
          terminateState(*result[i]);
          result[i] = NULL;
        }
      } 
    }
  }

  for (unsigned i=0; i<N; ++i)
    if (result[i])
      addConstraint(*result[i], conditions[i]);
}

Executor::StatePair 
Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal) {
  
  
  printf("DBG: FRK 0 \n");
  Solver::Validity res;
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&current);
  bool isSeeding = it != seedMap.end();

  if (!isSeeding && !isa<ConstantExpr>(condition) && 
      (MaxStaticForkPct!=1. || MaxStaticSolvePct != 1. ||
       MaxStaticCPForkPct!=1. || MaxStaticCPSolvePct != 1.) &&
      statsTracker->elapsed() > 60.) {
    StatisticManager &sm = *theStatisticManager;
    CallPathNode *cpn = current.stack.back().callPathNode;
    if ((MaxStaticForkPct<1. &&
         sm.getIndexedValue(stats::forks, sm.getIndex()) > 
         stats::forks*MaxStaticForkPct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::forks) > 
                 stats::forks*MaxStaticCPForkPct)) ||
        (MaxStaticSolvePct<1 &&
         sm.getIndexedValue(stats::solverTime, sm.getIndex()) > 
         stats::solverTime*MaxStaticSolvePct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::solverTime) > 
                 stats::solverTime*MaxStaticCPSolvePct))) {
      ref<ConstantExpr> value; 
      bool success = solver->getValue(current, condition, value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      addConstraint(current, EqExpr::create(value, condition));
      condition = value;
    }
  }

  printf("BR: FRK1 \n");
  
  double timeout = coreSolverTimeout;
  if (isSeeding)
    timeout *= it->second.size();
  solver->setTimeout(timeout);
  bool success = solver->evaluate(current, condition, res);
  printf("BR: FRK1.1 \n");
  solver->setTimeout(0);
  if (!success) {
    printf("INTERPRETER: QUERY TIMEOUT \n");
    current.pc = current.prevPC;
    terminateStateEarly(current, "Query timed out (fork).");
    return StatePair(0, 0);
  }
  printf("BR: FRK1.2 \n");
  if (!isSeeding) {
    if (replayPath && !isInternal) {
      assert(replayPosition<replayPath->size() &&
             "ran out of branches in replay path mode");
      bool branch = (*replayPath)[replayPosition++];
      
      if (res==Solver::True) {
        assert(branch && "hit invalid branch in replay path mode");
      } else if (res==Solver::False) {
        assert(!branch && "hit invalid branch in replay path mode");
      } else {
        // add constraints
        if(branch) {
          res = Solver::True;
          addConstraint(current, condition);
        } else  {
          res = Solver::False;
          addConstraint(current, Expr::createIsZero(condition));
        }
      }
    } else if (res==Solver::Unknown) {
      assert(!replayKTest && "in replay mode, only one branch can be true.");
      
      if ((MaxMemoryInhibit && atMemoryLimit) || 
          current.forkDisabled ||
          inhibitForking || 
          (MaxForks!=~0u && stats::forks >= MaxForks)) {

	if (MaxMemoryInhibit && atMemoryLimit)
	  klee_warning_once(0, "skipping fork (memory cap exceeded)");
	else if (current.forkDisabled)
	  klee_warning_once(0, "skipping fork (fork disabled on current path)");
	else if (inhibitForking)
	  klee_warning_once(0, "skipping fork (fork disabled globally)");
	else 
	  klee_warning_once(0, "skipping fork (max-forks reached)");

        TimerStatIncrementer timer(stats::forkTime);
        if (theRNG.getBool()) {
          addConstraint(current, condition);
          res = Solver::True;        
        } else {
          addConstraint(current, Expr::createIsZero(condition));
          res = Solver::False;
        }
      }
    }
  }
  printf("BR: FRK1.5 \n");
  // Fix branch in only-replay-seed mode, if we don't have both true
  // and false seeds.
  if (isSeeding && 
      (current.forkDisabled || OnlyReplaySeeds) && 
      res == Solver::Unknown) {
    bool trueSeed=false, falseSeed=false;
    // Is seed extension still ok here?
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      ref<ConstantExpr> res;
      bool success = 
        solver->getValue(current, siit->assignment.evaluate(condition), res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res->isTrue()) {
        trueSeed = true;
      } else {
        falseSeed = true;
      }
      if (trueSeed && falseSeed)
        break;
    }
    if (!(trueSeed && falseSeed)) {
      assert(trueSeed || falseSeed);
      
      res = trueSeed ? Solver::True : Solver::False;
      addConstraint(current, trueSeed ? condition : Expr::createIsZero(condition));
    }
  }

  printf("DBG: FRK 2\n");
  // XXX - even if the constraint is provable one way or the other we
  // can probably benefit by adding this constraint and allowing it to
  // reduce the other constraints. For example, if we do a binary
  // search on a particular value, and then see a comparison against
  // the value it has been fixed at, we should take this as a nice
  // hint to just use the single constraint instead of all the binary
  // search ones. If that makes sense.
  if (res==Solver::True) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "1";
      }
    }

    return StatePair(&current, 0);
  } else if (res==Solver::False) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "0";
      }
    }

    return StatePair(0, &current);
  } else {
    TimerStatIncrementer timer(stats::forkTime);
    

    ExecutionState *falseState, *trueState = &current;
  
    //Original code:
    //ExecutionState *falseState, *trueState = &current;
    //End hack
    ++stats::forks;


    /*
    falseState = trueState->branch();
    addedStates.push_back(falseState);
 
    if (it != seedMap.end()) {
      std::vector<SeedInfo> seeds = it->second;
      it->second.clear();
      std::vector<SeedInfo> &trueSeeds = seedMap[trueState];
      std::vector<SeedInfo> &falseSeeds = seedMap[falseState];
      for (std::vector<SeedInfo>::iterator siit = seeds.begin(), 
             siie = seeds.end(); siit != siie; ++siit) {
        ref<ConstantExpr> res;
        bool success = 
          solver->getValue(current, siit->assignment.evaluate(condition), res);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (res->isTrue()) {
          trueSeeds.push_back(*siit);
        } else {
          falseSeeds.push_back(*siit);
        }
      }
      
      bool swapInfo = false;
      if (trueSeeds.empty()) {
        if (&current == trueState) swapInfo = true;
        seedMap.erase(trueState);
      }
      if (falseSeeds.empty()) {
        if (&current == falseState) swapInfo = true;
        seedMap.erase(falseState);
      }
      if (swapInfo) {
        std::swap(trueState->coveredNew, falseState->coveredNew);
        std::swap(trueState->coveredLines, falseState->coveredLines);
      }
    }
  
    current.ptreeNode->data = 0;
    std::pair<PTree::Node*, PTree::Node*> res =
      processTree->split(current.ptreeNode, falseState, trueState);
    falseState->ptreeNode = res.first;
    trueState->ptreeNode = res.second;

    if (pathWriter) {
      // Need to update the pathOS.id field of falseState, otherwise the same id
      // is used for both falseState and trueState.
      falseState->pathOS = pathWriter->open(current.pathOS);
      if (!isInternal) {
        trueState->pathOS << "1";
        falseState->pathOS << "0";
      }
    }
    if (symPathWriter) {
      falseState->symPathOS = symPathWriter->open(current.symPathOS);
      if (!isInternal) {
        trueState->symPathOS << "1";
        falseState->symPathOS << "0";
      }
    }
    */
  
    //ABH: Here's where we fork in TASE.
    printf("DBG: FRK 3\n");
    int pid  = ::fork();
    hasForked = true;
    printf("Forking at 0x%llx \n", target_ctx_gregs[REG_RIP]);
    if (pid ==0 ) {
      int i = getpid();
      
      workerIDStream << ".";
      workerIDStream << i;
      std::string pidString ;
      pidString = workerIDStream.str();
      freopen(pidString.c_str(),"w",stdout);
      freopen(pidString.c_str(),"w",stderr);
      printf("DEBUG:  Child process created\n");
      addConstraint(*GlobalExecutionStatePtr, Expr::createIsZero(condition));
    } else {
      int i = getpid();
      workerIDStream << ".";
      workerIDStream << i;
      std::string pidString ;
      pidString = workerIDStream.str();
      freopen(pidString.c_str(),"w",stdout);
      freopen(pidString.c_str(),"w",stderr);
      printf("DEBUG: Parent process continues \n");
      addConstraint(*GlobalExecutionStatePtr, condition);
    }
    
    //Call to solver to make sure we're legit on this branch.

    printf("Calling solver for sanity check \n");
     std::vector< std::vector<unsigned char> > values;
     std::vector<const Array*> objects;
     for (unsigned i = 0; i != GlobalExecutionStatePtr->symbolics.size(); ++i)
       objects.push_back(GlobalExecutionStatePtr->symbolics[i].second);
     bool success = solver->getInitialValues(*GlobalExecutionStatePtr, objects, values);
     if (success)
       printf("Solver checked sanity \n");
     else {
       printf("Solver found invalid path \n");
       printCtx(target_ctx_gregs);
     }
    
    //Orig code:
    /*
    addConstraint(*trueState, condition);
    
    */

    
    
    // Kinda gross, do we even really still want this option?

     /*
    if (MaxDepth && MaxDepth<=trueState->depth) {
      terminateStateEarly(*trueState, "max-depth exceeded.");
      terminateStateEarly(*falseState, "max-depth exceeded.");
      return StatePair(0, 0);
    }
     */
    printf("Reached end of Executor::fork() \n");
     

    //used to be return StatePair(trueState,falseState);
    if (pid == 0)  {
      return StatePair(0, GlobalExecutionStatePtr);
    } else {
      return StatePair(GlobalExecutionStatePtr,0);
    }

  }
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    if (!CE->isTrue())
      llvm::report_fatal_error("attempt to add invalid constraint");
    return;
  }

  // Check to see if this constraint violates seeds.
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it != seedMap.end()) {
    bool warn = false;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      bool res;
      bool success = 
        solver->mustBeFalse(state, siit->assignment.evaluate(condition), res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res) {
        siit->patchSeed(state, condition, solver);
        warn = true;
      }
    }
    if (warn)
      klee_warning("seeds patched for violating constraint"); 
  }

  state.addConstraint(condition);
  if (ivcEnabled)
    doImpliedValueConcretization(state, condition, 
                                 ConstantExpr::alloc(1, Expr::Bool));
}

const Cell& Executor::eval(KInstruction *ki, unsigned index, 
                           ExecutionState &state) const {
  assert(index < ki->inst->getNumOperands());
  int vnumber = ki->operands[index];

  assert(vnumber != -1 &&
         "Invalid operand to eval(), not a value or constant!");

  // Determine if this is a constant or not.
  if (vnumber < 0) {
    unsigned index = -vnumber - 2;
    return kmodule->constantTable[index];
  } else {
    unsigned index = vnumber;
    StackFrame &sf = state.stack.back();
    return sf.locals[index];
  }
}

void Executor::bindLocal(KInstruction *target, ExecutionState &state, 
                         ref<Expr> value) {
  getDestCell(state, target).value = value;
}

void Executor::bindArgument(KFunction *kf, unsigned index, 
                            ExecutionState &state, ref<Expr> value) {
  getArgumentCell(state, kf, index).value = value;
}

ref<Expr> Executor::toUnique(const ExecutionState &state, 
                             ref<Expr> &e) {
  ref<Expr> result = e;

  if (!isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool isTrue = false;

    solver->setTimeout(coreSolverTimeout);      
    if (solver->getValue(state, e, value) &&
        solver->mustBeTrue(state, EqExpr::create(e, value), isTrue) &&
        isTrue)
      result = value;
    solver->setTimeout(0);
  }
  
  return result;
}


/* Concretize the given expression, and return a possible constant value. 
   'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr> 
Executor::toConstant(ExecutionState &state, 
                     ref<Expr> e,
                     const char *reason) {
  e = state.constraints.simplifyExpr(e);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE;

  ref<ConstantExpr> value;
  bool success = solver->getValue(state, e, value);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;

  std::string str;
  llvm::raw_string_ostream os(str);
  os << "silently concretizing (reason: " << reason << ") expression " << e
     << " to value " << value << " (" << (*(state.pc)).info->file << ":"
     << (*(state.pc)).info->line << ")";

  if (AllExternalWarnings)
    klee_warning(reason, os.str().c_str());
  else
    klee_warning_once(reason, "%s", os.str().c_str());

  addConstraint(state, EqExpr::create(e, value));
    
  return value;
}

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  e = state.constraints.simplifyExpr(e);
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it==seedMap.end() || isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, e, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bindLocal(target, state, value);
  } else {
    std::set< ref<Expr> > values;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      ref<ConstantExpr> value;
      bool success = 
        solver->getValue(state, siit->assignment.evaluate(e), value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      values.insert(value);
    }
    
    std::vector< ref<Expr> > conditions;
    for (std::set< ref<Expr> >::iterator vit = values.begin(), 
           vie = values.end(); vit != vie; ++vit)
      conditions.push_back(EqExpr::create(e, *vit));

    std::vector<ExecutionState*> branches;
    branch(state, conditions, branches);
    
    std::vector<ExecutionState*>::iterator bit = branches.begin();
    for (std::set< ref<Expr> >::iterator vit = values.begin(), 
           vie = values.end(); vit != vie; ++vit) {
      ExecutionState *es = *bit;
      if (es)
        bindLocal(target, *es, *vit);
      ++bit;
    }
  }
}

void Executor::printDebugInstructions(ExecutionState &state) {
  // check do not print
  if (DebugPrintInstructions.getBits() == 0)
	  return;

  llvm::raw_ostream *stream = 0;
  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(STDERR_SRC) ||
      DebugPrintInstructions.isSet(STDERR_COMPACT))
    stream = &llvm::errs();
  else
    stream = &debugLogBuffer;

  if (!DebugPrintInstructions.isSet(STDERR_COMPACT) &&
      !DebugPrintInstructions.isSet(FILE_COMPACT)) {
    (*stream) << "     ";
    state.pc->printFileLine(*stream);
    (*stream) << ":";
  }

  (*stream) << state.pc->info->assemblyLine;

  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(FILE_ALL))
    (*stream) << ":" << *(state.pc->inst);
  (*stream) << "\n";

  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    debugLogBuffer.flush();
    (*debugInstFile) << debugLogBuffer.str();
    debugBufferString = "";
  }
}

void Executor::stepInstruction(ExecutionState &state) {
  //printf(" DB -1 \n");
  printDebugInstructions(state);
  //printf(" DB 0 \n");
  if (statsTracker)
    statsTracker->stepInstruction(state);
  //printf(" DB 1 \n");
  ++stats::instructions;
  //printf(" DB 2 \n");
  state.prevPC = state.pc;
  //printf(" DB 3 \n");
  ++state.pc;

  if (stats::instructions==StopAfterNInstructions)
    haltExecution = true;
}

void Executor::executeCall(ExecutionState &state, 
                           KInstruction *ki,
                           Function *f,
                           std::vector< ref<Expr> > &arguments) {
  Instruction *i = ki->inst;
  if (f && f->isDeclaration()) {
    switch(f->getIntrinsicID()) {
    case Intrinsic::not_intrinsic:
      // state may be destroyed by this call, cannot touch
      callExternalFunction(state, ki, f, arguments);
      break;
        
      // va_arg is handled by caller and intrinsic lowering, see comment for
      // ExecutionState::varargs
    case Intrinsic::vastart:  {
      StackFrame &sf = state.stack.back();

      // varargs can be zero if no varargs were provided
      if (!sf.varargs)
        return;

      // FIXME: This is really specific to the architecture, not the pointer
      // size. This happens to work for x86-32 and x86-64, however.
      Expr::Width WordSize = Context::get().getPointerWidth();
      if (WordSize == Expr::Int32) {
        executeMemoryOperation(state, true, arguments[0], 
                               sf.varargs->getBaseExpr(), 0);
      } else {
        assert(WordSize == Expr::Int64 && "Unknown word size!");

        // x86-64 has quite complicated calling convention. However,
        // instead of implementing it, we can do a simple hack: just
        // make a function believe that all varargs are on stack.
        executeMemoryOperation(state, true, arguments[0],
                               ConstantExpr::create(48, 32), 0); // gp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(4, 64)),
                               ConstantExpr::create(304, 32), 0); // fp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(8, 64)),
                               sf.varargs->getBaseExpr(), 0); // overflow_arg_area
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(16, 64)),
                               ConstantExpr::create(0, 64), 0); // reg_save_area
      }
      break;
    }
    case Intrinsic::vaend:
      // va_end is a noop for the interpreter.
      //
      // FIXME: We should validate that the target didn't do something bad
      // with va_end, however (like call it twice).
      break;
        
    case Intrinsic::vacopy:
      // va_copy should have been lowered.
      //
      // FIXME: It would be nice to check for errors in the usage of this as
      // well.
    default:
      klee_error("unknown intrinsic: %s", f->getName().data());
    }

    if (InvokeInst *ii = dyn_cast<InvokeInst>(i))
      transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
  } else {
    // FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
    // guess. This just done to avoid having to pass KInstIterator everywhere
    // instead of the actual instruction, since we can't make a KInstIterator
    // from just an instruction (unlike LLVM).
    KFunction *kf = kmodule->functionMap[f];
    state.pushFrame(state.prevPC, kf);
    state.pc = kf->instructions;

    if (statsTracker)
      statsTracker->framePushed(state, &state.stack[state.stack.size()-2]);

     // TODO: support "byval" parameter attribute
     // TODO: support zeroext, signext, sret attributes

    unsigned callingArgs = arguments.size();
    unsigned funcArgs = f->arg_size();
    if (!f->isVarArg()) {
      if (callingArgs > funcArgs) {
        klee_warning_once(f, "calling %s with extra arguments.", 
                          f->getName().data());
      } else if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              User);
        return;
      }
    } else {
      Expr::Width WordSize = Context::get().getPointerWidth();

      if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              User);
        return;
      }

      StackFrame &sf = state.stack.back();
      unsigned size = 0;
      bool requires16ByteAlignment = false;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work for x86-32 and x86-64, however.
        if (WordSize == Expr::Int32) {
          size += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          Expr::Width argWidth = arguments[i]->getWidth();
          // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a
          // 16 byte boundary if alignment needed by type exceeds 8 byte
          // boundary.
          //
          // Alignment requirements for scalar types is the same as their size
          if (argWidth > Expr::Int64) {
             size = llvm::RoundUpToAlignment(size, 16);
             requires16ByteAlignment = true;
          }
          size += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
        }
      }

      MemoryObject *mo = sf.varargs =
          memory->allocate(size, true, false, state.prevPC->inst,
                           (requires16ByteAlignment ? 16 : 8));
      if (!mo && size) {
        terminateStateOnExecError(state, "out of memory (varargs)");
        return;
      }

      if (mo) {
        if ((WordSize == Expr::Int64) && (mo->address & 15) &&
            requires16ByteAlignment) {
          // Both 64bit Linux/Glibc and 64bit MacOSX should align to 16 bytes.
          klee_warning_once(
              0, "While allocating varargs: malloc did not align to 16 bytes.");
        }

        ObjectState *os = bindObjectInState(state, mo, true);
        unsigned offset = 0;
        for (unsigned i = funcArgs; i < callingArgs; i++) {
          // FIXME: This is really specific to the architecture, not the pointer
          // size. This happens to work for x86-32 and x86-64, however.
          if (WordSize == Expr::Int32) {
            os->write(offset, arguments[i]);
            offset += Expr::getMinBytesForWidth(arguments[i]->getWidth());
          } else {
            assert(WordSize == Expr::Int64 && "Unknown word size!");

            Expr::Width argWidth = arguments[i]->getWidth();
            if (argWidth > Expr::Int64) {
              offset = llvm::RoundUpToAlignment(offset, 16);
            }
            os->write(offset, arguments[i]);
            offset += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
          }
        }
      }
    }

    unsigned numFormals = f->arg_size();
    for (unsigned i=0; i<numFormals; ++i) 
      bindArgument(kf, i, state, arguments[i]);
  }
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src, 
                                    ExecutionState &state) {
  // Note that in general phi nodes can reuse phi values from the same
  // block but the incoming value is the eval() result *before* the
  // execution of any phi nodes. this is pathological and doesn't
  // really seem to occur, but just in case we run the PhiCleanerPass
  // which makes sure this cannot happen and so it is safe to just
  // eval things in order. The PhiCleanerPass also makes sure that all
  // incoming blocks have the same order for each PHINode so we only
  // have to compute the index once.
  //
  // With that done we simply set an index in the state so that PHI
  // instructions know which argument to eval, set the pc, and continue.
  
  // XXX this lookup has to go ?
  KFunction *kf = state.stack.back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  state.pc = &kf->instructions[entry];


  if (state.pc->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(state.pc->inst);
    state.incomingBBIndex = first->getBasicBlockIndex(src);
  }
}

/// Compute the true target of a function call, resolving LLVM and KLEE aliases
/// and bitcasts.
Function* Executor::getTargetFunction(Value *calledVal, ExecutionState &state) {
  SmallPtrSet<const GlobalValue*, 3> Visited;

  Constant *c = dyn_cast<Constant>(calledVal);
  if (!c)
    return 0;

  while (true) {
    if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
      if (!Visited.insert(gv).second)
        return 0;
#else
      if (!Visited.insert(gv))
        return 0;
#endif
      std::string alias = state.getFnAlias(gv->getName());
      if (alias != "") {
        llvm::Module* currModule = kmodule->module;
        GlobalValue *old_gv = gv;
        gv = currModule->getNamedValue(alias);
        if (!gv) {
          klee_error("Function %s(), alias for %s not found!\n", alias.c_str(),
                     old_gv->getName().str().c_str());
        }
      }
     
      if (Function *f = dyn_cast<Function>(gv))
        return f;
      else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(gv))
        c = ga->getAliasee();
      else
        return 0;
    } else if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
      if (ce->getOpcode()==Instruction::BitCast)
        c = ce->getOperand(0);
      else
        return 0;
    } else
      return 0;
  }
}

/// TODO remove?
static bool isDebugIntrinsic(const Function *f, KModule *KM) {
  return false;
}

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width) {
  switch(width) {
  case Expr::Int32:
    return &llvm::APFloat::IEEEsingle;
  case Expr::Int64:
    return &llvm::APFloat::IEEEdouble;
  case Expr::Fl80:
    return &llvm::APFloat::x87DoubleExtended;
  default:
    return 0;
  }
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki) {
  //printf( " KI is %s \n" , (ki->printFileLine()).c_str());
  //printf("Calling executeInstruction \n ");
Instruction *i = ki->inst;
  switch (i->getOpcode()) {
    // Control flow
  case Instruction::Ret: {

    ReturnInst *ri = cast<ReturnInst>(i);
    KInstIterator kcaller = state.stack.back().caller;
    Instruction *caller = kcaller ? kcaller->inst : 0;
    bool isVoidReturn = (ri->getNumOperands() == 0);
    ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);
    
    if (!isVoidReturn) {
      result = eval(ki, 0, state).value;
    }
    
    if (state.stack.size() <= 1) {
      assert(!caller && "caller set on initial stack frame");
      //printf("Choosing not to call terminateStateOnExit(state) \n \n ");
      //terminateStateOnExit(state);
      state.popFrame();
      
      haltExecution = true;
      break;
    } else {
      state.popFrame();

      if (statsTracker)
        statsTracker->framePopped(state);
      if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
        transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
      } else {
        state.pc = kcaller;
        ++state.pc;
      }

      if (!isVoidReturn) {
        Type *t = caller->getType();
        if (t != Type::getVoidTy(i->getContext())) {
          // may need to do coercion due to bitcasts
          Expr::Width from = result->getWidth();
          Expr::Width to = getWidthForLLVMType(t);
            
          if (from != to) {
            CallSite cs = (isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) : 
                           CallSite(cast<CallInst>(caller)));

            // XXX need to check other param attrs ?
      bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
            if (isSExt) {
              result = SExtExpr::create(result, to);
            } else {
              result = ZExtExpr::create(result, to);
            }
          }

          bindLocal(kcaller, state, result);
        }
      } else {
        // We check that the return value has no users instead of
        // checking the type, since C defaults to returning int for
        // undeclared functions.
        if (!caller->use_empty()) {
          terminateStateOnExecError(state, "return void when caller expected a result");
        }
      }
    }
   
    
    break;
  }
  case Instruction::Br: {

    printf("Hit br inst \n");
    BranchInst *bi = cast<BranchInst>(i);
    if (bi->isUnconditional()) {
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    } else {
      // FIXME: Find a way that we don't have this hidden dependency.
      assert(bi->getCondition() == bi->getOperand(0) &&
             "Wrong operand index!");
      ref<Expr> cond = eval(ki, 0, state).value;
      //cond->dump();

      //ABH: Within TASE, we're unix forking within Executor::fork
      // when a branch instruction depends on symbolic data.  Currently
      // only ever returning 1 state in "branches" becuase of this.
      Executor::StatePair branches = fork(state, cond, false);

      // NOTE: There is a hidden dependency here, markBranchVisited
      // requires that we still be in the context of the branch
      // instruction (it reuses its statistic id). Should be cleaned
      // up with convenient instruction specific data.
      if (statsTracker && state.stack.back().kf->trackCoverage)
        statsTracker->markBranchVisited(branches.first, branches.second);

      if (branches.second) {
        transferToBasicBlock(bi->getSuccessor(1), bi->getParent(), *branches.second);	
      }
      if (branches.first) {
        transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), *branches.first);
      }
            
    }
    break;
  }
  case Instruction::Switch: {
    SwitchInst *si = cast<SwitchInst>(i);
    ref<Expr> cond = eval(ki, 0, state).value;
    BasicBlock *bb = si->getParent();

    cond = toUnique(state, cond);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
      // Somewhat gross to create these all the time, but fine till we
      // switch to an internal rep.
      llvm::IntegerType *Ty = cast<IntegerType>(si->getCondition()->getType());
      ConstantInt *ci = ConstantInt::get(Ty, CE->getZExtValue());
      unsigned index = si->findCaseValue(ci).getSuccessorIndex();
      transferToBasicBlock(si->getSuccessor(index), si->getParent(), state);
    } else {
      // Handle possible different branch targets

      // We have the following assumptions:
      // - each case value is mutual exclusive to all other values including the
      //   default value
      // - order of case branches is based on the order of the expressions of
      //   the scase values, still default is handled last
      std::vector<BasicBlock *> bbOrder;
      std::map<BasicBlock *, ref<Expr> > branchTargets;

      std::map<ref<Expr>, BasicBlock *> expressionOrder;

      // Iterate through all non-default cases and order them by expressions
      for (SwitchInst::CaseIt i = si->case_begin(), e = si->case_end(); i != e;
           ++i) {
        ref<Expr> value = evalConstant(i.getCaseValue());

        BasicBlock *caseSuccessor = i.getCaseSuccessor();
        expressionOrder.insert(std::make_pair(value, caseSuccessor));
      }

      // Track default branch values
      ref<Expr> defaultValue = ConstantExpr::alloc(1, Expr::Bool);

      // iterate through all non-default cases but in order of the expressions
      for (std::map<ref<Expr>, BasicBlock *>::iterator
               it = expressionOrder.begin(),
               itE = expressionOrder.end();
           it != itE; ++it) {
        ref<Expr> match = EqExpr::create(cond, it->first);

        // Make sure that the default value does not contain this target's value
        defaultValue = AndExpr::create(defaultValue, Expr::createIsZero(match));

        // Check if control flow could take this case
        bool result;
        bool success = solver->mayBeTrue(state, match, result);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (result) {
          BasicBlock *caseSuccessor = it->second;

          // Handle the case that a basic block might be the target of multiple
          // switch cases.
          // Currently we generate an expression containing all switch-case
          // values for the same target basic block. We spare us forking too
          // many times but we generate more complex condition expressions
          // TODO Add option to allow to choose between those behaviors
          std::pair<std::map<BasicBlock *, ref<Expr> >::iterator, bool> res =
              branchTargets.insert(std::make_pair(
                  caseSuccessor, ConstantExpr::alloc(0, Expr::Bool)));

          res.first->second = OrExpr::create(match, res.first->second);

          // Only add basic blocks which have not been target of a branch yet
          if (res.second) {
            bbOrder.push_back(caseSuccessor);
          }
        }
      }

      // Check if control could take the default case
      bool res;
      bool success = solver->mayBeTrue(state, defaultValue, res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res) {
        std::pair<std::map<BasicBlock *, ref<Expr> >::iterator, bool> ret =
            branchTargets.insert(
                std::make_pair(si->getDefaultDest(), defaultValue));
        if (ret.second) {
          bbOrder.push_back(si->getDefaultDest());
        }
      }

      // Fork the current state with each state having one of the possible
      // successors of this switch
      std::vector< ref<Expr> > conditions;
      for (std::vector<BasicBlock *>::iterator it = bbOrder.begin(),
                                               ie = bbOrder.end();
           it != ie; ++it) {
        conditions.push_back(branchTargets[*it]);
      }
      std::vector<ExecutionState*> branches;
      branch(state, conditions, branches);

      std::vector<ExecutionState*>::iterator bit = branches.begin();
      for (std::vector<BasicBlock *>::iterator it = bbOrder.begin(),
                                               ie = bbOrder.end();
           it != ie; ++it) {
        ExecutionState *es = *bit;
        if (es)
          transferToBasicBlock(*it, bb, *es);
        ++bit;
      }
    }
    break;
 }
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call: {
    CallSite cs(i);

    unsigned numArgs = cs.arg_size();
    Value *fp = cs.getCalledValue();
    Function *f = getTargetFunction(fp, state);

    // Skip debug intrinsics, we can't evaluate their metadata arguments.
    if (f && isDebugIntrinsic(f, kmodule))
      break;

    if (isa<InlineAsm>(fp)) {
      terminateStateOnExecError(state, "inline assembly is unsupported");
      break;
    }
    // evaluate arguments
    std::vector< ref<Expr> > arguments;
    arguments.reserve(numArgs);

    for (unsigned j=0; j<numArgs; ++j)
      arguments.push_back(eval(ki, j+1, state).value);

    if (f) {
      const FunctionType *fType = 
        dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
      const FunctionType *fpType =
        dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());

      // special case the call with a bitcast case
      if (fType != fpType) {
        assert(fType && fpType && "unable to get function type");

        // XXX check result coercion

        // XXX this really needs thought and validation
        unsigned i=0;
        for (std::vector< ref<Expr> >::iterator
               ai = arguments.begin(), ie = arguments.end();
             ai != ie; ++ai) {
          Expr::Width to, from = (*ai)->getWidth();
            
          if (i<fType->getNumParams()) {
            to = getWidthForLLVMType(fType->getParamType(i));

            if (from != to) {
              // XXX need to check other param attrs ?
              bool isSExt = cs.paramHasAttr(i+1, llvm::Attribute::SExt);
              if (isSExt) {
                arguments[i] = SExtExpr::create(arguments[i], to);
              } else {
                arguments[i] = ZExtExpr::create(arguments[i], to);
              }
            }
          }
            
          i++;
        }
      }

      executeCall(state, ki, f, arguments);
    } else {
      ref<Expr> v = eval(ki, 0, state).value;

      ExecutionState *free = &state;
      bool hasInvalid = false, first = true;

      /* XXX This is wasteful, no need to do a full evaluate since we
         have already got a value. But in the end the caches should
         handle it for us, albeit with some overhead. */
      do {
        ref<ConstantExpr> value;
        bool success = solver->getValue(*free, v, value);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        StatePair res = fork(*free, EqExpr::create(v, value), true);
        if (res.first) {
          uint64_t addr = value->getZExtValue();
          if (legalFunctions.count(addr)) {
            f = (Function*) addr;

            // Don't give warning on unique resolution
            if (res.second || !first)
              klee_warning_once((void*) (unsigned long) addr, 
                                "resolved symbolic function pointer to: %s",
                                f->getName().data());

            executeCall(*res.first, ki, f, arguments);
          } else {
            if (!hasInvalid) {
              terminateStateOnExecError(state, "invalid function pointer");
              hasInvalid = true;
            }
          }
        }

        first = false;
        free = res.second;
      } while (free);
    }
    break;
  }
  case Instruction::PHI: {
    ref<Expr> result = eval(ki, state.incomingBBIndex, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Special instructions
  case Instruction::Select: {
    // NOTE: It is not required that operands 1 and 2 be of scalar type.
    ref<Expr> cond = eval(ki, 0, state).value;
    ref<Expr> tExpr = eval(ki, 1, state).value;
    ref<Expr> fExpr = eval(ki, 2, state).value;
    ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::VAArg:
    terminateStateOnExecError(state, "unexpected VAArg instruction");
    break;

    // Arithmetic / logical

  case Instruction::Add: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, AddExpr::create(left, right));
    break;
  }

  case Instruction::Sub: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, SubExpr::create(left, right));
    break;
  }
 
  case Instruction::Mul: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, MulExpr::create(left, right));
    break;
  }

  case Instruction::UDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = UDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::URem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = URemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SRem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SRemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::And: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AndExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Or: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = OrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Xor: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = XorExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Shl: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = ShlExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::LShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = LShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::AShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

    // Compare

  case Instruction::ICmp: {
    CmpInst *ci = cast<CmpInst>(i);
    ICmpInst *ii = cast<ICmpInst>(ci);

    switch(ii->getPredicate()) {
    case ICmpInst::ICMP_EQ: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = EqExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_NE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = NeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_UGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgtExpr::create(left, right);
      bindLocal(ki, state,result);
      break;
    }

    case ICmpInst::ICMP_UGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgtExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    default:
      terminateStateOnExecError(state, "invalid ICmp predicate");
    }
    break;
  }
 
    // Memory instructions...
  case Instruction::Alloca: {
    AllocaInst *ai = cast<AllocaInst>(i);
    unsigned elementSize = 
      kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
    ref<Expr> size = Expr::createPointer(elementSize);
    if (ai->isArrayAllocation()) {
      ref<Expr> count = eval(ki, 0, state).value;
      count = Expr::createZExtToPointerWidth(count);
      size = MulExpr::create(size, count);
    }
    executeAlloc(state, size, true, ki);
    break;
  }

  case Instruction::Load: {
    ref<Expr> base = eval(ki, 0, state).value;
    executeMemoryOperation(state, false, base, 0, ki);

    break;
  }
  case Instruction::Store: {
    ref<Expr> base = eval(ki, 1, state).value;
    ref<Expr> value = eval(ki, 0, state).value;
    //base->dump();
    executeMemoryOperation(state, true, base, value, 0);
    break;
  }

  case Instruction::GetElementPtr: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
    ref<Expr> base = eval(ki, 0, state).value;

    for (std::vector< std::pair<unsigned, uint64_t> >::iterator 
           it = kgepi->indices.begin(), ie = kgepi->indices.end(); 
         it != ie; ++it) {
      uint64_t elementSize = it->second;
      ref<Expr> index = eval(ki, it->first, state).value;
      base = AddExpr::create(base,
                             MulExpr::create(Expr::createSExtToPointerWidth(index),
                                             Expr::createPointer(elementSize)));
    }
    if (kgepi->offset)
      base = AddExpr::create(base,
                             Expr::createPointer(kgepi->offset));
    bindLocal(ki, state, base);
    break;
  }

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value,
                                           0,
                                           getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::SExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width pType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, pType));
    break;
  }
  case Instruction::PtrToInt: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width iType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, iType));
    break;
  }

  case Instruction::BitCast: {
    ref<Expr> result = eval(ki, 0, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Floating point instructions

  case Instruction::FAdd: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FAdd operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.add(APFloat(*fpWidthToSemantics(right->getWidth()),right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FSub: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FSub operation");
    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.subtract(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FMul: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FMul operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.multiply(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FDiv: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FDiv operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.divide(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FRem: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FRem operation");
    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.mod(APFloat(*fpWidthToSemantics(right->getWidth()),right->getAPValue()),
            APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FPTrunc: {
    FPTruncInst *fi = cast<FPTruncInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > arg->getWidth())
      return terminateStateOnExecError(state, "Unsupported FPTrunc operation");

    llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPExt: {
    FPExtInst *fi = cast<FPExtInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || arg->getWidth() > resultType)
      return terminateStateOnExecError(state, "Unsupported FPExt operation");
    llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPToUI: {
    FPToUIInst *fi = cast<FPToUIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToUI operation");

    llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::FPToSI: {
    FPToSIInst *fi = cast<FPToSIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToSI operation");
    llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());

    uint64_t value = 0;
    bool isExact = true;
    Arg.convertToInteger(&value, resultType, true,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::UIToFP: {
    UIToFPInst *fi = cast<UIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported UIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), false,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::SIToFP: {
    SIToFPInst *fi = cast<SIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported SIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), true,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::FCmp: {
    FCmpInst *fi = cast<FCmpInst>(i);
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FCmp operation");

    APFloat LHS(*fpWidthToSemantics(left->getWidth()),left->getAPValue());
    APFloat RHS(*fpWidthToSemantics(right->getWidth()),right->getAPValue());
    APFloat::cmpResult CmpRes = LHS.compare(RHS);

    bool Result = false;
    switch( fi->getPredicate() ) {
      // Predicates which only care about whether or not the operands are NaNs.
    case FCmpInst::FCMP_ORD:
      Result = CmpRes != APFloat::cmpUnordered;
      break;

    case FCmpInst::FCMP_UNO:
      Result = CmpRes == APFloat::cmpUnordered;
      break;

      // Ordered comparisons return false if either operand is NaN.  Unordered
      // comparisons return true if either operand is NaN.
    case FCmpInst::FCMP_UEQ:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OEQ:
      Result = CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_UGT:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OGT:
      Result = CmpRes == APFloat::cmpGreaterThan;
      break;

    case FCmpInst::FCMP_UGE:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OGE:
      Result = CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_ULT:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OLT:
      Result = CmpRes == APFloat::cmpLessThan;
      break;

    case FCmpInst::FCMP_ULE:
      if (CmpRes == APFloat::cmpUnordered) {
        Result = true;
        break;
      }
    case FCmpInst::FCMP_OLE:
      Result = CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
      break;

    case FCmpInst::FCMP_UNE:
      Result = CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
      break;
    case FCmpInst::FCMP_ONE:
      Result = CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
      break;

    default:
      assert(0 && "Invalid FCMP predicate!");
    case FCmpInst::FCMP_FALSE:
      Result = false;
      break;
    case FCmpInst::FCMP_TRUE:
      Result = true;
      break;
    }

    bindLocal(ki, state, ConstantExpr::alloc(Result, Expr::Bool));
    break;
  }
  case Instruction::InsertValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;
    ref<Expr> val = eval(ki, 1, state).value;

    ref<Expr> l = NULL, r = NULL;
    unsigned lOffset = kgepi->offset*8, rOffset = kgepi->offset*8 + val->getWidth();

    if (lOffset > 0)
      l = ExtractExpr::create(agg, 0, lOffset);
    if (rOffset < agg->getWidth())
      r = ExtractExpr::create(agg, rOffset, agg->getWidth() - rOffset);

    ref<Expr> result;
    if (!l.isNull() && !r.isNull())
      result = ConcatExpr::create(r, ConcatExpr::create(val, l));
    else if (!l.isNull())
      result = ConcatExpr::create(val, l);
    else if (!r.isNull())
      result = ConcatExpr::create(r, val);
    else
      result = val;

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ExtractValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;

    ref<Expr> result = ExtractExpr::create(agg, kgepi->offset*8, getWidthForLLVMType(i->getType()));

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::Fence: {
    // Ignore for now
    break;
  }
  case Instruction::InsertElement: {
    InsertElementInst *iei = cast<InsertElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> newElt = eval(ki, 1, state).value;
    ref<Expr> idx = eval(ki, 2, state).value;
    
    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnError(
          state, "InsertElement, support for symbolic index not implemented",
          Unhandled);
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
    const llvm::VectorType *vt = iei->getType();
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    //printf("\n Calling InsertElement at idx %lu \n", iIdx);

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds write
      terminateStateOnError(state, "Out of bounds write when inserting element",
                            BadVectorAccess);
      return;
    }

    const unsigned elementCount = vt->getNumElements();
    llvm::SmallVector<ref<Expr>, 8> elems;
    elems.reserve(elementCount);
    for (unsigned i = 0; i < elementCount; ++i) {
      // evalConstant() will use ConcatExpr to build vectors with the
      // zero-th element leftmost (most significant bits), followed
      // by the next element (second leftmost) and so on. This means
      // that we have to adjust the index so we read left to right
      // rather than right to left.
      unsigned bitOffset = EltBits * (elementCount - i - 1);
      //printf("bitOffset is %u \n\n",bitOffset);
      if (i == iIdx) {
	//printf("Found insert index at %u \n\n", bitOffset);
      }
      elems.push_back(i == iIdx ? newElt
                                : ExtractExpr::create(vec, bitOffset, EltBits));
    }

    ref<Expr> Result = ConcatExpr::createN(elementCount, elems.data());
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ExtractElement: {
    ExtractElementInst *eei = cast<ExtractElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> idx = eval(ki, 1, state).value;

    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnError(
          state, "ExtractElement, support for symbolic index not implemented",
          Unhandled);
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
    const llvm::VectorType *vt = eei->getVectorOperandType();
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds read
      terminateStateOnError(state, "Out of bounds read when extracting element",
                            BadVectorAccess);
      return;
    }

    // evalConstant() will use ConcatExpr to build vectors with the
    // zero-th element left most (most significant bits), followed
    // by the next element (second left most) and so on. This means
    // that we have to adjust the index so we read left to right
    // rather than right to left.
    unsigned bitOffset = EltBits*(vt->getNumElements() - iIdx -1);
    ref<Expr> Result = ExtractExpr::create(vec, bitOffset, EltBits);
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ShuffleVector:
    // Should never happen due to Scalarizer pass removing ShuffleVector
    // instructions.
    terminateStateOnExecError(state, "Unexpected ShuffleVector instruction");
    break;
  // Other instructions...
  // Unhandled
  default:
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }
}

void Executor::updateStates(ExecutionState *current) {
  if (searcher) {
    searcher->update(current, addedStates, removedStates);
    searcher->update(nullptr, continuedStates, pausedStates);
    pausedStates.clear();
    continuedStates.clear();
  }
  
  states.insert(addedStates.begin(), addedStates.end());
  addedStates.clear();

  for (std::vector<ExecutionState *>::iterator it = removedStates.begin(),
                                               ie = removedStates.end();
       it != ie; ++it) {
    ExecutionState *es = *it;
    std::set<ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);
    std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
      seedMap.find(es);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    processTree->remove(es->ptreeNode);
    delete es;
  }
  removedStates.clear();
}

template <typename TypeIt>
void Executor::computeOffsets(KGEPInstruction *kgepi, TypeIt ib, TypeIt ie) {
  ref<ConstantExpr> constantOffset =
    ConstantExpr::alloc(0, Context::get().getPointerWidth());
  uint64_t index = 1;
  for (TypeIt ii = ib; ii != ie; ++ii) {
    if (StructType *st = dyn_cast<StructType>(*ii)) {
      const StructLayout *sl = kmodule->targetData->getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      constantOffset = constantOffset->Add(ConstantExpr::alloc(addend,
                                                               Context::get().getPointerWidth()));
    } else {
      const SequentialType *set = cast<SequentialType>(*ii);
      uint64_t elementSize = 
        kmodule->targetData->getTypeStoreSize(set->getElementType());
      Value *operand = ii.getOperand();
      if (Constant *c = dyn_cast<Constant>(operand)) {
        ref<ConstantExpr> index = 
          evalConstant(c)->SExt(Context::get().getPointerWidth());
        ref<ConstantExpr> addend = 
          index->Mul(ConstantExpr::alloc(elementSize,
                                         Context::get().getPointerWidth()));
        constantOffset = constantOffset->Add(addend);
      } else {
        kgepi->indices.push_back(std::make_pair(index, elementSize));
      }
    }
    index++;
  }
  kgepi->offset = constantOffset->getZExtValue();
}

void Executor::bindInstructionConstants(KInstruction *KI) {
  KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);

  if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst)) {
    computeOffsets(kgepi, gep_type_begin(gepi), gep_type_end(gepi));
  } else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(KI->inst)) {
    computeOffsets(kgepi, iv_type_begin(ivi), iv_type_end(ivi));
    assert(kgepi->indices.empty() && "InsertValue constant offset expected");
  } else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(KI->inst)) {
    computeOffsets(kgepi, ev_type_begin(evi), ev_type_end(evi));
    assert(kgepi->indices.empty() && "ExtractValue constant offset expected");
  }
}

void Executor::bindModuleConstants() {
  //printf("Binding module constants... \n");
  for (std::vector<KFunction*>::iterator it = kmodule->functions.begin(), 
         ie = kmodule->functions.end(); it != ie; ++it) {
    KFunction *kf = *it;
    //printf();
    for (unsigned i=0; i<kf->numInstructions; ++i)
      bindInstructionConstants(kf->instructions[i]);
  }
  
  //printf("Evaluating constants ... \n");
  //printf("kmodule has %d constants \n", kmodule->constants.size());
  kmodule->constantTable = new Cell[kmodule->constants.size()];
  for (unsigned i=0; i<kmodule->constants.size(); ++i) {
    //printf("Evaluating constant %d \n", i);
    
    Cell &c = kmodule->constantTable[i];
    // assert(c);
    assert(kmodule->constants[i]);
    c.value = evalConstant(kmodule->constants[i]);
  }
}

void Executor::checkMemoryUsage() {
  if (!MaxMemory)
    return;
  if ((stats::instructions & 0xFFFF) == 0) {
    // We need to avoid calling GetTotalMallocUsage() often because it
    // is O(elts on freelist). This is really bad since we start
    // to pummel the freelist once we hit the memory cap.
    unsigned mbs = (util::GetTotalMallocUsage() >> 20) +
                   (memory->getUsedDeterministicSize() >> 20);

    if (mbs > MaxMemory) {
      if (mbs > MaxMemory + 100) {
        // just guess at how many to kill
        unsigned numStates = states.size();
        unsigned toKill = std::max(1U, numStates - numStates * MaxMemory / mbs);
        klee_warning("killing %d states (over memory cap)", toKill);
        std::vector<ExecutionState *> arr(states.begin(), states.end());
        for (unsigned i = 0, N = arr.size(); N && i < toKill; ++i, --N) {
          unsigned idx = rand() % N;
          // Make two pulls to try and not hit a state that
          // covered new code.
          if (arr[idx]->coveredNew)
            idx = rand() % N;

          std::swap(arr[idx], arr[N - 1]);
          terminateStateEarly(*arr[N - 1], "Memory limit exceeded.");
        }
      }
      atMemoryLimit = true;
    } else {
      atMemoryLimit = false;
    }
  }
}

void Executor::doDumpStates() {
  if (!DumpStatesOnHalt || states.empty())
    return;
  klee_message("halting execution, dumping remaining states");
  for (std::set<ExecutionState *>::iterator it = states.begin(),
                                            ie = states.end();
       it != ie; ++it) {
    ExecutionState &state = **it;
    stepInstruction(state); // keep stats rolling
    terminateStateEarly(state, "Execution halting.");
  }
  updateStates(0);
}

void Executor::run(ExecutionState  & initialState) {
  printf("ENTERING EXECUTOR::RUN \n");

  bindModuleConstants();

  //printf("Initializing timers... \n");
  // Delay init till now so that ticks don't accrue during
  // optimization and such.
  initTimers();
  //AH: Changed code to NOT insert states since we only have 1 state per process
  //printf("Inserting state... \n");
  //states.insert(&initialState);

  if (usingSeeds) {
    std::vector<SeedInfo> &v = seedMap[&initialState];
    
    for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(), 
           ie = usingSeeds->end(); it != ie; ++it)
      v.push_back(SeedInfo(*it));

    int lastNumSeeds = usingSeeds->size()+10;
    double lastTime, startTime = lastTime = util::getWallTime();
    ExecutionState *lastState = 0;
    while (!seedMap.empty()) {
      if (haltExecution) {
        doDumpStates();
        return;
      }

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
      updateStates(&state);

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

    if (OnlySeed) {
      doDumpStates();
      return;
    }
  }
  
  //printf("Constructing searcher... \n");
  //searcher = constructUserSearcher(*this);
  
  //AH: I removed the check for !states.empty() because we have 
  // only 1 execution state.

  //std::vector<ExecutionState *> newStates(states.begin(), states.end());
  //printf("Calling searcher-> update... \n");
  //searcher->update(0, newStates, std::vector<ExecutionState *>());
  //printf("Completed call to searcher update \n" );
  //while (!states.empty() && !haltExecution) {

  int stepInstCtr =0;
  
  ExecutionState & state = *GlobalExecutionStatePtr;
  while ( !haltExecution) {
    /*
    printf("Entering main execution loop ... \n");
    ExecutionState &state = searcher->selectState();
    KInstruction *ki = state.pc;
    printf("Calling stepInstruction ... \n");
    stepInstruction(state);

    printf("Calling execute instruction ... \n");
    executeInstruction(state, ki);
    processTimers(&state, MaxInstructionTime);

    checkMemoryUsage();

    updateStates(&state);
    */
    //printf("Entering main execution loop ... \n");
    //ExecutionState &state = searcher->selectState();
    KInstruction *ki = state.pc;
    stepInstCtr++;
    printf("Calling stepInstruction time %d ... \n", stepInstCtr);
    std::cout.flush();
    stepInstruction(state);

    //printf("Calling execute instruction ... \n");
    executeInstruction(state, ki);
    processTimers(&state, MaxInstructionTime);

    //checkMemoryUsage();

    //updateStates(&state);
    
  }
  
  printf("Finished main instruction interpretation loop \n ");

  //delete searcher;
  //searcher = 0;

  //doDumpStates();
}

std::string Executor::getAddressInfo(ExecutionState &state, 
                                     ref<Expr> address) const{
  std::string Str;
  llvm::raw_string_ostream info(Str);
  info << "\taddress: " << address << "\n";
  uint64_t example;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    example = CE->getZExtValue();
  } else {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, address, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    example = value->getZExtValue();
    info << "\texample: " << example << "\n";
    std::pair< ref<Expr>, ref<Expr> > res = solver->getRange(state, address);
    info << "\trange: [" << res.first << ", " << res.second <<"]\n";
  }
  
  MemoryObject hack((unsigned) example);    
  MemoryMap::iterator lower = state.addressSpace.objects.upper_bound(&hack);
  info << "\tnext: ";
  if (lower==state.addressSpace.objects.end()) {
    info << "none\n";
  } else {
    const MemoryObject *mo = lower->first;
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    info << "object at " << mo->address
         << " of size " << mo->size << "\n"
         << "\t\t" << alloc_info << "\n";
  }
  if (lower!=state.addressSpace.objects.begin()) {
    --lower;
    info << "\tprev: ";
    if (lower==state.addressSpace.objects.end()) {
      info << "none\n";
    } else {
      const MemoryObject *mo = lower->first;
      std::string alloc_info;
      mo->getAllocInfo(alloc_info);
      info << "object at " << mo->address 
           << " of size " << mo->size << "\n"
           << "\t\t" << alloc_info << "\n";
    }
  }

  return info.str();
}

void Executor::pauseState(ExecutionState &state){
  auto it = std::find(continuedStates.begin(), continuedStates.end(), &state);
  // If the state was to be continued, but now gets paused again
  if (it != continuedStates.end()){
    // ...just don't continue it
    std::swap(*it, continuedStates.back());
    continuedStates.pop_back();
  } else {
    pausedStates.push_back(&state);
  }
}

void Executor::continueState(ExecutionState &state){
  auto it = std::find(pausedStates.begin(), pausedStates.end(), &state);
  // If the state was to be paused, but now gets continued again
  if (it != pausedStates.end()){
    // ...don't pause it
    std::swap(*it, pausedStates.back());
    pausedStates.pop_back();
  } else {
    continuedStates.push_back(&state);
  }
}

void Executor::terminateState(ExecutionState &state) {
  if (replayKTest && replayPosition!=replayKTest->numObjects) {
    klee_warning_once(replayKTest,
                      "replay did not consume all objects in test input.");
  }

  interpreterHandler->incPathsExplored();

  std::vector<ExecutionState *>::iterator it =
      std::find(addedStates.begin(), addedStates.end(), &state);
  if (it==addedStates.end()) {
    state.pc = state.prevPC;

    removedStates.push_back(&state);
  } else {
    // never reached searcher, just delete immediately
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
      seedMap.find(&state);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    addedStates.erase(it);
    processTree->remove(state.ptreeNode);
    printf(" WOULD NORMALL DELETE STATE HERE \n \n \n \n ");
    delete &state;
  }
}

void Executor::terminateStateEarly(ExecutionState &state, 
                                   const Twine &message) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew ||
      (AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(state, (message + "\n").str().c_str(),
                                        "early");
  terminateState(state);
}

void Executor::terminateStateOnExit(ExecutionState &state) {
  if (!OnlyOutputStatesCoveringNew || state.coveredNew || 
      (AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(state, 0, 0);
  terminateState(state);
}

const InstructionInfo & Executor::getLastNonKleeInternalInstruction(const ExecutionState &state,
    Instruction ** lastInstruction) {
  // unroll the stack of the applications state and find
  // the last instruction which is not inside a KLEE internal function
  ExecutionState::stack_ty::const_reverse_iterator it = state.stack.rbegin(),
      itE = state.stack.rend();

  // don't check beyond the outermost function (i.e. main())
  itE--;

  const InstructionInfo * ii = 0;
  if (kmodule->internalFunctions.count(it->kf->function) == 0){
    ii =  state.prevPC->info;
    *lastInstruction = state.prevPC->inst;
    //  Cannot return yet because even though
    //  it->function is not an internal function it might of
    //  been called from an internal function.
  }

  // Wind up the stack and check if we are in a KLEE internal function.
  // We visit the entire stack because we want to return a CallInstruction
  // that was not reached via any KLEE internal functions.
  for (;it != itE; ++it) {
    // check calling instruction and if it is contained in a KLEE internal function
    const Function * f = (*it->caller).inst->getParent()->getParent();
    if (kmodule->internalFunctions.count(f)){
      ii = 0;
      continue;
    }
    if (!ii){
      ii = (*it->caller).info;
      *lastInstruction = (*it->caller).inst;
    }
  }

  if (!ii) {
    // something went wrong, play safe and return the current instruction info
    *lastInstruction = state.prevPC->inst;
    return *state.prevPC->info;
  }
  return *ii;
}

bool Executor::shouldExitOn(enum TerminateReason termReason) {
  std::vector<TerminateReason>::iterator s = ExitOnErrorType.begin();
  std::vector<TerminateReason>::iterator e = ExitOnErrorType.end();

  for (; s != e; ++s)
    if (termReason == *s)
      return true;

  return false;
}

void Executor::terminateStateOnError(ExecutionState &state,
                                     const llvm::Twine &messaget,
                                     enum TerminateReason termReason,
                                     const char *suffix,
                                     const llvm::Twine &info) {
  std::string message = messaget.str();
  static std::set< std::pair<Instruction*, std::string> > emittedErrors;
  Instruction * lastInst;
  const InstructionInfo &ii = getLastNonKleeInternalInstruction(state, &lastInst);
  
  if (EmitAllErrors ||
      emittedErrors.insert(std::make_pair(lastInst, message)).second) {
    if (ii.file != "") {
      klee_message("ERROR: %s:%d: %s", ii.file.c_str(), ii.line, message.c_str());
    } else {
      klee_message("ERROR: (location information missing) %s", message.c_str());
    }
    if (!EmitAllErrors)
      klee_message("NOTE: now ignoring this error at this location");

    std::string MsgString;
    llvm::raw_string_ostream msg(MsgString);
    msg << "Error: " << message << "\n";
    if (ii.file != "") {
      msg << "File: " << ii.file << "\n";
      msg << "Line: " << ii.line << "\n";
      msg << "assembly.ll line: " << ii.assemblyLine << "\n";
    }
    msg << "Stack: \n";
    state.dumpStack(msg);

    std::string info_str = info.str();
    if (info_str != "")
      msg << "Info: \n" << info_str;

    std::string suffix_buf;
    if (!suffix) {
      suffix_buf = TerminateReasonNames[termReason];
      suffix_buf += ".err";
      suffix = suffix_buf.c_str();
    }

    interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);
  }
    
  terminateState(state);

  if (shouldExitOn(termReason))
    haltExecution = true;
}

// XXX shoot me
static const char *okExternalsList[] = { "printf", 
                                         "fprintf", 
                                         "puts",
                                         "getpid" };
static std::set<std::string> okExternals(okExternalsList,
                                         okExternalsList + 
                                         (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void Executor::callExternalFunction(ExecutionState &state,
                                    KInstruction *target,
                                    Function *function,
                                    std::vector< ref<Expr> > &arguments) {
  // check if specialFunctionHandler wants it
  if (specialFunctionHandler->handle(state, function, target, arguments))
    return;
  
  if (NoExternals && !okExternals.count(function->getName())) {
    klee_warning("Disallowed call to external function: %s\n",
               function->getName().str().c_str());
    terminateStateOnError(state, "externals disallowed", User);
    return;
  }

  // normal external function handling path
  // allocate 128 bits for each argument (+return value) to support fp80's;
  // we could iterate through all the arguments first and determine the exact
  // size we need, but this is faster, and the memory usage isn't significant.
  uint64_t *args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
  memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
  unsigned wordIndex = 2;
  for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), 
       ae = arguments.end(); ai!=ae; ++ai) {
    if (AllowExternalSymCalls) { // don't bother checking uniqueness
      ref<ConstantExpr> ce;
      bool success = solver->getValue(state, *ai, ce);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      ce->toMemory(&args[wordIndex]);
      wordIndex += (ce->getWidth()+63)/64;
    } else {
      ref<Expr> arg = toUnique(state, *ai);
      if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
        // XXX kick toMemory functions from here
        ce->toMemory(&args[wordIndex]);
        wordIndex += (ce->getWidth()+63)/64;
      } else {
        terminateStateOnExecError(state, 
                                  "external call with symbolic argument: " + 
                                  function->getName());
        return;
      }
    }
  }

  state.addressSpace.copyOutConcretes();

  if (!SuppressExternalWarnings) {

    std::string TmpStr;
    llvm::raw_string_ostream os(TmpStr);
    os << "calling external: " << function->getName().str() << "(";
    for (unsigned i=0; i<arguments.size(); i++) {
      os << arguments[i];
      if (i != arguments.size()-1)
	os << ", ";
    }
    os << ") at ";
    state.pc->printFileLine(os);
    
    if (AllExternalWarnings)
      klee_warning("%s", os.str().c_str());
    else
      klee_warning_once(function, "%s", os.str().c_str());
  }
  bool success = externalDispatcher->executeCall(function, target->inst, args);
  if (!success) {
    terminateStateOnError(state, "failed external call: " + function->getName(),
                          External);
    return;
  }

  if (!state.addressSpace.copyInConcretes()) {
    terminateStateOnError(state, "external modified read-only object",
                          External);
    return;
  }

  Type *resultType = target->inst->getType();
  if (resultType != Type::getVoidTy(function->getContext())) {
    ref<Expr> e = ConstantExpr::fromMemory((void*) args, 
                                           getWidthForLLVMType(resultType));
    bindLocal(target, state, e);
  }
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state, 
                                            ref<Expr> e) {
  unsigned n = interpreterOpts.MakeConcreteSymbolic;
  if (!n || replayKTest || replayPath)
    return e;

  // right now, we don't replace symbolics (is there any reason to?)
  if (!isa<ConstantExpr>(e))
    return e;

  if (n != 1 && random() % n)
    return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.
  
  static unsigned id;
  const Array *array =
      arrayCache.CreateArray("rrws_arr" + llvm::utostr(++id),
                             Expr::getMinBytesForWidth(e->getWidth()));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  llvm::errs() << "Making symbolic: " << eq << "\n";
  state.addConstraint(eq);
  return res;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state, 
                                         const MemoryObject *mo,
                                         bool isLocal,
                                         const Array *array) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);
  state.addressSpace.bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal)
    state.stack.back().allocas.push_back(mo);

  return os;
}

void Executor::executeAlloc(ExecutionState &state,
                            ref<Expr> size,
                            bool isLocal,
                            KInstruction *target,
                            bool zeroMemory,
                            const ObjectState *reallocFrom) {
  size = toUnique(state, size);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
    const llvm::Value *allocSite = state.prevPC->inst;
    size_t allocationAlignment = getAllocationAlignment(allocSite);
    MemoryObject *mo =
        memory->allocate(CE->getZExtValue(), isLocal, /*isGlobal=*/false,
                         allocSite, allocationAlignment);
    if (!mo) {
      bindLocal(target, state, 
                ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      ObjectState *os = bindObjectInState(state, mo, isLocal);
      if (zeroMemory) {
        os->initializeToZero();
      } else {
        os->initializeToRandom();
      }
      bindLocal(target, state, mo->getBaseExpr());
      
      if (reallocFrom) {
        unsigned count = std::min(reallocFrom->size, os->size);
        for (unsigned i=0; i<count; i++)
          os->write(i, reallocFrom->read8(i));
        state.addressSpace.unbindObject(reallocFrom->getObject());
      }
    }
  } else {
    // XXX For now we just pick a size. Ideally we would support
    // symbolic sizes fully but even if we don't it would be better to
    // "smartly" pick a value, for example we could fork and pick the
    // min and max values and perhaps some intermediate (reasonable
    // value).
    // 
    // It would also be nice to recognize the case when size has
    // exactly two values and just fork (but we need to get rid of
    // return argument first). This shows up in pcre when llvm
    // collapses the size expression with a select.

    ref<ConstantExpr> example;
    bool success = solver->getValue(state, size, example);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    
    // Try and start with a small example.
    Expr::Width W = example->getWidth();
    while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
      ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
      bool res;
      bool success = solver->mayBeTrue(state, EqExpr::create(tmp, size), res);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (!res)
        break;
      example = tmp;
    }

    StatePair fixedSize = fork(state, EqExpr::create(example, size), true);
    
    if (fixedSize.second) { 
      // Check for exactly two values
      ref<ConstantExpr> tmp;
      bool success = solver->getValue(*fixedSize.second, size, tmp);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      bool res;
      success = solver->mustBeTrue(*fixedSize.second, 
                                   EqExpr::create(tmp, size),
                                   res);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (res) {
        executeAlloc(*fixedSize.second, tmp, isLocal,
                     target, zeroMemory, reallocFrom);
      } else {
        // See if a *really* big value is possible. If so assume
        // malloc will fail for it, so lets fork and return 0.
        StatePair hugeSize = 
          fork(*fixedSize.second, 
               UltExpr::create(ConstantExpr::alloc(1U<<31, W), size),
               true);
        if (hugeSize.first) {
          klee_message("NOTE: found huge malloc, returning 0");
          bindLocal(target, *hugeSize.first, 
                    ConstantExpr::alloc(0, Context::get().getPointerWidth()));
        }
        
        if (hugeSize.second) {

          std::string Str;
          llvm::raw_string_ostream info(Str);
          ExprPPrinter::printOne(info, "  size expr", size);
          info << "  concretization : " << example << "\n";
          info << "  unbound example: " << tmp << "\n";
          terminateStateOnError(*hugeSize.second, "concretized symbolic size",
                                Model, NULL, info.str());
        }
      }
    }

    if (fixedSize.first) // can be zero when fork fails
      executeAlloc(*fixedSize.first, example, isLocal, 
                   target, zeroMemory, reallocFrom);
  }
}

void Executor::executeFree(ExecutionState &state,
                           ref<Expr> address,
                           KInstruction *target) {
  StatePair zeroPointer = fork(state, Expr::createIsZero(address), true);
  if (zeroPointer.first) {
    if (target)
      bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
  }
  if (zeroPointer.second) { // address != 0
    ExactResolutionList rl;
    resolveExact(*zeroPointer.second, address, rl, "free");
    
    for (Executor::ExactResolutionList::iterator it = rl.begin(), 
           ie = rl.end(); it != ie; ++it) {
      const MemoryObject *mo = it->first.first;
      if (mo->isLocal) {
        terminateStateOnError(*it->second, "free of alloca", Free, NULL,
                              getAddressInfo(*it->second, address));
      } else if (mo->isGlobal) {
        terminateStateOnError(*it->second, "free of global", Free, NULL,
                              getAddressInfo(*it->second, address));
      } else {
        it->second->addressSpace.unbindObject(mo);
        if (target)
          bindLocal(target, *it->second, Expr::createPointer(0));
      }
    }
  }
}

void Executor::resolveExact(ExecutionState &state,
                            ref<Expr> p,
                            ExactResolutionList &results, 
                            const std::string &name) {
  // XXX we may want to be capping this?
  ResolutionList rl;
  state.addressSpace.resolve(state, solver, p, rl);
  
  ExecutionState *unbound = &state;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); 
       it != ie; ++it) {
    ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());
    
    StatePair branches = fork(*unbound, inBounds, true);
    
    if (branches.first)
      results.push_back(std::make_pair(*it, branches.first));

    unbound = branches.second;
    if (!unbound) // Fork failure
      break;
  }

  if (unbound) {
    terminateStateOnError(*unbound, "memory error: invalid pointer: " + name,
                          Ptr, NULL, getAddressInfo(*unbound, p));
  }
}

void Executor::executeMemoryOperation(ExecutionState &state,
                                      bool isWrite,
                                      ref<Expr> address,
                                      ref<Expr> value /* undef if read */,
                                      KInstruction *target /* undef if write */) {
  Expr::Width type = (isWrite ? value->getWidth() : 
                     getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);

  if (SimplifySymIndices) {
    if (!isa<ConstantExpr>(address))
      address = state.constraints.simplifyExpr(address);
    if (isWrite && !isa<ConstantExpr>(value))
      value = state.constraints.simplifyExpr(value);
  }

  // fast path: single in-bounds resolution
  ObjectPair op;
  bool success;
  solver->setTimeout(coreSolverTimeout);
  //printf("Calling resolveOne \n");
  if (!state.addressSpace.resolveOne(state, solver, address, op, success)) {
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(0);

  if (success) {
    const MemoryObject *mo = op.first;

    if (MaxSymArraySize && mo->size>=MaxSymArraySize) {
      address = toConstant(state, address, "max-sym-array-size");
    }
    
    ref<Expr> offset = mo->getOffsetExpr(address);

    bool inBounds;
    solver->setTimeout(coreSolverTimeout);
    bool success = solver->mustBeTrue(state, 
                                      mo->getBoundsCheckOffset(offset, bytes),
                                      inBounds);
    solver->setTimeout(0);
    if (!success) {
      state.pc = state.prevPC;
      terminateStateEarly(state, "Query timed out (bounds check).");
      return;
    }
    
    if (inBounds) {
      const ObjectState *os = op.second;
      if (isWrite) {
        if (os->readOnly) {
          terminateStateOnError(state, "memory error: object read only",
                                ReadOnly);
        } else {
	  //printf("Trying to write to MO representing buffer starting at %lu, hex 0x%p \n ", (uint64_t) mo->address, (void *) mo->address);
          ObjectState *wos = state.addressSpace.getWriteable(mo, os);
          wos->write(offset, value);
        }          
      } else {
	ref<Expr> result = os->read(offset, type);
	
	if (interpreterOpts.MakeConcreteSymbolic)
	  result = replaceReadWithSymbolic(state, result);
	
	bindLocal(target, state, result);	
      }
      
      return;
    }
  } 
  
  // we are on an error path (no resolution, multiple resolution, one
  // resolution with out of bounds)
  
  ResolutionList rl;  
  solver->setTimeout(coreSolverTimeout);
  bool incomplete = state.addressSpace.resolve(state, solver, address, rl,
                                               0, coreSolverTimeout);
  solver->setTimeout(0);
  
  // XXX there is some query wasteage here. who cares?
  ExecutionState *unbound = &state;
  
  for (ResolutionList::iterator i = rl.begin(), ie = rl.end(); i != ie; ++i) {
    const MemoryObject *mo = i->first;
    const ObjectState *os = i->second;
    ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);
    
    StatePair branches = fork(*unbound, inBounds, true);
    ExecutionState *bound = branches.first;

    // bound can be 0 on failure or overlapped 
    if (bound) {
      if (isWrite) {
        if (os->readOnly) {
          terminateStateOnError(*bound, "memory error: object read only",
                                ReadOnly);
        } else {
          ObjectState *wos = bound->addressSpace.getWriteable(mo, os);
          wos->write(mo->getOffsetExpr(address), value);
        }
      } else {
        ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
        bindLocal(target, *bound, result);
      }
    }

    unbound = branches.second;
    if (!unbound)
      break;
  }
  
  // XXX should we distinguish out of bounds and overlapped cases?
  if (unbound) {
    if (incomplete) {
      terminateStateEarly(*unbound, "Query timed out (resolve).");
    } else {
      terminateStateOnError(*unbound, "memory error: out of bound pointer", Ptr,
                            NULL, getAddressInfo(*unbound, address));
    }
  }
}

//Todo: Recheck ALL the AH addition code one more time before you rip out the orig code beneath it.
void Executor::executeMakeSymbolic(ExecutionState &state, 
                                   const MemoryObject *mo,
                                   const std::string &name) {
  
  //AH Addition: ------------------------
  
  bool unnamed = (name == "unnamed") ? true : false;
  std::vector<unsigned char> *bindings = NULL;

  std::string array_name = name;
  
  const klee::Array * array = NULL;
  if (!unnamed)
    array = multipassInfo.currMultipassAssignment.getArray(array_name);

  bool multipass = false;
  if (array != NULL) {
    multipass = true;
  } else {
    if (!unnamed) {
    } //Todo: handle array creation with a unique name for unnamed arrays.
    array = arrayCache.CreateArray(array_name, mo->size);
  }

  bindObjectInState(state, mo, false, array);

  if (multipassInfo.passCount > 0 && multipass) {
    bindings = multipassInfo.currMultipassAssignment.getBindings(array_name);

    if (!bindings || bindings->size() != mo->size) {
      printf("ERROR: Multi-pass: Terminating state, bindings mismatch");
      std::exit(EXIT_FAILURE);
    } else {
      const klee::ObjectState *os = state.addressSpace.findObject(mo);
      klee::ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      assert(wos && "Writeable object is NULL!");
      unsigned idx = 0;

      for (std::vector<unsigned char>::iterator it = bindings->begin(), ie = bindings->end(); it != ie; ++it) {
	wos->write8(idx, *it);
	idx++;
      }
    }

  } else {
    state.addSymbolic(mo, array);
  }
  
  //End AH Addition---------------------
  //(orig code)
  

  // Create a new object state for the memory object (instead of a copy).
  if (!replayKTest) {
    // Find a unique name for this array.  First try the original name,
    // or if that fails try adding a unique identifier.
    unsigned id = 0;
    std::string uniqueName = name;
    while (!state.arrayNames.insert(uniqueName).second) {
      uniqueName = name + "_" + llvm::utostr(++id);
    }
    const Array *array = arrayCache.CreateArray(uniqueName, mo->size);
    bindObjectInState(state, mo, false, array);
    state.addSymbolic(mo, array);
    
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
      seedMap.find(&state);
    if (it!=seedMap.end()) { // In seed mode we need to add this as a
                             // binding.
      for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
             siie = it->second.end(); siit != siie; ++siit) {
        SeedInfo &si = *siit;
        KTestObject *obj = si.getNextInput(mo, NamedSeedMatching);

        if (!obj) {
          if (ZeroSeedExtension) {
            std::vector<unsigned char> &values = si.assignment.bindings[array];
            values = std::vector<unsigned char>(mo->size, '\0');
          } else if (!AllowSeedExtension) {
            terminateStateOnError(state, "ran out of inputs during seeding",
                                  User);
            break;
          }
        } else {
          if (obj->numBytes != mo->size &&
              ((!(AllowSeedExtension || ZeroSeedExtension)
                && obj->numBytes < mo->size) ||
               (!AllowSeedTruncation && obj->numBytes > mo->size))) {
	    std::stringstream msg;
	    msg << "replace size mismatch: "
		<< mo->name << "[" << mo->size << "]"
		<< " vs " << obj->name << "[" << obj->numBytes << "]"
		<< " in test\n";

            terminateStateOnError(state, msg.str(), User);
            break;
          } else {
            std::vector<unsigned char> &values = si.assignment.bindings[array];
            values.insert(values.begin(), obj->bytes, 
                          obj->bytes + std::min(obj->numBytes, mo->size));
            if (ZeroSeedExtension) {
              for (unsigned i=obj->numBytes; i<mo->size; ++i)
                values.push_back('\0');
            }
          }
        }
      }
    }
  } else {
    ObjectState *os = bindObjectInState(state, mo, false);
    if (replayPosition >= replayKTest->numObjects) {
      terminateStateOnError(state, "replay count mismatch", User);
    } else {
      KTestObject *obj = &replayKTest->objects[replayPosition++];
      if (obj->numBytes != mo->size) {
        terminateStateOnError(state, "replay size mismatch", User);
      } else {
        for (unsigned i=0; i<mo->size; i++)
          os->write8(i, obj->bytes[i]);
      }
    }
  }
}

/***/

extern "C" void klee_interp () {
  printf("---------------ENTERING KLEE_INTERP ---------------------- \n");
  assert (GlobalInterpreter);
  GlobalInterpreter->klee_interp_internal();
  return;
}

//This function's purpose is to take a context from native execution 
//and return an llvm function for interpretation.  It's OK for now if
// this returns a KFunction with LLVM IR for only one machine instruction at a time. 
KFunction * findInterpFunction (greg_t * registers, KModule * kmod) {

  uint64_t nativePC = registers[REG_RIP];
   printf("Looking up interp info for RIP : decimal %lu, hex  %lx \n", nativePC, nativePC);
  //Arithmetic to find interpretation function.
  std::stringstream converter;
  converter << std::hex << nativePC;  
  std::string hexNativePCString(converter.str());
  std::string functionName =   "interp_fn_" + hexNativePCString;
  llvm::Function * interpFn = interpModule->getFunction(functionName);
  KFunction * KInterpFunction = kmod->functionMap[interpFn];
  //printf(" Trying to find interp function for %s \n",functionName.c_str());
  
  if (!KInterpFunction)
    printf("Unable to find interp function for entrypoint PC 0x%lx \n", nativePC);
  return KInterpFunction;
  
}
//Here's the interface expected for the llvm interpretation function.

// void @interp_fn_PCValHere( %greg_t * %target_ctx_ptr) {
//; Emulate the native code modeled in the function, including an
//; updated program counter.  %target_ctx_ptr will take the initial greg_t ctx,
//; and the necessary interpretation will occur in-place at the ctx pointed to. Also need 
//; to perform loads and stores to main memory.  By this point, an llvm load/store to
//; a given address in the interpreter will result in a load/store from/to the actual
//; native memory address.
// }


//write model ------------- 
//ssize_t write (int filedes, const void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html
void Executor::model_write() {
  
  //Get the input args per system V linux ABI.
  uint64_t rawArg1Val = target_ctx_gregs[REG_RDI]; //fd
  uint64_t rawArg2Val = target_ctx_gregs[REG_RSI]; //address of buf to copy
  uint64_t rawArg3Val = target_ctx_gregs[REG_RDX]; //len to copy
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){
    
    //Note that we just verify against the actual number of bytes that
    //would be printed, and ignore null terminators in the src buffer.
    //Write can non-deterministically print up to X chars, depending
    //on state of network stack buffers at time of call.
    //Believe this is the behavior intended by the posix spec

    //TODO: check fd to make sure the write goes to server before we get c2s server
    
    //Get correct message buffer to verify against based on fd
    KTestObject *o = KTOV_next_object(&ktov,
				      "c2s");
    if (o->numBytes > rawArg3Val) {
      printf("ktest_writesocket playback error: %lu bytes of input, %u bytes recorded", rawArg3Val, o->numBytes);
      std::exit(EXIT_FAILURE);
    }
    // Since this is a write, compare for equality.
    if (o->numBytes > 0 && memcmp((void *) rawArg2Val, o->bytes, o->numBytes) != 0) {
      printf("WARNING: ktest_writesocket playback - data mismatch\n");
      std::exit(EXIT_FAILURE);
    }
    
    unsigned char * wireMessageRecord = o->bytes;
    int wireMessageLength = o->numBytes;

    ref<Expr> writeCondition = ConstantExpr::create(1,Expr::Bool);
    
    for (int j = 0; j < wireMessageLength; j++) {
      ref<Expr> srcCandidateVal = tase_helper_read((uint64_t) (rawArg2Val + j), 1);  //Get 1 byte from addr rawArg2Val + j
      ref<ConstantExpr> wireMessageVal = ConstantExpr::create( *((uint8_t *) wireMessageRecord + j), Expr::Int8);
      ref<Expr> equalsExpr = EqExpr::create(srcCandidateVal,wireMessageVal);
      writeCondition = AndExpr::create(writeCondition, equalsExpr);
    }

    //Todo -- Double check if this is actually needed if we implement solveForBindings to
    //be aware of global constraints
    bool result;
    //Changed below from cliver because we don't have a definition for compute_false
    //compute_false(GlobalExecutionStatePtr, writeCondition, result);
    solver->mustBeFalse(*GlobalExecutionStatePtr, writeCondition, result);
    if (result) {
      printf("ERROR: model_write detected inconsistency in state \n");
      std::exit(EXIT_FAILURE);
    }
    
    addConstraint(*GlobalExecutionStatePtr, writeCondition);
    //Wrap up and get back to execution.
    //Return number of bytes sent and bump RIP.
    ref<ConstantExpr> bytesWrittenExpr = ConstantExpr::create(wireMessageLength, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, bytesWrittenExpr);
    target_ctx_gregs[REG_RIP] += 5;

    //Determine if we can infer new bindings based on current round of execution.
    //If not, it's time to move on to next round.

    multipassInfo.currMultipassAssignment.clear();
    multipassInfo.currMultipassAssignment.solveForBindings( solver->solver, writeCondition,GlobalExecutionStatePtr);

    if (multipassInfo.currMultipassAssignment.bindings.size() && multipassInfo.currMultipassAssignment.bindings.size() !=
	multipassInfo.prevMultipassAssignment.bindings.size()) {

      //Todo -- IMPLEMENT
      //Request a new process to run through this stage of multipass and provide all the new details of multipass info
      //ex new pass count, "old" CVAssignment, current message number.

      
    } else {
      //increment message count to indicate we've reached a new stage of verification and keep going.
      //todo -- IMPLEMENT
    }
    
  } else {   
    printf("Found symbolic argument to model_send \n");
    std::exit(EXIT_FAILURE);
  } 
}


//read model --------
//ssize_t read (int filedes, void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html
//Can be read from stdin or from socket -- modeled separately to break up the code.
void Executor::model_read() {
  printf("model_read still being implemented \n");
  std::exit(EXIT_FAILURE);

  //Get the input args per system V linux ABI.
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){
    //Get correct message buffer to verify against based on fd
    //TODO: Find a better way to do this.

    uint64_t rawArg1Val = target_ctx_gregs[REG_RDI]; //fd
    //uint64_t rawArg2Val = target_ctx_gregs[REG_RSI]; //address of dest buf  NOT USED
    //uint64_t rawArg3Val = target_ctx_gregs[REG_RDX]; //max len to read in NOT USED
    
    if (rawArg1Val == STDIN_FD)
      model_readstdin();    
    else if (rawArg1Val == SOCKET_FD)
      model_readsocket();
  } else  {   
    printf("Found symbolic argument to model_read \n");
    std::exit(EXIT_FAILURE);
  }  
}

void Executor::model_readsocket() {

//Get the input args per system V linux ABI.
  uint64_t rawArg1Val = target_ctx_gregs[REG_RDI]; //fd
  uint64_t rawArg2Val = target_ctx_gregs[REG_RSI]; //address of dest buf 
  uint64_t rawArg3Val = target_ctx_gregs[REG_RDX]; //max len to read in
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    KTestObject *o = KTOV_next_object(&ktov, "s2c");
    if (o->numBytes > rawArg3Val) {
      printf("readsocket error: %zu byte destination buffer, %d bytes recorded", rawArg3Val, o->numBytes);
      std::exit(EXIT_FAILURE);
    }
    
    memcpy((void *) rawArg2Val, o->bytes, o->numBytes);
    
    //Return number of bytes read and bump RIP.
    ref<ConstantExpr> bytesReadExpr = ConstantExpr::create(o->numBytes, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, bytesReadExpr);
    target_ctx_gregs[REG_RIP] += 5;
    return; 
  } else {
    printf("Found symbolic args to model_readsocket \n");
    std::exit(EXIT_FAILURE);
  }
}


void spinAndAwaitForkRequest() {
  while (true) {
    //Todo: Implement fork request mechanism, probably via named pipes
    //forkRequest FR = getForkRequest(fd);
    //if (FR) {
    if (false) {
      int pid = ::fork();
      if (pid ==0) {
	//multipassInfo.roundRootPID = FR.roundRootPID;
	//multipassInfo.prevMultipassAssignment = FR.currMultipassAssignment;
	//multipassInfo.currMultipassAssignment = FR.currMultipassAssignment;
	//multipassInfo.messageNumber = FR.messageNumber;
	//multipassInfo.passCount = FR.passCount;
	return;
      } else {
	//just continue handling fork requests.
      }
    }
  }
}


void Executor::model_readstdin() {
  //Get the input args per system V linux ABI.
  uint64_t fd = target_ctx_gregs[REG_RDI]; //fd
  uint64_t dest = target_ctx_gregs[REG_RSI]; //address of dest buf 
  uint64_t maxlen = target_ctx_gregs[REG_RDX]; //max len to read in
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){
    
    if (enableMultipass) {

      
      
      //BEGIN NEW VERIFICATION STAGE =====>
      //clear old multipass assignment info.

      multipassInfo.prevMultipassAssignment.clear();
      multipassInfo.currMultipassAssignment.clear();
      multipassInfo.messageNumber++;
      multipassInfo.passCount = 0;
      multipassInfo.roundRootPID = getpid();
      
      int pid = ::fork();
      if (pid == 0) {
	//Continue on 
      }else {
	spinAndAwaitForkRequest();  //Should only hit this code once.
      }

      //BEGIN NEW VERIFICATION PASS =====>
      //Entry point is here from spinAndAwaitForkRequest().  At this point,
      //pass number info and the latest multipass assignment info should have been entered.     
    }

    uint64_t numSymBytes = tls_predict_stdin_size(fd, maxlen);
    
    if (numSymBytes <= maxlen) // redundant based on tls_predict_stdin_size's logic.
      tase_make_symbolic(dest, numSymBytes, "stdinRead");
    else {
      printf("ERROR: detected too many bytes in stdin read within model_readstdin() \n");
      std::exit(EXIT_FAILURE);
    }
 
    //Return number of bytes read and bump RIP.
    ref<ConstantExpr> bytesReadExpr = ConstantExpr::create(numSymBytes, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, bytesReadExpr);
    target_ctx_gregs[REG_RIP] += 5;
    

  } else {
    printf("Found symbolic args to model_readstdin \n");
    std::exit(EXIT_FAILURE);
  }
}

//int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
//https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html
//We examine fds 0 to ndfs-1.  Don't model the results of exceptfds, at least not yet.
//Todo: determine if we need to use kernel interface abi for this or any of the other i/o modeling functions
void Executor::model_select() {
  static int times_model_select_called = 0;
  times_model_select_called++;
  
  //Get the input args per system V linux ABI.
  uint64_t nfds = target_ctx_gregs[REG_RDI]; // int nfds
  fd_set * readfds = (fd_set *) target_ctx_gregs[REG_RSI]; // fd_set * readfds
  fd_set * writefds = (fd_set *) target_ctx_gregs[REG_RDX]; // fd_set * writefds
  //fd_set * exceptfds = (fd_set *) target_ctx_gregs[REG_RCX]; // fd_set * exceptfds NOT USED
  //struct timeval * timeout = (struct timeval *) target_ctx_gregs[REG_R8];  // struct timeval * timeout  NOT USED
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(REG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    //Safety check for some low-level operations done later.
    //Probably can move this out later when we're getting perf numbers.
    /*
    if (sizeof(fds_bits) != 1) {
      printf("ERROR: Size of fds_bits is not a byte \n");
      std::exit(EXIT_FAILURE);
      }*/

    bool make_readfds_symbolic [nfds];
    bool make_writefds_symbolic [nfds];

    ref<Expr> bitsSetExpr = ConstantExpr::create(0, Expr::Int8);
    
    //Go through each fd.  Really out to be able to handle a symbolic
    //indicator var for a given fds_bits entry, but not worried about that for now.
    for (uint64_t i = 0; i < nfds ; i++) {
      //if given fd is set in readfds, return sym val 
      ref<Expr> isReadFDSetExpr = tase_helper_read((uint64_t) &(readfds->fds_bits[i]), Expr::Int8);
      if (isa<ConstantExpr>(isReadFDSetExpr)) {
	if (FD_ISSET(i,readfds)) {
	  make_readfds_symbolic[i] = true;
	} else {
	  make_readfds_symbolic[i] = false;
	}
      } else {
	printf("Found symbolic readfdset bit indicator in model_select.  Not implemented yet \n");
	std::exit(EXIT_FAILURE);
      }

      //if given fd is set in writefds,  return sym val
      ref<Expr> isWriteFDSetExpr = tase_helper_read((uint64_t) &(writefds->fds_bits[i]), Expr::Int8);
      if (isa<ConstantExpr>(isWriteFDSetExpr) ) {
	if (FD_ISSET(i,writefds)) {
	  make_writefds_symbolic[i] = true;
	} else {
	  make_writefds_symbolic[i] = false;
	} 
      } else {
	printf("Found symbolic writefdset bit indicator in model_select.  Not implemented yet \n");
	std::exit(EXIT_FAILURE);
      }
    }

    //Now, actually make the appropriate bits in readfdset and writefdset symbolic.
    for (uint64_t j = 0; j < nfds; j++) {
      if (make_readfds_symbolic[j]) {

	void * readFDResultBuf = malloc(4); //Technically this only needs to be a bit or byte
	//Get the MO, then call executeMakeSymbolic()
	MemoryObject * readFDResultBuf_MO = memory->allocateFixed( (uint64_t) readFDResultBuf ,4,NULL);
	std::string nameString = "readFDSymVar " + std::to_string(times_model_select_called) + " " + std::to_string(j);
	readFDResultBuf_MO->name = nameString;
	executeMakeSymbolic(*GlobalExecutionStatePtr, readFDResultBuf_MO, nameString);
	
	const ObjectState *constreadFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.findObject(readFDResultBuf_MO);
	ObjectState * readFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(readFDResultBuf_MO,constreadFDResultBuf_OS);

	//Got to be a better way to write this... but we basically constrain the
	//sym var representing readfds[j] to be 0 or 1.
	ref <Expr> readFDExpr = readFDResultBuf_OS->read(0,Expr::Int8);
	ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
	ref <ConstantExpr> one = ConstantExpr::create(1, Expr::Int8);
	ref<Expr> equalsZeroExpr = EqExpr::create(readFDExpr,zero);
	ref<Expr> equalsOneExpr = EqExpr::create(readFDExpr, one);
	ref<Expr> equalsZeroOrOneExpr = OrExpr::create(equalsZeroExpr, equalsOneExpr);
	addConstraint(*GlobalExecutionStatePtr, equalsZeroOrOneExpr);

	tase_helper_write((uint64_t) &(readfds->fds_bits[j]), readFDExpr);

	bitsSetExpr = AddExpr::create(bitsSetExpr, readFDExpr);
	
      } else {
	ref <ConstantExpr> zeroVal = ConstantExpr::create(0, Expr::Int8);
	tase_helper_write((uint64_t)&(readfds->fds_bits[j]), zeroVal);
      }

      if (make_writefds_symbolic[j]) {
	void * writeFDResultBuf = malloc(4); //Technically this only needs to be a bit or byte
	//Get the MO, then call executeMakeSymbolic()
	MemoryObject * writeFDResultBuf_MO = memory->allocateFixed( (uint64_t) writeFDResultBuf ,4,NULL);
	std::string nameString = "writeFDSymVar " + std::to_string(times_model_select_called) + " " + std::to_string(j);
	writeFDResultBuf_MO->name = nameString;
	executeMakeSymbolic(*GlobalExecutionStatePtr, writeFDResultBuf_MO, nameString);
	
	const ObjectState *constwriteFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.findObject(writeFDResultBuf_MO);
	ObjectState * writeFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(writeFDResultBuf_MO,constwriteFDResultBuf_OS);

	//Got to be a better way to write this... but we basically constrain the
	//sym var representing writefds[j] to be 0 or 1.
	ref <Expr> writeFDExpr = writeFDResultBuf_OS->read(0,Expr::Int8);
	ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
	ref <ConstantExpr> one = ConstantExpr::create(1, Expr::Int8);
	ref<Expr> equalsZeroExpr = EqExpr::create(writeFDExpr,zero);
	ref<Expr> equalsOneExpr = EqExpr::create(writeFDExpr, one);
	ref<Expr> equalsZeroOrOneExpr = OrExpr::create(equalsZeroExpr, equalsOneExpr);
	addConstraint(*GlobalExecutionStatePtr, equalsZeroOrOneExpr);
	tase_helper_write((uint64_t)&(writefds->fds_bits[j]), writeFDExpr);
	bitsSetExpr = AddExpr::create(bitsSetExpr, writeFDExpr);
	
      } else {
	ref <ConstantExpr> zeroVal = ConstantExpr::create(0, Expr::Int8);
	tase_helper_write((uint64_t) &(writefds->fds_bits[j]), zeroVal);
      }

    }

    //For now, just model with a successful return.  But we could make this symbolic.
    target_ctx_gregs_OS->write(REG_RAX * 8, bitsSetExpr);
    //bump RIP and interpret next instruction
    target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
    printf("INTERPRETER: Exiting model_select \n");
    
  } else {
    printf("ERROR: Found symbolic input to model_select()");
    std::exit(EXIT_FAILURE);
  }  
}

void Executor::model_tls1_generate_master_secret () {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //SSL * s
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //unsigned char * out
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //unsigned char * p
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64); //int len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) ) {

    //Interestingly, the implementation of tls1_generate_master_secret in ssl/t1_enc.c in OpenSSL
    //doesn't actually write the master secret to out; it instead writes to s->session->master_key
    //Todo -- this may be different in BoringSSL; find out if out is actually used there.

    SSL * s = (SSL *) target_ctx_gregs[REG_RDI];
    char * secretFileName = "master_secret.txt";
    FILE * secretFile = fopen (secretFileName, "r");
    char secret[48];
    fgets(secret,48,secretFile);
    printf("secret is %s \n", secret);
    fwrite(s->session->master_key, SSL3_MASTER_SECRET_SIZE, 1,secretFile);
    fclose(secretFile);
    
    //return value is size of tls master secret macro
    ref<ConstantExpr> masterSecretSize = ConstantExpr::create(SSL3_MASTER_SECRET_SIZE,Expr::Int32); //May want to adjust size for different int sizes
    target_ctx_gregs_OS->write(REG_RAX * 8, masterSecretSize);
    //bump RIP and interpret next instruction
    target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
    printf("INTERPRETER: Exiting model_tls1_generate_master_secret \n");
    
  } else {
    printf("ERROR: symbolic arg passed to tls1_generate_master_secret \n");
    std::exit(EXIT_FAILURE);
  } 
}


//model for void AES_encrypt(const unsigned char *in, unsigned char *out,
//const AES_KEY *key);

void Executor::model_AES_encrypt () {

  static int timesModelAESEncryptIsCalled = 0;
  timesModelAESEncryptIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //const unsigned char *in
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //unsigned char * out
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //const AES_KEY * key

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    const unsigned char * in =  (const unsigned char *)target_ctx_gregs[REG_RDI];
    unsigned char * out = (unsigned char *) target_ctx_gregs[REG_RSI];
    const AES_KEY * key = (const AES_KEY *) target_ctx_gregs[REG_RDX];
    
    int AESBlockSize = 16; //Number of bytes in AES block
    bool hasSymbolicDependency = false;
    
    //Check to see if any input bytes or the key are symbolic
    for (int i = 0; i < AESBlockSize; i++) {
      ref<Expr> inByteExpr = tase_helper_read( ((uint64_t) in) +i, 1);
      if (!isa<ConstantExpr>(inByteExpr))
	hasSymbolicDependency = true;
    }

    //Todo: Chase down any structs that AES_KEY points to if it's not a simple struct.
    for (uint64_t i = 0; i < sizeof(AES_KEY); i++) {
      ref<Expr> keyByteExpr = tase_helper_read( ((uint64_t) key) + i, 1);
      if (!isa<ConstantExpr>(keyByteExpr))
	hasSymbolicDependency = true;
    }

    if (hasSymbolicDependency) {
      //Get the MO, then call executeMakeSymbolic()
      void * symOutBuffer = malloc(AESBlockSize);
      MemoryObject * AESEncryptOutMO = memory->allocateFixed( (uint64_t) symOutBuffer,16,NULL);
      std::string nameString = "aes_Encrypt_output " + std::to_string(timesModelAESEncryptIsCalled);
      AESEncryptOutMO->name = nameString;
      executeMakeSymbolic(*GlobalExecutionStatePtr, AESEncryptOutMO, "modelAESEncryptOutputBuffer");
      const ObjectState * constAESEncryptOutOS = GlobalExecutionStatePtr->addressSpace.findObject(AESEncryptOutMO);
      ObjectState * AESEncryptOutOS = GlobalExecutionStatePtr->addressSpace.getWriteable(AESEncryptOutMO,constAESEncryptOutOS);
      for (int i = 0; i < AESBlockSize; i++)
	tase_helper_write( ((uint64_t) out) +i, AESEncryptOutOS->read(i, Expr::Int8)); 
    } else {
      //Otherwise we're good to call natively
      printf("ERROR: Native AES_Encrypt not implemented yet \n");
      std::exit(EXIT_FAILURE);
      //AES_Encrypt(in,out,key);  //Todo -- get native call for AES_Encrypt
    }
    //increment pc and get back to execution.
    target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
    printf("INTERPRETER: Exiting model_AES_encrypt \n");
    
  } else {
    printf("ERROR: symbolic arg passed to model_AES_encrypt \n");
    std::exit(EXIT_FAILURE);
  } 
}

//Model for 
//void gcm_ghash_4bit(u64 Xi[2],const u128 Htable[16],
//				const u8 *inp,size_t len)
//in crypto/modes/gcm128.c
//Todo: Check to see if we're incorrectly assuming that the Xi and Htable arrays are passed as ptrs in the abi.
void Executor::model_gcm_ghash_4bit () {
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const u128 Htable[16]
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); // const u8 *inp
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) ) {

    u64 * XiPtr = (u64 *) target_ctx_gregs[REG_RDI];
    u128 * HtablePtr = (u128 *) target_ctx_gregs[REG_RSI];
    const u8 * inp = (const u8 *) target_ctx_gregs[REG_RDX];
    size_t len = (size_t) target_ctx_gregs[REG_RCX];
    
    //Todo: Double check the dubious ptr casts and figure out if we
    //are falsely assuming any structs or arrays are packed
    bool hasSymbolicInput = false;
    for (uint64_t i = 0; i < sizeof(u64) *2; i++) {
      ref<Expr> inputByteExpr = tase_helper_read( ((uint64_t) XiPtr) + i,1);
      if (!isa<ConstantExpr>(inputByteExpr))
	hasSymbolicInput = true;
    }

    // Todo: Double check  if this is OK for different size_t values.
    for (uint64_t i = 0; i < len; i++) {
      ref<Expr> inputByteExpr = tase_helper_read( ((uint64_t) inp) + i,1);
      if (!isa<ConstantExpr>(inputByteExpr))
	hasSymbolicInput = true;
    }

    if (hasSymbolicInput) {
      tase_make_symbolic ((uint64_t) XiPtr, sizeof(u64) * 2, "GCMGHashOutput");
    } else {
      printf("ERROR: No native call for gcm_ghash_4bit yet. \n");
      std::exit(EXIT_FAILURE);
      //gcm_ghash_4bit(XiPtr, HtablePtr, inp, len);
    }

    //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_gcm_ghash_4bit \n"); 
     
  } else {
    printf("ERROR: symbolic arg passed to model_gcm_ghash_4bit \n");
    std::exit(EXIT_FAILURE);
  }  
}

//Model for
//void gcm_gmult_4bit(u64 Xi[2], const u128 Htable[16])
// in crypto/modes/gcm128.c
void Executor::model_gcm_gmult_4bit () {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); // const u128 Htable[16]

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr))
	) {

     u64 * XiPtr = (u64 *) target_ctx_gregs[REG_RDI];
     u128 * HtablePtr = (u128 *) target_ctx_gregs[REG_RSI];
     
     //Todo: Double check the dubious ptr cast and figure out if we
     //are assuming any structs are packed
     bool hasSymbolicInput = false;
     for (uint64_t i = 0; i < sizeof(u64) *2; i++) {
       ref<Expr> inputByteExpr = tase_helper_read( ((uint64_t) XiPtr) + i,1);
       if (!isa<ConstantExpr>(inputByteExpr))
	 hasSymbolicInput = true;
     }

     if (hasSymbolicInput) {
       tase_make_symbolic((uint64_t) XiPtr, sizeof(u64) * 2, "GCMGMultOutput");
     } else {
       //Todo -- double check that gcm_gmult_4bit is side-effect free
       printf("ERROR: No native implementation available for gcm_gmult_4bit yet \n");
       //gcm_gmult_4bit(XiPtr, HtablePtr); 
     }
     //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_gcm_gmult_4bit \n"); 
   } else {
     printf("ERROR: symbolic arg passed to model_gcm_gmult_4bit \n");
     std::exit(EXIT_FAILURE);
   }
}

//Model for int SHA1_Update(SHA_CTX *c, const void *data, size_t len);
//defined in crypto/sha/sha.h.
//
void Executor::model_SHA1_Update () {
 
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //SHA_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    //Determine if SHA_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[REG_RDI];
    const void * data = (const void *) target_ctx_gregs[REG_RSI];
    size_t len = (size_t) target_ctx_gregs[REG_RDX];
    
    bool hasSymbolicInput = false;

    for (uint64_t i = 0; i < sizeof(SHA_CTX) ; i++) {
      ref <Expr> sha1CtxByteExpr = tase_helper_read( ((uint64_t) c) + i, 1);
      if (!isa<ConstantExpr>(sha1CtxByteExpr))
	hasSymbolicInput = true;
    }
    for (uint64_t i = 0; i < len ; i++) {
      ref <Expr> dataInputByteExpr = tase_helper_read( ((uint64_t) data) + i, 1);
      if (!isa<ConstantExpr>(dataInputByteExpr))
	hasSymbolicInput = true;
    }

    if (hasSymbolicInput) {
      tase_make_symbolic((uint64_t) c, 20, "SHA1_Update_Output");
       
       void * intResultPtr = malloc(sizeof(int));
       MemoryObject * resultMO = memory->allocateFixed( (uint64_t) intResultPtr, sizeof(int),NULL);
       resultMO->name = "intResult";
       executeMakeSymbolic(*GlobalExecutionStatePtr, resultMO, "intResultMO");
       const ObjectState * constIntResultOS = GlobalExecutionStatePtr->addressSpace.findObject(resultMO);
       ObjectState * intResultOS = GlobalExecutionStatePtr->addressSpace.getWriteable(resultMO,constIntResultOS);

       target_ctx_gregs_OS->write(REG_RAX * 8, intResultOS->read(0, Expr::Int32));
       
    } else { //Call natively
      printf("ERROR: No native sha1_update implementation available \n");
      std::exit(EXIT_FAILURE);
      //Todo: provide SHA1_Update implementation for fast native execution
      /*
      int res = SHA1_Update(c, data, len);
      ref<ConstantExpr> resExpr = ConstantExpr::create(res, Expr::Int32);
       target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
      */
    }
    //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_SHA1_Update \n"); 
  } else {
    printf("ERROR: symbolic arg passed to model_SHA1_Update \n");
    std::exit(EXIT_FAILURE);
  }
}

//Model for int SHA1_Final(unsigned char *md, SHA_CTX *c)
//defined in crypto/sha/sha.h
void Executor::model_SHA1_Final() {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //SHA_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[REG_RDI];
     SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[REG_RSI];
     
     bool hasSymbolicInput = false;
     
     for (uint64_t i = 0; i < sizeof(SHA_CTX) ; i++) {
       ref <Expr> sha1CtxByteExpr = tase_helper_read( ((uint64_t) c) + i, 1);
       if (!isa<ConstantExpr>(sha1CtxByteExpr))
	 hasSymbolicInput = true;
     }
     if (hasSymbolicInput) {
       tase_make_symbolic( (uint64_t) md, SHA_DIGEST_LENGTH, "SHA1_Final_Output");

       void * intResultPtr = malloc(sizeof(int));
       MemoryObject * resultMO = memory->allocateFixed( (uint64_t) intResultPtr, sizeof(int),NULL);
       resultMO->name = "intResult";
       executeMakeSymbolic(*GlobalExecutionStatePtr, resultMO, "intResultMO");
       const ObjectState * constIntResultOS = GlobalExecutionStatePtr->addressSpace.findObject(resultMO);
       ObjectState * intResultOS = GlobalExecutionStatePtr->addressSpace.getWriteable(resultMO,constIntResultOS);
       
       target_ctx_gregs_OS->write(REG_RAX * 8, intResultOS->read(0, Expr::Int32));
     } else {
       printf("ERROR: no sha1_final implementation available \n");
       std::exit(EXIT_FAILURE);
       //Todo: Provide sha1_final native implementation for concrete execution
       /*
       int res = SHA1_Final(md, c);
       ref<ConstantExpr> resExpr = ConstantExpr::create(res, Expr::Int32);
       target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
       */
     }
      //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_SHA1_Final \n"); 
     
   } else {
     printf("ERROR: symbolic arg passed to model_SHA1_Final \n");
    std::exit(EXIT_FAILURE);
   }
}

//Model for int SHA256_Update(SHA256_CTX *c, const void *data, size_t len)
//defined in crypto/sha/sha.h.
//
void Executor::model_SHA256_Update () {
  static int timesModelSHA256UpdateIsCalled = 0;
  timesModelSHA256UpdateIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //SHA256_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    //Determine if SHA256_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[REG_RDI];
    const void * data = (const void *) target_ctx_gregs[REG_RSI];
    size_t len = (size_t) target_ctx_gregs[REG_RDX];
    
    bool hasSymbolicInput = false;
    for (uint64_t i = 0; i < sizeof(SHA256_CTX) ; i++) {
      ref <Expr> sha256CtxByteExpr = tase_helper_read( ((uint64_t) c) + i, 1);
      if (!isa<ConstantExpr>(sha256CtxByteExpr))
	hasSymbolicInput = true;
    }
    for (uint64_t i = 0; i < len ; i++) {
      ref <Expr> dataInputByteExpr = tase_helper_read( ((uint64_t) data) + i, 1);
      if (!isa<ConstantExpr>(dataInputByteExpr))
	hasSymbolicInput = true;
    }

    if (hasSymbolicInput) {

      tase_make_symbolic( (uint64_t) c, 32, "SHA256_Update_Output");
      
      void * intResultPtr = malloc(sizeof(int));
      MemoryObject * resultMO = memory->allocateFixed( (uint64_t) intResultPtr, sizeof(int),NULL);
      resultMO->name = "intResult";
      executeMakeSymbolic(*GlobalExecutionStatePtr, resultMO, "intResultMO");
      const ObjectState * constIntResultOS = GlobalExecutionStatePtr->addressSpace.findObject(resultMO);
      ObjectState * intResultOS = GlobalExecutionStatePtr->addressSpace.getWriteable(resultMO,constIntResultOS);
      target_ctx_gregs_OS->write(REG_RAX * 8, intResultOS->read(0, Expr::Int32));
      
    } else { //Call natively
      printf("ERROR: No sha256_update native implementation available \n");
      std::exit(EXIT_FAILURE);
      //todo: provide sha256_update native implementation
      /*
      int res =SHA256_Update(c, data, len);
      ref<ConstantExpr> resExpr = ConstantExpr::create(res, Expr::Int32);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
      */
    }

    //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_SHA256_Update \n"); 
    
  } else {
    printf("ERROR: symbolic arg passed to model_SHA256_Update \n");
    std::exit(EXIT_FAILURE);
  }
}


//Model for int SHA256_Final(unsigned char *md, SHA256_CTX *c)
//defined in crypto/sha/sha.h
//Todo: determine if we can just pass a return value of success for all 4 sha models
void Executor::model_SHA256_Final() {
  static int timesModelSHA256FinalIsCalled = 0;
  timesModelSHA256FinalIsCalled++;

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //SHA256_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[REG_RDI];
     SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[REG_RSI];
     
     bool hasSymbolicInput = false;
     
     for (int i = 0; i < sizeof(SHA256_CTX) ; i++) {
       ref <Expr> sha256CtxByteExpr = tase_helper_read( ((uint64_t) c) + i, 1);
       if (!isa<ConstantExpr>(sha256CtxByteExpr))
	 hasSymbolicInput = true;
     }

     if (hasSymbolicInput) {
       tase_make_symbolic((uint64_t) md, SHA_DIGEST_LENGTH, "SHA256_Final_Output");

       void * intResultPtr = malloc(sizeof(int));
       MemoryObject * resultMO = memory->allocateFixed( (uint64_t) intResultPtr, sizeof(int),NULL);
       resultMO->name = "intResult";
       executeMakeSymbolic(*GlobalExecutionStatePtr, resultMO, "intResultMO");
       const ObjectState * constIntResultOS = GlobalExecutionStatePtr->addressSpace.findObject(resultMO);
       ObjectState * intResultOS = GlobalExecutionStatePtr->addressSpace.getWriteable(resultMO,constIntResultOS);
       target_ctx_gregs_OS->write(REG_RAX * 8, intResultOS->read(0, Expr::Int32));
       
     } else {
       printf("ERROR: No native sha256_final implementation \n");
       std::exit(EXIT_FAILURE);
       //Todo: provide fast native sha256_final implementation
       /*
       int res = SHA256_Final(md, c);
       ref<ConstantExpr> resExpr = ConstantExpr::create(res, Expr::Int32);
       target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
       */
     }

      //increment pc and get back to execution.
     target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
     printf("INTERPRETER: Exiting model_SHA256_Final \n"); 
     
   } else {
     printf("ERROR: symbolic arg passed to model_SHA256_Final \n");
    std::exit(EXIT_FAILURE);
   }
}


//Model for int EC_KEY_generate_key(EC_KEY *key)
// from crypto/ec/ec.h

//This is a little different because we have to reach into the struct
//and make its fields symbolic.
//Point of this function is to produce ephemeral key pair for Elliptic curve diffie hellman
//key exchange and evenutally premaster secret generation during the handshake.

//EC_KEY struct has a private key k, which is a number between 1 and the size of the Elliptic curve subgroup
//generated by base point G.
//Public key is kG for the base point G.
//So k is an integer, and kG is a point (three coordinates in jacobian projection or two in affine projection)
//on the curve produced by "adding" G to itself k times.
void Executor::model_EC_KEY_generate_key () {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //EC_KEY * key

   if (  (isa<ConstantExpr>(arg1Expr)) ) {

     bool hasSymbolicInput = false;
     
     EC_KEY * eckey = (EC_KEY *) target_ctx_gregs[REG_RDI];
     for (uint64_t i = 0; i < sizeof(EC_KEY) ; i++) {
       ref<Expr> keyByteExpr = tase_helper_read( ((uint64_t) eckey) +i, Expr::Int8);
       if (!isa<ConstantExpr>(keyByteExpr)) {
	 hasSymbolicInput = true;
       }
     }
 
     if (hasSymbolicInput) {
       printf("ERROR: Symbolic EC_KEY detected in model_EC_KEY_generate_key \n");
       std::exit(EXIT_FAILURE);
     } //At this point, all structs in ec_key must be concrete
     
     //private key is a "BIGNUM" struct in openssl.
     BIGNUM * priv_key = eckey->priv_key;

     //Check to see if priv_key struct is initialized; if not, init it and populate later.
     printf("ERROR: No bn_new implementation provided \n");
     std::exit(EXIT_FAILURE);
     //Todo: Provide BN_new implementation
     if(priv_key == NULL)
       priv_key = BN_new();

     //Check to see if pub_key struct is initialized; if not, init it and populate later.
     EC_POINT * pub_key = eckey->pub_key;
     printf("ERROR: No implementaiton available for EC_POINT_new \n");
     std::exit(EXIT_FAILURE);
     //Todo: link in ec_point_new implementation/
     /*
     if (pub_key == NULL)
       pub_key = EC_POINT_new(eckey->group);
     */
     //BIGNUM struct can be different sizes, so deal with the mess in make_BN_symbolic.
     make_BN_symbolic(priv_key);

     //Need to make sure pub key contains concrete pointers to x, y, z bignums that represent a point.
     bool ecPtHasSymbolicData = false;
     for (uint64_t i = 0; i < sizeof(EC_POINT) ; i++) {
       ref<Expr> ecPtByteExpr = tase_helper_read((uint64_t) pub_key + i, Expr::Int8);
       if (!isa<ConstantExpr>(ecPtByteExpr))
	 ecPtHasSymbolicData = true;
     }
     if ( ecPtHasSymbolicData) {
       printf("ERROR: Symbolic data detected too early in ec_point while modeling EC_KEY_generate_key \n");
       std::exit(EXIT_FAILURE);
     }

     make_BN_symbolic(&(pub_key->X));
     make_BN_symbolic(&(pub_key->Y));
     make_BN_symbolic(&(pub_key->Z));

     //Always model the return as a success.  We can generalize this later if we want.
     ref<ConstantExpr> zeroResultExpr = ConstantExpr::create(0, Expr::Int32);
     target_ctx_gregs_OS->write(REG_RAX * 8, zeroResultExpr);

     //Bump RIP and get back to execution
     target_ctx_gregs[REG_RAX] += 5;
     
   } else {
      printf("ERROR: symbolic arg passed to model_EC_KEY_generate_key \n");
      std::exit(EXIT_FAILURE);
   }
}

//model for 
//int ecdh_compute_key(void *out, size_t outlen, const EC_POINT *pub_key,
//EC_KEY *ecdh,
//void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen))
//from crypto/ecdh/ech_ossl.c

//Todo: Double check that model for ABI is accurate since 5 args are passed.

//Point of the method is to compute shared premaster secret from private key in eckey and pubkey pub_key.
//Todo -- determine if we ever need to actually call this with concrete values during verification since
//we never get access to the client's private key in eckey.
void Executor::model_ECDH_compute_key() {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(REG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    void * out = (void *) target_ctx_gregs[REG_RDI];
    size_t outlen = (size_t) target_ctx_gregs[REG_RSI];
    
    tase_make_symbolic( (uint64_t) out, outlen, "ecdh_compute_key_output");

    //return value is outlen
    //Todo -- determine if we really need to make the return value exactly size_t
    ref<ConstantExpr> returnVal = ConstantExpr::create(outlen, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, returnVal);

    //bump rip and return to execution
    target_ctx_gregs[REG_RAX] += 5;
    
  } else {
    printf("ERROR: model_ECDH_compute_key called with symbolic input args\n");
    std::exit(EXIT_FAILURE);
  }
}

//model for size_t EC_POINT_point2oct(const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form,
//        unsigned char *buf, size_t len, BN_CTX *ctx)
//Function defined in crypto/ec/ec_oct.c

//Todo: Double check this to see if we actually need to peek further into structs to see if they have symbolic
//taint
//The purpose of this function is to convert from an EC_POINT representation to an octet string encoding in buf.
void Executor::model_EC_POINT_point2oct() {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(REG_R8 * 8, Expr::Int64);
  ref<Expr> arg6Expr = target_ctx_gregs_OS->read(REG_R9 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr)) &&
	(isa<ConstantExpr>(arg6Expr))
	) {

    const EC_GROUP * group = (const EC_GROUP *) target_ctx_gregs[REG_RDI];
    const EC_POINT * point = (const EC_POINT *) target_ctx_gregs[REG_RSI];
    point_conversion_form_t form = (point_conversion_form_t) target_ctx_gregs[REG_RDX];
    unsigned char * buf = (unsigned char * ) target_ctx_gregs[REG_RCX];
    size_t len = (size_t) target_ctx_gregs[REG_R8];
    BN_CTX * ctx = (BN_CTX *) target_ctx_gregs[REG_R9];

    bool ecGrpHasSymbolicInput = false;
    for (uint64_t i = 0 ; i < sizeof(EC_GROUP); i++) {
      ref<Expr> ecGrpByteExpr = tase_helper_read(((uint64_t) group) + i, Expr::Int8);
      if (!isa<ConstantExpr>(ecGrpByteExpr))
	  ecGrpHasSymbolicInput = true;
    }
    if (ecGrpHasSymbolicInput) {
      printf("ERROR: model_EC_POINT_point2oct has symbolic group information \n");
      std::exit(EXIT_FAILURE);
    }
    //Todo -- make sure this executes with transactions, or manually chase pointers to make sure there's no symbolic dependency.
    //Todo: Link in definition for BN_num_bytes
    printf("ERROR: Need definition for BN_num_bytes \n");
    std::exit(EXIT_FAILURE);
    size_t field_len = 0;
    
    /*
    size_t field_len = BN_num_bytes(&group->field); 
    */
    size_t ret = (form == POINT_CONVERSION_COMPRESSED) ? 1 + field_len : 1 + 2*field_len;

    //If EC_POINT point has symbolic components, make the output buffer entirely symbolic
    bool hasSymbolicPoint = false;
    for (uint64_t i = 0; i < sizeof(EC_POINT); i++ ) {
      ref<Expr> ecPtByteExpr = tase_helper_read( ((uint64_t) point) +i, Expr::Int8);
      if (!isa<ConstantExpr>(ecPtByteExpr))
	hasSymbolicPoint = true;
    }

    if (hasSymbolicPoint) {
      tase_make_symbolic((uint64_t) buf,ret, "ECpoint2Oct");
      //Todo: determine if we need to return a different width specific to size_t
      ref<ConstantExpr> returnValExpr = ConstantExpr::create(ret, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, returnValExpr);
    } else {

      printf("ERROR: No native implementation available for EC_POINT_point2oct \n");
      std::exit(EXIT_FAILURE);
      //todo -- fill this out later.
      /*
      size_t returnVal = EC_POINT_point2oct(group, point, form, buf, len, ctx);
      //Todo: determine if we need to return a different width specific to size_t
      ref<ConstantExpr> returnValExpr = ConstantExpr::create(returnVal, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, returnValExpr);
      */
    }

    //bump rip and get back to execution
    target_ctx_gregs[REG_RIP] += 5;
    
  } else {
    printf("ERROR: model_EC_POINT_point2oct called with symbolic input \n");
    std::exit(EXIT_FAILURE);
  }
}

//Pretty much syncs right up with cliver's make_BN_symbolic in runtime/openssl.c
#define SYMBOLIC_BN_DMAX 64
  
void Executor::make_BN_symbolic(BIGNUM * bn) {

  //Make sure no fields of BIGNUM struct pointed to by bn are symbolic.
  bool hasSymbolicInput = false;
  for (int i = 0 ; i < sizeof(BIGNUM); i++ ) {
    ref<Expr> bnByteExpr = tase_helper_read( ((uint64_t) bn) + i, Expr::Int8);
    if (!isa<ConstantExpr>(bnByteExpr) )
      hasSymbolicInput = true;
  }

  if (hasSymbolicInput) {
    printf("ERROR: symbolic input passed to make_BN_symbolic\n");
    std::exit(EXIT_FAILURE);
  }
  
  if (bn->dmax > 0 ) {
    tase_make_symbolic((uint64_t) bn->d, (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
  } else {
    bn->dmax = SYMBOLIC_BN_DMAX;
    tase_make_symbolic((uint64_t) bn->d, (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
  }

  tase_make_symbolic( (uint64_t) &(bn->neg), sizeof(bn->neg), "BNSign"); //Should maybe add a constraint to make 0 or 1?
  
}

//Reworked from cliver's CVExecutor.cpp.
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
uint64_t Executor::tls_predict_stdin_size (uint64_t fd, uint64_t maxLen) {

  const uint8_t TLS_ALERT = 21;
  const uint8_t TLS_APPDATA = 23;

  uint64_t stdin_len;
    
  if (fd != 0) {
    printf("tls_predict_stdin_size() called with unknown fd %lu \n", fd);
    std::exit(EXIT_FAILURE);
  }

  KTestObject * kto = peekNextKTestObject();
  //Todo: Figure out better way to handle case 3
  
  if (kto == NULL) { //Case 3

    printf("Warning: no c2s record found in peekNextKTestObject()\n");
    stdin_len = 0;

  } else if (kto->name == "c2s" &&
	     kto->bytes[0] == TLS_ALERT &&
	     kto->numBytes == 31) { //Case 2
    
    stdin_len = 0;
    
  } else if (kto->name == "c2s" &&
	     kto->bytes[0] == TLS_APPDATA &&
	     kto->numBytes > 29) {//Case 1
    
    stdin_len = kto->numBytes - 29;
    
  } else {

    printf("Error in tls_predict_stdin_size \n");
    std::exit(EXIT_FAILURE);
    
  }
  
  if ( stdin_len > maxLen) {
    printf("ERROR: tls_predict_stdin_size returned value larger than maxLen \n");
    std::exit(EXIT_FAILURE);

  }else {
    return stdin_len;
  }
}

// Model for
// int RAND_bytes(unsigned char *buf, int num)
// from openssl/crypto/rand/rand_lib.c. 
void Executor::model_RAND_bytes() {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char * buf
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //int num

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * buf = (unsigned char *) target_ctx_gregs[REG_RDI];
     int num = (int) target_ctx_gregs[REG_RSI];
     
     tase_make_symbolic((uint64_t) buf, num, "model_RAND_bytes_output");
     ref <Expr> retVal = ConstantExpr::create(num, Expr::Int32);
     target_ctx_gregs_OS->write(REG_RAX * 8, retVal);
     target_ctx_gregs[REG_RIP] += 5;
     
   } else {
     printf("ERROR: Symbolic args passed to model_RAND_bytes \n");
     std::exit(EXIT_FAILURE);
   } 
}

// Model for
// int RAND_pseudo_bytes(unsigned char *buf, int num)
// from openssl/crypto/rand/rand_lib.c
void Executor::model_RAND_pseudo_bytes() {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char * buf
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //int num

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {
     
     unsigned char * buf = (unsigned char *) target_ctx_gregs[REG_RDI];
     int num = (int) target_ctx_gregs[REG_RSI];

     tase_make_symbolic((uint64_t) buf, num, "model_RAND_pseudo_bytes_output");
     ref <Expr> retVal = ConstantExpr::create(num, Expr::Int32);
     target_ctx_gregs_OS->write(REG_RAX * 8, retVal);
     target_ctx_gregs[REG_RIP] += 5;

   } else {
     printf("ERROR: Symbolic args passed to model_RAND_pseudo_bytes \n");
     std::exit(EXIT_FAILURE);
   }
}

// Model for
// int RAND_poll (void)
// from multiple implementations in openssl/rand/

// Purpose for this is to generate entropy for
// other rng purposes later, but for TASE we
// just stub it out because the RNG functions
// are skipped until we observe their outputs
// in relation to the IV.
void Executor::model_RAND_poll() {
  ref <Expr> returnVal = ConstantExpr::create(1, Expr::Int32);
  target_ctx_gregs_OS->write(REG_RAX * 8, returnVal);
  target_ctx_gregs[REG_RIP] += 5;

}

//Populate a buffer at addr with len bytes of unconstrained symbolic data.
//We make the symbolic memory object at a malloc'd address and write the bytes to addr.
void Executor::tase_make_symbolic(uint64_t addr, uint64_t len, char * name)  {
  void * buf = malloc(len);
  MemoryObject * bufMO = memory->allocateFixed((uint64_t) buf, len,  NULL);
  std::string nameString = name;
  bufMO->name = nameString;
  executeMakeSymbolic(*GlobalExecutionStatePtr, bufMO, name);
  const ObjectState * constBufOS = GlobalExecutionStatePtr->addressSpace.findObject(bufMO);
  ObjectState * bufOS = GlobalExecutionStatePtr->addressSpace.getWriteable(bufMO, constBufOS);
  
  for (int i = 0; i < len; i++)
    tase_helper_write(addr + i, bufOS->read(i, Expr::Int8));
}
  
  
//TODO: Determine if this is always a 4 byte return value in eax
//or if it's ever an 8 byte return into rax.
void Executor::model_random() {

  static int timesRandomIsCalled = 0;
  timesRandomIsCalled++;

  printf("INTERPRETER: calling model_random() \n");   
  //printf(" DEBUG 1: conc store at 0x%llx \n", target_ctx_gregs_OS->concreteStore );

  //Make buf have symbolic value.
  void * randBuf = malloc(4);
  //klee_make_symbolic (randBuf,4,"RandomSysCall");
  printf(" Called malloc to create buf at 0x%lx \n", (uint64_t) randBuf);
  
  //Get the MO, then call executeMakeSymbolic()
  MemoryObject * randBuf_MO = memory->allocateFixed( (uint64_t) randBuf,4,NULL);
  std::string nameString = "randCall" + std::to_string(timesRandomIsCalled);
  randBuf_MO->name = nameString;
  executeMakeSymbolic(*GlobalExecutionStatePtr, randBuf_MO, "modelRandomBuffer");
  const ObjectState *constRandBufOS = GlobalExecutionStatePtr->addressSpace.findObject(randBuf_MO);
  ObjectState * rand_buf_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(randBuf_MO,constRandBufOS);

  ref <Expr> psnRand = rand_buf_OS->read(0,Expr::Int32);
  
  int objSize = rand_buf_OS->size;
  
  for (int i = 0; i < objSize; i++) {
    printf("in obj location %d is val 0x%x \n", i, rand_buf_OS->concreteStore[i]);
  }
  
  //printf(" conc store is at 0x%llx, obj represents 0x%llx \n", &rand_buf_OS->concreteStore, rand_buf_OS->getObject()->address);
  
  //rand_buf_OS->print();
  
  //printf("Finished print call \n");

  //printf("conc store at 0x%llx \n", target_ctx_gregs_OS->concreteStore );
  
  target_ctx_gregs_OS->write(REG_RAX * 8,psnRand );
  printf("Called write in model_rand \n");
  
  //target_ctx_gregs_OS->print();
  
  /*
  //TODO: Make this more robust later for call instructions with more than 5 bytes.
  //TODO: Add asserts to make sure RIP and RSP aren't symbolic.
  */
  target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
  printf("INTERPRETER: Exiting model_random \n");
  
  printf("Ctx after modeling is ... \n");
  printCtx(target_ctx_gregs);  
  
}




//strncat model-------
//Using implementation from https://linux.die.net/man/3/strncat :
//Start copying at end of string in dest located at &dest + destLength
/*
  for (i = 0 ; i < len && (src[i] != '\0'); i++) {
  dest[destLength + i] = src[i];
  }
  dest[destLength + i] = '\0';
*/
void Executor::model_strncat() {
  //In our testing for fruitbasket this represents a send point.
  printf("INTERPRETER: Entering model_strncat() \n");
  static int timesInStrncat =0;
  timesInStrncat++;
  printf("Entered strncat %d times \n", timesInStrncat);
  uint64_t rawArg1Val = target_ctx_gregs[REG_RDI]; //Ptr to string we append to.
  uint64_t rawArg2Val = target_ctx_gregs[REG_RSI]; //Ptr to string we're appending to arg1
  uint64_t rawArg3Val = target_ctx_gregs[REG_RDX]; //Max number of bytes to append
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); 
  
  uint64_t actualLength;  // number of chars printed.
  
  // Case 1: Check to see if all the args are concrete for fast path resolution
  if ( (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ) {
    
    char * dest = (char *) rawArg1Val;
    char * src  = (char *) rawArg2Val;
    uint64_t len = rawArg3Val;
    uint64_t destLength = strlen(dest);
    uint64_t destFirstAvailableByte = rawArg1Val + destLength;
    uint64_t i;

    actualLength = std::min(len, strlen(src));
    for (i = 0; i < actualLength; i++) {
      ref<Expr> destAddressExpr = ConstantExpr::create( destFirstAvailableByte + i, Expr::Int64);
      ref<Expr> valueExpr       = ConstantExpr::create( (uint8_t)(*(src + i)), Expr::Int8);
      executeMemoryOperation(*GlobalExecutionStatePtr, /*isWrite*/ true,  destAddressExpr, valueExpr, /*not used for write*/ NULL);
    }
    
    //Write the last value into dest, which is the null terminator
    ref<Expr> nullCharExpr = ConstantExpr::create(0, Expr::Int8);     
    ref<Expr> lastAddress = ConstantExpr::create(  destFirstAvailableByte + i, Expr::Int64);
    executeMemoryOperation(*GlobalExecutionStatePtr, /*isWrite*/ true, lastAddress, nullCharExpr, /*not used for write */ NULL);
    
    //Return address of dest in RAX.  In this case, should be concrete.
    //Todo: Should this actually be the last byte written to in dest, as opposed to the beginning of the array?
    ref<Expr> destAddr = ConstantExpr::create( (uint64_t) dest, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, destAddr);
  } else {
    //Case 2:  Some args are symbolic.  Not implemented yet.
    printf("INTERPRETER: CALLING MODEL_STRNCAT WITH SYMBOLIC ARGS.  NOT IMPLMENTED \n");
    target_ctx_gregs_OS->print();
    std::exit(EXIT_FAILURE);
  }

  bytes_printed += actualLength;
  printf("bytes_printed is %lu \n",bytes_printed);
  
  if (bytes_printed > message_buf_length) {
    printf("INTERPRETER: printed more bytes in verification (%lu) than message_buf_length (%lu) \n", bytes_printed, message_buf_length);
    std::exit(EXIT_FAILURE);
  }
  
  //At this point, invoke the solver and  get back to execution by bumping RIP if necessary.
  //RAX already has received the return value.
  printf("Adding constraints between test and verifier buffers \n");
  for (uint64_t j = 0; j < bytes_printed; j++) {
    ref<Expr> messageBufByteExpr = ConstantExpr::create(message_test_buffer[j],Expr::Int8);
    ref<Expr> verifierBytePrinted = basket_OS->read(j,Expr::Int8);
    ref<Expr> equalsExpr = EqExpr::create(messageBufByteExpr,verifierBytePrinted);
    addConstraint(*GlobalExecutionStatePtr, equalsExpr);
    //equalsExpr->dump();
    //solve and signal back to parent process.
  }

  printf("Calling solver... \n");
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != GlobalExecutionStatePtr->symbolics.size(); ++i) {
    objects.push_back(GlobalExecutionStatePtr->symbolics[i].second);
  }
  bool success = solver->getInitialValues(*GlobalExecutionStatePtr, objects, values);
  if (success){
    printf("Solver success \n");  
  }
  else {
    printf("Solver failed \n");
    std::exit(EXIT_FAILURE);
  }
  
  if (bytes_printed == message_buf_length  && success) {
    printf("SUCCESS: All messages accounted for \n");

    std::string successIDString = "SUCCESS." +  workerIDStream.str(); // pre-pend "SUCCESS." to process ID string
    freopen(successIDString.c_str(),"w",stdout);
    
    freopen(successIDString.c_str(),"w",stderr);

    write(worker2managerFD[1], "SUCCESS", 8);
    printf("Just wrote? \n");
    for ( unsigned k =0; k < GlobalExecutionStatePtr->symbolics.size(); k++) {
      printf("Symbolic var name: %s \n", GlobalExecutionStatePtr->symbolics[k].first->name.c_str());
      printf("Symbolic var solution: ");
      for (unsigned m =0; m < values[k].size(); m++ )
	printf(" %d ", values[k][m]);
      printf("\n");
    }
    std::exit(EXIT_SUCCESS);
  } else if (bytes_printed <= message_buf_length && success) {
    target_ctx_gregs[REG_RIP] += 5;  //Increment RIP and get back to execution
  } else {
    printf("ERROR in model strncat() \n"); //Fallthrough case should never be reached
    std::exit(EXIT_FAILURE);
  }

}

void Executor::model_entertran() {
//Movb 0x0 into springboard_flags -- todo: double check this later
  *((uint8_t *) &springboard_flags) = 0;
  //Jump to address in r15
  target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_R15];
}

  //Set the next instruction to the value in R15, since that's what would happen
  //in sb.exittran (along with poison checks we don't care about for the intepreter)
void Executor::model_exittran() {
  //Todo: add an assert later that R15 isn't symbolic, and if it is, potentially fork.
  printf("Entered model_exittran() \n ");
  target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_R15];
  return;
}

void Executor::model_reopentran() {
  printf("Calling model_reopentran() \n ");
//Movb 0x1 into springboard_flags -- todo: double check this later
  *((uint8_t *) &springboard_flags) = 1;
  //Jump to address in r15
  target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_R15];
}


//Todo:  Make fastpath lookup where we leverage poison checking instead of
//full lookup.
//tase_helper_read: This func exists because reads and writes to
//object states in klee need to happen as offsets to buffers that have
//already been allocatated.  
ref<Expr> Executor::tase_helper_read (uint64_t addr, uint8_t byteWidth) {

  ref<Expr> addrExpr = ConstantExpr::create(addr, Expr::Int64);

  //Find a better way to do this.  Using logic from Executor::executeMemoryOperation
  //begin gross------------------
  ObjectPair op;
  bool success;
  if (! GlobalExecutionStatePtr->addressSpace.resolveOne(*GlobalExecutionStatePtr,solver,addrExpr, op, success) ) {
    printf("ERROR in tase_helper_read: Couldn't resolve addr to fixed value \n");
    std::exit(EXIT_FAILURE);
  }
  ref<Expr> offset = op.first->getOffsetExpr(addrExpr);
  //end gross----------------------------
  ref<Expr> returnVal;

  ObjectState *wos = GlobalExecutionStatePtr->addressSpace.getWriteable(op.first, op.second);
  switch (byteWidth) {
  case 1:
    returnVal = wos->read(offset, Expr::Int8);  
    break;
  case 2:
    returnVal = wos->read(offset, Expr::Int16);
    break;
  case 4:
    returnVal = wos->read(offset, Expr::Int32);
    break;
  case 8:
    returnVal = wos->read(offset, Expr::Int64);
    break;
  default:
    printf("Unrecognized byteWidth in tase_helper_read: %u \n", byteWidth);
    std::exit(EXIT_FAILURE);
  }

  if (returnVal != NULL) 
    return returnVal;
  else {
    printf("ERROR: Returned NULL from tase_helper_read() \n");
    std::exit(EXIT_FAILURE);
  }
    
}

//Todo: See if we can make this faster with the poison checking scheme.
//tase_helper_write: Helper function similar to tase_helper_read
//that helps us avoid rewriting the offset-lookup logic over and over.
void Executor::tase_helper_write (uint64_t addr, ref<Expr> val) {
  
  ref<Expr> addrExpr = ConstantExpr::create(addr, Expr::Int64);

  //Find a better way to do this.  Using logic from Executor::executeMemoryOperation
  //begin gross------------------
  ObjectPair op;
  bool success;
  if (! GlobalExecutionStatePtr->addressSpace.resolveOne(*GlobalExecutionStatePtr,solver,addrExpr, op, success) ) {
    printf("ERROR in tase_helper_write: Couldn't resolve addr to fixed value \n");
    std::exit(EXIT_FAILURE);
  }
  ref<Expr> offset = op.first->getOffsetExpr(addrExpr);
  //end gross----------------------------
  ObjectState *wos = GlobalExecutionStatePtr->addressSpace.getWriteable(op.first, op.second);
  
  wos->write(offset, val);  
}
  
  
 
bool Executor::gprsAreConcrete() {
  return target_ctx_gregs_OS->isObjectEntirelyConcrete();
}

bool Executor::instructionBeginsTransaction(uint64_t pc) {
  if (pc == (uint64_t) &sb_entertran || pc == (uint64_t) &sb_reopen) 
    return true;
  else
    return false; 
}


//Function assumes PC has been incremented to point to next instruction
bool Executor::resumeNativeExecution (){
  greg_t * registers = target_ctx_gregs;

if (gprsAreConcrete() && (instructionBeginsTransaction(registers[REG_RIP]))  ) {
   return true;
   } else {
      return false;
   }
}

//Look for a model for the current instruction at target_ctx_gregs[REG_RIP]
//and call the model.
void Executor::model_inst () {

  uint64_t rip = target_ctx_gregs[REG_RIP];
  int  callqOpc = 0xe8;

  
  uint8_t * oneBytePtr = (uint8_t *) rip;
  uint8_t firstByte = *oneBytePtr;

  
  if (firstByte == callqOpc)  { 
    printf("INTERPRETER: Modeling call \n");
    uint64_t dest = target_ctx_gregs[REG_RAX]; //Grab address of func to model from rax    
    if (dest == (uint64_t) &random) {
      model_random();
    }else if (dest == (uint64_t) &strncat) {
      model_strncat();
    }else {
      printf("INTERPRETER: Couldn't find function model \n");
      std::exit(EXIT_FAILURE);
    }
    return;
  }else if (rip == (uint64_t) &sb_entertran) {
    model_entertran();
  }else if (rip == (uint64_t) &sb_exittran) {
    model_exittran();
  }else if (rip == (uint64_t) &sb_reopen) {
    model_reopentran();
  }else {
    printf("INTERPRETER: Couldn't find  model \n");
    std::exit(EXIT_FAILURE);
  }
  return;
  
}

bool isSpecialInst (uint64_t rip) {

  //printf("Entering isSpecialInst() \n");
  //TO-DO: Add more opcodes here later on.
  uint8_t callqOpc = 0xe8;

  
  uint8_t * oneBytePtr = (uint8_t *) rip;
  uint8_t firstByte = *oneBytePtr;

  
  if (firstByte == callqOpc)  { 
     printf("Found Call. Checking to see if fn is modeled \n");

     //This is ugly, but we need to check to see if the 
     //function we're calling is explicitly modeled in our interpreter.
     //That means we have to add the number after the call instruction
     //to the current instruction pointer.
     oneBytePtr++;
     uint32_t * fourBytePtr = (uint32_t *) oneBytePtr;
     //printf("fourBytePtr is 0x%lx \n", fourBytePtr);
     uint32_t offset = *fourBytePtr;
     //printf("offset is 0x%lx \n",offset);
     //Must add 5 to account for call 0xe8 opcode and the 4 bytes for offset
     uint64_t dest = rip + (uint64_t) offset + 5;
     //Checks are here to see if the dest is a function we model.
     if (dest == (uint64_t) &sb_enter_modeled) {
       return true;
     } else {
       return false;
     }

  } else if ((uint64_t)rip == (uint64_t) &sb_entertran || (uint64_t)rip == (uint64_t) &sb_reopen || (uint64_t)rip == (uint64_t) &sb_exittran  ) {
    return true;
  }  else {
    return false;
  }
}

void Executor::klee_interp_internal () {
  static int interpCtr = 0;
  interpCtr++;
  int max = 1000;
  if (interpCtr > max) {
    printf("Hit interp counter %d times. Something is probably wrong. \n\n",interpCtr);
    std::exit(EXIT_FAILURE);
  }
  printf("------------------------------------------- \n");
  printf("Entering interpreter for time %d \n \n \n", interpCtr);
   uint64_t rip = target_ctx_gregs[REG_RIP];
  printf("RIP is %lu in decimal, 0x%lx in hex.\n", rip, rip);
  printf("Initial ctx BEFORE interpretation is \n");
  printCtx(target_ctx_gregs);
  printf("\n");
  std::cout.flush();
  
  if (isSpecialInst(rip)) {
    printf("INTERPRETER: FOUND SPECIAL MODELED INST \n");
    std::cout.flush();
    model_inst();
  } else {
    
    printf("INTERPRETER: FOUND NORMAL USER INST \n");
    std::cout.flush();
    KFunction * interpFn = findInterpFunction (target_ctx_gregs, kmodule);
  
    //We have to manually push a frame on for the function we'll be
    //interpreting through.  At this point, no other frames should exist
    // on klee's interpretation "stack".
    GlobalExecutionStatePtr->pushFrame(0,interpFn);
    GlobalExecutionStatePtr->pc = interpFn->instructions ;
    GlobalExecutionStatePtr->prevPC = GlobalExecutionStatePtr->pc;
    
    printf("Pushing back args ... \n");
    std::vector<ref<Expr> > arguments;
    std::cout.flush();
    assert(target_ctx_gregs_MO);
    uint64_t regAddr = (uint64_t) &target_ctx_gregs;
    ref<ConstantExpr> regExpr = ConstantExpr::create(regAddr, Context::get().getPointerWidth());
    arguments.push_back(regExpr);

    assert(GlobalExecutionStatePtr);
    bindArgument(interpFn, 0, *GlobalExecutionStatePtr, arguments[0]);
    //printf("Calling statsTracker...\n");
    if (statsTracker)
      statsTracker->framePushed(*GlobalExecutionStatePtr, 0);

    std::cout.flush();
    //AH: This haltExecution thing is to exit out of the interpreter loop.
    haltExecution = false;
    printf("Calling run! \n ");
    run(*GlobalExecutionStatePtr);
    std::cout.flush();
    if (statsTracker)
      statsTracker->done();
  }

  //We always require a completely concrete RIP for execution, so deal with it here
  //if it's a symbolic expression we didn't fork on.

  ref<Expr> RIPExpr = target_ctx_gregs_OS->read(REG_RIP * 8, Expr::Int64);
  if (!(isa<ConstantExpr>(RIPExpr))) {
    printf("Calling forkOnPossibleRIPValues() \n");
    std::cout.flush();
    forkOnPossibleRIPValues(RIPExpr);
  }
  
  ref<Expr> FinalRIPExpr = target_ctx_gregs_OS->read(REG_RIP * 8, Expr::Int64);
  if (!(isa<ConstantExpr>(FinalRIPExpr))) {
    printf("ERROR: Failed to concretize RIP \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

  //Todo -- Add check to make sure we haven't made more than one execution state.

  
  printf("Finished round %d of interpretation. \n", interpCtr);
  printf("-------------------------------------------\n");
  if (resumeNativeExecution()) {
    printf("--------RETURNING TO TARGET--------------------- \n");
    return;
  }  else {
    GlobalInterpreter->klee_interp_internal();
  }  
  return;
}

//Take an Expr and find all the possible concrete solutions.
//Hopefully there's a better builtin function in klee that we can
//use, but if not this should do the trick.  Intended to be used
//to help us get all possible concrete values of RIP (has dependency on RIP).
void Executor::forkOnPossibleRIPValues (ref <Expr> inputExpr) {

  int maxSolutions = 2; //Completely arbitrary.  Should not be more than 2 for our use cases in TASE
  //or we're in trouble anyway.

  int numSolutions = 0;
  
  while (true) {
    ref<ConstantExpr> solution;
    numSolutions++;
    printf("Looking at solution number %d in forkOnPossibleRIPValues() \n", numSolutions);
    if (numSolutions > maxSolutions) {
      printf("Found too many symbolic values for RIP \n ");
      std::exit(EXIT_FAILURE);
    }
      
    bool success = solver->getValue(*GlobalExecutionStatePtr, inputExpr, solution);
    if (!success) {
      printf("ERROR: couldn't get initial value in forkOnPossibleRIPValues \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    } 

    int pid = ::fork();
    int i = getpid();
    workerIDStream << ".";
    workerIDStream << i;
    std::string pidString ;
    pidString = workerIDStream.str();
    freopen(pidString.c_str(),"w",stdout);
    freopen(pidString.c_str(),"w",stderr);

    if (pid == 0) { //Rule out latest solution and see if more exist
      ref<Expr> notEqualsSolution = NotExpr::create(EqExpr::create(inputExpr,solution));
      addConstraint(*GlobalExecutionStatePtr, notEqualsSolution);
    } else { // Take the concrete value of solution and explore that path.  
      addConstraint(*GlobalExecutionStatePtr, EqExpr::create(inputExpr, solution));
      target_ctx_gregs_OS->write(REG_RIP*8, solution);
      break;
    }
  }
}

void printCtx(gregset_t registers ) {

  printf("R8   : 0x%llx \n", registers[REG_R8]);
  printf("R9   : 0x%llx \n", registers[REG_R9]);
  printf("R10  : 0x%llx \n", registers[REG_R10]);
  printf("R11  : 0x%llx \n", registers[REG_R11]);
  printf("R12  : 0x%llx \n", registers[REG_R12]);
  printf("R13  : 0x%llx \n", registers[REG_R13]);
  printf("R14  : 0x%llx \n", registers[REG_R14]);
  printf("R15  : 0x%llx \n", registers[REG_R15]);
  printf("RDI  : 0x%llx \n", registers[REG_RDI]);
  printf("RSI  : 0x%llx \n", registers[REG_RSI]);
  printf("RBP  : 0x%llx \n", registers[REG_RBP]);
  printf("RBX  : 0x%llx \n", registers[REG_RBX]);
  printf("RDX  : 0x%llx \n", registers[REG_RDX]);
  printf("RAX  : 0x%llx \n", registers[REG_RAX]);
  printf("RCX  : 0x%llx \n", registers[REG_RCX]);
  printf("RSP  : 0x%llx \n", registers[REG_RSP]);
  printf("RIP  : 0x%llx \n", registers[REG_RIP]);
  printf("EFL  : 0x%llx \n", registers[REG_EFL]);
  printf("CSGSFS : 0x%llx \n", registers[REG_CSGSFS]);
  printf("ERR  : 0x%llx \n", registers[REG_ERR]);
  printf("TRAPNO : 0x%llx \n", registers[REG_TRAPNO]);
  printf("OLDMASK : 0x%llx \n", registers[REG_OLDMASK]);
  printf("CR2  : 0x%llx \n", registers[REG_CR2]);

  return;
}

void Executor::initializeInterpretationStructures (Function *f) {

  printf("INITIALIZING INTERPRETATION STRUCTURES \n");

  printf("Creating new execution state \n");
  GlobalExecutionStatePtr = new ExecutionState(kmodule->functionMap[f]);

  //AH: We may not actually need this...
  printf("Initializing globals ... \n");
  initializeGlobals(*GlobalExecutionStatePtr);
  //initializeGlobals(*GlobalExecutionStatePtr);
  
  //Set up the KLEE memory object for the stack, and back the concrete store with the actual stack.
  //Need to be careful here.  The buffer we allocate for the stack is char [X] target_stack. It
  // starts at address StackBase and covers up to StackBase + sizeof(target_stack) -1.
  
  printf("Adding structures to track target stack starting at 0x%p with size %lu \n", StackBase, sizeof(target_stack));
  printf("target stack base ptr is 0x%p \n", target_stack_begin_ptr);
  printf("interp stack base ptr is 0x%p \n", interp_stack_begin_ptr);
  
  MemoryObject * stackMem = addExternalObject(*GlobalExecutionStatePtr,StackBase, sizeof(target_stack), false );
  const ObjectState *stackOS = GlobalExecutionStatePtr->addressSpace.findObject(stackMem);
  ObjectState * stackOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(stackMem,stackOS);  
  printf("Setting concrete store to point to target stack \n");
  stackOSWrite->concreteStore = (uint8_t *) StackBase;

  printf("Adding external object target_ctx_gregs_MO \n");
  target_ctx_gregs_MO = addExternalObject(*GlobalExecutionStatePtr, (void *) &target_ctx_gregs, sizeof (target_ctx_gregs), false );
  printf("target_ctx_gregs is address %lu, hex 0x%p \n", (uint64_t) &target_ctx_gregs, (void *) &target_ctx_gregs);
  printf("target_ctx_gregs is located at address %lu, hex 0x%p \n", (uint64_t) (&target_ctx_gregs), (void *) &target_ctx_gregs);
  printf("target_ctx_gregs_MO represents address %lu, hex 0x%p \n", (uint64_t) target_ctx_gregs_MO->address, (void *) &target_ctx_gregs);

  printf("Size of gregs is %lu \n", sizeof(target_ctx_gregs));
  
  printf("Setting concrete store in target_ctx_gregs_OS to target_ctx_gregs \n");
  const ObjectState *targetCtxOS = GlobalExecutionStatePtr->addressSpace.findObject(target_ctx_gregs_MO);
  target_ctx_gregs_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(target_ctx_gregs_MO,targetCtxOS);
  target_ctx_gregs_OS->concreteStore = (uint8_t *) &target_ctx_gregs;
  
  printf("Adding external object prev_ctx_gregs_MO \n");
  prev_ctx_gregs_MO = addExternalObject(*GlobalExecutionStatePtr,(void *)&prev_ctx_gregs, sizeof(prev_ctx_gregs), false );
  printf("prev_ctx_gregs is address %lu, hex 0x%p \n", (uint64_t) &prev_ctx_gregs, (void *) &prev_ctx_gregs);
  printf("prev_ctx_gregs_MO represents address %lu, hex 0x%p \n", (uint64_t) prev_ctx_gregs_MO->address, (void *) &prev_ctx_gregs);
  printf("Setting concrete store in prev_ctx_gregs_OS to prev_ctx_gregs \n");
  const ObjectState *prevCtxOS = GlobalExecutionStatePtr->addressSpace.findObject(prev_ctx_gregs_MO);
  prev_ctx_gregs_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(prev_ctx_gregs_MO,prevCtxOS);
  prev_ctx_gregs_OS->concreteStore = (uint8_t *) &prev_ctx_gregs;

  assert( ((uint8_t *) &prev_ctx_gregs) == (prev_ctx_gregs_OS->concreteStore));
  assert( ((uint8_t *) &target_ctx_gregs) == (target_ctx_gregs_OS->concreteStore)); 

  
  printf("IMMEDIATELY AFTER ADDING extern objs:  target_ctx_gregs conc store at %lx \n",(uint64_t) &(target_ctx_gregs_OS->concreteStore[0]));
  printf("Adding structure to track verification message output \n");
  basket_MO = addExternalObject(*GlobalExecutionStatePtr,(void *)&basket, BUFFER_SIZE, false );
  const ObjectState * basketOS = GlobalExecutionStatePtr->addressSpace.findObject(basket_MO);
  basket_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(basket_MO,basketOS);
  basket_OS->concreteStore = (uint8_t *) &basket;
  
  //Map in globals into klee from .vars file.
  //Todo: Needs to be tested after we get everything compiling again.
  //Also need to make a non-hardcoded file path and project name below.
  char * varsFileLocation = "/playpen/humphries/tase/TASE/test/fruit_basket.vars";
  FILE * externalsFile = fopen (varsFileLocation, "r+");

  if (externalsFile == NULL) {
    printf("Error reading externals file within initializeInterpretationStructures() \n");
    std::exit(EXIT_FAILURE);
  }

 
  printf("basket located at 0x%lx \n", (uint64_t) &basket);
  while (true) {
    char addr [30];
    char size [30];

    if ( fscanf (externalsFile, "%s", addr) != EOF)
      printf("Found global var with addr %s ", addr);
    else
      break;
    
    if (fscanf (externalsFile, "%s", size) != EOF)
      printf("and size %s \n", size);
    else
      break;

    uint64_t addrVal;
    uint64_t sizeVal;
    
    std::stringstream addrStream;
    addrStream << std::hex << addr;
    addrStream >> addrVal;

    std::stringstream sizeStream;
    sizeStream << std::hex << size;
    sizeStream >> sizeVal;
    
    printf ("After parsing,  global addr is 0x%lx, ", addrVal);
    printf (" and sizeVal is 0x%lx \n", sizeVal);


    //Todo:  Get rid of the special cases below for target_ctx_gregs and basket.
    //The checks are there now to prevent us from creating two different MO/OS
    //for the variables, since we map them above already.
    if ((uint64_t) addrVal == (uint64_t) &target_ctx_gregs) {
      printf("Found target_ctx_gregs while mapping extern symbols \n");
      continue;
    }
    if ((uint64_t) addrVal == (uint64_t) &basket) {
      printf("Found basket while mapping extern symbols \n ");
      continue;
    }
          
    MemoryObject * GlobalVarMO = addExternalObject(*GlobalExecutionStatePtr,(void *) addrVal, sizeVal, false );
    const ObjectState * ConstGlobalVarOS = GlobalExecutionStatePtr->addressSpace.findObject(GlobalVarMO);
    ObjectState * GlobalVarOS = GlobalExecutionStatePtr->addressSpace.getWriteable(GlobalVarMO,ConstGlobalVarOS);
    //Technically I think this is redundant, with addExternalObject changed in tase. 
    GlobalVarOS->concreteStore = (uint8_t *) addrVal;
    
  }

  printf("PRIOR TO INITIALIZEINTERPSTRUCTS:  target_ctx_gregs conc store at %lx \n", (uint64_t)&(target_ctx_gregs_OS->concreteStore[0]));

  //Get rid of the dummy function used for initialization
  GlobalExecutionStatePtr->popFrame();
  processTree = new PTree(GlobalExecutionStatePtr);
  GlobalExecutionStatePtr->ptreeNode = processTree->root;

  printf("END OF INITIALIZEINTERPSTRUCTS:  target_ctx_gregs conc store at %lx \n",(uint64_t) &(target_ctx_gregs_OS->concreteStore[0]));

  //std::exit(EXIT_SUCCESS);
  
}
				   

void Executor::runFunctionAsMain(Function *f,
				 int argc,
				 char **argv,
				 char **envp) {
  std::vector<ref<Expr> > arguments;

  // force deterministic initialization of memory objects
  srand(1);
  srandom(1);
  
  MemoryObject *argvMO = 0;

  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  for (envc=0; envp[envc]; ++envc) ;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];
  assert(kf);
  Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {
    arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));
    if (++ai!=ae) {
      Instruction *first = &*(f->begin()->begin());
      argvMO =
         memory->allocate((argc + 1 + envc + 1 + 1) * NumPtrBytes,
                           /*isLocal=*/false, /*isGlobal=*/true,
                           /*allocSite=*/first, /*alignment=*/8);

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

  ExecutionState *state = new ExecutionState(kmodule->functionMap[f]);
  
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

        MemoryObject *arg =
            memory->allocate(len + 1, /*isLocal=*/false, /*isGlobal=*/true,
                             /*allocSite=*/state->pc->inst, /*alignment=*/8);
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

  //AH addition
  /*
  if (true) {
    MemoryObject * stackMem = addExternalObject(*state,(void *)targetMemAddr, 8, false );
    const ObjectState *stackOS = state->addressSpace.findObject(stackMem);
    ObjectState * stackOSWrite = state->addressSpace.getWriteable(stackMem,stackOS);
    stackOSWrite->concreteStore = (uint8_t *) targetMemAddr;
  }
  */
  processTree = new PTree(state);
  state->ptreeNode = processTree->root;
  run(*state);
  delete processTree;
  processTree = 0;

  // hack to clear memory objects
  delete memory;
  memory = new MemoryManager(NULL);

  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();
}

unsigned Executor::getPathStreamID(const ExecutionState &state) {
  assert(pathWriter);
  return state.pathOS.getID();
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
  assert(symPathWriter);
  return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state, std::string &res,
                                Interpreter::LogType logFormat) {

  switch (logFormat) {
  case STP: {
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    char *log = solver->getConstraintLog(query);
    res = std::string(log);
    free(log);
  } break;

  case KQUERY: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprPPrinter::printConstraints(info, state.constraints);
    res = info.str();
  } break;

  case SMTLIB2: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprSMTLIBPrinter printer;
    printer.setOutput(info);
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    printer.setQuery(query);
    printer.generateOutput();
    res = info.str();
  } break;

  default:
    klee_warning("Executor::getConstraintLog() : Log format not supported!");
  }
}

bool Executor::getSymbolicSolution(const ExecutionState &state,
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) {
  solver->setTimeout(coreSolverTimeout);

  ExecutionState tmp(state);

  // Go through each byte in every test case and attempt to restrict
  // it to the constraints contained in cexPreferences.  (Note:
  // usually this means trying to make it an ASCII character (0-127)
  // and therefore human readable. It is also possible to customize
  // the preferred constraints.  See test/Features/PreferCex.c for
  // an example) While this process can be very expensive, it can
  // also make understanding individual test cases much easier.
  for (unsigned i = 0; i != state.symbolics.size(); ++i) {
    const MemoryObject *mo = state.symbolics[i].first;
    std::vector< ref<Expr> >::const_iterator pi = 
      mo->cexPreferences.begin(), pie = mo->cexPreferences.end();
    for (; pi != pie; ++pi) {
      bool mustBeTrue;
      // Attempt to bound byte to constraints held in cexPreferences
      bool success = solver->mustBeTrue(tmp, Expr::createIsZero(*pi), 
					mustBeTrue);
      // If it isn't possible to constrain this particular byte in the desired
      // way (normally this would mean that the byte can't be constrained to
      // be between 0 and 127 without making the entire constraint list UNSAT)
      // then just continue on to the next byte.
      if (!success) break;
      // If the particular constraint operated on in this iteration through
      // the loop isn't implied then add it to the list of constraints.
      if (!mustBeTrue) tmp.addConstraint(*pi);
    }
    if (pi!=pie) break;
  }

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);
  bool success = solver->getInitialValues(tmp, objects, values);
  solver->setTimeout(0);
  if (!success) {
    klee_warning("unable to compute initial values (invalid constraints?)!");
    ExprPPrinter::printQuery(llvm::errs(), state.constraints,
                             ConstantExpr::alloc(0, Expr::Bool));
    return false;
  }
  
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    res.push_back(std::make_pair(state.symbolics[i].first->name, values[i]));
  return true;
}

void Executor::getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res) {
  res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(ExecutionState &state,
                                            ref<Expr> e,
                                            ref<ConstantExpr> value) {
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver, e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);
  for (ImpliedValueList::iterator it = results.begin(), ie = results.end();
       it != ie; ++it) {
    ReadExpr *re = it->first.get();
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
      // FIXME: This is the sole remaining usage of the Array object
      // variable. Kill me.
      const MemoryObject *mo = 0; //re->updates.root->object;
      const ObjectState *os = state.addressSpace.findObject(mo);

      if (!os) {
        // object has been free'd, no need to concretize (although as
        // in other cases we would like to concretize the outstanding
        // reads, but we have no facility for that yet)
      } else {
        assert(!os->readOnly && 
               "not possible? read only object with static read?");
        ObjectState *wos = state.addressSpace.getWriteable(mo, os);
        wos->write(CE, it->second);
      }
    }
  }
}

Expr::Width Executor::getWidthForLLVMType(llvm::Type *type) const {
  return kmodule->targetData->getTypeSizeInBits(type);
}

size_t Executor::getAllocationAlignment(const llvm::Value *allocSite) const {
  // FIXME: 8 was the previous default. We shouldn't hard code this
  // and should fetch the default from elsewhere.
  const size_t forcedAlignment = 8;
  size_t alignment = 0;
  llvm::Type *type = NULL;
  std::string allocationSiteName(allocSite->getName().str());
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(allocSite)) {
    alignment = GV->getAlignment();
    if (const GlobalVariable *globalVar = dyn_cast<GlobalVariable>(GV)) {
      // All GlobalVariables's have pointer type
      llvm::PointerType *ptrType =
          dyn_cast<llvm::PointerType>(globalVar->getType());
      assert(ptrType && "globalVar's type is not a pointer");
      type = ptrType->getElementType();
    } else {
      type = GV->getType();
    }
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(allocSite)) {
    alignment = AI->getAlignment();
    type = AI->getAllocatedType();
  } else if (isa<InvokeInst>(allocSite) || isa<CallInst>(allocSite)) {
    // FIXME: Model the semantics of the call to use the right alignment
    llvm::Value *allocSiteNonConst = const_cast<llvm::Value *>(allocSite);
    const CallSite cs = (isa<InvokeInst>(allocSiteNonConst)
                             ? CallSite(cast<InvokeInst>(allocSiteNonConst))
                             : CallSite(cast<CallInst>(allocSiteNonConst)));
    llvm::Function *fn =
        klee::getDirectCallTarget(cs, /*moduleIsFullyLinked=*/true);
    if (fn)
      allocationSiteName = fn->getName().str();

    klee_warning_once(fn != NULL ? fn : allocSite,
                      "Alignment of memory from call \"%s\" is not "
                      "modelled. Using alignment of %zu.",
                      allocationSiteName.c_str(), forcedAlignment);
    alignment = forcedAlignment;
  } else {
    llvm_unreachable("Unhandled allocation site");
  }

  if (alignment == 0) {
    assert(type != NULL);
    // No specified alignment. Get the alignment for the type.
    if (type->isSized()) {
      alignment = kmodule->targetData->getPrefTypeAlignment(type);
    } else {
      klee_warning_once(allocSite, "Cannot determine memory alignment for "
                                   "\"%s\". Using alignment of %zu.",
                        allocationSiteName.c_str(), forcedAlignment);
      alignment = forcedAlignment;
    }
  }

  // Currently we require alignment be a power of 2
  if (!bits64::isPowerOfTwo(alignment)) {
    klee_warning_once(allocSite, "Alignment of %zu requested for %s but this "
                                 "not supported. Using alignment of %zu",
                      alignment, allocSite->getName().str().c_str(),
                      forcedAlignment);
    alignment = forcedAlignment;
  }
  assert(bits64::isPowerOfTwo(alignment) &&
         "Returned alignment must be a power of two");
  return alignment;
}

void Executor::prepareForEarlyExit() {
  if (statsTracker) {
    // Make sure stats get flushed out
    statsTracker->done();
  }
}
///

Interpreter *Interpreter::create(LLVMContext &ctx, const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new Executor(ctx, opts, ih);
}
