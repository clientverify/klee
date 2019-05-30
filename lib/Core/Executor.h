//===-- Executor.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Class to perform actual execution, hides implementation details from external
// interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTOR_H
#define KLEE_EXECUTOR_H

#include "klee/ExecutionState.h"
#include "klee/Interpreter.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/util/ArrayCache.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/Twine.h"

#include <vector>
#include <string>
#include <map>
#include <set>

struct KTest;

//AH: I'd really rather not include the openssl stuff here but I guess we need it
// for the make_BN_symbolic declaration since it references a bignum struct
// FIXME: need internal openssl data types

#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/ssl.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/evp.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/ssl3.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/sha.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/ec.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/include/openssl/ossl_typ.h"

// modes_lcl.h redfines objects included here
//#include "../../../openssl/include/openssl/modes.h"

#include "/playpen/humphries/cliver/gsec-support/src/openssl/crypto/ec/ec_lcl.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/crypto/modes/modes_lcl.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/crypto/aes/aes.h"
#include "/playpen/humphries/cliver/gsec-support/src/openssl/crypto/sha/sha.h"


/*
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/lhash.h"
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/crypto.h"
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/objects.h"
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/bio.h"
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/aes.h"
#include "/playpen/humphries/zTASE/TASE/openssl/include/openssl/ssl.h"
#include "/playpen/humphries/zTASE/TASE/openssl/crypto/x509/x509_vfy.h"
#include "/playpen/humphries/zTASE/TASE/openssl/crypto/err/err.h"
#include "/playpen/humphries/zTASE/TASE/openssl/crypto/modes/modes_lcl.h"
*/


namespace llvm {
  class BasicBlock;
  class BranchInst;
  class CallInst;
  class Constant;
  class ConstantExpr;
  class Function;
  class GlobalValue;
  class Instruction;
  class LLVMContext;
  class DataLayout;
  class Twine;
  class Value;
}

namespace klee {  
  class Array;
  struct Cell;
  class ExecutionState;
  class ExternalDispatcher;
  class Expr;
  class InstructionInfoTable;
  struct KFunction;
  struct KInstruction;
  class KInstIterator;
  class KModule;
  class MemoryManager;
  class MemoryObject;
  class ObjectState;
  class PTree;
  class Searcher;
  class SeedInfo;
  class SpecialFunctionHandler;
  struct StackFrame;
  class StatsTracker;
  class TimingSolver;
  class TreeStreamWriter;
  class MergeHandler;
  template<class T> class ref;



  /// \todo Add a context object to keep track of data only live
  /// during an instruction step. Should contain addedStates,
  /// removedStates, and haltExecution, among others.

class Executor : public Interpreter {
  friend class RandomPathSearcher;
  friend class OwningSearcher;
  friend class WeightedRandomSearcher;
  friend class SpecialFunctionHandler;
  friend class StatsTracker;
  friend class MergeHandler;

public:
  class Timer {
  public:
    Timer();
    virtual ~Timer();

    /// The event callback.
    virtual void run() = 0;
  };

  typedef std::pair<ExecutionState*,ExecutionState*> StatePair;

  enum TerminateReason {
    Abort,
    Assert,
    BadVectorAccess,
    Exec,
    External,
    Free,
    Model,
    Overflow,
    Ptr,
    ReadOnly,
    ReportError,
    User,
    Unhandled
  };

  ArrayCache * getArrayCache() {
    return &arrayCache;
  }
  
private:
  static const char *TerminateReasonNames[];

  class TimerInfo;

  KModule *kmodule;
  InterpreterHandler *interpreterHandler;
  Searcher *searcher;

  ExternalDispatcher *externalDispatcher;
  TimingSolver *solver;
  MemoryManager *memory;
  std::set<ExecutionState*> states;
  StatsTracker *statsTracker;
  TreeStreamWriter *pathWriter, *symPathWriter;
  SpecialFunctionHandler *specialFunctionHandler;
  std::vector<TimerInfo*> timers;
  PTree *processTree;

