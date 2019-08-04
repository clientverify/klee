
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
#include <iostream>
#include "klee/CVAssignment.h"
#include "klee/util/ExprUtil.h"
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <fcntl.h>
#include "../../../test/tase/include/tase/tase_interp.h"
#include "tase/TASEControl.h"
//#include <signal.h>
//Can't include signal.h directly if it has conflicts with our tase_interp.h definitions
extern "C"  void ( *signal(int signum, void (*handler)(int)) ) (int){
  return NULL;
  }

extern "C" {
  void * calloc_tase (unsigned long num, unsigned long size);
  void * realloc_tase (void * ptr, unsigned long new_size);
  void * malloc_tase(unsigned long s);
  void   free_tase(void * ptr);
  } 

//Symbols we need to map in for TASE
extern char edata;
extern char __GNU_EH_FRAME_HDR,  _IO_stdin_used; //used for mapping .rodata section
extern int __ctype_tolower;
extern char ** environ;
extern int * __errno_location();
extern "C" int __isoc99_sscanf ( const char * s, const char * format, ...);

//TASE internals--------------------
extern uint16_t poison_val;
enum runType : int {INTERP_ONLY, MIXED};
extern std::string project;
extern enum runType exec_mode;
extern Module * interpModule;
extern klee::Interpreter * GlobalInterpreter;
MemoryObject * target_ctx_gregs_MO;
ObjectState * target_ctx_gregs_OS;
ExecutionState * GlobalExecutionStatePtr;
extern target_ctx_t target_ctx;
extern greg_t * target_ctx_gregs;
void printCtx(greg_t *);
#include <sys/time.h>
extern struct timeval taseStartTime;
extern struct timeval targetStartTime;
extern struct timeval targetEndTime;
extern  int ktest_master_secret_calls;
extern  int ktest_start_calls;
extern  int ktest_writesocket_calls;
extern  int ktest_readsocket_calls;
extern  int ktest_raw_read_stdin_calls;
extern  int ktest_connect_calls;
extern  int ktest_select_calls ;
extern  int ktest_RAND_bytes_calls ;
extern  int ktest_RAND_pseudo_bytes_calls;
#include <unordered_set>
extern std::unordered_set<uint64_t> cartridge_entry_points;
extern std::unordered_set<uint64_t> cartridges_with_flags_live;
void * rodata_base_ptr;
uint64_t rodata_size;
extern "C" void make_byte_symbolic(uint64_t addr);
uint64_t bounceback_offset = 14;
bool tase_buf_could_be_symbolic (void * ptr, int size);
uint64_t trap_off = 14;  //Offset from function address at which we trap

//Debug info
extern bool taseDebug;
extern bool modelDebug;
uint64_t interpCtr =0;
uint64_t instCtr=0;
int forkSolverCalls = 0;
extern std::stringstream globalLogStream;
extern std::stringstream workerIDStream;
int BB_UR = 0; //Unknown return codes
int BB_MOD = 0; //Modeled return
int BB_PSN = 0; //PSN return
int BB_OTHER = 0;//Other return
double psn_time = 0.0;
double mdl_time = 0.0;
uint64_t total_interp_returns = 0;

double run_start_time = 0;
double run_end_time = 0;
double run_interp_time = 0;
double run_fork_time = 0;
double run_solver_time = 0;


//Multipass
extern int c_special_cmds; //Int used by cliver to disable special commands to s_client.  Made global for debugging
extern bool UseForkedCoreSolver;
extern void worker_exit();
extern void multipass_reset_round();
extern void multipass_start_round(Executor * theExecutor, bool isReplay);
extern void multipass_replay_round(void * assignmentBufferPtr, CVAssignment * mpa, int thePid);
extern multipassRecord multipassInfo;
extern KTestObjectVector ktov;
extern bool enableMultipass;
extern bool enableBounceback;
extern bool killFlagsHack;
extern int passCount;
int multipass_symbolic_vars = 0;
extern CVAssignment prevMPA;

bool forceNativeRet = false;
bool dont_model = false;

extern int retryMax;

//Todo : fix these functions and remove traps

extern "C" {
  void RAND_add(const void * buf, int num, double entropy);
  int RAND_load_file(const char *filename, long max_bytes);
}


void OpenSSLDie (const char * file, int line, const char * assertion);

extern "C" {
  int RAND_poll();
  int tls1_generate_master_secret(SSL *s, unsigned char *out, unsigned char *p, int len);
  int ssl3_connect(SSL *s);
  void gcm_gmult_4bit(u64 Xi[2],const u128 Htable[16]);
  void gcm_ghash_4bit(u64 Xi[2],const u128 Htable[16],const u8 *inp,size_t len);
}

//Distinction between prohib_fns and modeled_fns is that we sometimes may want to "jump back" into native execution
//for prohib_fns.  Modeled fns are always skipped and emulated with a return.
static const uint64_t prohib_fns [] = { (uint64_t) &AES_encrypt, (uint64_t) &ECDH_compute_key, (uint64_t) &EC_POINT_point2oct, (uint64_t) &EC_KEY_generate_key, (uint64_t) &SHA1_Update, (uint64_t) &SHA1_Final, (uint64_t) &SHA256_Update, (uint64_t) &SHA256_Final, (uint64_t) &gcm_gmult_4bit, (uint64_t) &gcm_ghash_4bit, (uint64_t) &tls1_generate_master_secret };

static const uint64_t sys_mem_fns [] = {(uint64_t) &malloc_tase, (uint64_t) &realloc_tase, (uint64_t) &calloc_tase, (uint64_t) &free_tase};

bool isSpecialInst(uint64_t rip);

//Addition from cliver
std::map<std::string, uint64_t> array_name_index_map_;
std::string get_unique_array_name(const std::string &s) {
  // Look up unique name for this variable, incremented per variable name 
  return s + "_" + llvm::utostr(array_name_index_map_[s]++);
}

