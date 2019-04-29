
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
#include "../Core/ExecutorTimerInfo.h"

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

using namespace llvm;
using namespace klee;

//AH: Our additions below. --------------------------------------
//extern int loopCtr;

#include "/playpen/humphries/zTASE/TASE/test/tase/include/tase/tase_interp.h"
#include <iostream>
#include "klee/CVAssignment.h"
#include "klee/util/ExprUtil.h"
#include "tase/TASEControl.h"
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <errno.h>
#include <cxxabi.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>

extern void tase_exit();

extern bool taseManager;
extern greg_t * target_ctx_gregs;
extern klee::Interpreter * GlobalInterpreter;
extern MemoryObject * target_ctx_gregs_MO;
extern ObjectState * target_ctx_gregs_OS;
extern ExecutionState * GlobalExecutionStatePtr;

extern FILE * heapMemLog;
extern FILE * modelLog;

enum testType : int {EXPLORATION, VERIFICATION};
extern enum testType test_type;

extern uint64_t interpCtr;
extern bool taseDebug;

//Multipass
extern void * MPAPtr;
extern int replayPID;
//extern multipassRecord multipassInfo;
extern KTestObjectVector ktov;
extern bool enableMultipass;
extern void spinAndAwaitForkRequest();

CVAssignment * prevMPA = NULL;
extern void multipass_reset_round();
extern void multipass_start_round(Executor * theExecutor);
extern void multipass_replay_round(void * assignmentBufferPtr, CVAssignment * mpa, int thePid);
extern char* ktest_object_names[];
enum { CLIENT_TO_SERVER=0, SERVER_TO_CLIENT, RNG, PRNG, TIME, STDIN, SELECT,
       MASTER_SECRET };

bool roundUpHeapAllocations = true; //Round the size Arg of malloc, realloc, and calloc up to a multiple of 8
//This matters because it controls the size of the MemoryObjects allocated in klee.  Reads executed by
//some library functions (ex memcpy) may copy 4 or 8 byte-chunks of data at a time that cross over the edge
//of memory object buffers that aren't 4 or 8 byte aligned.

int maxFDs = 10;
int openFD = 3;

/*
enum FDStat_ty = {Ready, Blocked, Err}; //Oversimplification for now

struct FDSim_ty {
  int num;
  FDStat_ty FDStat; 
}

  std::map <int, FDSim_ty> fds ;
  
//Create FD tracking structures for limited IO interactions later
bool initializeFDTracking () {
  

}
*/


// Network capture for Cliver

int ktest_master_secret_calls = 0;
int ktest_start_calls = 0;
int ktest_writesocket_calls = 0;
int ktest_readsocket_calls = 0;
int ktest_raw_read_stdin_calls = 0;
int ktest_connect_calls = 0;
int ktest_select_calls = 0;
int ktest_RAND_bytes_calls = 0;
int ktest_RAND_pseudo_bytes_calls = 0;


//extern "C"
int ktest_connect_tase(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
  int ktest_select_tase(int nfds, fd_set *readfds, fd_set *writefds,
		   fd_set *exceptfds, struct timeval *timeout);
  ssize_t ktest_writesocket_tase(int fd, const void *buf, size_t count);
  ssize_t ktest_readsocket_tase(int fd, void *buf, size_t count);

  // stdin capture for Cliver
  int ktest_raw_read_stdin_tase(void *buf, int siz);

  // Random number generator capture for Cliver
  int ktest_RAND_bytes_tase(unsigned char *buf, int num);
  int ktest_RAND_pseudo_bytes_tase(unsigned char *buf, int num);

  // Time capture for Cliver (actually unnecessary!)
  time_t ktest_time_tase(time_t *t);

  // TLS Master Secret capture for Cliver
  void ktest_master_secret_tase(unsigned char *ms, int len);

  void ktest_start_tase(const char *filename, enum kTestMode mode);
  void ktest_finish_tase();               // write capture to file


extern int * __errno_location();
extern int __isoc99_sscanf ( const char * s, const char * format, ...);

//Todo: don't just skip, even though this is only for printing, change ret size

void Executor::model_sprintf() {
  printf("Entering model_sprintf at RIP 0x%llx \n", target_ctx_gregs[REG_RIP].u64);
  fprintf(modelLog,"Calling model_sprintf on string %s at interpCtr %lu \n", (char *) target_ctx_gregs[REG_RDI].u64, interpCtr);

  int res = 1;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  
}

//Todo: don't just skip, even though this is only for printing, change ret size
void Executor::model_vfprintf(){

  printf("Entering model_vfprintf at RIP 0x%llx \n", target_ctx_gregs[REG_RIP].u64);
  
  int res = 1;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  
}

void Executor::model___errno_location() {
  printf("Entering model for __errno_location \n");
  std::cout.flush();

  //Perform the call
  int * res = __errno_location();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //If it doesn't exit, back errno with a memory object.
  ObjectPair OP;
  ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
    printf("errno var appears to have MO already backing it \n");
    std::cout.flush();
    
  } else {
    printf("Creating MO to back errno at 0x%llx with size 0x%x \n", (uint64_t) res, sizeof(int));
    std::cout.flush();
    MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, sizeof(int), false);
    const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
    ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
    newOS->concreteStore = (uint8_t *) res;
  }
  
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  
}