  /// Used to track states that have been added during the current
  /// instructions step. 
  /// \invariant \ref addedStates is a subset of \ref states. 
  /// \invariant \ref addedStates and \ref removedStates are disjoint.
  std::vector<ExecutionState *> addedStates;
  /// Used to track states that have been removed during the current
  /// instructions step. 
  /// \invariant \ref removedStates is a subset of \ref states. 
  /// \invariant \ref addedStates and \ref removedStates are disjoint.
  std::vector<ExecutionState *> removedStates;

  /// Used to track states that are not terminated, but should not
  /// be scheduled by the searcher.
  std::vector<ExecutionState *> pausedStates;
  /// States that were 'paused' from scheduling, that now may be
  /// scheduled again
  std::vector<ExecutionState *> continuedStates;

  /// When non-empty the Executor is running in "seed" mode. The
  /// states in this map will be executed in an arbitrary order
  /// (outside the normal search interface) until they terminate. When
  /// the states reach a symbolic branch then either direction that
  /// satisfies one or more seeds will be added to this map. What
  /// happens with other states (that don't satisfy the seeds) depends
  /// on as-yet-to-be-determined flags.
  std::map<ExecutionState*, std::vector<SeedInfo> > seedMap;
  
  /// Map of globals to their representative memory object.
  std::map<const llvm::GlobalValue*, MemoryObject*> globalObjects;

  /// Map of globals to their bound address. This also includes
  /// globals that have no representative object (i.e. functions).
  std::map<const llvm::GlobalValue*, ref<ConstantExpr> > globalAddresses;

  /// The set of legal function addresses, used to validate function
  /// pointers. We use the actual Function* address as the function address.
  std::set<uint64_t> legalFunctions;

  /// When non-null the bindings that will be used for calls to
  /// klee_make_symbolic in order replay.
  const struct KTest *replayKTest;
  /// When non-null a list of branch decisions to be used for replay.
  const std::vector<bool> *replayPath;
  /// The index into the current \ref replayKTest or \ref replayPath
  /// object.
  unsigned replayPosition;

  /// When non-null a list of "seed" inputs which will be used to
  /// drive execution.
  const std::vector<struct KTest *> *usingSeeds;  

  /// Disables forking, instead a random path is chosen. Enabled as
  /// needed to control memory usage. \see fork()
  bool atMemoryLimit;

  /// Disables forking, set by client. \see setInhibitForking()
  bool inhibitForking;

  /// Signals the executor to halt execution at the next instruction
  /// step.
  bool haltExecution;  

  /// Whether implied-value concretization is enabled. Currently
  /// false, it is buggy (it needs to validate its writes).
  bool ivcEnabled;

  /// The maximum time to allow for a single core solver query.
  /// (e.g. for a single STP query)
  double coreSolverTimeout;

  /// Assumes ownership of the created array objects
  ArrayCache arrayCache;

  /// File to print executed instructions to
  llvm::raw_ostream *debugInstFile;

  // @brief Buffer used by logBuffer
  std::string debugBufferString;

  // @brief buffer to store logs before flushing to file
  llvm::raw_string_ostream debugLogBuffer;

  llvm::Function* getTargetFunction(llvm::Value *calledVal,
                                    ExecutionState &state);
  
  void executeInstruction(ExecutionState &state, KInstruction *ki);

  void printFileLine(ExecutionState &state, KInstruction *ki,
                     llvm::raw_ostream &file);

  void run(ExecutionState &initialState);

  // Given a concrete object in our [klee's] address space, add it to 
  // objects checked code can reference.
  MemoryObject *addExternalObject(ExecutionState &state, void *addr, 
                                  unsigned size, bool isReadOnly);

  void initializeGlobalObject(ExecutionState &state, ObjectState *os, 
			      const llvm::Constant *c,
			      unsigned offset);
  void initializeGlobals(ExecutionState &state);