// Network capture for Cliver
extern "C" { int ktest_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
  int ktest_select(int nfds, fd_set *readfds, fd_set *writefds,
		   fd_set *exceptfds, struct timeval *timeout);
  ssize_t ktest_writesocket(int fd, const void *buf, size_t count);
  ssize_t ktest_readsocket(int fd, void *buf, size_t count);
  // stdin capture for Cliver
  int ktest_raw_read_stdin(void *buf, int siz);
  // Random number generator capture for Cliver
  int ktest_RAND_bytes(unsigned char *buf, int num);
  int ktest_RAND_pseudo_bytes(unsigned char *buf, int num);
  // Time capture for Cliver (actually unnecessary!)
  time_t ktest_time(time_t *t);
  // TLS Master Secret capture for Cliver
  void ktest_master_secret(unsigned char *ms, int len);
  void ktest_start(const char *filename, enum kTestMode mode);
  void ktest_finish();               // write capture to file
 
  
}



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
                                           bool isReadOnly, bool forTASE) {
  MemoryObject *mo = memory->allocateFixed((uint64_t) (unsigned long) addr, 
                                           size, 0);

  ObjectState *os = bindObjectInState(state, mo, false, NULL, forTASE);
  
  //printf("Mapping external buf: mo->address is 0x%lx, size is 0x%x \n", mo->address, size);
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
  //printf("Entering regular klee fork() \n");
  Solver::Validity res;
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&current);
  bool isSeeding = it != seedMap.end();
  double timeout = coreSolverTimeout;
  if (isSeeding)
    timeout *= it->second.size();
  solver->setTimeout(timeout);
  if (taseDebug) {
    if (!isa<ConstantExpr> (condition)) {
      printf("DEBUG:FORK ctx is \n");
      printCtx(target_ctx_gregs);
      forkSolverCalls++;
    }
  }
  bool success = solver->evaluate(current, condition, res);
  solver->setTimeout(0);
  if (!success) {
    printf("INTERPRETER: QUERY TIMEOUT \n");
    current.pc = current.prevPC;
    terminateStateEarly(current, "Query timed out (fork).");
    return StatePair(0, 0);
  }

  //Either condition is always true, always false, or we need to fork.
  if (res==Solver::True) {
    if (taseDebug) {
      printf("DEBUG:FORK - Solver returned true \n");
      std::cout.flush();
    }
    return StatePair(&current, 0);
  } else if (res==Solver::False) {
    if (taseDebug) {
      printf("Debug:FORK - Solver returned false \n");
      std::cout.flush();
    }
    return StatePair(0, &current);
  } else {
    TimerStatIncrementer timer(stats::forkTime);
    ExecutionState *falseState, *trueState = &current;
    ++stats::forks;
    //ABH: Here's where we fork in TASE.
    int parentPID = getpid();
    uint64_t rip = target_ctx_gregs[GREG_RIP].u64;
    int pid  = tase_fork(parentPID,rip);
    printf("TASE Forking at 0x%lx \n", target_ctx_gregs[GREG_RIP].u64);
    if (pid ==0 ) {
      int i = getpid(); 
      workerIDStream << ".";
      workerIDStream << i;
      std::string pidString ;
      pidString = workerIDStream.str();
      FILE * res1 = freopen(pidString.c_str(),"w",stdout);
      FILE * res2 = freopen(pidString.c_str(),"w",stderr);
      if (res1 == NULL || res2 == NULL) {
	printf("ERROR: Could not open new file for logging child output \n");
	fflush(stdout);
      }
      /*
      printf("Resetting interp time counters for this round \n");
      interpreter_time = 0.0;
      interp_setup_time = 0.0;
      interp_run_time = 0.0;
      interp_cleanup_time = 0.0;
      interp_find_fn_time = 0.0;

      mdl_time = 0.0;
      psn_time = 0.0;
      */
      
      printf("DEBUG:  Child process created\n");
      addConstraint(*GlobalExecutionStatePtr, Expr::createIsZero(condition));
    } else {
      int i = getpid();
      workerIDStream << ".";
      workerIDStream << i;
      std::string pidString ;
      pidString = workerIDStream.str();
      FILE * res1 = freopen(pidString.c_str(),"w",stdout);
      FILE * res2 = freopen(pidString.c_str(),"w",stderr);
      if (res1 == NULL || res2 == NULL) {
	printf("ERROR: Could not open new file for logging child output \n");
	fflush(stdout);
      }
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
    if (pid == 0)  {
      return StatePair(0, GlobalExecutionStatePtr);
    } else {
      return StatePair(GlobalExecutionStatePtr,0);
    }
  }
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
  printf("Entered addConstraint \n");
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    if (!CE->isTrue())
      llvm::report_fatal_error("attempt to add invalid constraint");
    return;
  }
  printf("DBG1 \n");
  fflush(stdout);
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
  printf("DBG2 \n");
  fflush(stdout);
  
  state.addConstraint(condition);
  printf("DBG3 \n");
  fflush(stdout);
  if (ivcEnabled)
    doImpliedValueConcretization(state, condition, 
                                 ConstantExpr::alloc(1, Expr::Bool));

  printf("DBG4 \n");
  fflush(stdout);
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
  ++stats::instructions;
  state.prevPC = state.pc;
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

  instCtr++;
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

    //printf("Hit br inst \n");
    BranchInst *bi = cast<BranchInst>(i);
    if (bi->isUnconditional()) {
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    } else {
      // FIXME: Find a way that we don't have this hidden dependency.
      assert(bi->getCondition() == bi->getOperand(0) &&
             "Wrong operand index!");
      ref<Expr> cond = eval(ki, 0, state).value;

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
  
  //bindModuleConstants(); AH MOVED
  //initTimers();  AH MOVED
  //AH: Changed code to NOT insert states since we only have 1 state per process;
  //states.insert(&initialState);

  if (usingSeeds) {
    printf("ERROR: Seeds not supported in TASE \n");
    std::exit(EXIT_FAILURE);
  }

  ExecutionState & state = *GlobalExecutionStatePtr;

  haltExecution = false;
  while ( !haltExecution) {

    KInstruction *ki = state.pc;
    stepInstruction(state);
    executeInstruction(state, ki);

    //ABH: We don't need these.
    //processTimers(&state, MaxInstructionTime); 
    //checkMemoryUsage();
    //updateStates(&state);
    
  }
  
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
                                         const Array *array,
					 bool forTASE ) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo, forTASE);
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

  
   if(taseDebug) {
    printf("executeMemoryOperation DBG: \n");
    printf("bytes is %d \n", bytes);
    printf("isWrite is %d \n", isWrite);
    if (!isa<ConstantExpr>(address))
	printf("Non-constant address \n");
    else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
      printf("Addr is 0x%lx \n", CE->getZExtValue());
    } else {
      printf("ERROR: addr should be constant or symbolic \n");
    } 
   }


  // fast path: single in-bounds resolution
  ObjectPair op;
  bool success;
  solver->setTimeout(coreSolverTimeout);
  if (!state.addressSpace.resolveOne(state, solver, address, op, success)) {
    printf("resolveOne failure! \n");
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(0);

  if (!success)
    printf("Could not resolve address to MO \n");
  
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
          ObjectState *wos = state.addressSpace.getWriteable(mo, os);
          wos->write(offset, value);
	  wos->applyPsnOnWrite(offset,value);

        }          
      } else {
	
	ref<Expr> result = os->read(offset, type);
	ObjectState *wos = state.addressSpace.getWriteable(mo, os);
	wos->applyPsnOnRead(offset);
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
    //printf("In resolutionList \n");
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
	  wos->applyPsnOnWrite(mo->getOffsetExpr(address), value);
        }
      } else {
        ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
	ObjectState *wos = bound->addressSpace.getWriteable(mo, os);
	wos->applyPsnOnRead(mo->getOffsetExpr(address));  //Todo: ABH - should this be on os instead of wos?
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
      if (!isa<ConstantExpr>(address))
	printf("Non-constant address \n");
      else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
	printf("Addr is 0x%lx \n", CE->getZExtValue());
      } else {
	printf("ERROR: addr should be constant or symbolic \n");
      }


      terminateStateOnError(*unbound, "memory error: out of bound pointer", Ptr,
                            NULL, getAddressInfo(*unbound, address));

      printf("Out of bound ptr error; termination \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
      
    }
  }
}

//Todo: Recheck ALL the AH addition code one more time before you rip out the orig code beneath it.
void Executor::executeMakeSymbolic(ExecutionState &state, 
                                   const MemoryObject *mo,
                                   const std::string &name) {

  static int executeMakeSymbolicCalls = 0;  
  executeMakeSymbolicCalls++;
  
  //#ifdef TASE_OPENSSL
  if (executeMakeSymbolicCalls ==1 ) {
    //Bootstrap multipass here for the very first round
    //before we hit a concretized writesocket call

    multipass_reset_round();
    multipass_start_round(this, false);
  }
  //#endif
  if(modelDebug) {
    printf("Calling executeMakeSymbolic on name %s \n", name.c_str());
    std::cout.flush();
  }

  bool unnamed = (name == "unnamed") ? true : false;
  // Create a new object state for the memory object (instead of a copy).
  std::cout.flush();
  
  std::string array_name = get_unique_array_name(name);
  
  printf("DBG executeMakeSymbolic: Encountered unique name for %s \n", array_name.c_str());
  std::cout.flush();
  //See if we have an assignment
  const klee::Array *array = NULL;
  if (!unnamed && prevMPA.bindings.size() != 0) {
    printf("Trying to find array with MP assignment \n");
    array = prevMPA.getArray(array_name);

  }
  
  bool multipass = false;
  if (array != NULL) {
    printf("Found concretization \n");
    std::cout.flush();
    //CVDEBUG("Multi-pass: Concretization found for " << array_name);
    multipass = true;
  } else {
    if (!unnamed) {
      printf("Didn't find concretization \n");
      std::cout.flush();
    }
    array = arrayCache.CreateArray(array_name, mo->size);
  }
  
  bindObjectInState(state, mo, false, array);

  std::vector<unsigned char> *bindings = NULL;
  if (passCount > 0 && multipass) {
    bindings = prevMPA.getBindings(array_name);
    
    if (!bindings || bindings->size() != mo->size) {

      printf("Bindings mismatch in executeMakeSymbolic; terminating \n");
      worker_exit();
    } else {
      const klee::ObjectState *os = state.addressSpace.findObject(mo);
      klee::ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      assert(wos && "Writeable object is NULL!");
      unsigned idx = 0;
      for (std::vector<unsigned char>::iterator it = bindings->begin();  it!= bindings->end(); it++) {
	wos->write8(idx, *it);
	idx++;
      }
    }
  } else {
    printf("DBG executeMakeSymbolic: Created symbolic var for %s \n", name.c_str());
    std::cout.flush();
    state.addSymbolic(mo, array);
    multipass_symbolic_vars++;
    std::cout.flush();
  }

}

extern "C" void target_exit() {
  printf("Target exited \n");
  printf("Executed %lu total interp instructions \n", instCtr);
  printf("Execution State has stack size %lu \n", GlobalExecutionStatePtr->stack.size());
  printf("Found %d calls to solver in fork \n", forkSolverCalls);
  
  tase_exit();

}


extern double target_start_time;
extern double target_end_time;

extern bool measureTime;

double interp_setup_time = 0.0; 
double interp_find_fn_time = 0.0; //Should also account for interp_setup_time
double interp_run_time = 0.0;
double interp_cleanup_time = 0.0;

double interp_enter_time;
double interp_exit_time;
double interpreter_time = 0.0;
double solver_time = 0.0;
double solver_start_time;
double solver_end_time;
double solver_diff_time;

double malloc_time = 0.0;
double prohib_time = 0.0;

int total_ret_codes = 0;

int AES_encrypt_calls = 0;
int ECDH_compute_key_calls = 0;
int EC_POINT_point2oct_calls = 0;
int EC_KEY_generate_key_calls = 0;
int SHA1_Update_calls = 0;
int SHA1_Final_calls = 0;
int SHA256_Update_calls = 0;
int SHA256_Final_calls = 0;
int gcm_gmult_4bit_calls = 0;
int gcm_ghash_4bit_calls = 0;


//Make sure these are actually correctly tracked
void resetProhibCounters() {
  //#ifdef TASE_OPENSSL
  AES_encrypt_calls = 0;
  ECDH_compute_key_calls = 0;
  EC_POINT_point2oct_calls = 0;
  EC_KEY_generate_key_calls = 0;
  SHA1_Update_calls = 0;
  SHA1_Final_calls = 0;
  SHA256_Update_calls = 0;
  SHA256_Final_calls = 0;
  gcm_gmult_4bit_calls = 0;
  gcm_ghash_4bit_calls = 0;
  //#endif
}