void Executor::model_ktest_master_secret(  ) {
  ktest_master_secret_calls++;
  
  printf("Entering model_ktest_master_secret \n");
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    ktest_master_secret_tase( (unsigned char *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR Found symbolic input to model_ktest_master_secret \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }


}

void Executor::model_exit() {

  
  
  //printf("loopCtr is %d \n", loopCtr);
  printf(" Found call to exit.  TASE should shutdown. \n");
  std::cout.flush();
  std::exit(EXIT_SUCCESS);

}

void Executor::model_printf() {
  static int numCalls = 0;
  numCalls++;
  printf("Found call to printf for time %d \n",numCalls);
  char * stringArg = (char *) target_ctx_gregs[REG_RDI].u64;
  printf("First arg as string is %s \n", stringArg);

  std::cout.flush();
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

  
}

void Executor::model_ktest_start() {
  
  ktest_start_calls++;
  
  printf("Entering model_ktest_start \n");
  fprintf(modelLog,"Entering model_ktest_start at interpCtr %lu \n",interpCtr);
  //fprintf(modelLog,"ktest_start args are 
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) 
       ){
  
    ktest_start_tase( (char *)target_ctx_gregs[REG_RDI].u64, KTEST_PLAYBACK);

    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
    //return
  } else {
    printf("ERROR in ktest_start -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}


//writesocket(int fd, const void * buf, size_t count)
void Executor::model_ktest_writesocket() {
  ktest_writesocket_calls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  printf("Entering model_ktest_writesocket \n");
  fprintf(modelLog, "Entering writesocket call at PC 0x%lx, interpCtr %lu", target_ctx_gregs[REG_RIP].u64, interpCtr);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ){

    int fd = (int) target_ctx_gregs[REG_RDI].u64;
    void * buf = (void *) target_ctx_gregs[REG_RSI].u64;
    size_t count = (size_t) target_ctx_gregs[REG_RDX].u64;
    

    
    if (enableMultipass) {
      //Basic structure comes from NetworkManager in klee repo of cliver.
      
      //Get log entry for c2s
      KTestObject *o = KTOV_next_object(&ktov, ktest_object_names[CLIENT_TO_SERVER]);
      if (o->numBytes != count) {
	fprintf(modelLog,"VERIFICATION ERROR: mismatch in replay size \n");
      }

      //Create write condition
      klee::ref<klee::Expr> write_condition = klee::ConstantExpr::alloc(1, klee::Expr::Bool);
      for (int i = 0; i < count; i++) {
	klee::ref<klee::Expr> condition = klee::EqExpr::create(tase_helper_read((uint64_t) buf + i, 1),
                             klee::ConstantExpr::alloc(o->bytes[i], klee::Expr::Int8));
	write_condition = klee::AndExpr::create(write_condition, condition);
      }
      
      //Check validity of write condition
      if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
	if (CE->isFalse())
	  fprintf(modelLog, "VERIFICATION ERROR: false write condition \n");
      } else {
	bool result = true;
	//compute_false(GlobalExecutionStatePtr, write_condition, result);
	solver->mustBeFalse(*GlobalExecutionStatePtr, write_condition, result);
	
	if (result)
	  fprintf(modelLog, "VERIFICATION ERROR: write condition determined false \n");
      }
	
      //Solve for multipass assignments
      CVAssignment currMPA;
      currMPA.clear();
      if (!isa<ConstantExpr>(write_condition)) {
	currMPA.solveForBindings(solver->solver, write_condition,GlobalExecutionStatePtr);
      }
      
      //Determine if we should execute another pass
      //CVAssignment  * curr_MPA = &(GlobalExecutionStatePtr->multi_pass_assignment);
      
      if (currMPA.size() != 0 && prevMPA->bindings != currMPA.bindings) {
	multipass_replay_round(MPAPtr, &currMPA, replayPID); //Sets up child to run from prev "NEW ROUND" point
	
      }
	
      //RESET ROUND      
      multipass_reset_round(); //Sets up new buffer for MPA and destroys multipass child process

      //NEW ROUND
      multipass_start_round(this);  //Gets semaphore,sets prevMPA, and sets a replay child process up
      
      
    } else {
      ssize_t res = ktest_writesocket_tase((int) target_ctx_gregs[REG_RDI].u64, (void *) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    }
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_ktest_writesocket - symbolic arg \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}

void Executor::model_ktest_readsocket() {
  ktest_readsocket_calls++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  printf("Entering model_ktest_readsocket \n");
  fprintf(modelLog,"Entering model_ktest_readsocket \n");

  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ){

    //Return result of call
    ssize_t res = ktest_readsocket_tase((int) target_ctx_gregs[REG_RDI].u64, (void *) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_ktest_readsocket - symbolic arg \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}

void Executor::model_ktest_raw_read_stdin() {
  ktest_raw_read_stdin_calls++;
  printf("Entering model_ktest_raw_read_stdin \n");
  fprintf(modelLog,"Entering model_ktest_raw_read_stdin at interpCtr %lu \n", interpCtr);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    if (enableMultipass) {

      void * buf = (void *) target_ctx_gregs[REG_RDI].u64;
      int buffsize = (int) target_ctx_gregs[REG_RSI].u64;
      
      
      KTestObject * kto = peekNextKTestObject();
      int len = kto->numBytes;

      if (len > buffsize) {
	fprintf(modelLog, "Buffer too large for ktest_raw_read stdin \n");
	fflush(modelLog);
	std::exit(EXIT_FAILURE);
      }
      
      tase_make_symbolic( (uint64_t) buf, len, "stdin");
      
      
    } else {
      
      
      //return result of call
      int res = ktest_raw_read_stdin_tase((void *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    }
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in ktest_raw_read_stdin -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}

void Executor::model_ktest_connect() {
  ktest_connect_calls++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  printf("Calling model_ktest_connect \n");

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    //return result
    int res = ktest_connect_tase((int) target_ctx_gregs[REG_RDI].u64, (struct sockaddr *) target_ctx_gregs[REG_RSI].u64, (socklen_t) target_ctx_gregs[REG_RDX].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_ktest_connect -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
      
  }
}


//https://linux.die.net/man/2/shutdown
//int shutdown(int sockfd, int how);
void Executor::model_shutdown() {

  printf("Entering model_shutdown \n");
  fprintf(modelLog,"Entering model_shutdown at interpCtr %lu ", interpCtr);
  std::cout.flush();
  fflush(modelLog);

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr))
	) {

    std::cerr << " Entered model_shutdown call on FD %lu \n ", target_ctx_gregs[REG_RDI].u64;
    
    if (ktov.size == ktov.playback_index) {
      std::cerr << "All playback messages retrieved \n";
      fprintf(modelLog, "All playback messages retrieved \n");
      fflush(modelLog);

      tase_exit();
      std::exit(EXIT_SUCCESS);
      
    } else {
      std::cerr << "ERROR: playback message index wrong at shutdown \n";
      fprintf(modelLog, "ERROR: playback message index wrong at shutdown \n");
      fflush(modelLog);
      std::exit(EXIT_FAILURE);
    }
    
  } else {
    printf("ERROR in model_shutdown -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);

  }

}

void Executor::model_ktest_select() {
  ktest_select_calls++;
  printf("Entering model_ktest_select \n");
  fprintf(modelLog,"Entering model_ktest_select at interpCtr %lu ", interpCtr);
  std::cout.flush();
  fflush(modelLog);
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

    fprintf(modelLog, "Before ktest_select, readfds is 0x%lx, writefds is 0x%lx \n", *( (uint64_t *) target_ctx_gregs[REG_RSI].u64), *( (uint64_t *) target_ctx_gregs[REG_RDX].u64));
    fflush(modelLog);

    if (enableMultipass) {
      
      model_select();

    } else {
    
      int res = ktest_select_tase((int) target_ctx_gregs[REG_RDI].u64, (fd_set *) target_ctx_gregs[REG_RSI].u64, (fd_set *) target_ctx_gregs[REG_RDX].u64, (fd_set *) target_ctx_gregs[REG_RCX].u64, (struct timeval *) target_ctx_gregs[REG_R8].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

      fprintf(modelLog, "After ktest_select, readfds is 0x%lx, writefds is 0x%lx \n", *( (uint64_t *) target_ctx_gregs[REG_RSI].u64), *( (uint64_t *) target_ctx_gregs[REG_RDX].u64));
      fflush(modelLog);
    
      //Fake a return
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
    }
  } else {
    printf("ERROR in model_ktest_select -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }    
}

//void *memcpy(void *dest, const void *src, size_t n);
void Executor::model_memcpy() {
  fprintf(modelLog,"Entering model memcpy: Moving 0x%lx bytes from 0x%lx to 0x%lx \n",(size_t) target_ctx_gregs[REG_RDX].u64,  (void *) target_ctx_gregs[REG_RSI].u64 ,  (void*) target_ctx_gregs[REG_RDI].u64  );
  fflush(modelLog);
  void * res = memcpy((void*) target_ctx_gregs[REG_RDI].u64, (void *) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64);
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

void Executor::model_ktest_RAND_bytes() {
  ktest_RAND_bytes_calls++;
  
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  printf("Calling model_ktest_RAND_bytes \n");
  fprintf(modelLog,"Calling model_ktest_RAND_bytes at %lu \n", interpCtr);
  
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {


    if (enableMultipass) {
      tase_make_symbolic((uint64_t) &target_ctx_gregs[REG_RAX].u64, 4, "rng");
      
    } else {
      //return val
      int res = ktest_RAND_bytes_tase((unsigned char *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_ktest_RAND_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}

void Executor::model_ktest_RAND_pseudo_bytes() {
  ktest_RAND_pseudo_bytes_calls++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  printf("Calling model_test_RAND_pseudo_bytes \n");
  fprintf(modelLog,"Calling model_ktest_RAND_PSEUDO_bytes at %lu \n", interpCtr);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    //return result of call

    if (enableMultipass) {

      tase_make_symbolic((uint64_t) &target_ctx_gregs[REG_RAX].u64, 4, "prng");

    } else {
    
      int res = ktest_RAND_pseudo_bytes_tase((unsigned char *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_test_RAND_pseudo_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
}

//https://linux.die.net/man/3/fileno
//int fileno(FILE *stream); 
void Executor::model_fileno() {
  
  printf("Entering model_fileno \n");
  std::cout.flush();
  fprintf(modelLog,"Entering fileno model at %d \n",interpCtr);
  fflush(modelLog);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr)) 
       ){
    
    //return res of call
    int res = fileno((FILE *) target_ctx_gregs[REG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    fprintf(modelLog,"fileno model returned %d \n", res);
    fflush(modelLog);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_test_RAND_pseudo_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE); 
  }
}

//http://man7.org/linux/man-pages/man2/fcntl.2.html
//int fcntl(int fd, int cmd, ... /* arg */ );
void Executor::model_fcntl() {
  fprintf(modelLog, "Entering model_fcntl at interpCtr %lu \n", interpCtr);
  fflush(modelLog);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr)) 
       ){


    if ( (int) target_ctx_gregs[REG_RSI].u64 == F_SETFL && (int) target_ctx_gregs[REG_RDX].u64 == O_NONBLOCK) {
      fprintf(modelLog,"fcntl call to set fd as nonblocking \n");
      fflush(modelLog);
    } else {
      fprintf(modelLog,"fcntrl called with unmodeled args \n");
    }

    int res = 0;
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_fcntl -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);

  }
  
}


//http://man7.org/linux/man-pages/man2/stat.2.html
//int stat(const char *pathname, struct stat *statbuf);
//Todo: Make option to return symbolic result, and proprerly inspect input
void Executor::model_stat() {
  printf("Entering model_stat \n");
  std::cout.flush();
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    //return res of call
    int res = stat((char *) target_ctx_gregs[REG_RDI].u64, (struct stat *) target_ctx_gregs[REG_RSI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_start -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}

void Executor::model_getpid() {

  int pid = getpid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) pid, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;


}

//uid_t getuid(void)
//http://man7.org/linux/man-pages/man2/getuid.2.html
//Todo -- determine if we should fix result, see if uid_t is ever > 64 bits
void Executor::model_getuid() {

  printf("Calling model_getuid \n");
  
  uid_t uidResult = getuid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) uidResult, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  
}

//uid_t geteuid(void)
//http://man7.org/linux/man-pages/man2/geteuid.2.html
//Todo -- determine if we should fix result prior to forking, see if uid_t is ever > 64 bits
void Executor::model_geteuid() {

  printf("Calling model_geteuid() \n");
  
  uid_t euidResult = geteuid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) euidResult, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//gid_t getgid(void)
//http://man7.org/linux/man-pages/man2/getgid.2.html
//Todo -- determine if we should fix result, see if gid_t is ever > 64 bits
void Executor::model_getgid() {

  printf("Calling model_getgid() \n");
  
  gid_t gidResult = getgid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) gidResult, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//gid_t getegid(void)
//http://man7.org/linux/man-pages/man2/getegid.2.html
//Todo -- determine if we should fix result, see if gid_t is ever > 64 bits
void Executor::model_getegid() {

  printf("Calling model_getegid() \n");
  
  gid_t egidResult = getegid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) egidResult, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//char * getenv(const char * name)
//http://man7.org/linux/man-pages/man3/getenv.3.html
//Todo: This should be generalized, and also technically should inspect the input string's bytes
void Executor::model_getenv() {

  printf("Entering model_getenv \n");
  std::cout.flush();
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) 
       ){

    char * res = getenv((char *) target_ctx_gregs[REG_RDI].u64);

    printf("Called getenv on 0x%llx, returned 0x%llx \n", target_ctx_gregs[REG_RDI].u64, (uint64_t) res);
    std::cout.flush();
    
    //Create MO to represent the env entry
    /*
    int len;
    if (res != NULL)
      len = strlen(res);
    else
      len = 0;
    
    printf("Called strlen \n");
    std::cout.flush();
    
    if (len %2 ==1)
      len++;

    
    if ( len > 0 ) {
    
      MemoryObject * envMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, len, true);
      const ObjectState * envOSConst = GlobalExecutionStatePtr->addressSpace.findObject(envMO);
      ObjectState *envOS = GlobalExecutionStatePtr->addressSpace.getWriteable(envMO,envOSConst);
      envOS->concreteStore = (uint8_t *) res;
      printf("added MO \n");
      std::cout.flush();
      } */
    
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    printf("Leaving model_getenv \n");
    std::cout.flush();
  } else {

    printf("Found symbolic argument to model_getenv \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
}

//int socket(int domain, int type, int protocol);
//http://man7.org/linux/man-pages/man2/socket.2.html
void Executor::model_socket() {
  printf("Entering model_socket \n");
  std::cerr << "Entering model_socket \n";
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  if  (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    //Todo: Verify domain, type, protocol args.
    
    //Todo:  Generalize for better FD tracking
    int res = 3;
    fprintf(modelLog,"Setting socket FD to %d \n", res);
    
    ref<ConstantExpr> FDExpr = ConstantExpr::create(res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, FDExpr);

    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
    //openFD++;
  } else {
    printf("Found symbolic argument to model_socket \n");
    std::exit(EXIT_FAILURE);
  }
}


//This is here temporarily until we emit code without bswap assembly instructions
void Executor::model_htonl () {

  fprintf(modelLog,"Entering model_htonl at in interpCtr %lu \n", interpCtr);
  
  uint32_t res = htonl((uint32_t) target_ctx_gregs[REG_RDI].u64);
  ref<ConstantExpr> FDExpr = ConstantExpr::create(res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, FDExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
}




//Todo: Check input for symbolic args, or generalize to make not openssl-specific
void Executor::model_BIO_printf() {

  fprintf(modelLog,"Entered bio_printf at interp Ctr %lu \n", interpCtr);
  
  
  char * errMsg = (char *) target_ctx_gregs[REG_RSI].u64;

  int arg3 = (int) target_ctx_gregs[REG_RDX].u64;
  
  fprintf(modelLog, " %s \n", errMsg);
  
  fflush(modelLog);
  
  std::cerr << errMsg;
  std::cerr << " | First arg as int: ";
  std::cerr << arg3;
  std::cerr << " | interpCtr: ";
  std::cerr << interpCtr;
  std::cerr << "\n";
  
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//Todo: Check input for symbolic args, or generalize to make not openssl-specific
void Executor::model_BIO_snprintf() {

  fprintf(modelLog,"Entered bio_snprintf at interp Ctr %lu \n", interpCtr);
  
  
  char * errMsg = (char *) target_ctx_gregs[REG_RDX].u64;

  fprintf(modelLog, " %s \n", errMsg);
  fprintf(modelLog, "First snprintf arg as int: %lu \n", target_ctx_gregs[REG_RCX].u64);
  fflush(modelLog);

  if (strcmp("error:%08lX:%s:%s:%s", errMsg) == 0) {

    fprintf(modelLog, " %s \n", (char *) target_ctx_gregs[REG_R8].u64);
    fprintf(modelLog, " %s \n", (char *) target_ctx_gregs[REG_R9].u64);

  }
  
  
  std::cerr << errMsg;
  

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}


//time_t time(time_t *tloc);
// http://man7.org/linux/man-pages/man2/time.2.html
void Executor::model_time() {
  printf("Entering model_time() \n");
  std::cout.flush();
  fprintf(modelLog, "Entering call to time at interpCtr %lu \n", interpCtr);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) ) {
    time_t res = time( (time_t *) target_ctx_gregs[REG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);

    char * timeString = ctime(&res);
    fprintf(modelLog,"timeString is %s \n", timeString);
    fprintf(modelLog,"Size of timeVal is %d \n", sizeof(time_t));
    fflush(modelLog);
    
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("Found symbolic argument to model_time \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}



//struct tm *gmtime(const time_t *timep);
//https://linux.die.net/man/3/gmtime
void Executor::model_gmtime() {
 printf("Entering model_gmtime() \n");
  std::cout.flush();
  fprintf(modelLog, "Entering call to gmtime at interpCtr %lu \n", interpCtr);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) ) {
    //Do call
    struct tm * res = gmtime( (time_t *) target_ctx_gregs[REG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    char timeBuf[30];
    strftime(timeBuf, 30, "%Y-%m-%d %H:%M:%S", res);

    fprintf(modelLog, "gmtime result is %s \n", timeBuf);
    fflush(modelLog);
    
    
    //If it doesn't exit, back returned struct with a memory object.
    ObjectPair OP;
    ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
      fprintf(modelLog,"model_gmtime result appears to have MO already backing it \n");
      std::cout.flush();
      
    } else {
      fprintf(modelLog,"Creating MO to back tm at 0x%llx with size 0x%x \n", (uint64_t) res, sizeof(struct tm));
      std::cout.flush();
      MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, sizeof(struct tm), false);
      const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
      ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
      newOS->concreteStore = (uint8_t *) res;
    }
    
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("Found symbolic argument to model_gmtime \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}

//int gettimeofday(struct timeval *tv, struct timezone *tz);
//http://man7.org/linux/man-pages/man2/gettimeofday.2.html
//Todo -- properly check contents of args for symbolic content, allow for symbolic returns
void Executor::model_gettimeofday() {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    //Do call
    int res = gettimeofday( (struct timeval *) target_ctx_gregs[REG_RDI].u64, (struct timezone *) target_ctx_gregs[REG_RSI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  }  else {
    printf("Found symbolic argument to model_gettimeofday \n");
    std::exit(EXIT_FAILURE);
  }
}

size_t roundUp(size_t input, size_t multiple) {

  if (input < 0 || multiple < 0) {
    printf("Check your implementation of round_up for negative vals \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
  if (input % multiple == 0)
    return input;
  else {
    size_t divRes = input/multiple;
    return (divRes +1)* multiple;
  }
}

//void *calloc(size_t nmemb, size_t size);
//https://linux.die.net/man/3/calloc

void Executor::model_calloc() {
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    size_t nmemb = target_ctx_gregs[REG_RDI].u64;
    size_t size  = target_ctx_gregs[REG_RSI].u64;
    void * res = calloc(nmemb, size);

    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    size_t numBytes = size*nmemb;

    if (roundUpHeapAllocations)
      numBytes = roundUp(numBytes,8);
    
    printf("calloc at 0x%llx for 0x%x bytes \n", (uint64_t) res, numBytes);
    std::cout.flush();
    fprintf(heapMemLog, "CALLOC buf at 0x%llx - 0x%llx, size 0x%x, interpCtr %lu \n", (uint64_t) res, ((uint64_t) res + numBytes -1), numBytes, interpCtr);
    //Make a memory object to represent the requested buffer
    MemoryObject * heapMem = addExternalObject(*GlobalExecutionStatePtr,res, numBytes , false );
    const ObjectState *heapOS = GlobalExecutionStatePtr->addressSpace.findObject(heapMem);
    ObjectState * heapOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(heapMem,heapOS);  
    heapOSWrite->concreteStore = (uint8_t *) res;

    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("Found symbolic argument to model_calloc \n");
    std::exit(EXIT_FAILURE);
  }
    
}




//void *realloc(void *ptr, size_t size);
//https://linux.die.net/man/3/realloc
//Todo: Set up additional memory objects if realloc adds extra space
void Executor::model_realloc() {
ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
 ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

 printf("Calling model_realloc \n");
 
 if  (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) ) {

   void * ptr = (void *) target_ctx_gregs[REG_RDI].u64;
   size_t size = (size_t) target_ctx_gregs[REG_RSI].u64;
   void * res = realloc(ptr,size);

   printf("Calling realloc on 0x%llx with size 0x%x.  Ret val is 0x%llx \n", (uint64_t) ptr, (uint64_t) size, (uint64_t) res);
   if (roundUpHeapAllocations)
     size = roundUp(size, 8);
   
   ref<ConstantExpr> resultExpr = ConstantExpr::create( (uint64_t) res, Expr::Int64);
   target_ctx_gregs_OS->write(REG_RAX * 8, resultExpr);


   std::cout.flush();
   fprintf(heapMemLog, "REALLOC call on 0x%llx for size 0x%x with return value 0x%llx. interpCtr is %lu \n", (uint64_t) ptr, size, (uint64_t) res , interpCtr);

   if (res != ptr) {
     printf("REALLOC call moved site of allocation \n");
     std::cout.flush();
     ObjectPair OP;
     ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) ptr, Expr::Int64);
     if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP) ) {
       const MemoryObject * MO = OP.first;
       //Todo: carefully copy out/ copy in symbolic data if present

       GlobalExecutionStatePtr->addressSpace.unbindObject(MO);

       MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, size, false);
       const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
       ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
       newOS->concreteStore = (uint8_t *) res;
       printf("added MO for realloc at 0x%llx with size 0x%lx after orig location 0x%llx  \n", (uint64_t) res, size, (uint64_t) ptr);

     } else {
       printf("ERROR: realloc called on ptr without underlying buffer \n");
       std::cout.flush();
       std::exit(EXIT_FAILURE);
       
     }
     
   } else {

     ObjectPair OP;
     ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
     if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
       const MemoryObject * MO = OP.first;
       size_t origObjSize = MO->size;
       printf("REALLOC call kept buffer in same location \n");
       std::cout.flush();
       
       if (size <= origObjSize) {
	 //Don't need to do anything
	 printf("Realloc to smaller or equal size buffer -- no action needed \n");
       } else {
	 printf("Realloc to larger buffer \n");
	 //extend size of MO
	 //Todo: carefully copy out/ copy in symbolic data if present
	 GlobalExecutionStatePtr->addressSpace.unbindObject(MO);
	 
	 MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, size, false);
	 const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
	 ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
	 newOS->concreteStore = (uint8_t *) res;
	 printf("added MO for realloc at 0x%llx with size 0x%lx after orig size 0x%lx  \n", (uint64_t) res, size, origObjSize);
       }
     } else {
       printf("Error in realloc -- could not find original buffer info for ptr \n");
       std::cout.flush();
       std::exit(EXIT_FAILURE);
     }
   }
     
   //Fake a return
   uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
   target_ctx_gregs[REG_RIP].u64 = retAddr;
   target_ctx_gregs[REG_RSP].u64 += 8;
   
 } else {
    printf("Found symbolic argument to model_realloc \n");
    std::exit(EXIT_FAILURE);
 }
}



//http://man7.org/linux/man-pages/man3/malloc.3.html
void Executor::model_malloc() {
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);

  if (isa<ConstantExpr>(arg1Expr)) {
    size_t sizeArg = (size_t) target_ctx_gregs[REG_RDI].u64;
    if (taseDebug)
      printf("Entered model_malloc with requested size %d \n", sizeArg);

    if (roundUpHeapAllocations) 
      sizeArg = roundUp(sizeArg, 8);
    /*
    if (sizeArg % 2 == 1) {
      printf("Found malloc request for odd num of bytes; adding 1 to requested size. \n");
      sizeArg++;
      }*/
    void * buf = malloc(sizeArg);
    if (taseDebug) {
      printf("Returned ptr at 0x%llx \n", (uint64_t) buf);
      std::cout.flush();
    }
    fprintf(heapMemLog, "MALLOC buf at 0x%llx - 0x%llx, size 0x%x, interpCtr %lu \n", (uint64_t) buf, ((uint64_t) buf + sizeArg -1), sizeArg, interpCtr);
    //Make a memory object to represent the requested buffer
    MemoryObject * heapMem = addExternalObject(*GlobalExecutionStatePtr,buf, sizeArg, false );
    const ObjectState *heapOS = GlobalExecutionStatePtr->addressSpace.findObject(heapMem);
    ObjectState * heapOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(heapMem,heapOS);  
    heapOSWrite->concreteStore = (uint8_t *) buf;

    //Return pointer to malloc'd buffer in RAX
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) buf, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr); 

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

    /*
    //Ugly code to determine size of call inst
    uint64_t rip = target_ctx_gregs[REG_RIP].u64; 
    uint8_t callqOpc = 0xe8;
    uint16_t callIndOpc = 0x15ff;
    uint8_t * oneBytePtr = (uint8_t *) rip;
    uint8_t firstByte = *oneBytePtr;
    uint16_t firstTwoBytes = *( (uint16_t *) oneBytePtr);
    if (firstByte == callqOpc) {
      target_ctx_gregs[REG_RIP].u64 = target_ctx_gregs[REG_RIP].u64 +5;
    } else if (firstTwoBytes == callIndOpc) {
      target_ctx_gregs[REG_RIP].u64 = target_ctx_gregs[REG_RIP].u64 +6;
    } else {
      printf("Can't determine call opcode \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
    */
    if (taseDebug) {
      printf("INTERPRETER: Exiting model_malloc \n"); 
      std::cout.flush();
    }
  } else {
    printf("INTERPRETER: CALLING MODEL_MALLOC WITH SYMBOLIC ARGS.  NOT IMPLMENTED \n");
    target_ctx_gregs_OS->print();
    std::exit(EXIT_FAILURE);
  }
}

//https://linux.die.net/man/3/free
//Todo -- add check to see if rsp is symbolic, or points to symbolic data (somehow)

void Executor::model_free() {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  if (isa<ConstantExpr>(arg1Expr)) {

    void * freePtr = (void *) target_ctx_gregs[REG_RDI].u64;
    printf("Calling model_free on addr 0x%llx \n", (uint64_t) freePtr);
    free(freePtr);

    fprintf(heapMemLog, "FREE buf at 0x%llx, interpCtr is %lu \n", (uint64_t) freePtr, interpCtr);

    ObjectPair OP;
    ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) freePtr, Expr::Int64);
    if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
      printf("Unbinding object in free \n");
      std::cout.flush();
      GlobalExecutionStatePtr->addressSpace.unbindObject(OP.first);
      
    } else {
      printf("ERROR: Found free called without buffer corresponding to ptr \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("INTERPRETER: CALLING MODEL_FREE WITH SYMBOLIC ARGS.  NOT IMPLMENTED YET \n");
    target_ctx_gregs_OS->print();
    std::exit(EXIT_FAILURE);
  } 
}

//Todo -- check byte-by-byte through the input args for symbolic data
//http://man7.org/linux/man-pages/man3/fopen.3.html
//FILE *fopen(const char *pathname, const char *mode);
void Executor::model_fopen() {

  printf("Entering model_fopen \n");
  fprintf(modelLog,"Entering model_fopen");
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    FILE * res = fopen( (char *) target_ctx_gregs[REG_RDI].u64, (char *) target_ctx_gregs[REG_RSI].u64);
    printf("Calling fopen on file %s \n", (char *) target_ctx_gregs[REG_RDI].u64);
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR found symbolic input to model_fopen \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}

//Todo -- check byte-by-byte through the input args for symbolic data
//http://man7.org/linux/man-pages/man3/fopen.3.html
//FILE *fopen64(const char *pathname, const char *mode);
void Executor::model_fopen64() {

  printf("Entering model_fopen64 \n");

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    FILE * res = fopen64( (char *) target_ctx_gregs[REG_RDI].u64, (char *) target_ctx_gregs[REG_RSI].u64);
    printf("Calling fopen64 on file %s \n", (char *) target_ctx_gregs[REG_RDI].u64);
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR found symbolic input to model_fopen64 \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}


//http://man7.org/linux/man-pages/man3/fclose.3.html
//int fclose(FILE *stream);
//Todo -- examine all bytes of stream for symbolic taint
void Executor::model_fclose() {
  printf("Entering model_fclose \n");
  fprintf(modelLog,"Entering model_fclose at %lu \n", interpCtr);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){
    
    //We don't need to make any call
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_fclose -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}


//http://man7.org/linux/man-pages/man3/fread.3.html
//size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
//Todo -- Inspect byte-by-byte for symbolic taint
void Executor::model_fread() {

  printf("Entering model_fread \n");

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) 
	) {
  
    size_t res = fread( (void *) target_ctx_gregs[REG_RDI].u64, (size_t) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64, (FILE *) target_ctx_gregs[REG_RCX].u64);
    
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_fread -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}


void Executor::model___isoc99_sscanf() {
  
  std::cerr << "WARNING: Return 0 on unmodeled sscanf call \n";
  
  int res = 0;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  
}


//http://man7.org/linux/man-pages/man3/gethostbyname.3.html
//struct hostent *gethostbyname(const char *name);
//Todo -- check bytes of input for symbolic taint
void Executor::model_gethostbyname() {
  printf("Entering model_gethostbyname \n");
  fprintf(modelLog,"Entering model_gethostbyname at interpCtr %lu \n", interpCtr);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){
    //Do the call
    fprintf(modelLog, "Calling model_gethostbyname on %s \n", (char *) target_ctx_gregs[REG_RDI].u64);
    struct hostent * res = (struct hostent *) gethostbyname ((const char *) target_ctx_gregs[REG_RDI].u64);

    //If it doesn't exit, back hostent struct with a memory object.
    ObjectPair OP;
    ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
      fprintf(modelLog,"hostent result appears to have MO already backing it \n");
      std::cout.flush();
      
    } else {
      fprintf(modelLog,"Creating MO to back hostent at 0x%llx with size 0x%x \n", (uint64_t) res, sizeof(hostent));
      std::cout.flush();
      MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, sizeof(hostent), false);
      const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
      ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
      newOS->concreteStore = (uint8_t *) res;

      //Also map in h_addr_list elements for now until we get a better way of mapping in env vars and their associated data
      //Todo -get rid of this hack 
      
      uint64_t  baseAddr = (uint64_t) &(res->h_addr_list[0]);
      uint64_t  endAddr  = (uint64_t) &(res->h_addr_list[0][3]);
      size_t size = endAddr - baseAddr + 1;
      // 0xcfd232a0 with size 5
      
      
      fprintf(modelLog,"Mapping in buf at 0x%lx with size 0x%lx for h_addr_list", baseAddr, size);
      MemoryObject * listMO = addExternalObject(*GlobalExecutionStatePtr, (void *) baseAddr, size, false);
      const ObjectState * listOSConst = GlobalExecutionStatePtr->addressSpace.findObject(listMO);
      ObjectState * listOS = GlobalExecutionStatePtr->addressSpace.getWriteable(listMO, listOSConst);
      listOS->concreteStore = (uint8_t *) baseAddr;
      
    }

    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_gethostbyname -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}