  void stepInstruction(ExecutionState &state);
  void updateStates(ExecutionState *current);
  void transferToBasicBlock(llvm::BasicBlock *dst, 
			    llvm::BasicBlock *src,
			    ExecutionState &state);

  void callExternalFunction(ExecutionState &state,
                            KInstruction *target,
                            llvm::Function *function,
                            std::vector< ref<Expr> > &arguments);

  ObjectState *bindObjectInState(ExecutionState &state, const MemoryObject *mo,
                                 bool isLocal, const Array *array = 0);

  /// Resolve a pointer to the memory objects it could point to the
  /// start of, forking execution when necessary and generating errors
  /// for pointers to invalid locations (either out of bounds or
  /// address inside the middle of objects).
  ///
  /// \param results[out] A list of ((MemoryObject,ObjectState),
  /// state) pairs for each object the given address can point to the
  /// beginning of.
  typedef std::vector< std::pair<std::pair<const MemoryObject*, const ObjectState*>, 
                                 ExecutionState*> > ExactResolutionList;
  void resolveExact(ExecutionState &state,
                    ref<Expr> p,
                    ExactResolutionList &results,
                    const std::string &name);

  /// Allocate and bind a new object in a particular state. NOTE: This
  /// function may fork.
  ///
  /// \param isLocal Flag to indicate if the object should be
  /// automatically deallocated on function return (this also makes it
  /// illegal to free directly).
  ///
  /// \param target Value at which to bind the base address of the new
  /// object.
  ///
  /// \param reallocFrom If non-zero and the allocation succeeds,
  /// initialize the new object from the given one and unbind it when
  /// done (realloc semantics). The initialized bytes will be the
  /// minimum of the size of the old and new objects, with remaining
  /// bytes initialized as specified by zeroMemory.
  void executeAlloc(ExecutionState &state,
                    ref<Expr> size,
                    bool isLocal,
                    KInstruction *target,
                    bool zeroMemory=false,
                    const ObjectState *reallocFrom=0);

  /// Free the given address with checking for errors. If target is
  /// given it will be bound to 0 in the resulting states (this is a
  /// convenience for realloc). Note that this function can cause the
  /// state to fork and that \ref state cannot be safely accessed
  /// afterwards.
  void executeFree(ExecutionState &state,
                   ref<Expr> address,
                   KInstruction *target = 0);
  
  void executeCall(ExecutionState &state, 
                   KInstruction *ki,
                   llvm::Function *f,
                   std::vector< ref<Expr> > &arguments);
                   
  // do address resolution / object binding / out of bounds checking
  // and perform the operation
  void executeMemoryOperation(ExecutionState &state,
                              bool isWrite,
                              ref<Expr> address,
                              ref<Expr> value /* undef if read */,
                              KInstruction *target /* undef if write */);

  ////////////////////////////////////////////////////////////
  //AH: Tase additions below ---------------------------------
  ////////////////////////////////////////////////////////////
  bool isBufferEntirelyConcrete(uint64_t addr, int size);
  bool gprsAreConcrete();
  bool instructionBeginsTransaction(uint64_t pc);
  bool instructionIsModeled();
  void model_taseMakeSymbolic();
  void model_inst();
  void model_sb_disabled();
  void deadRegisterFlush();
  void printDebugInterpHeader();
  void printDebugInterpFooter();
  int  isNop(uint64_t rip);
  //AH: Special modeled labels added during our instrumentation.
  void model_entertran();
  void model_exittran();
  void model_reopentran();
  
  //AH: Internal helper functions--------------------------

  int printAllPossibleValues(ref<Expr> input);
  