//Todo: Make sure call counters are correctly updated
void printKTestCounters() {
  //#ifdef TASE_OPENSSL
  printf("Total calls to ktest_start             %d \n",  ktest_start_calls);
  printf("Total calls to ktest_connect           %d \n", ktest_connect_calls);
  printf("Total calls to ktest_master_secret     %d \n", ktest_master_secret_calls);
  printf("Total calls to ktest_RAND_bytes        %d \n", ktest_RAND_bytes_calls);
  printf("Total calls to ktest_RAND_pseudo_bytes %d \n", ktest_RAND_pseudo_bytes_calls);
  printf("Total calls to ktest_raw_read_stdin    %d \n", ktest_raw_read_stdin_calls);
  printf("Total calls to ktest_select            %d \n", ktest_select_calls );
  printf("Total calls to ktest_writesocket       %d \n", ktest_writesocket_calls );
  printf("Total calls to ktest_readsocket        %d \n", ktest_readsocket_calls );
  fflush(stdout);
  //#endif
}

//Todo: Make sure call counters are correctly updated
void printProhibCounters() {
  //#ifdef TASE_OPENSSL
  printf("AES_encrypt calls: %d \n", AES_encrypt_calls);
  printf("ECDH_compute_key calls: %d \n", ECDH_compute_key_calls);
  printf("EC_POINT_point2oct calls: %d \n", EC_POINT_point2oct_calls);
  printf("EC_KEY_generate_key calls: %d \n", EC_KEY_generate_key_calls);
  printf("SHA1_Update calls: %d \n", SHA1_Update_calls);
  printf("SHA1_Final calls:  %d \n", SHA1_Final_calls);
  printf("SHA256_Update calls: %d \n", SHA256_Update_calls);
  printf("SHA256_Final calls: %d \n", SHA256_Final_calls);
  printf("gcm_gmult_4bit calls: %d \n", gcm_gmult_4bit_calls );
  printf("gcm_ghash_4bit calls: %d \n", gcm_ghash_4bit_calls );
  fflush(stdout);
  //#endif
}

uint64_t garbageCtr = 0;
uint64_t * last_heap_addr = 0;


void reset_run_timers() {
  printf("Resetting run timers at %lf seconds into analysis \n", util::getWallTime() - target_start_time);
  run_start_time = util::getWallTime();
  interp_enter_time = util::getWallTime();
  run_interp_time = 0;
  run_fork_time = 0;
  run_solver_time = 0;
}

void print_run_timers() {
  printf(" --- Printing run timers ----\n");
  printf("Total run time: %lf \n",  util::getWallTime() - run_start_time);
  printf(" - Interp time: %lf \n", run_interp_time);
  printf("        Solver: %lf \n", run_solver_time);
  printf(" - Fork   time: %lf \n", run_fork_time );
}
  
void measure_interp_time(bool isPsnTrap, bool isModelTrap, uint64_t interpCtr_init, uint64_t rip) {

    double interp_exit_time = util::getWallTime();
    run_interp_time += (interp_exit_time - interp_enter_time);    
    double diff_time = (interp_exit_time) - (interp_enter_time);
    interpreter_time += diff_time;

    if (isPsnTrap)
      psn_time += diff_time;
    if(isModelTrap)
      mdl_time += diff_time;
      
    printf("Elapsed time is %lf at interpCtr %lu rip 0x%lx with %lu instructions \n", diff_time, interpCtr, rip, interpCtr - interpCtr_init);
    /*
    printf("Total time in interpreter is %lf so far \n", interpreter_time);
    printf("   - modeled time:       %lf \n", mdl_time);
    printf("   - poison interp time: %lf \n", psn_time);
    printf(" Cumulative breakdown of interpreter time: \n %lf seconds total performing setup \n %lf seconds total calling run \n %lf seconds total doing cleanup \n %lf seconds total on findFunction \n", interp_setup_time, interp_run_time, interp_cleanup_time, interp_find_fn_time);
    printf( "Total interpreter returns: %lu \n",total_interp_returns);
    printf( "Abort_count_total: %d \n", target_ctx.abort_count_total);
    printf( "  - modeled %d \n", target_ctx.abort_count_modeled);
    printf( "  - poison %d \n", target_ctx.abort_count_poison);
    printf("   - unknown %d \n", target_ctx.abort_count_unknown);
    */
    printf("------------------------------\n");

    //fflush(stdout);

    
}

bool canBounceback( uint32_t abortStatus , uint64_t rip);

bool is_psn_trap = false;
bool is_model_trap = false;

extern "C" void klee_interp () {
  total_interp_returns++;
  uint64_t interpCtr_init = interpCtr;
  forceNativeRet = false;  

  
  if (measureTime) 
    interp_enter_time = util::getWallTime();
  
  if (taseDebug) {
    printf("---------------ENTERING KLEE_INTERP ---------------------- \n");
    std::cout.flush();
  }
  
  if (canBounceback(target_ctx.abort_status, target_ctx_gregs[GREG_RIP].u64))
    {    
      target_ctx_gregs[GREG_RIP].u64 -= bounceback_offset;
      if (taseDebug) 
	printf("After adjusting offset, attempting to bounceback to 0x%lx \n", target_ctx_gregs[GREG_RIP].u64);
      return;
    }
  
  GlobalInterpreter->klee_interp_internal();
  
  uint64_t rip =  target_ctx_gregs[GREG_R15].u64;
  if (measureTime) 
    measure_interp_time(is_psn_trap,  is_model_trap, interpCtr_init, rip);

  return; //Returns to loop in main
}

bool canBounceback (uint32_t abort_status, uint64_t rip) {
  
  bool retry = false;
  static int retryCtr = 0;
  static uint64_t prevRIP = 0;

  if (rip == prevRIP) {
    retryCtr++;
  } else {
    prevRIP = rip;
    retryCtr = 0;
  }

  //Classify the type of return first
  is_model_trap = false;
  is_psn_trap = false;
  
  if ((abort_status & 0xff) == 0) {
    //Unknown return code
    if (taseDebug)
      printf("Bounceback unknown return code \n");
    BB_UR++;
    retry = true; 
  } else if (abort_status & (1 << TSX_XABORT)) {
    if (abort_status & TSX_XABORT_MASK) {
      retry = false;
      is_model_trap = true;
      BB_MOD++;
    } else {
      is_psn_trap = true;
      retry = false;
      BB_PSN++;
    }
  } else {
    if (taseDebug)
      printf("Bounceback fall-through case \n");
    retry = true;
    BB_OTHER++;
  }

  if (exec_mode == MIXED && enableBounceback && retry && retryCtr < retryMax ) {
    if (taseDebug){
      printf("Attempting to bounceback to native execution at RIP 0x%lx \n", rip);
      fflush(stdout);
    }
    //Heuristic to try and avoid page faults.
    garbageCtr += target_ctx_gregs[GREG_RSP].u64;
    if (last_heap_addr != 0) {
      garbageCtr += *last_heap_addr; //Todo: this won't work if we start doing frees.
    }
    return true;
  } else {
    if (taseDebug) {
      printf("Not attempting to bounceback to native execution at RIP 0x%lx \n",rip);
      fflush(stdout);
    }
    retryCtr = 0;
    return false;
  }
}