//int setsockopt(int sockfd, int level, int optname,
//             const void *optval, socklen_t optlen);
//https://linux.die.net/man/2/setsockopt
//Todo -- actually model this
void Executor::model_setsockopt() {
  printf("Entering model_setsockopt at interpCtr %lu \n", interpCtr);
  fprintf(modelLog,"Entering model_setsockopt at interpCtr %lu \n", interpCtr);
  
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

    int res = 0; //Pass success
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_setsockoptions -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE); 
  }
}

//No args for this one
void Executor::model___ctype_b_loc() {
  printf("Entering model__ctype_b_loc at interpCtr %lu \n", interpCtr);
  fprintf(modelLog,"Entering model__ctype_b_loc at interpCtr %lu \n", interpCtr);

  const unsigned short ** constRes = __ctype_b_loc();
  unsigned short ** res = const_cast<unsigned short **>(constRes);
  
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

  
}


//int32_t * * __ctype_tolower_loc(void);
//No args
//Todo -- allocate symbolic underlying results later for testing
void Executor::model___ctype_tolower_loc() {
  printf("Entering model__ctype_tolower_loc at interpCtr %lu \n", interpCtr);
  fprintf(modelLog,"Entering model__ctype_tolower_loc at interpCtr %lu \n", interpCtr);

  const int  ** constRes = __ctype_tolower_loc();
  int ** res = const_cast<int **>(constRes);
  
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  

}