  //Tase helper to write an expr directly to an addr.  Width
  //(1/2/4/8 bytes) is inferred based on type of val.
  //Written because KLEE typically requires mem ops in terms of an
  //object's base & offset rather than direct addr.
  void tase_helper_write (uint64_t address, ref<Expr> val);
  //Tase helper to read an expr directly from addr with 1/2/4/8 bytes.
  //Written because KLEE typically requires mem ops in terms of an
  //object's base & offset rather than direct addr.
  ref<Expr> tase_helper_read (uint64_t address, uint8_t byteWidth);
  void tase_make_symbolic (uint64_t addr, uint64_t len,const  char * name);
  void rewriteConstants(uint64_t addr, size_t num);
  
  void model_tase_debug();
  
  void model_ktest_start();
  void model_ktest_master_secret();
  void model_ktest_writesocket();
  void model_ktest_readsocket();
  void model_ktest_raw_read_stdin();
  void model_ktest_connect();
  void model_ktest_select();
  void model_ktest_RAND_bytes();
  void model_ktest_RAND_pseudo_bytes();
  
  //AH: Modeled sys functions--------------------------------
  //Todo -- get rid of  rand_add and rand_load_file traps
  void model_RAND_add(); //Temporarily trapping on RAND_add bc of dependcy on doubles
  void model_RAND_load_file(); //Temp trapping on rand_load_file bc of dependency on floats
  void model_htonl();//Temporarily here bc of bswap assembly

  

  //For debugging
  void model_memmove(); 
  void model_memset();
  void model_memcpy();
  
  void model_setsockopt();
  void model_socket();
  void model_connect();

  void model_time();
  void model_gmtime();
  void model_getpid();
  void model_getuid();
  void model_geteuid();
  void model_getgid();
  void model_getegid();
  void model_getenv();
  void model_stat();
  void model_gettimeofday();
  void model_gethostbyname();
  void model_fileno();
  void model_fcntl();
  void model_fopen();
  void model_fopen64();
  void model_fclose();
  void model_fread();
  void model_fwrite();
  void model_fgets();
  void model_fflush();
  //void model_read();  Read modeling goes through ktest fns
  //void model_readstdin();
  //void model_readsocket();
  //void model_write();  Write modeling goes through ktest fns
  void model_shutdown();
  void model_random();
  void model_strncat();
  void model_select();
  void model_signal();
  void model_malloc();
  void model_calloc();
  void model_realloc();
  void model_free();
  void model_vfprintf();
  void model_sprintf();
  void model_printf();
  void model_exit();
  void model_write();
  void model___errno_location();
  void model___ctype_b_loc();
  void model___ctype_tolower_loc();
  void model___isoc99_sscanf();
  //AH: Modeling specific to tls---------------------------

  void model_OpenSSLDie();
  void model_BIO_printf();  //Todo: generalize to make less openssl-dependent
  void model_BIO_snprintf();
  void model_tls1_generate_master_secret();

  //Prohib functions
  void model_AES_encrypt();
  void model_gcm_ghash_4bit();
  void model_gcm_gmult_4bit();
  void model_SHA1_Update();
  void model_SHA1_Final();
  void model_SHA256_Update();
  void model_SHA256_Final();
  void model_EC_KEY_generate_key();
  void model_ECDH_compute_key();
  void model_EC_POINT_point2oct(); 

  void model_sha1_block_data_order(); //Internal function in sha
  void model_sha256_block_data_order(); //Internal function in sha
  
  
  uint64_t tls_predict_stdin_size (int fd, uint64_t maxLen);
  BIGNUM * BN_new_tase();
  EC_POINT * EC_POINT_new_tase(EC_GROUP * group);
  void make_BN_symbolic(BIGNUM * bn,const char * name); 
  void make_EC_POINT_symbolic(EC_POINT* p);
  bool is_symbolic_EC_POINT(EC_POINT * p);
  bool is_symbolic_BIGNUM(BIGNUM * bn);
  //AH: RNG modeling---------------------------------------

  void model_RAND_bytes();
  void model_RAND_pseudo_bytes();
  void model_RAND_poll();
  
  //////////////////////////////////////////////////////////
  //END Tase additions--------------------------------------
  //////////////////////////////////////////////////////////
  