//This function's purpose is to take a context from native execution 
//and return an llvm function for interpretation.  It's OK for now if
// this returns a KFunction with LLVM IR for only one machine instruction at a time. 
KFunction * findInterpFunction (greg_t * registers, KModule * kmod) {

  if (taseDebug) {
    printf("Attempting to find interp function \n");
    fflush(stdout);
  }
  
  uint64_t nativePC = registers[GREG_RIP].u64;
  std::stringstream converter;
  converter << std::hex << nativePC;  
  std::string hexNativePCString(converter.str());
  std::string functionName =   "interp_fn_" + hexNativePCString;
  llvm::Function * interpFn = interpModule->getFunction(functionName);
  KFunction * KInterpFunction = kmod->functionMap[interpFn];
  
  if (!KInterpFunction) {
    printf("Unable to find interp function for entrypoint PC 0x%lx \n", nativePC);
    fflush(stdout);
    worker_exit();
    std::exit(EXIT_FAILURE);
  } else {
    if (taseDebug) {
	printf("Found interp function \n");
	fflush(stdout);
      }
  }
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

//Just an external trap for making a byte symbolic
void Executor::make_byte_symbolic_model() {
  printf("Hit make_byte_symbolic_model \n");
  fflush(stdout);
  
  uint64_t addr = target_ctx_gregs[GREG_RDI].u64;
  tase_make_symbolic(addr, 1, "external_request");

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//Populate a buffer at addr with len bytes of unconstrained symbolic data.
//We make the symbolic memory object at a malloc'd address and write the bytes to addr.
void Executor::tase_make_symbolic(uint64_t addr, uint64_t len, const char * name)  {
  printf("tase_make_symbolic called on buf 0x%lx with size 0x%lx named %s \n", addr, len, name);
  if (addr %2 != 0)
    printf("WARNING: tase_make_symbolic called on unaligned object \n");
  std::cout.flush();
  void * buf = malloc(len);
  MemoryObject * bufMO = memory->allocateFixed((uint64_t) buf, len,  NULL);
  std::string nameString = name;
  bufMO->name = nameString;
  executeMakeSymbolic(*GlobalExecutionStatePtr, bufMO, name);
  const ObjectState * constBufOS = GlobalExecutionStatePtr->addressSpace.findObject(bufMO);
  ObjectState * bufOS = GlobalExecutionStatePtr->addressSpace.getWriteable(bufMO, constBufOS);
  
  for (uint64_t i = 0; i < len; i++) {
    tase_helper_write(addr + i, bufOS->read(i, Expr::Int8));
  }
}

void Executor::model_sb_disabled() {
  target_ctx_gregs[GREG_RIP].u64 = target_ctx_gregs[GREG_R15].u64;
}

void Executor::model_reopentran() {
 
  target_ctx_gregs[GREG_RIP].u64 = target_ctx_gregs[GREG_RAX].u64;
 
}


//Todo: Double check the edge cases and make non-ugly
bool Executor::isBufferEntirelyConcrete (uint64_t addr, int size) {
  /*
  uint16_t * objIterator;
  uint64_t addrInt = (uint64_t) addr;
  if (addrInt % 2 == 1) {
    printf("WARNING: Working with unaligned buffer at 0x%lx \n", addrInt);
    objIterator = (uint16_t *) addrInt -1;  //Todo -- is this a bug?
  } else {
    objIterator = (uint16_t *) addrInt;
  }

  //Fast path
  bool foundPsn = false;
  for (int i = 0; i < size/2 ; i++){
    if (*(objIterator + i) == poison_val)
      foundPsn = true;
  }
  if (foundPsn == false) 
    return true;
  */

  //Debug the issue with the fast path check.  Until then, easy does it...
  
  //Slow path
  uint64_t byteItr = (uint64_t) addr;
  for (int i = 0; i < size; i++) {
    ref<Expr> byteExpr = tase_helper_read ( byteItr +i , 1);
    if (!(isa<ConstantExpr> (byteExpr)))
      return false;	
  }
  return true;
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
  
  if (!(GlobalExecutionStatePtr->addressSpace.resolveOne(*GlobalExecutionStatePtr,solver,addrExpr, op, success))) {
    printf("ERROR in tase_helper_read: Couldn't resolve addr to fixed value \n");
    std::cout.flush();
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
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  wos->applyPsnOnRead(offset);
  return returnVal;
}


//Todo -- make this play nice with our alignment requirements
ObjectState * Executor::tase_map_buf(uint64_t addr, size_t size) {
  MemoryObject * MOres = addExternalObject(*GlobalExecutionStatePtr, (void *) addr, size, false, true);
  const ObjectState * OSConst = GlobalExecutionStatePtr->addressSpace.findObject(MOres);
  ObjectState * OSres = GlobalExecutionStatePtr->addressSpace.getWriteable(MOres, OSConst);
  OSres->concreteStore = (uint8_t *) addr;

  return OSres;
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
    fflush(stdout);
    std::exit(EXIT_FAILURE);
  }

  ref<Expr> offset = op.first->getOffsetExpr(addrExpr);
  //end gross----------------------------
  ObjectState *wos = GlobalExecutionStatePtr->addressSpace.getWriteable(op.first, op.second);

  wos->write(offset, val);
  wos->applyPsnOnWrite(offset,val);

}

//Todo: Update if we switch to SIMD execution
bool Executor::gprsAreConcrete() {

  return !tase_buf_could_be_symbolic((void *) &target_ctx_gregs[0], NGREG * GREG_SIZE);
  
  //return target_ctx_gregs_OS->isObjectEntirelyConcrete();
}


//CAUTION: This only works if the pc points to the beginning of the cartridge 
bool cartridgeHasFlagsDead(uint64_t pc) {
  return (cartridges_with_flags_live.find(pc) == cartridges_with_flags_live.end());
}



bool isProhibFn(uint64_t pc) {
  return  std::find(std::begin(prohib_fns), std::end(prohib_fns), pc) != std::end(prohib_fns);
}

bool Executor::instructionBeginsTransaction(uint64_t pc) {
  return  (cartridge_entry_points.find(pc) != cartridge_entry_points.end());
}

bool Executor::resumeNativeExecution (){
  if (exec_mode == INTERP_ONLY) {
    return false;
  }
  
  greg_t * registers = target_ctx_gregs;
  bool instBeginsTrans = instructionBeginsTransaction(registers[GREG_RIP].u64);
  if (instBeginsTrans) {
    
    if (isProhibFn(registers[GREG_RIP].u64))
	return false;
    bool concGprs = gprsAreConcrete();
    if (taseDebug)
      printf("Inst begins transaction \n");
    if (concGprs) {
      if (taseDebug)
	printf("Registers are concrete \n");
      return true;
    } else {
      if (taseDebug)
	printf("Registers aren't concrete \n");
      return false;
    }
  } else {
    if (taseDebug)
      printf("Inst doesn't begin transaction \n");
    return false;
  }
}

void Executor::model_taseMakeSymbolic() {
  //Two args are symbolic address, and size.
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //void * symAddress
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //int addrSize

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     uint64_t symAddr = target_ctx_gregs[GREG_RDI].u64;
     uint64_t size = target_ctx_gregs[GREG_RSI].u64;

     uint16_t valBeforePsn = *( (uint16_t *) symAddr);
     printf("val before psn of first two bytes is 0x%x \n", valBeforePsn);
     
     printf("taseMakeSymbolic called on addr 0x%lx with size 0x%lx \n", symAddr, size);
     tase_make_symbolic(symAddr, size, "ExternalTaseMakeSymbolicCall");

     uint16_t valAfterPsn = *( (uint16_t *) symAddr);
     printf("val after psn of first two bytes is 0x%x \n", valAfterPsn);
     //fake a ret
     uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
     target_ctx_gregs[GREG_RIP].u64 = retAddr;
     target_ctx_gregs[GREG_RSP].u64 += 8;
     
     printf("INTERPRETER: Exiting model_taseMakeSymbolic \n");
     std::cout.flush();
   } else {

     printf("ERROR: sym arg passed into taseMakeSymbolic \n");
     std::exit(EXIT_FAILURE);
   }
}

//int RAND_load_file(const char *filename, long max_bytes);                                                                   
void Executor::model_RAND_load_file() {
  printf("Entering model_RAND_load_file \n");
  std::cout.flush();

  //Perform the call                                                                                                          
  //int res = RAND_load_file((char *) target_ctx_gregs[GREG_RDI], (long) target_ctx_gregs[GREG_RSI]);                           
  int res = 1024;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //Fake a ret                                                                                                                
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  printf("Exiting model_RAND_load_file \n");
  std::cout.flush();

}


void Executor::model_RAND_add() {

  printf("Entering model_RAND_add \n");
  std::cout.flush();
  //RAND_add((void *) target_ctx_gregs[GREG_RDI], (int) target_ctx_gregs[GREG_RSI], 0);                                         

  //fake a ret                                                                                                                
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;


}

static int model_malloc_calls = 0;

//Look for a model for the current instruction at target_ctx_gregs[GREG_RIP].u64
//and call the model.
void Executor::model_inst () {

  if (taseDebug) {
    printf("INTERPRETER: FOUND SPECIAL MODELED INST \n");
  }
  uint64_t rip = target_ctx_gregs[GREG_RIP].u64;  
  #ifdef TASE_BIGNUM
  if (rip == (uint64_t) &make_byte_symbolic +14 || rip == (uint64_t) &make_byte_symbolic) {
    make_byte_symbolic_model();
  } else if (rip == (uint64_t) &exit_tase_shim) {
    fprintf(stderr,"Successfully exited from target.  Shutting down with %d x86 instructions interpreted \n", interpCtr);
    fflush(stdout);
    std::exit(EXIT_SUCCESS);
  }
  #endif
  //#ifdef TASE_OPENSSL
  if (rip == (uint64_t) &free || rip == ((uint64_t) &free_tase) || rip == ((uint64_t) &free_tase + 14) ) {
    model_free();
  }  else if (rip == (uint64_t) &sb_modeled) {
    printf("Doing sb_modeled hack \n");
    target_ctx_gregs[GREG_RIP].u64 = target_ctx_gregs[GREG_R15].u64;
  } else if (rip == (uint64_t) &signal) {
    model_signal();
  } else if (rip == (uint64_t) &malloc  || rip == ((uint64_t) &malloc_tase + 14) || rip == (uint64_t) &malloc_tase  ) {
    model_malloc_calls++;
    model_malloc();
  } else if (rip == (uint64_t) &realloc  || rip == ((uint64_t) &realloc_tase + 14) || rip == (uint64_t) &realloc_tase ) {
    model_realloc();
  } else if (rip == (uint64_t) &__errno_location) {
    model___errno_location();
  } else if (rip == (uint64_t) &__ctype_b_loc) {
    model___ctype_b_loc();
  } else if (rip == (uint64_t) &__ctype_tolower_loc) {
    model___ctype_tolower_loc();
  } else if (rip == (uint64_t) &BIO_printf) {
    model_BIO_printf();
  }  else if (rip == (uint64_t) &vfprintf) {
    model_vfprintf();
  } else if (rip == (uint64_t) &sprintf) {
    model_sprintf();
  } else if (rip == (uint64_t) &OpenSSLDie) {
    model_OpenSSLDie();
  } else if (rip == (uint64_t) &shutdown) {
    target_end_time = util::getWallTime();
    double total_time =  target_end_time - target_start_time;  
    printf("----END RUN STATS ---- \n");
    printf("Entire time in target took roughly %f seconds \n", total_time);

    model_shutdown();
    
  } else if (rip == (uint64_t) &time ) {
    model_time();
  } else if (rip == (uint64_t) &gmtime) {
    model_gmtime();
  } else if (rip == (uint64_t) &stat) {
    model_stat();
  } else if (rip == (uint64_t) &fileno) {
    model_fileno();
  } else if (rip == (uint64_t) &fcntl ) {
    model_fcntl();
  } else if (rip == (uint64_t) &fopen) {
    model_fopen();
  } else if (rip == (uint64_t) &fclose) {
    model_fclose();
  } else if (rip == (uint64_t) &fopen64) {
    model_fopen64();
  } else if (rip == (uint64_t) &fread || (rip == (uint64_t) &fread_unlocked)) {
    model_fread();
    } else if (rip == (uint64_t) &fwrite || (rip == (uint64_t) &fwrite_unlocked)) {
    model_fwrite();
  } else if (rip == (uint64_t) &fgets) {
    model_fgets();
  } else if (rip == (uint64_t) &fflush) {
    model_fflush();
  } else if (rip == (uint64_t) &__isoc99_sscanf || rip == (uint64_t) &sscanf) {
    model___isoc99_sscanf();
  } else if (rip == (uint64_t) &gethostbyname) {
    model_gethostbyname();
  } else if ( rip == (uint64_t) &tls1_generate_master_secret + trap_off ) {
    model_tls1_generate_master_secret();
  } else if (rip == (uint64_t) &EC_POINT_point2oct + trap_off) {
    model_EC_POINT_point2oct();
  } else if (rip == (uint64_t) &ECDH_compute_key + trap_off) {
    model_ECDH_compute_key();
  } else if (rip == (uint64_t) &EC_KEY_generate_key + trap_off ) {
    model_EC_KEY_generate_key();
  } else if (rip == (uint64_t) &SHA1_Update + trap_off) {
    model_SHA1_Update();
  } else if (rip == (uint64_t) &SHA1_Final + trap_off) {
    model_SHA1_Final();
  } else if (rip == (uint64_t) &SHA256_Update + trap_off) {
    model_SHA256_Update();
  } else if (rip == (uint64_t) &SHA256_Final + trap_off) {
    model_SHA256_Final();
  } else if (rip == (uint64_t) &gcm_gmult_4bit + trap_off) {
    model_gcm_gmult_4bit();
  } else if (rip == (uint64_t) &gcm_ghash_4bit + trap_off) {
    model_gcm_ghash_4bit();
  } else if (rip == (uint64_t) &AES_encrypt + trap_off) {
    model_AES_encrypt();
  } else if (rip == (uint64_t) &ktest_master_secret + trap_off  || rip == (uint64_t) &ktest_master_secret) {

    printf("Entering ktest_master_secret model \n");
    fflush(stdout);
    model_ktest_master_secret();
  } else if (rip == (uint64_t)  &ktest_writesocket + trap_off || rip == (uint64_t) &ktest_writesocket) {
    model_ktest_writesocket();
  } else if (rip == (uint64_t) &ktest_readsocket + trap_off || rip == (uint64_t) &ktest_readsocket) {
    model_ktest_readsocket();
  } else if (rip == (uint64_t) &ktest_raw_read_stdin + trap_off || rip == (uint64_t) &ktest_raw_read_stdin) {
    model_ktest_raw_read_stdin();
  } else if (rip == (uint64_t) &ktest_connect + trap_off || rip == (uint64_t) &ktest_connect) {
    model_ktest_connect();
  } else if (rip == (uint64_t) &ktest_select + trap_off || rip == (uint64_t) &ktest_select) {
    model_ktest_select();
  } else if (rip == (uint64_t) &ktest_RAND_bytes + trap_off || rip == (uint64_t) &ktest_RAND_bytes) {
    model_ktest_RAND_bytes();
  } else if (rip == (uint64_t) &ktest_RAND_pseudo_bytes +trap_off   || rip == (uint64_t) &ktest_RAND_pseudo_bytes) {
    model_ktest_RAND_pseudo_bytes();
  } else if (rip == (uint64_t) &ktest_start + trap_off  || rip == (uint64_t) &ktest_start) {
    model_ktest_start();
  } else if (rip == (uint64_t) &gettimeofday)  {
    model_gettimeofday();
  } else if (rip == (uint64_t) &getuid) {
    model_getuid();
  } else if (rip == (uint64_t) &geteuid) {
    model_geteuid();
  } else if (rip == (uint64_t) &getgid) {
    model_getgid();
  } else if (rip == (uint64_t) &getegid) {
    model_getegid();
  }  else if (rip == (uint64_t) &getenv) {
    model_getenv();
  }  else if (rip == (uint64_t) &getpid) {
    model_getpid();
  } else if (rip == (uint64_t) &socket) {
   model_socket();
 } else if (rip == (uint64_t) &setsockopt) {
   model_setsockopt();
 } else if (rip == (uint64_t) &exit) {
   model_exit();
 } else if (rip == (uint64_t) &write ) {
   model_write();
 } else if (rip == (uint64_t) &printf || rip == (uint64_t) &puts) {
   model_printf();
  } else if (rip == (uint64_t) &RAND_add) {
    model_RAND_add();
  } else if (rip == (uint64_t) &RAND_load_file ) {
    model_RAND_load_file();
  } else if (rip == (uint64_t) &RAND_poll + trap_off || rip == (uint64_t) &RAND_poll) {  
    model_RAND_poll();
  }  else if (rip == (uint64_t) &sb_reopen) {
    model_reopentran();
  }
  else if (rip == (uint64_t) &sb_disabled) {
    model_sb_disabled();
  }
  //#endif

  else if (rip == (uint64_t) &target_exit) {
    printf("Found call to target_exit in interpreter \n");
    std::cout.flush();
    worker_exit();
    target_exit();
  } else {
    printf("INTERPRETER: Couldn't find  model \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  return;
}

void Executor::model_OpenSSLDie() {

  printf("OpenSSLDie called -- args are \n");
  std::cout.flush();
  printf("%s \n", (char *)target_ctx_gregs[GREG_RDI].u64);
  printf("%d \n", (int) target_ctx_gregs[GREG_RSI].u64);
  printf("%s \n", (char *) target_ctx_gregs[GREG_RDX].u64);

  std::cout.flush();
  std::exit(EXIT_FAILURE);
}




bool isSpecialInst (uint64_t rip) {

  //Todo -- get rid of traps for RAND_add and RAND_load_file
#ifdef TASE_BIGNUM
  static const uint64_t modeledFns [] = { (uint64_t) &make_byte_symbolic, (uint64_t) &make_byte_symbolic + trap_off, (uint64_t) &target_exit, (uint64_t) &exit_tase_shim};
#endif


  //#ifdef TASE_OPENSSL
  static const uint64_t modeledFns [] = {(uint64_t)&signal, (uint64_t)&malloc, (uint64_t)&read, (uint64_t)&write, (uint64_t)&connect, (uint64_t)&select, (uint64_t)&socket, (uint64_t) &getuid, (uint64_t) &geteuid, (uint64_t) &getgid, (uint64_t) &getegid, (uint64_t) &getenv, (uint64_t) &stat, (uint64_t) &free, (uint64_t) &realloc,  (uint64_t) &RAND_add, (uint64_t) &RAND_load_file,  (uint64_t) &kTest_free, (uint64_t) &kTest_fromFile, (uint64_t) &kTest_getCurrentVersion,  (uint64_t) &kTest_isKTestFile, (uint64_t) &kTest_numBytes, (uint64_t) &kTest_toFile, (uint64_t) &ktest_RAND_bytes, (uint64_t) &ktest_RAND_pseudo_bytes, (uint64_t) &ktest_connect, (uint64_t) &ktest_finish, (uint64_t) &ktest_master_secret, (uint64_t) &ktest_raw_read_stdin, (uint64_t) &ktest_readsocket, (uint64_t) &ktest_select,  (uint64_t) &ktest_start, (uint64_t) &ktest_time, (uint64_t) &time, (uint64_t) &gmtime, (uint64_t) &gettimeofday,  (uint64_t) &ktest_writesocket, (uint64_t) &fileno, (uint64_t) &fcntl, (uint64_t) &fopen, (uint64_t) &fopen64, (uint64_t) &fclose,  (uint64_t) &fwrite, (uint64_t) &fwrite_unlocked, (uint64_t) &fflush, (uint64_t) &fread, (uint64_t) &fread_unlocked, (uint64_t) &fgets, (uint64_t) &__isoc99_sscanf, (uint64_t) &sscanf, (uint64_t) &gethostbyname, (uint64_t) &setsockopt, (uint64_t) &__ctype_tolower_loc, (uint64_t) &__ctype_b_loc, (uint64_t) &__errno_location,  (uint64_t) &BIO_printf, /* (uint64_t) &BIO_snprintf,*/ (uint64_t) &vfprintf,  (uint64_t) &sprintf, (uint64_t) &printf,   (uint64_t) &OpenSSLDie, (uint64_t) &shutdown , (uint64_t) &malloc_tase, (uint64_t) &realloc_tase, (uint64_t) &calloc_tase, (uint64_t) &free_tase, (uint64_t) &getpid , (uint64_t) &RAND_poll};
  //#endif 
  
  bool isModeled = std::find(std::begin(modeledFns), std::end(modeledFns), rip) != std::end(modeledFns);

  //ABH: The trap_off offset below is a temporary hack.
  //For modeled or prohibitive functions, we trap
  //to the interpreter at exactly trap_off bytes from the
  //label of the function.


  //#ifdef TASE_OPENSSL
  if (
      rip == (uint64_t) &ktest_start             + trap_off ||
      rip == (uint64_t) &ktest_writesocket       + trap_off ||
      rip == (uint64_t) &ktest_readsocket        + trap_off ||
      rip == (uint64_t) &ktest_raw_read_stdin    + trap_off ||
      rip == (uint64_t) &ktest_connect           + trap_off ||
      rip == (uint64_t) &ktest_select            + trap_off ||
      rip == (uint64_t) &ktest_RAND_bytes        + trap_off ||
      rip == (uint64_t) &ktest_RAND_pseudo_bytes + trap_off ||
      rip == (uint64_t) &ktest_master_secret     + trap_off ||
      rip == (uint64_t) &sb_reopen                    ||
      rip == (uint64_t) &sb_disabled                  ||
      rip == (uint64_t) &target_exit                  ||
      rip == (uint64_t) &sb_modeled                   ||
      rip == (uint64_t) &malloc_tase                 + 14  ||
      rip == (uint64_t) &realloc_tase                + 14  ||
      rip == (uint64_t) &free_tase                   + 14  ||
      rip == (uint64_t) &SHA1_Update                 + trap_off  ||
      rip == (uint64_t) &SHA1_Final                  + trap_off  ||
      rip == (uint64_t) &SHA256_Update               + trap_off  ||
      rip == (uint64_t) &SHA256_Final                + trap_off  ||
      rip == (uint64_t) &AES_encrypt                 + trap_off  ||
      rip == (uint64_t) &gcm_gmult_4bit              + trap_off  ||
      rip == (uint64_t) &gcm_ghash_4bit              + trap_off  ||
      rip == (uint64_t) &EC_KEY_generate_key         + trap_off  ||
      rip == (uint64_t) &ECDH_compute_key            + trap_off  ||
      rip == (uint64_t) &EC_POINT_point2oct          + trap_off  ||
      //Trapping during replays should happen further down in tls1_generate_master_secret
      (rip == (uint64_t) &tls1_generate_master_secret + trap_off ) ||
      rip == (uint64_t) &RAND_poll                   + trap_off  ||
      rip == (uint64_t) &strtoul                     + trap_off  ||
      isModeled
      )  {
    return true;
  }  else {
    return false;
  }
  //#endif
}


void Executor::printDebugInterpHeader() {

  printf("------------------------------------------- \n");
  printf("Entering interpreter for time %lu \n \n \n", interpCtr);
  uint64_t rip = target_ctx_gregs[GREG_RIP].u64;
  printf("RIP is %lu in decimal, 0x%lx in hex.\n", rip, rip);
  printf("Initial ctx BEFORE interpretation is \n");
  printCtx(target_ctx_gregs);
  printf("\n");
  std::cout.flush();
  
}

void Executor::printDebugInterpFooter() {
  printCtx(target_ctx_gregs);
  printf("Executor has %lu states \n", this->states.size() );
  printf("Finished round %lu of interpretation. \n", interpCtr);
  printf("-------------------------------------------\n");
}


//Fast-path check for poison tag in buffer.
//Todo -- double check the corner cases
bool tase_buf_could_be_symbolic (void * ptr, int size) {
  //Promote size to an even number
  //printf("Input args to tase_buf_could_be_symbolic: ptr 0x%lx, size %d \n", (uint64_t) ptr, size);
  int checkSize;
  uint16_t * checkBase;
  
  if ( (uint64_t) ptr % 2 == 1) {
    checkBase = (uint16_t *)  ((uint64_t) ptr -1);
    if (size % 2 == 0)
      checkSize  = size + 2;
    else
      checkSize = size + 1;
  } else {
    checkBase = (uint16_t *) ptr;
    if (size % 2 ==0)
      checkSize = size;
    else
      checkSize = size +1;
  }

  //We're checking in 2-byte aligned chunks, so cut size in half.
  //It must be even by now.

  checkSize = checkSize/2;
  
  //printf("After normalization, args to tase_buf_could_be_symbolic: 0x%lx, size %d \n", (uint64_t) checkBase, checkSize);
  for (int i = 0; i < checkSize; i++) {
    if ( *( checkBase +i ) == poison_val)
      return true;
  }

  return false;
}

void Executor::klee_interp_internal () {
  double interpSetupStartTime = 0.0, interpRunStartTime = 0.0, interpCleanupStartTime = 0.0;
  
  tase_model = (void *) &sb_modeled;
  
  while (true) {
    interpCtr++;
    if (taseDebug)
      printDebugInterpHeader();
    if (measureTime)
      interpSetupStartTime = util::getWallTime();
    
    uint64_t rip = target_ctx_gregs[GREG_RIP].u64;
    uint64_t rip_init = rip;
    
    //IMPORTANT -- The springboard is written assuming we never try to
    // jump right back into a modeled fn.  The sb_modeled label immediately XENDs, which will
    // cause a segfault if the process isn't executing a transaction.

    //dont_model is used to force execution in interpreter when a register is tainted (but no args are symbolic) for a modeled fn 
    if (isSpecialInst(rip) && !dont_model) {
      model_inst();
    } else if (resumeNativeExecution() && !dont_model) {
      break;
    } else {
      dont_model = false;
      if (killFlagsHack) {
	if (instructionBeginsTransaction(rip)) {
	  if (cartridgeHasFlagsDead(rip)) {
	    if (taseDebug) {
	      printf("Killing flags \n");
	    }
	    uint64_t zero = 0;
	    ref<ConstantExpr> zeroExpr = ConstantExpr::create(zero, Expr::Int64);
	    tase_helper_write((uint64_t) &target_ctx_gregs[GREG_EFL], zeroExpr);
	  }
	}
      }
  
      double findInterpFnStartTime = 0.0;
      if (measureTime)
	findInterpFnStartTime = util::getWallTime();
      KFunction * interpFn = findInterpFunction (target_ctx_gregs, kmodule);
      if (measureTime) {
	double findInterpFnElapsedTime = (util::getWallTime() - findInterpFnStartTime);
	if (taseDebug)
	  printf(" %lf seconds elapsed finding interp fn at interpCtr %lu \n", findInterpFnElapsedTime, interpCtr);
	interp_find_fn_time += findInterpFnElapsedTime;
      }
      
      //We have to manually push a frame on for the function we'll be
      //interpreting through.  At this point, no other frames should exist
      // on klee's interpretation "stack".
      GlobalExecutionStatePtr->pushFrame(0,interpFn);
      GlobalExecutionStatePtr->pc = interpFn->instructions ;
      GlobalExecutionStatePtr->prevPC = GlobalExecutionStatePtr->pc;
      std::vector<ref<Expr> > arguments;
      uint64_t regAddr = (uint64_t) target_ctx_gregs;
      ref<ConstantExpr> regExpr = ConstantExpr::create(regAddr, Context::get().getPointerWidth());
      arguments.push_back(regExpr);
      bindArgument(interpFn, 0, *GlobalExecutionStatePtr, arguments[0]);
      
      if (measureTime) {
	double setupDiff = util::getWallTime() - interpSetupStartTime;
	interp_setup_time += setupDiff;
	if (taseDebug)
	  printf("%lf seconds during interp setup for interpCtr %lu \n", setupDiff, interpCtr);
      }

      if (measureTime) 
	interpRunStartTime = util::getWallTime();
      
      run(*GlobalExecutionStatePtr);
      
      if (measureTime) {
	double runDiff = util::getWallTime() - interpRunStartTime;
	interp_run_time += runDiff;
	if (taseDebug)
	  printf("%lf seconds during interp run for interpCtr %lu \n", runDiff, interpCtr);
      }
      
    }
    if (measureTime) {
      interpCleanupStartTime = util::getWallTime();
    }

    if(tase_buf_could_be_symbolic((void *) &(target_ctx_gregs[GREG_RIP].u64), 8) ) {
      ref<Expr> RIPExpr = tase_helper_read((uint64_t) &(target_ctx_gregs[GREG_RIP].u64), 8);
      if (!(isa<ConstantExpr>(RIPExpr))) {
	printf("Detected symbolic RIP \n");
	printf("Attempting to call toUnique on symbolic RIP \n");
	solver_start_time = util::getWallTime();
	ref <Expr> uniqueRIPExpr  = toUnique(*GlobalExecutionStatePtr,RIPExpr);
	solver_end_time = util::getWallTime();
	solver_diff_time = solver_end_time - solver_start_time;	
	printf("Elapsed solver time (RIP toUnique) is %lf at interpCtr %lu \n", solver_diff_time, interpCtr);
	run_solver_time += solver_diff_time;
	solver_time += solver_diff_time;
	printf("Total solver time is %lf at interpCtr %lu \n", solver_time, interpCtr);
	
	if (isa<ConstantExpr> (uniqueRIPExpr)) {
	  printf("Only one valid value for RIP \n");
	  fflush(stdout);
	  tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RIP], uniqueRIPExpr);
	  
	} else {      
	  printf("IMPORTANT: Calling forkOnPossibleRIPValues() \n");
	  std::cout.flush();
	  forkOnPossibleRIPValues(RIPExpr, rip_init);
	}
	ref<Expr> FinalRIPExpr = target_ctx_gregs_OS->read(GREG_RIP * 8, Expr::Int64);
	if (!(isa<ConstantExpr>(FinalRIPExpr))) {
	  //Hack to make sure garbageCtr isn't optimized out
	  printf("garbageCtr is 0x%lx \n", garbageCtr);
	  
	  printf("ERROR: Failed to concretize RIP \n");
	  std::cout.flush();
	  std::exit(EXIT_FAILURE);
	}
      }
    }

    
    //Kludge to get us back to native execution for prohib fns with concrete input
    
    if (forceNativeRet) {
      if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	if (taseDebug) {
	  printf("Trying to return to native execution \n");
	}
	break;
      }
    }
    if (measureTime) {
      double interpCleanupDiffTime = util::getWallTime() - interpCleanupStartTime;
      interp_cleanup_time += interpCleanupDiffTime;
      if (taseDebug)
	printf("%lf seconds during interp cleanup for interpCtr %lu \n", interpCleanupDiffTime, interpCtr);
    }
  }

  static int numReturns = 0;
  numReturns++;
  if (taseDebug)
    printf("Returning to native execution for time %d \n", numReturns);
  
  if (taseDebug) {
    printf("Prior to return to native execution, ctx is ... \n");
    printCtx(target_ctx_gregs);
    printf("--------RETURNING TO TARGET --- ------------ \n" );
    std::cout.flush();
  }
  
  return;
}