//int fflush(FILE *stream);
//Todo -- Actually model this or provide a symbolic return status
void Executor::model_fflush(){
  printf("Entering model_fflush \n");
  fprintf(modelLog, "Entering model_fflush at %lu \n", interpCtr);
  fflush(modelLog);
  
  std::cout.flush();
  //Get the input args per system V linux ABI.

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){

    int res = 0;
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;

  } else {
    printf("ERROR Found symbolic input to model_fflush \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}


//char *fgets(char *s, int size, FILE *stream);
//https://linux.die.net/man/3/fgets
void Executor::model_fgets() {
  printf("Entering model_fgets \n");
  fprintf(modelLog, "Entering model_fgets at %lu \n", interpCtr);
  
  std::cout.flush();
  //Get the input args per system V linux ABI.
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){
    //Do call
    char * res = fgets((char *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64, (FILE *) target_ctx_gregs[REG_RDX].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_fgets -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
}

//Todo -- Inspect byte-by-byte for symbolic taint
void Executor::model_fwrite() {
  printf("Entering model_fwrite \n");

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64);
  
  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr))
	) {
    
    size_t res = fwrite( (void *) target_ctx_gregs[REG_RDI].u64, (size_t) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64, (FILE *) target_ctx_gregs[REG_RCX].u64);
    
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_fwrite -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
  
  
}

//read model --------
//ssize_t read (int filedes, void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html
//Can be read from stdin or from socket -- modeled separately to break up the code.
void Executor::model_read() {
  printf("Entering model_read \n");
  std::cout.flush();
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

    int filedes = (int) target_ctx_gregs[REG_RDI].u64; //fd
    //target_ctx_gregs[REG_RSI].u64; //address of dest buf  NOT USED YET
    //target_ctx_gregs[REG_RDX].u64; //max len to read in NOT USED YET
    
    if (filedes == STDIN_FD)
      model_readstdin();    
    else if (filedes == SOCKET_FD)
      model_readsocket();
    else {
      printf("Unrecognized fd %d for model_read \n", filedes);
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
  } else  {   
    printf("Found symbolic argument to model_read \n");
    std::exit(EXIT_FAILURE);
  }  
}

void Executor::model_readsocket() {
//Get the input args per system V linux ABI.
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  printf("Entering model_readsocket \n");
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    int filedes = target_ctx_gregs[REG_RDI].u64; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI].u64; //address of dest buf 
    size_t size = target_ctx_gregs[REG_RDX].u64; //max len to read in
    
    KTestObject *o = KTOV_next_object(&ktov, "s2c");
    if (o->numBytes > size) {
      printf("readsocket error: %zu byte destination buffer, %d bytes recorded", size, o->numBytes);
      std::exit(EXIT_FAILURE);
    }

    printf("Printing read call: \n");
    memcpy((void *) buffer, o->bytes, o->numBytes);
    char * printPtr = (char *) buffer;
    for (int i = 0; i < size; i++) {
      printf("%c \n",*printPtr);
      printPtr++;
    }
    
    //Return number of bytes read and bump RIP.
    ref<ConstantExpr> bytesReadExpr = ConstantExpr::create(o->numBytes, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, bytesReadExpr);
    target_ctx_gregs[REG_RIP].u64 += 5;
    return; 
  } else {
    printf("Found symbolic args to model_readsocket \n");
    std::exit(EXIT_FAILURE);
  }
}