  void executeMakeSymbolic(ExecutionState &state, const MemoryObject *mo,
                           const std::string &name);

  /// Create a new state where each input condition has been added as
  /// a constraint and return the results. The input state is included
  /// as one of the results. Note that the output vector may included
  /// NULL pointers for states which were unable to be created.
  void branch(ExecutionState &state, 
              const std::vector< ref<Expr> > &conditions,
              std::vector<ExecutionState*> &result);

  // Fork current and return states in which condition holds / does
  // not hold, respectively. One of the states is necessarily the
  // current state, and one of the states may be null.
  StatePair fork(ExecutionState &current, ref<Expr> condition, bool isInternal);

  /// Add the given (boolean) condition as a constraint on state. This
  /// function is a wrapper around the state's addConstraint function
  /// which also manages propagation of implied values,
  /// validity checks, and seed patching.
  void addConstraint(ExecutionState &state, ref<Expr> condition);

  // Called on [for now] concrete reads, replaces constant with a symbolic
  // Used for testing.
  ref<Expr> replaceReadWithSymbolic(ExecutionState &state, ref<Expr> e);

  const Cell& eval(KInstruction *ki, unsigned index, 
                   ExecutionState &state) const;

  Cell& getArgumentCell(ExecutionState &state,
                        KFunction *kf,
                        unsigned index) {
    return state.stack.back().locals[kf->getArgRegister(index)];
  }

  Cell& getDestCell(ExecutionState &state,
                    KInstruction *target) {
    return state.stack.back().locals[target->dest];
  }

  void bindLocal(KInstruction *target, 
                 ExecutionState &state, 
                 ref<Expr> value);
  void bindArgument(KFunction *kf, 
                    unsigned index,
                    ExecutionState &state,
                    ref<Expr> value);

  /// Evaluates an LLVM constant expression.  The optional argument ki
  /// is the instruction where this constant was encountered, or NULL
  /// if not applicable/unavailable.
  ref<klee::ConstantExpr> evalConstantExpr(const llvm::ConstantExpr *c,
					   const KInstruction *ki = NULL);

  /// Evaluates an LLVM constant.  The optional argument ki is the
  /// instruction where this constant was encountered, or NULL if
  /// not applicable/unavailable.
  ref<klee::ConstantExpr> evalConstant(const llvm::Constant *c,
				       const KInstruction *ki = NULL);

  /// Return a unique constant value for the given expression in the
  /// given state, if it has one (i.e. it provably only has a single
  /// value). Otherwise return the original expression.
  ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &e);

  /// Return a constant value for the given expression, forcing it to
  /// be constant in the given state by adding a constraint if
  /// necessary. Note that this function breaks completeness and
  /// should generally be avoided.
  ///
  /// \param purpose An identify string to printed in case of concretization.
  ref<klee::ConstantExpr> toConstant(ExecutionState &state, ref<Expr> e, 
                                     const char *purpose);

  /// Bind a constant value for e to the given target. NOTE: This
  /// function may fork state if the state has multiple seeds.
  void executeGetValue(ExecutionState &state, ref<Expr> e, KInstruction *target);

  /// Get textual information regarding a memory address.
  std::string getAddressInfo(ExecutionState &state, ref<Expr> address) const;

  // Determines the \param lastInstruction of the \param state which is not KLEE
  // internal and returns its InstructionInfo
  const InstructionInfo & getLastNonKleeInternalInstruction(const ExecutionState &state,
      llvm::Instruction** lastInstruction);

  bool shouldExitOn(enum TerminateReason termReason);

  // remove state from searcher only
  void pauseState(ExecutionState& state);
  // add state to searcher only
  void continueState(ExecutionState& state);
  // remove state from queue and delete
  void terminateState(ExecutionState &state);
  // call exit handler and terminate state
  void terminateStateEarly(ExecutionState &state, const llvm::Twine &message);
  // call exit handler and terminate state
  void terminateStateOnExit(ExecutionState &state);
  // call error handler and terminate state
  void terminateStateOnError(ExecutionState &state, const llvm::Twine &message,
                             enum TerminateReason termReason,
                             const char *suffix = NULL,
                             const llvm::Twine &longMessage = "");