//For debugging
//This is broken.  Fix it sometime.
int Executor::printAllPossibleValues (ref <Expr> inputExpr) {
  //printf("Calling printAllPossibleValues at rip 0x%lx \n", target_ctx_gregs[GREG_RIP].u64);
  /*
  if (isa<ConstantExpr> (inputExpr)) {
    printf("printAllPossibleValues called on Constant Expr \n ");
    return 1;
  }
  ref <ConstantExpr> trueExpr = ConstantExpr::create(1, Expr::Bool);
  ref <ConstantExpr> solution;
  ref<Expr> theExpr = AndExpr::create(inputExpr, trueExpr);
  bool success = solver->getValue(*GlobalExecutionStatePtr, theExpr, solution);
  if (!success)
    printf("ERROR: Couldn't get solution in printAllPossibleValues \n");

  uint64_t value = solution->getZExtValue();
  printf(" Found solution 0x%lx \n",  value);
  */
  //bool res = false;
  //ref<Expr> equalsExpr  = EqExpr::create(solution, inputExpr); 
  
  //solver->mustBeTrue(*GlobalExecutionStatePtr,equalsExpr , res);

  //printf("mustBeTrue on first solution returns %d \n", res);
  
  //if (res == true)



  return 1; //This part works




  //else
    //return 2; //Todo: Fixme

  
  //Or we could just copy inputExpr 
  /*
  for (int i = 0; i < 20 ; i++) {
    ref <ConstantExpr> solution;
    bool success = solver->getValue(*GlobalExecutionStatePtr, theExpr, solution);
    if (!success)
      printf("ERROR: Couldn't get solution in printAllPossibleValues \n");

    uint64_t value = solution->getZExtValue();
    printf(" On iteration %d, found solution 0x%lx \n", i, value);
    ref<Expr> notEqualsSolution = NotExpr::create(EqExpr::create(inputExpr,solution));
    theExpr = AndExpr::create(theExpr, notEqualsSolution);
  }
  */
  //std::cout.flush();

}