void Executor::model_readstdin() {
  //Get the input args per system V linux ABI.
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    int filedes = target_ctx_gregs[REG_RDI].u64; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI].u64; //address of dest buf 
    size_t size = target_ctx_gregs[REG_RDX].u64; //max len to read in
    
    if (enableMultipass) {

      //BEGIN NEW VERIFICATION STAGE =====>
      //clear old multipass assignment info.
      /*
      multipassInfo.prevMultipassAssignment.clear();
      multipassInfo.currMultipassAssignment.clear();
      multipassInfo.messageNumber++;
      multipassInfo.passCount = 0;
      multipassInfo.roundRootPID = getpid();
      */
      int pid = tase_fork();
      if (pid == 0) {
	//Continue on 
      }else {
	spinAndAwaitForkRequest();  //Should only hit this code once.
      }

      //BEGIN NEW VERIFICATION PASS =====>
      //Entry point is here from spinAndAwaitForkRequest().  At this point,
      //pass number info and the latest multipass assignment info should have been entered.     
    }

    
    printf("link in tls_predict_stdin_size again!\n");
    std::exit(EXIT_FAILURE);
    
    uint64_t numSymBytes =0;
    
    //uint64_t numSymBytes = tls_predict_stdin_size(filedes, size);
    
    if (numSymBytes <= size) // redundant based on tls_predict_stdin_size's logic.
      tase_make_symbolic((uint64_t) buffer, numSymBytes, "stdinRead");
    else {
      printf("ERROR: detected too many bytes in stdin read within model_readstdin() \n");
      std::exit(EXIT_FAILURE);
    }
 
    //Return number of bytes read and bump RIP.
    ref<ConstantExpr> bytesReadExpr = ConstantExpr::create(numSymBytes, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, bytesReadExpr);
    target_ctx_gregs[REG_RIP].u64 += 5;

  } else {
    printf("Found symbolic args to model_readstdin \n");
    std::exit(EXIT_FAILURE);
  }
}