  // call error handler and terminate state, for execution errors
  // (things that should not be possible, like illegal instruction or
  // unlowered instrinsic, or are unsupported, like inline assembly)
  void terminateStateOnExecError(ExecutionState &state, 
                                 const llvm::Twine &message,
                                 const llvm::Twine &info="") {
    terminateStateOnError(state, message, Exec, NULL, info);
  }

  /// bindModuleConstants - Initialize the module constant table.
  void bindModuleConstants();

  template <typename TypeIt>
  void computeOffsets(KGEPInstruction *kgepi, TypeIt ib, TypeIt ie);

  /// bindInstructionConstants - Initialize any necessary per instruction
  /// constant values.
  void bindInstructionConstants(KInstruction *KI);

  void handlePointsToObj(ExecutionState &state, 
                         KInstruction *target, 
                         const std::vector<ref<Expr> > &arguments);

  void doImpliedValueConcretization(ExecutionState &state,
                                    ref<Expr> e,
                                    ref<ConstantExpr> value);

  /// Add a timer to be executed periodically.
  ///
  /// \param timer The timer object to run on firings.
  /// \param rate The approximate delay (in seconds) between firings.
  void addTimer(Timer *timer, double rate);

  void initTimers();
  void processTimers(ExecutionState *current,
                     double maxInstTime);
  void checkMemoryUsage();
  void printDebugInstructions(ExecutionState &state);
  void doDumpStates();

public:
  Executor(llvm::LLVMContext &ctx, const InterpreterOptions &opts,
      InterpreterHandler *ie);
  virtual ~Executor();

  const InterpreterHandler& getHandler() {
    return *interpreterHandler;
  }

  virtual void setPathWriter(TreeStreamWriter *tsw) {
    pathWriter = tsw;
  }
  virtual void setSymbolicPathWriter(TreeStreamWriter *tsw) {
    symPathWriter = tsw;
  }

  virtual void setReplayKTest(const struct KTest *out) {
    assert(!replayPath && "cannot replay both buffer and path");
    replayKTest = out;
    replayPosition = 0;
  }

  virtual void setReplayPath(const std::vector<bool> *path) {
    assert(!replayKTest && "cannot replay both buffer and path");
    replayPath = path;
    replayPosition = 0;
  }

  virtual const llvm::Module *
  setModule(llvm::Module *module, const ModuleOptions &opts);

  virtual void useSeeds(const std::vector<struct KTest *> *seeds) { 
    usingSeeds = seeds;
  }
  
  virtual void klee_interp_internal ();
  virtual bool resumeNativeExecution ();
  virtual void forkOnPossibleRIPValues(ref <Expr> RIPExpr, uint64_t initRIP);
  virtual void initializeInterpretationStructures (llvm::Function *f);

  virtual void runFunctionAsMain(llvm::Function *f,
                                 int argc,
                                 char **argv,
                                 char **envp);

  /*** Runtime options ***/
  
  virtual void setHaltExecution(bool value) {
    haltExecution = value;
  }

  virtual void setInhibitForking(bool value) {
    inhibitForking = value;
  }

  void prepareForEarlyExit();

  /*** State accessor methods ***/

  virtual unsigned getPathStreamID(const ExecutionState &state);

  virtual unsigned getSymbolicPathStreamID(const ExecutionState &state);

  virtual void getConstraintLog(const ExecutionState &state,
                                std::string &res,
                                Interpreter::LogType logFormat = Interpreter::STP);

  virtual bool getSymbolicSolution(const ExecutionState &state, 
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res);

  virtual void getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res);

  Expr::Width getWidthForLLVMType(llvm::Type *type) const;
  size_t getAllocationAlignment(const llvm::Value *allocSite) const;
};
  
} // End klee namespace

#endif