//Take an Expr and find all the possible concrete solutions.
//Hopefully there's a better builtin function in klee that we can
//use, but if not this should do the trick.  Intended to be used
//to help us get all possible concrete values of RIP (has dependency on RIP).

void Executor::forkOnPossibleRIPValues (ref <Expr> inputExpr, uint64_t initRIP) {

  int maxSolutions = 2; //Completely arbitrary.  Should not be more than 2 for our use cases in TASE
  //or we're in trouble anyway.

  int numSolutions = 0;  
  while (true) {
    ref<ConstantExpr> solution;
    numSolutions++;
    printf("Looking at solution number %d in forkOnPossibleRIPValues() \n", numSolutions);

    if (numSolutions > maxSolutions) {
      printf("IMPORTANT: control debug: Found too many symbolic values for next instruction after 0x%lx \n ", initRIP);
      std::cout.flush();
      worker_exit();
      std::exit(EXIT_FAILURE);
    }
    
    solver_start_time = util::getWallTime();
    bool success = solver->getValue(*GlobalExecutionStatePtr, inputExpr, solution);
    solver_end_time = util::getWallTime();
    
    solver_diff_time = solver_end_time - solver_start_time;
    
    printf("Elapsed solver time (forking) is %lf at interpCtr %lu \n", solver_diff_time, interpCtr);
    solver_time += solver_diff_time;
    run_solver_time += solver_diff_time;
    
    printf("Total solver time is %lf at interpCtr %lu \n", solver_time, interpCtr);
    
    if (!success) {
      printf("ERROR: couldn't get initial value in forkOnPossibleRIPValues \n");  
      std::cout.flush();
      worker_exit();
      std::exit(EXIT_FAILURE);
    }

    int initPID = getpid();
    std::cout.flush();
    int isTrueChild = tase_fork(initPID, initRIP); //Returns 0 for false branch, 1 for true.  Not intuitive
    int i = getpid();
    workerIDStream << ".";
    workerIDStream << i;
    std::string pidString ;
    
    pidString = workerIDStream.str();
    if (pidString.size() > 250) {
      printf("Cycling log names due to large size \n");
      workerIDStream.str("");
      workerIDStream << "Monitor.Wrapped.";
      workerIDStream << i;
      pidString = workerIDStream.str();
      printf("Cycled log name is %s \n", pidString.c_str());

    }
    printf("Before freopen, new string for log is %s \n", pidString.c_str());
    FILE * res1 = freopen(pidString.c_str(),"w",stdout);
    FILE * res2 = freopen(pidString.c_str(), "w", stderr);
    reset_run_timers();
    
    printf("Resetting interp time counters for fork  %lf seconds after analysis began \n", util::getWallTime() - target_start_time );
    interpreter_time = 0.0;
    interp_setup_time = 0.0;
    interp_run_time = 0.0;
    interp_cleanup_time = 0.0;
    interp_find_fn_time = 0.0;

    run_interp_time = 0;
    interp_enter_time = util::getWallTime();
    
    mdl_time = 0.0;
    psn_time = 0.0;
    
    if (res1 == NULL ) {
      printf("ERROR opening new file for child process logging \n");
      fprintf(stderr, "ERROR opening new file for child process logging for pid %d \n", i);
      fflush(stdout);
      worker_exit();
      std::exit(EXIT_FAILURE);
    }

    //Force two destinations for debugging
    //ABH: Todo -- roll this back and support > 2 symbolic dests for things like indirect jumps
    if (isTrueChild == 0) { //Rule out latest solution and see if more exist
      ref<Expr> notEqualsSolution = NotExpr::create(EqExpr::create(inputExpr,solution));
      if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(notEqualsSolution)) {
	if (CE->isFalse()) {
	  printf("IMPORTANT: forked child %d is not exploring a feasible path \n", getpid());
	  fflush(stdout);
	  worker_exit();
	}
      }

	
      addConstraint(*GlobalExecutionStatePtr, notEqualsSolution);

      solver_start_time = util::getWallTime();
      success = solver->getValue(*GlobalExecutionStatePtr, inputExpr, solution);
      solver_end_time = util::getWallTime();
    
      solver_diff_time = solver_end_time - solver_start_time;
      run_solver_time += solver_diff_time;

      
      printf("Elapsed solver time (fork - path constraint) is %lf at interpCtr %lu \n", solver_diff_time, interpCtr);
      solver_time += solver_diff_time;
      printf("Total solver time is %lf at interpCtr %lu \n", solver_time, interpCtr);
      
      if (!success) {
	printf("ERROR: couldn't get RIP value in forkOnPossibleRIPValues for false child \n");
	std::cout.flush();
	worker_exit();
	std::exit(EXIT_FAILURE);
      }
      printf("IMPORTANT: control debug: Found dest RIP 0x%lx on false branch in forkOnRip from RIP 0x%lx with pid %d \n", (uint64_t) solution->getZExtValue(), initRIP, getpid());
      addConstraint(*GlobalExecutionStatePtr, EqExpr::create(inputExpr, solution));
      target_ctx_gregs_OS->write(GREG_RIP*8, solution);
      break;

    } else { // Take the concrete value of solution and explore that path.
      printf("IMPORTANT: control debug: Found dest RIP 0x%lx on true branch in forkOnRip from RIP 0x%lx with pid %d \n", (uint64_t) solution->getZExtValue(), initRIP, getpid());
      addConstraint(*GlobalExecutionStatePtr, EqExpr::create(inputExpr, solution));
      target_ctx_gregs_OS->write(GREG_RIP*8, solution);
      break;
    }
  }
}