//http://man7.org/linux/man-pages/man2/signal.2.html
// We're modeling a client that receives no signals, so just
// bump RIP for now.
void Executor::model_signal() {
  printf("Entering model_signal \n");

  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//write model ------------- 
//ssize_t write (int filedes, const void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html
void Executor::model_write() {
  printf("Entering model_write \n");
  //Get the input args per system V linux ABI.
 
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    int filedes = (int)  target_ctx_gregs[REG_RDI].u64; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI].u64; //address of dest buf 
    size_t size = (size_t) target_ctx_gregs[REG_RDX].u64; //max len to read in
    
    //Note that we just verify against the actual number of bytes that
    //would be printed, and ignore null terminators in the src buffer.
    //Write can non-deterministically print up to X chars, depending
    //on state of network stack buffers at time of call.
    //Believe this is the behavior intended by the posix spec
    
    //Get correct message buffer to verify against based on fd
    KTestObject *o = KTOV_next_object(&ktov,
				      "c2s");
    if (o->numBytes > size) {
      printf("ktest_writesocket playback error: %lu bytes of input, %u bytes recorded", size, o->numBytes);
      std::exit(EXIT_FAILURE);
    }
    // Since this is a write, compare for equality.
    if (o->numBytes > 0 && memcmp((void *) buffer, o->bytes, o->numBytes) != 0) {
      printf("WARNING: ktest_writesocket playback - data mismatch\n");
      std::exit(EXIT_FAILURE);
    }
    
    unsigned char * wireMessageRecord = o->bytes;
    int wireMessageLength = o->numBytes;

    ref<Expr> writeCondition = ConstantExpr::create(1,Expr::Bool);
    
    for (int j = 0; j < wireMessageLength; j++) {
      ref<Expr> srcCandidateVal = tase_helper_read(((uint64_t) buffer) + j, 1);  //Get 1 byte from addr buffer + j
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

    
    target_ctx_gregs[REG_RIP].u64 += 5;

    printf("Returning from model_write \n");

    if (test_type == EXPLORATION) {
      return;
    } else if (test_type == VERIFICATION) {
    
      //Determine if we can infer new bindings based on current round of execution.
      //If not, it's time to move on to next round.
      /*
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
      */
    } else {
      printf("Unhandled test type in model_send \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }

  } else {   
    printf("Found symbolic argument to model_send \n");
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
  int nfds = (int) target_ctx_gregs[REG_RDI].u64; // int nfds
  fd_set * readfds = (fd_set *) target_ctx_gregs[REG_RSI].u64; // fd_set * readfds
  fd_set * writefds = (fd_set *) target_ctx_gregs[REG_RDX].u64; // fd_set * writefds
  //fd_set * exceptfds = (fd_set *) target_ctx_gregs[REG_RCX].u64; // fd_set * exceptfds NOT USED
  //struct timeval * timeout = (struct timeval *) target_ctx_gregs[REG_R8].u64;  // struct timeval * timeout  NOT USED
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
    //target_ctx_gregs[REG_RIP].u64 = target_ctx_gregs[REG_RIP].u64 +5;
    //printf("INTERPRETER: Exiting model_select \n");
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
    target_ctx_gregs[REG_RIP].u64 = retAddr;
    target_ctx_gregs[REG_RSP].u64 += 8;
    
  } else {
    printf("ERROR: Found symbolic input to model_select()");
    std::exit(EXIT_FAILURE);
  }  
}

//int connect (int socket, struct sockaddr *addr, socklen_t length)
//https://www.gnu.org/software/libc/manual/html_node/Connecting.html
//http://man7.org/linux/man-pages/man2/connect.2.html
//Todo -- determine if we want to validate addr vs length, or the length
//of the type.
//Todo -- determine if we should simulate failures.
void Executor::model_connect () {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64);

  if ( (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr)) ) { 

    int socket = (int) target_ctx_gregs[REG_RDI].u64;
    struct sockaddr * addr = (struct sockaddr *) target_ctx_gregs[REG_RSI].u64;
    socklen_t length = (socklen_t) target_ctx_gregs[REG_RDX].u64;

    //Todo -- Generalize in case sockaddr struct isn't 14 on all platfroms
    //Need additional check to make sure sockaddr struct has no symbolic data
    bool hasSymbolicDependency = false;
    for (uint64_t i = 0; i < 14; i++) {
      ref<Expr> sockaddrExpr = tase_helper_read( ((uint64_t) addr) + i, 1);
      if (!isa<ConstantExpr>(sockaddrExpr))
	hasSymbolicDependency = true;
    }
    if (hasSymbolicDependency) {
      printf("ERROR: model_connect has unhandled symbolic dependency \n");
      std::exit(EXIT_FAILURE);
    }

    //Make sure the fd would have existed prior to the call
    if (socket == SOCKET_FD) {
      //Model the return as a success.  We can generalize this later if we want.
      ref<ConstantExpr> zeroResultExpr = ConstantExpr::create(0, Expr::Int32);
      target_ctx_gregs_OS->write(REG_RAX * 8, zeroResultExpr);
    } else {
      printf("ERROR: Unhandled model_connect failure-- unknown socket fd \n");
      std::exit(EXIT_FAILURE);
    }

    //bump RIP and interpret next instruction
    target_ctx_gregs[REG_RIP].u64 = target_ctx_gregs[REG_RIP].u64 +5;   
    
  } else {
     printf("ERROR: Found symbolic input to model_connect()");
     std::exit(EXIT_FAILURE);
  }
}