void printCtx(greg_t * registers ) {

  printf("R8   : 0x%lx \n", registers[GREG_R8].u64);
  printf("R9   : 0x%lx \n", registers[GREG_R9].u64);
  printf("R10  : 0x%lx \n", registers[GREG_R10].u64);
  printf("R11  : 0x%lx \n", registers[GREG_R11].u64);
  printf("R12  : 0x%lx \n", registers[GREG_R12].u64);
  printf("R13  : 0x%lx \n", registers[GREG_R13].u64);
  printf("R14  : 0x%lx \n", registers[GREG_R14].u64);
  printf("R15  : 0x%lx \n", registers[GREG_R15].u64);
  printf("RDI  : 0x%lx \n", registers[GREG_RDI].u64);
  printf("RSI  : 0x%lx \n", registers[GREG_RSI].u64);
  printf("RBP  : 0x%lx \n", registers[GREG_RBP].u64);
  printf("RBX  : 0x%lx \n", registers[GREG_RBX].u64);
  printf("RDX  : 0x%lx \n", registers[GREG_RDX].u64);
  printf("RAX  : 0x%lx \n", registers[GREG_RAX].u64);
  printf("RCX  : 0x%lx \n", registers[GREG_RCX].u64);
  printf("RSP  : 0x%lx \n", registers[GREG_RSP].u64);
  printf("RIP  : 0x%lx \n", registers[GREG_RIP].u64);
  printf("EFL  : 0x%lx \n", registers[GREG_EFL].u64);

  return;
}

void Executor::initializeInterpretationStructures (Function *f) {

  printf("INITIALIZING INTERPRETATION STRUCTURES \n");
  printf("UseForkedCoreSolver is %d \n", (bool ) UseForkedCoreSolver);
  printf("Creating new execution state \n");
  GlobalExecutionStatePtr = new ExecutionState(kmodule->functionMap[f]);

  //AH: We may not actually need this...
  printf("Initializing globals ... \n");
  initializeGlobals(*GlobalExecutionStatePtr);
  
  //Set up the KLEE memory object for the stack, and back the concrete store with the actual stack.
  //Need to be careful here.  The buffer we allocate for the stack is char [X] target_stack. It
  // starts at address StackBase and covers up to StackBase + sizeof(target_stack) -1.

  printf("target_ctx.target_stack located at 0x%lx \n", (uint64_t) &target_ctx.target_stack);
  uint64_t stackBase = (uint64_t) &target_ctx.target_stack - STACK_SIZE;
  uint64_t stackSize = STACK_SIZE;
  
  printf("Adding structures to track target stack starting at 0x%lx with size %lu \n", stackBase, stackSize);
  tase_map_buf(stackBase, stackSize);
  /*
  MemoryObject * stackMem = addExternalObject(*GlobalExecutionStatePtr,(void *) stackBase, stackSize, false );
  const ObjectState *stackOS = GlobalExecutionStatePtr->addressSpace.findObject(stackMem);
  ObjectState * stackOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(stackMem,stackOS);  
  printf("Setting concrete store to point to target stack \n");
  stackOSWrite->concreteStore = (uint8_t *) stackBase;
  */
  printf("Adding external object target_ctx_gregs_MO \n");
  target_ctx_gregs_MO = addExternalObject(*GlobalExecutionStatePtr, (void *) target_ctx_gregs, NGREG * GREG_SIZE, false );
  printf("target_ctx_gregs is address %lu, hex 0x%p \n", (uint64_t) target_ctx_gregs, (void *) target_ctx_gregs);
  printf("Size of gregs is %d \n", NGREG * GREG_SIZE);
  
  printf("Setting concrete store in target_ctx_gregs_OS to target_ctx_gregs \n");
  const ObjectState *targetCtxOS = GlobalExecutionStatePtr->addressSpace.findObject(target_ctx_gregs_MO);
  target_ctx_gregs_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(target_ctx_gregs_MO,targetCtxOS);
  target_ctx_gregs_OS->concreteStore = (uint8_t *) target_ctx_gregs;
    
  printf("Target_ctx_gregs conc store at %lx \n",(uint64_t) &(target_ctx_gregs_OS->concreteStore[0]));

  //Map in read-only globals
  //Todo -- find a less hacky way of getting the exact size of the .rodata section

  rodata_base_ptr = (void *) (&_IO_stdin_used);
  rodata_size = (uint64_t) ((uint64_t) (&__GNU_EH_FRAME_HDR) - (uint64_t) (&_IO_stdin_used)) ;
  printf("Estimated rodataSize as 0x%lx \n", rodata_size);

  rodata_size += (0x2949c + 0x2949c); //Hack to map in eh_frame_hdr and eh_frame also
  
  printf("Attempting to map in .rodata section at base 0x%lx with size 0x%lx up to  0x%lx \n", (uint64_t) rodata_base_ptr, rodata_size, (uint64_t) &__GNU_EH_FRAME_HDR);
  std::cout.flush();

  tase_map_buf((uint64_t) rodata_base_ptr, rodata_size);
  /*
  MemoryObject * rodataMO = addExternalObject(*GlobalExecutionStatePtr, rodata_base_ptr, rodata_size, false);
  const ObjectState * rodataOSConst = GlobalExecutionStatePtr->addressSpace.findObject(rodataMO);
  ObjectState * rodataOS = GlobalExecutionStatePtr->addressSpace.getWriteable(rodataMO,rodataOSConst);
  rodataOS->concreteStore = (uint8_t *) rodata_base_ptr;
  */
  //Map in special stdin libc symbol
  MemoryObject * stdinMO = addExternalObject(*GlobalExecutionStatePtr, (void *) &stdin, 8, false);
  const ObjectState * stdinOSConst = GlobalExecutionStatePtr->addressSpace.findObject(stdinMO);
  ObjectState * stdinOS = GlobalExecutionStatePtr->addressSpace.getWriteable(stdinMO,stdinOSConst);
  stdinOS->concreteStore = (uint8_t *) &stdin;
  
  //Map in special stdout libc symbol
  MemoryObject * stdoutMO = addExternalObject(*GlobalExecutionStatePtr, (void *) &stdout, 8, false);
  const ObjectState * stdoutOSConst = GlobalExecutionStatePtr->addressSpace.findObject(stdoutMO);
  ObjectState * stdoutOS = GlobalExecutionStatePtr->addressSpace.getWriteable(stdoutMO,stdoutOSConst);
  stdoutOS->concreteStore = (uint8_t *) &stdout;
  
  //Map in special stderr libc symbol
  MemoryObject * stderrMO = addExternalObject(*GlobalExecutionStatePtr, (void *) &stderr, 8, false);
  const ObjectState * stderrOSConst = GlobalExecutionStatePtr->addressSpace.findObject(stderrMO);
  ObjectState * stderrOS = GlobalExecutionStatePtr->addressSpace.getWriteable(stderrMO,stderrOSConst);
  stderrOS->concreteStore = (uint8_t *) &stderr;
  
  //Map in initialized and uninitialized non-read only globals into klee from .vars file.
  std::string varsFileLocation = "./" + project + ".vars";

  FILE * externalsFile = fopen (varsFileLocation.c_str(), "r+");

  if (externalsFile == NULL) {
    printf("Error reading externals file within initializeInterpretationStructures() \n");
    worker_exit();
    std::exit(EXIT_FAILURE);
  }

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

    if ((sizeVal %2) == 1) {
      sizeVal = sizeVal + 1;
      printf("rounding up sizeval to even number - %lu \n",sizeVal);
    }

    //Todo:  Get rid of the special cases below for target_ctx_gregs and basket.
    //The checks are there now to prevent us from creating two different MO/OS
    //for the variables, since we map them above already.
    if ((uint64_t) addrVal == (uint64_t) &target_ctx_gregs) {
      printf("Found target_ctx_gregs while mapping extern symbols \n");
      continue;
    }

    tase_map_buf(addrVal, sizeVal);
    /*
    MemoryObject * GlobalVarMO = addExternalObject(*GlobalExecutionStatePtr,(void *) addrVal, sizeVal, false );
    const ObjectState * ConstGlobalVarOS = GlobalExecutionStatePtr->addressSpace.findObject(GlobalVarMO);
    ObjectState * GlobalVarOS = GlobalExecutionStatePtr->addressSpace.getWriteable(GlobalVarMO,ConstGlobalVarOS);
    //Technically I think this is redundant, with addExternalObject changed in tase. 
    GlobalVarOS->concreteStore = (uint8_t *) addrVal;
    */
  }

  printf("Mapping in env vars \n");
  std::cout.flush();


  //Todo -- De-hackify this environ variable mapping
  char ** environPtr = environ;
  char * envStr = environ[0];
  size_t len = 0;
  char * latestEnvStr = NULL;
  
  while (envStr != NULL) {
    
    uint32_t size = strlen(envStr);
    printf("Found env var at 0x%lx with size 0x%x \n", (uint64_t) envStr, size);

    envStr = *(environPtr++);
    if (envStr != NULL) {
      len = strlen(envStr);
      latestEnvStr = envStr;
    }
  }

  uint64_t baseEnvAddr =  (uint64_t) environ[0];
  uint64_t endEnvAddr = (uint64_t) latestEnvStr + len + 1;
  uint64_t envSize = endEnvAddr - baseEnvAddr;

  if (envSize % 2 == 1)
    envSize++;
  
  printf("Size of envs is 0x%lx \n", envSize);
  std::cout.flush();

  tase_map_buf(baseEnvAddr, envSize);
  /*
  MemoryObject * envMO = addExternalObject(*GlobalExecutionStatePtr, (void *) baseEnvAddr, envSize, false);
  const ObjectState * envOSConst = GlobalExecutionStatePtr->addressSpace.findObject(envMO);
  ObjectState *envOS = GlobalExecutionStatePtr->addressSpace.getWriteable(envMO,envOSConst);
  envOS->concreteStore = (uint8_t *) baseEnvAddr;
  */
  
  //Add mappings for stderr and stdout
  //Todo -- remove dependency on _edata location
  printf("Mapping edata at 0x%lx \n", (uint64_t) &edata);
  MemoryObject * stdMO = addExternalObject(*GlobalExecutionStatePtr, (void *) &edata, 16, false);
  const ObjectState * stdOSConst = GlobalExecutionStatePtr->addressSpace.findObject(stdMO);
  ObjectState *stdOS = GlobalExecutionStatePtr->addressSpace.getWriteable(stdMO,stdOSConst);
  stdOS->concreteStore = (uint8_t *) &edata;

  //Get rid of the dummy function used for initialization
  GlobalExecutionStatePtr->popFrame();
  processTree = new PTree(GlobalExecutionStatePtr);
  GlobalExecutionStatePtr->ptreeNode = processTree->root;

  bindModuleConstants(); //Moved from "run"

  fclose(externalsFile);
 
  
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
