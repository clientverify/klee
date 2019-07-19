
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

#include "../../../test/tase/include/tase/tase_interp.h"
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
#include <fstream>

extern void tase_exit();

extern uint64_t total_interp_returns;
extern bool taseManager;
extern greg_t * target_ctx_gregs;
extern klee::Interpreter * GlobalInterpreter;
extern MemoryObject * target_ctx_gregs_MO;
extern ObjectState * target_ctx_gregs_OS;
extern ExecutionState * GlobalExecutionStatePtr;
extern bool gprsAreConcrete();
enum testType : int {EXPLORATION, VERIFICATION};
extern enum testType test_type;

extern uint64_t interpCtr;
extern bool taseDebug;
bool modelDebug = false;
extern void * rodata_base_ptr;
extern uint64_t rodata_size;

//Multipass
extern double solver_start_time;
extern double solver_end_time;
extern double solver_diff_time;
extern double target_start_time;
extern double solver_time;
extern double interpreter_time;
enum runType : int {INTERP_ONLY, MIXED};
extern enum runType exec_mode;
extern bool lockOnSolverCalls; //ABH: Made for debugging.  Take semaphore when calling solver in case it's forked.
extern int c_special_cmds; //Int used by cliver to disable special commands to s_client.  Made global for debugging
extern void get_sem_lock();
extern void release_sem_lock();
extern std::stringstream workerIDStream;
extern void * MPAPtr;
extern int * replayPIDPtr;
//extern multipassRecord multipassInfo;
extern void printKTestCounters();
extern void printProhibCounters();
extern int roundCount;
extern int passCount;
extern KTestObjectVector ktov;
extern bool enableMultipass;
extern void spinAndAwaitForkRequest();
extern bool dropS2C;
uint64_t native_ret_off = 0;

extern bool dont_model;

extern CVAssignment prevMPA ;
extern void multipass_reset_round();
extern void multipass_start_round(Executor * theExecutor, bool isReplay);
extern void multipass_replay_round(void * assignmentBufferPtr, CVAssignment * mpa, int * thePid);
extern void worker_exit();
extern char* ktest_object_names[];
enum { CLIENT_TO_SERVER=0, SERVER_TO_CLIENT, RNG, PRNG, TIME, STDIN, SELECT,
       MASTER_SECRET };

bool roundUpHeapAllocations = true; //Round the size Arg of malloc, realloc, and calloc up to a multiple of 8
//This matters because it controls the size of the MemoryObjects allocated in klee.  Reads executed by
//some library functions (ex memcpy) may copy 4 or 8 byte-chunks of data at a time that cross over the edge
//of memory object buffers that aren't 4 or 8 byte aligned.



extern int AES_encrypt_calls ;
extern int ECDH_compute_key_calls ;
extern int EC_POINT_point2oct_calls ;
extern int EC_KEY_generate_key_calls ;
extern int SHA1_Update_calls;
extern int SHA1_Final_calls ;
extern int SHA256_Update_calls ;
extern int SHA256_Final_calls ;
extern int gcm_gmult_4bit_calls ;
extern int gcm_ghash_4bit_calls ;

extern FILE * bignumLog;
void tase_print_BIGNUM(FILE * f, BIGNUM * bn);


void printBuf(FILE * f,void * buf, size_t count)
{
  fprintf(f,"Calling printBuf with count %d \n", count);
  fflush(f);
  for (size_t i = 0; i < count; i++) {
    fprintf(f,"%02x", *((uint8_t *) buf + i));
    fflush(f);
  }
  fprintf(f,"\n\n");
  fflush(f);
}

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
  printf("Calling model_sprintf on string %s at interpCtr %lu \n", (char *) target_ctx_gregs[GREG_RDI].u64, interpCtr);
  fflush(stdout);
  
  int res = 1;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}

//Todo: don't just skip, even though this is only for printing, change ret size
void Executor::model_vfprintf(){

  printf("Entering model_vfprintf at RIP 0x%lx \n", target_ctx_gregs[GREG_RIP].u64);
  
  int res = 1;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}


//Fake a ret

/*
void Executor::fake_ret() {

ref <Expr> rv = tase_helper_read(target_ctx_gregs[GREG_RSP].u64, 8);
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}
*/
void Executor::model___errno_location() {
  printf("Entering model for __errno_location \n");
  std::cout.flush();

  //Perform the call
  int * res = __errno_location();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //If it doesn't exit, back errno with a memory object.
  ObjectPair OP;
  ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
    printf("errno var appears to have MO already backing it \n");
    std::cout.flush();
    
  } else {
    printf("Creating MO to back errno at 0x%lx with size 0x%lx \n", (uint64_t) res, sizeof(int));
    std::cout.flush();
    MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, sizeof(int), false);
    const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
    ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
    newOS->concreteStore = (uint8_t *) res;
  }
  
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}


void Executor::model_ktest_master_secret(  ) {
  ktest_master_secret_calls++;
  
  printf("Entering model_ktest_master_secret \n");
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    printf("Entering model_ktest_master_secret \n");
    
    
    unsigned char * buf = (unsigned char *) target_ctx_gregs[GREG_RDI].u64; 
    int num = (int) target_ctx_gregs[GREG_RSI].u64;
    
    if (enableMultipass) {
      printf("CRITICAL ERROR: Should have trapped on tls1_generate_master_secret since multipass is enabled but landed in ktest_master_secret instead \n");
      fflush(stdout);
      worker_exit();

      FILE * theFile = fopen("monday.secret", "rb");
      unsigned char tmp [48];
      
      fread(tmp, 1 , 48, theFile);
      printf("Printing    results of attempt to load master secret as binary... \n");
      for (int i = 0; i < 48; i++) {
	printf(" %2.2x", tmp[i]);
      }
      printf("\n");
      
      memcpy (buf, tmp, num); //Todo - use tase_helper read/write
	       
      //Todo: - Less janky io here.
      
    }else {
      ktest_master_secret_tase( (unsigned char *) target_ctx_gregs[GREG_RDI].u64, (int) target_ctx_gregs[GREG_RSI].u64);

      printf("PRINTING MASTER SECRET as hex \n");
      uint8_t * base = (uint8_t *) target_ctx_gregs[GREG_RDI].u64;
      for (int i = 0; i < num; i++)
	printf("%02x", *(base + i));
      printf("\n------------\n");
      printf("PRINTING MASTER SECRET as uint8_t line-by-line \n");
      for (int i = 0; i < num; i++)
	printf("%u\n", (*(base +i)));
      printf("\n------------\n");
      
    }
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

  } else {
    printf("ERROR Found symbolic input to model_ktest_master_secret \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
}

void Executor::model_exit() {

  printf(" Found call to exit.  TASE should shutdown. \n");
  std::cout.flush();
  printf("IMPORTANT: Worker exiting from terminal path in round %d pass %d from model_exit \n", roundCount, passCount);
  std::cout.flush();
  worker_exit();
  std::exit(EXIT_SUCCESS);

}

//http://man7.org/linux/man-pages/man2/write.2.html
//ssize_t write(int fd, const void *buf, size_t count);
void Executor::model_write() {
  //Just print the second arg for debugging.
  //We will probably drop the server -> client messages
  //which call write anyway as per the NSDI paper.

  char * theBuf = (char *)  target_ctx_gregs[GREG_RSI].u64;
  size_t size = target_ctx_gregs[GREG_RDX].u64;
  printf("Entering model_write \n");
  fflush(stdout);

  char printMe [size];

  strncpy (printMe, theBuf, size);

  //Assume that the write succeeds 
  uint64_t res = target_ctx_gregs[GREG_RDX].u64;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX].u64, resExpr);
  
  printf("Found call to write.  Buf appears to be %s \n", printMe);
  
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

void Executor::model_printf() {
  static int numCalls = 0;
  numCalls++;
  printf("Found call to printf for time %d \n",numCalls);
  char * stringArg = (char *) target_ctx_gregs[GREG_RDI].u64;
  printf("First arg as string is %s \n", stringArg);
  printf("Second arg as num is 0x%lx \n", target_ctx_gregs[GREG_RSI].u64);
  
  //Check for special tase debug calls to bio printf
  //where we just pass a string arg.
  std::string argString (stringArg);
  std::string taseDbgString ("TASE_DBG_STR");
  if (argString.find(taseDbgString) != std::string::npos) {
    printf("Attempting to print special TASE_DBG_STR: \n");
    std::cout.flush();
    printf("%s \n", (char *) target_ctx_gregs[GREG_RSI].u64);
    
  }

  std::string taseDbgNum ("TASE_DBG_NUM");
  if (argString.find(taseDbgNum) != std::string::npos) {
    printf("Attempting to print special TASE_DBG_NUM: \n");
    std::cout.flush();

    printf("DBG NUM is %d \n", (char *) target_ctx_gregs[GREG_RSI].u64);
    
  }

  std::cout.flush();
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
}





void Executor::model_ktest_start() {
  
  ktest_start_calls++;
  printf("Entering model_ktest_start at interpCtr %lu \n",interpCtr);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) 
       ){
  
    ktest_start_tase( (char *)target_ctx_gregs[GREG_RDI].u64, KTEST_PLAYBACK);

    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
    //return
  } else {
    printf("ERROR in ktest_start -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}

//write model ------------- 
//ssize_t write (int filedes, const void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.htm
//writesocket(int fd, const void * buf, size_t count)
void Executor::model_ktest_writesocket() {
  ktest_writesocket_calls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);

  printf("Entering model_ktest_writesocket for time %d \n", ktest_writesocket_calls);
  printf("Entering writesocket call at PC 0x%lx, interpCtr %lu", target_ctx_gregs[GREG_RIP].u64, interpCtr);

  printf("Total interpreter returns: %d \n",total_interp_returns);
  printf("Abort_count_total: %d \n", target_ctx.abort_count_total);
  printf("  - modeled %d \n", target_ctx.abort_count_modeled);
  printf("  - poison %d \n", target_ctx.abort_count_poison);
  printf("  - unknown %d \n", target_ctx.abort_count_unknown);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ){

    int fd = (int) target_ctx_gregs[GREG_RDI].u64;
    void * buf = (void *) target_ctx_gregs[GREG_RSI].u64;
    size_t count = (size_t) target_ctx_gregs[GREG_RDX].u64;
    
    bool debugMultipass = true;

    if (debugMultipass) {
      printf("MULTIPASS DEBUG: Entering call to ktest_writesocket round %d pass %d \n", roundCount, passCount);
      double theTime = util::getWallTime();
      
      printf("MULTIPASS DEBUG: Entering ktest_writesocket at round %d pass %d %lf seconds into analysis \n", roundCount, passCount, theTime - target_start_time);
      std::cout.flush();
      printProhibCounters();
      printKTestCounters();
    }

   
    
    bool concWrite = isBufferEntirelyConcrete((uint64_t)buf, count);
    if (concWrite)
      printf("Buffer entirely concrete for writesock call \n");
    else
      printf("Symbolic data in buffer for writesock call \n");

    std::cout.flush();
    
    if (enableMultipass) {
     
      //Basic structure comes from NetworkManager in klee repo of cliver.
      //Get log entry for c2s
      KTestObject *o = KTOV_next_object(&ktov, ktest_object_names[CLIENT_TO_SERVER]);
      if (modelDebug) {
	printf("Buffer in writesock call : \n");
	printBuf (stdout,(void *) buf, count);
	printf("Buffer in            log : \n");
	printBuf (stdout,(void *) o->bytes, o->numBytes);
      }
      if (o->numBytes > count) {
	printf("IMPORTANT: VERIFICATION ERROR - write buffer size mismatch %u vs %u : Worker exiting from terminal path in round %d pass %d. \n",o->numBytes, count,  roundCount, passCount);
	std::cout.flush();
	worker_exit();
      }

      //Create write condition
      klee::ref<klee::Expr> write_condition = klee::ConstantExpr::alloc(1, klee::Expr::Bool);
      for (int i = 0; i < o->numBytes; i++) {
	klee::ref<klee::Expr> val = tase_helper_read((uint64_t) buf + i, 1);
	fflush(stderr);
	klee::ref<klee::Expr> condition = klee::EqExpr::create(tase_helper_read((uint64_t) buf + i, 1),
							       klee::ConstantExpr::alloc(o->bytes[i], klee::Expr::Int8));
	write_condition = klee::AndExpr::create(write_condition, condition);
      }
      
      //Fast path
      /*
	if (concWrite) {
	int cmp = strncmp((const char *) buf, (const char *) o->bytes, count);
	if (cmp == 0 && count == o->numBytes)
	printf("Concrete match between verifier and log message \n");
	std::cout.flush();
	std::exit(EXIT_SUCCESS);
	}*/

      addConstraint(*GlobalExecutionStatePtr, write_condition);
      //Check validity of write condition
      
      if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
	if (CE->isFalse()) {
	  printf("IMPORTANT: VERIFICATION ERROR: false write condition. Worker exiting from terminal path in round %d pass %d \n", roundCount, passCount);
	  std::cout.flush();
	  worker_exit();
	}
      } else {
	
	bool result;
	solver_start_time = util::getWallTime();
	if (lockOnSolverCalls)
	  get_sem_lock();
	
	solver->mustBeFalse(*GlobalExecutionStatePtr, write_condition, result);
	printf("lockOnSolverCalls is %d \n", lockOnSolverCalls);
	if (lockOnSolverCalls)
	  release_sem_lock();
	solver_end_time = util::getWallTime();
	solver_diff_time = solver_end_time - solver_start_time;	
	printf("Elapsed solver time (multipass) is %lf at interpCtr %lu \n", solver_diff_time, interpCtr);
	solver_time += solver_diff_time;
	printf("Total solver time is %lf at interpCtr %lu \n", solver_time, interpCtr);
	
	if (result) {
	  printf("VERIFICATION ERROR: write condition determined false \n");
	  printf("IMPORTANT: VERIFICATION ERROR: false write condition. Worker exiting from terminal path in round %d pass %d \n", roundCount, passCount);
	  fflush(stdout);
	  worker_exit();
	} else {
	  printf("Good news.  mustBeFalse(write_condition) returned false \n");
	  fflush(stdout);
	}
      }
      
      //Solve for multipass assignments
      CVAssignment currMPA;
      currMPA.clear();
      
      if (!isa<ConstantExpr>(write_condition)) {
	solver_start_time = util::getWallTime();
	if (lockOnSolverCalls)
	  get_sem_lock();
	currMPA.solveForBindings(solver->solver, write_condition,GlobalExecutionStatePtr);
	if (lockOnSolverCalls)
	  release_sem_lock();
	solver_end_time = util::getWallTime();
	solver_diff_time = solver_end_time - solver_start_time;
	printf("Elapsed solver time (solver) is %lf at interpCtr %lu \n", solver_diff_time, interpCtr);
	solver_time += solver_diff_time;
	printf("Total solver time is %lf at interpCtr %lu \n", solver_time, interpCtr);
      }

      //print assignments
      if (modelDebug) {
	printf("About to print assignments \n");
	std::cout.flush();
	
	currMPA.printAllAssignments(NULL);
      }
      //REPLAY ROUND
      //------------------------
      // NOT(isInQA(*replayPidPtr)) => isDead(MPAPtr);
      //-------------------------------
      //In other words, we deserialize the data in the MMap'd MPAPtr buffer and set up a new replay PID
      //atomically so that multiple processes replaying in the current round don't clobber each other's
      //serialized constraints.
      //1.  Spin until semaphore is available, AND NOT(isInQA(*replayPidPtr)).
      //2.  After acquiring semaphore when NOT(isInQA(*replayPidPtr)),
      //     atomically (serialize current MPA assignment in MPAPtr, and move *replayPidPtr into QA)
      //3.  Remove self from QR and exit, releasing semaphore.

      double curr_time = util::getWallTime();
      
      printf("Near end of ktest_writesocket round %d pass %d, times are as follows: \n", roundCount, passCount);
      printf("Total time since analysis began: %lf \n", curr_time - target_start_time  );
      printf("Total time in solver calls : %lf \n", solver_time);
      printf("Total time in interpreter  : %lf \n", interpreter_time);
     
      
      if (currMPA.size()  != 0 ) {
	if (prevMPA.bindings.size() != 0) {
	  if  (prevMPA.bindings != currMPA.bindings ) {
	    printf("IMPORTANT: prevMPA and currMPA bindings differ. Replaying round from round %d pass %d \n", roundCount, passCount);
	    std::cout.flush();
	    multipass_replay_round(MPAPtr, &currMPA, replayPIDPtr); //Sets up child to run from prev "NEW ROUND" point
	  } else {
	    printf("IMPORTANT: No new bindings found at end of round %d pass %d.  Not replaying. \n", roundCount, passCount);
	    std::cout.flush();
	  }
	} else {
	  printf("IMPORTANT: found assignments and prevMPA is null so replaying at end of round %d pass %d \n",  roundCount, passCount);
	  multipass_replay_round(MPAPtr, &currMPA, replayPIDPtr); //Sets up child to run from prev "NEW ROUND" point
	  std::cout.flush();
	}
      } else {
	printf("IMPORTANT: No assignments found in currMPA. Not replaying inside writesocket call at round %d pass %d \n", roundCount, passCount);
	std::cout.flush();
      }

      printf("Hit new call to multipass_reset_round in writesocket for round %d pass %d \n", roundCount, passCount);
      std::cout.flush();
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX], ConstantExpr::create(o->numBytes, Expr::Int64));
      
      //RESET ROUND
      //-------------------------------------------
      //1. MMAP a new buffer storing the ID of the replay for the current round.
      //2. MMAP a new buffer for storing the assignments learned from the previous pass 
      multipass_reset_round(); //Sets up new buffer for MPA and destroys multipass child process
      
      //NEW ROUND
      //-------------------------------------------
      //1. Atomically create a new SIGSTOP'd replay process and deserialize the constraints
      multipass_start_round(this, false);  //Gets semaphore,sets prevMPA, and sets a replay child process up

      curr_time = util::getWallTime();
      
      printf("At start of ktest_writesocket round %d pass %d, time since beginning is %lf \n", roundCount, passCount, curr_time - target_start_time);
      printf("Total time since analysis began: %lf \n", curr_time - target_start_time  );
      printf("Total time in solver calls : %lf \n", solver_time);
      printf("Total time in interpreter  : %lf \n", interpreter_time);
      
    } else {
      printf("Buffer in writesock call : \n");
      printBuf (stdout, (void *) buf, count);

      ssize_t res = ktest_writesocket_tase((int) target_ctx_gregs[GREG_RDI].u64, (void *) target_ctx_gregs[GREG_RSI].u64, (size_t) target_ctx_gregs[GREG_RDX].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    }
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_ktest_writesocket - symbolic arg \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

  }

//read model --------
//ssize_t read (int filedes, void *buffer, size_t size)
//https://www.gnu.org/software/libc/manual/html_node/I_002fO-Primitives.html

//Can be read from stdin or from socket -- modeled separately to break up the code.
void Executor::model_ktest_readsocket() {
  ktest_readsocket_calls++;
  /*
  if (ktest_readsocket_calls == 2) {
    fprintf(stderr, "Setting  interp only to true \n");
    exec_mode= INTERP_ONLY;
    }*/
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);

  //Hack to trim down size of log for debugging 01
  //TODO: Remove
  /*
  if (ktest_readsocket_calls == 6) {
    fprintf(stderr,"TEMP DBG: setting taseDebug to true \n");
    taseDebug = true;
  }
  */
  printf("Entering model_ktest_readsocket for time %d \n", ktest_readsocket_calls);
  fflush(stdout);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ){

    //Return result of call
    ssize_t res = ktest_readsocket_tase((int) target_ctx_gregs[GREG_RDI].u64, (void *) target_ctx_gregs[GREG_RSI].u64, (size_t) target_ctx_gregs[GREG_RDX].u64);
    printf("Returned from ktest_readsocket_tase call \n");

    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_ktest_readsocket - symbolic arg \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
}




// Reworked from cliver's CVExecutor.cpp.
// Predict size of stdin read based on the size of the next
// client-to-server TLS record, assuming the negotiated symmetric
// ciphersuite is AES128-GCM.

// Case 1: (OpenSSL and BoringSSL) The next c2s TLS application data
//         record [byte#1 = 23] is 29 bytes longer than stdin.

// Case 2: (OpenSSL only) The stdin read is 0, i.e., Control-D on a
//         blank line, thereby closing the connection.  In this case,
//         the subsequent c2s TLS alert record [byte#1 = 21] has
//         length 31.

// Case 3: (BoringSSL only) The stdin read is 0, i.e., Control-D on a
//         blank line, thereby closing the connection.  There is no
//         subsequent c2s TLS alert record in this case; instead we
//         simply see the connection close.

// Any other situation terminates the state (stdin read disallowed).

uint64_t Executor::tls_predict_stdin_size (int fd, uint64_t maxLen) {

  const uint8_t TLS_ALERT = 21;
  const uint8_t TLS_APPDATA = 23;

  uint64_t stdin_len;
    
  if (fd != 0) {
    printf("tls_predict_stdin_size() called with unknown fd %d \n", fd);
    worker_exit();
    std::exit(EXIT_FAILURE);
  }

  //Kludge to allow us to verify against gmail.ktest files without a gmail.net.ktest file
  KTestObject * kto;

  kto = peekNextKTestObject();

  if (dropS2C) {
    if (strcmp (kto->name, "c2s") != 0 && (strcmp (kto->name,"s2c") != 0 || ( roundCount >=4 ) ) ) {
      printf("Advancing peek from record type %s in tls_predict_stdin_size \n", kto->name);
      int i = 0;
      while(true) {
	printf("Advancing for time %d \n", i);
	kto = &(ktov.objects[ktov.playback_index + i]);
	
	if ( roundCount < 4) {
	  if  (strcmp (kto->name, "c2s") == 0 || strcmp (kto->name,"s2c") == 0   ) 
	    break;
	} else {
	  if  (strcmp (kto->name, "c2s") == 0)
	    break;
	}
	i++;
      }
      fflush(stdout);
    }
  } else {

    //If it's not s2c or c2s, advance until the playback index matches one of those records.
    if (strcmp (kto->name, "c2s") != 0 && (strcmp (kto->name,"s2c") != 0 ) ) {
      printf("Advancing peek from record type %s in tls_predict_stdin_size \n", kto->name);
      int i = 0;
      while (true) {
	printf("Advancing for time %d \n", i);
	kto = &(ktov.objects[ktov.playback_index + i]);
	if  (strcmp (kto->name, "c2s") == 0 || strcmp (kto->name,"s2c") == 0   ) 
	  break;
	i++;
      }
      fflush(stdout);
    }
  }

     



  printf("predict_stdin_debug: kto->name is %s, kto->bytes[0] is 0x%02x, kto->numBytes is %d, name comp with c2s is %d \n", kto->name, kto->bytes[0], kto->numBytes, strncmp(kto->name, "c2s", 3));
  fflush(stdout);
  
  if (kto == NULL) { //Case 3

    printf("Warning: no c2s record found in peekNextKTestObject()\n");
    stdin_len = 0;

  } else if (strncmp(kto->name, "c2s", 3) == 0 &&
	     (uint8_t) kto->bytes[0] == TLS_ALERT &&
	     kto->numBytes == 31) { //Case 2
    printf("In TLS Alert case in predict stdin len \n");
    fflush(stdout);
    stdin_len = 0;
    
  } else if (strncmp(kto->name,"c2s", 3) == 0 &&
	     (uint8_t) kto->bytes[0] == TLS_APPDATA &&
	     kto->numBytes > 29) {//Case 1
    printf("In TLS Appdata case in predict stdin len \n");
    fflush(stdout);
    stdin_len = kto->numBytes - 29;
    
  } else {

    printf("Error in tls_predict_stdin_size \n");
    fflush(stdout);
    worker_exit();
    std::exit(EXIT_FAILURE);
    
  }
  if ( stdin_len > maxLen) {
    printf("ERROR: tls_predict_stdin_size returned value larger than maxLen \n");
    fflush(stdout);
    worker_exit();
    std::exit(EXIT_FAILURE);

  }else {
    return stdin_len;
  }
}


void Executor::model_ktest_raw_read_stdin() {
  ktest_raw_read_stdin_calls++;
  printf("Entering model_ktest_raw_read_stdin for time %d \n", ktest_raw_read_stdin_calls);
  fflush(stdout);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    if (enableMultipass) {

      int max = (int) target_ctx_gregs[GREG_RSI].u64;
      void * buf = (void *) target_ctx_gregs[GREG_RDI].u64;
      uint64_t len = tls_predict_stdin_size(0,max); 
      printf("stdin debug: predicted stdin len of %lu for stdin read %d \n", len, ktest_raw_read_stdin_calls);
      fflush(stdout);

      uint64_t res = len;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX].u64, resExpr);
      tase_make_symbolic( (uint64_t) buf, len, "stdin");
      
    } else {      
      //return result of call
      int res = ktest_raw_read_stdin_tase((void *) target_ctx_gregs[GREG_RDI].u64, (int) target_ctx_gregs[GREG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX].u64, resExpr);
    }
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in ktest_raw_read_stdin -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}

void Executor::model_ktest_connect() {
  ktest_connect_calls++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  printf("Calling model_ktest_connect \n");

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    //return result
    int res = ktest_connect_tase((int) target_ctx_gregs[GREG_RDI].u64, (struct sockaddr *) target_ctx_gregs[GREG_RSI].u64, (socklen_t) target_ctx_gregs[GREG_RDX].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_ktest_connect -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
      
  }
}


//https://linux.die.net/man/2/shutdown
//int shutdown(int sockfd, int how);
void Executor::model_shutdown() {
  printf("Entering model_shutdown at interpCtr %lu ", interpCtr);
  fflush(stdout);

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr))
	) {

    std::cerr << " Entered model_shutdown call on FD %lu \n ", target_ctx_gregs[GREG_RDI].u64;
    
    if (ktov.size == ktov.playback_index) {
      printf("SUCCESS: All messages verified \n");
      fflush(stdout);
      std::cerr << "All playback messages retrieved \n";
      printf("All playback messages retrieved \n");
      fflush(stdout);
      worker_exit();
      
      tase_exit();
      std::exit(EXIT_SUCCESS);
      
    } else {
      std::cerr << "ERROR: playback message index wrong at shutdown \n";
      printf("ERROR: playback message index wrong at shutdown \n");
      fflush(stdout);
      worker_exit();
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
  fflush(stdout);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(GREG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    printf("Before ktest_select, readfds is 0x%lx, writefds is 0x%lx \n", *( (uint64_t *) target_ctx_gregs[GREG_RSI].u64), *( (uint64_t *) target_ctx_gregs[GREG_RDX].u64));

    printf("DBG: Pushed RV for model_ktest_select is 0x%lx \n", *((uint64_t *) target_ctx_gregs[GREG_RSP].u64 ) );
    fflush(stdout);

    if (enableMultipass) {
      model_select();
    } else {
    
      int res = ktest_select_tase((int) target_ctx_gregs[GREG_RDI].u64, (fd_set *) target_ctx_gregs[GREG_RSI].u64, (fd_set *) target_ctx_gregs[GREG_RDX].u64, (fd_set *) target_ctx_gregs[GREG_RCX].u64, (struct timeval *) target_ctx_gregs[GREG_R8].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

      printf( "After ktest_select, readfds is 0x%lx, writefds is 0x%lx \n", *( (uint64_t *) target_ctx_gregs[GREG_RSI].u64), *( (uint64_t *) target_ctx_gregs[GREG_RDX].u64));
      fflush(stdout);
    
      
      //Fake a return
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
    }
  } else {
    printf("ERROR in model_ktest_select -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }    
}

//int RAND_poll() from openssl
void Executor::model_RAND_poll(){

  printf("Entering model_RAND_poll at interpCtr %lu \n", interpCtr);
  fflush(stdout);
  ref<ConstantExpr> resExpr = ConstantExpr::create(1, Expr::Int64);
  tase_helper_write( (uint64_t) &(target_ctx_gregs[GREG_RAX].u64), resExpr);
      
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}


//Just for debugging perf
/*
 */

//void * memmove (void * dst, void * src, size_t number)
void Executor::model_memmove() {
  static int model_memmove_calls = 0;
  model_memmove_calls++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) )
    {

      uint8_t * dest = (uint8_t *) target_ctx_gregs[GREG_RDI].u64;
      uint8_t * src = (uint8_t *) target_ctx_gregs[GREG_RSI].u64;
      size_t num = (size_t) target_ctx_gregs[GREG_RDX].u64;
      printf("Entered model_memmove at interpCtr %lu for dest 0x%lx and src 0x%lx \n", interpCtr, (uint64_t) dest, (uint64_t) src);
      fflush(stdout);
      
      ref<Expr> tmp [num];
      for (size_t i = 0; i < num; i++) {
	tmp[i] = tase_helper_read((uint64_t) (src + i), 1);
      }
      for (size_t i = 0; i < num; i++) {
	tase_helper_write( (uint64_t) (dest + i), tmp[i]);
      }

      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) dest, Expr::Int64);
      tase_helper_write( (uint64_t) &(target_ctx_gregs[GREG_RAX].u64), resExpr);
      
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
      
    } else  {
    printf("ERROR: Symbolic input provided to memmove \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}

// void * memset (void * buf, int val, size_t number)
void Executor::model_memset() {
  static int model_memset_calls = 0;
  model_memset_calls++;
  printf("Entering model_memset for time %d \n", model_memset_calls);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  
  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) )
    {
      void * buf = (void *) target_ctx_gregs[GREG_RDI].u64;
      uint8_t val = (uint8_t) target_ctx_gregs[GREG_RSI].u64;
      size_t num = (size_t) target_ctx_gregs[GREG_RDX].u64;
      ref<ConstantExpr> valExpr = ConstantExpr::create(val, Expr::Int8);
      uint8_t * bufPtr = (uint8_t *) buf;
      
      for (int i = 0; i < num ; i++) 
	tase_helper_write((uint64_t) (bufPtr + i), valExpr);
      
      //void * res = memset(buf,val,num);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) buf, Expr::Int64);
      tase_helper_write( (uint64_t) &(target_ctx_gregs[GREG_RAX].u64), resExpr);
      
      //Fake a return
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
    } else  {
    printf("ERROR: Symbolic ptr provided to memset \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
}




//Just for debugging
//void *memcpy(void *dest, const void *src, size_t n);
void Executor::model_memcpy() {
  printf("Entering model_memcpy: Moving 0x%lx bytes from 0x%lx to 0x%lx \n",(size_t) target_ctx_gregs[GREG_RDX].u64,   target_ctx_gregs[GREG_RSI].u64 ,  target_ctx_gregs[GREG_RDI].u64  );
  fflush(stdout);
  
  void * dst = (void *) target_ctx_gregs[GREG_RDI].u64;
  void * src = (void *) target_ctx_gregs[GREG_RSI].u64;
  size_t num = (size_t) target_ctx_gregs[GREG_RDX].u64;
    
  void * res;
  if (isBufferEntirelyConcrete((uint64_t) src, num) && isBufferEntirelyConcrete((uint64_t) dst, num)) 
    printf("Found entirely concrete buffer to buffer copy \n ");
  else {
    printf("Found some symbolic taint in mempcy buffers \n");
    if (!isBufferEntirelyConcrete((uint64_t) src, num) )
      printf("Src has symbolic taint \n");
    if (!isBufferEntirelyConcrete((uint64_t) dst, num) )
      printf("Dst has symbolic taint \n");
    
  }
    
  printf("Memcpy dbg -- printing source buffer as raw bytes \n");
  printBuf(stdout,src, num);

  printf("Memcpy dbg -- printing dest   buffer as raw bytes \n");
  printBuf(stdout,dst, num);
    
  std::cout.flush();
  
  
  std::cout.flush();
  for (int i = 0; i < num; i++) {
    ref<Expr> srcByte = tase_helper_read( ((uint64_t) src)+i, 1);
    tase_helper_write (((uint64_t)dst +i), srcByte);
  }
  res = dst;
  
    
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), resExpr);
  
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

void Executor::model_ktest_RAND_bytes() {
  ktest_RAND_bytes_calls++;
  printf("Calling model_ktest_RAND_bytes for time %d at interpCtr %lu \n", ktest_RAND_bytes_calls, interpCtr);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    char * buf = (char *) target_ctx_gregs[GREG_RDI].u64;
    int num    = (int) target_ctx_gregs[GREG_RSI].u64;

    if (enableMultipass) {
      tase_make_symbolic((uint64_t) buf, num, "rng");
       //Double check this
      printf("After call to tase_make_symbolic for rng, raw bytes are : \n");
      printBuf(stdout,buf, num);
      int res = num;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    } else {
      //return val
      int res = ktest_RAND_bytes_tase((unsigned char *) target_ctx_gregs[GREG_RDI].u64, (int) target_ctx_gregs[GREG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_ktest_RAND_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
    
}

void Executor::model_ktest_RAND_pseudo_bytes() {
  ktest_RAND_pseudo_bytes_calls++;
  printf("Calling model_ktest_RAND_PSEUDO_bytes for time %d at interp ctr %lu \n", ktest_RAND_pseudo_bytes_calls, interpCtr);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    char * buf = (char *) target_ctx_gregs[GREG_RDI].u64;
    int num   = (int) target_ctx_gregs[GREG_RSI].u64;
    
    //return result of call

    if (enableMultipass) {

      tase_make_symbolic((uint64_t) buf, num, "prng");

      printf("After call to tase_make_symbolic on prng, raw output in output buffer is : \n");
      printBuf(stdout,(void *) buf, num);
      
      //Double check this
      int res = num;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
      
    } else {
    
      int res = ktest_RAND_pseudo_bytes_tase((unsigned char *) target_ctx_gregs[GREG_RDI].u64, (int) target_ctx_gregs[GREG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);  
    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

  } else {
    printf("ERROR in model_test_RAND_pseudo_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
}

//https://linux.die.net/man/3/fileno
//int fileno(FILE *stream); 
void Executor::model_fileno() {

  printf("Entering model_fileno at %lu \n",interpCtr);
  fflush(stdout);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr)) 
       ){
    
    //return res of call
    int res = fileno((FILE *) target_ctx_gregs[GREG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

    printf("fileno model returned %d \n", res);
    fflush(stdout);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_test_RAND_pseudo_bytes \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE); 
  }
}

//http://man7.org/linux/man-pages/man2/fcntl.2.html
//int fcntl(int fd, int cmd, ... /* arg */ );
void Executor::model_fcntl() {
  printf("Entering model_fcntl at interpCtr %lu \n", interpCtr);
  fflush(stdout);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr)) 
       ){


    if ( (int) target_ctx_gregs[GREG_RSI].u64 == F_SETFL && (int) target_ctx_gregs[GREG_RDX].u64 == O_NONBLOCK) {
      printf("fcntl call to set fd as nonblocking \n");
      fflush(stdout);
    } else {
      printf("fcntrl called with unmodeled args \n");
      fflush(stdout);
    }

    int res = 0;
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

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
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    //return res of call
    int res = stat((char *) target_ctx_gregs[GREG_RDI].u64, (struct stat *) target_ctx_gregs[GREG_RSI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR in model_start -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}


//Just returns the current process's pid.  We can make this symbolic if we want later, or force a val
//that returns the same number regardless of worker forking.
void Executor::model_getpid() {

  int pid = getpid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) pid, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
  
  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//uid_t getuid(void)
//http://man7.org/linux/man-pages/man2/getuid.2.html
//Todo -- determine if we should fix result, see if uid_t is ever > 64 bits
void Executor::model_getuid() {

  printf("Calling model_getuid \n");
  uid_t uidResult = getuid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) uidResult, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
  
  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}

//uid_t geteuid(void)
//http://man7.org/linux/man-pages/man2/geteuid.2.html
//Todo -- determine if we should fix result prior to forking, see if uid_t is ever > 64 bits
void Executor::model_geteuid() {

  printf("Calling model_geteuid() \n");
  
  uid_t euidResult = geteuid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) euidResult, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//gid_t getgid(void)
//http://man7.org/linux/man-pages/man2/getgid.2.html
//Todo -- determine if we should fix result, see if gid_t is ever > 64 bits
void Executor::model_getgid() {

  printf("Calling model_getgid() \n");
  
  gid_t gidResult = getgid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) gidResult, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//gid_t getegid(void)
//http://man7.org/linux/man-pages/man2/getegid.2.html
//Todo -- determine if we should fix result, see if gid_t is ever > 64 bits
void Executor::model_getegid() {

  printf("Calling model_getegid() \n");
  
  gid_t egidResult = getegid();
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) egidResult, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//char * getenv(const char * name)
//http://man7.org/linux/man-pages/man3/getenv.3.html
//Todo: This should be generalized, and also technically should inspect the input string's bytes
void Executor::model_getenv() {

  printf("Entering model_getenv \n");
  std::cout.flush();
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) 
       ){

    char * res = getenv((char *) target_ctx_gregs[GREG_RDI].u64);

    printf("Called getenv on 0x%lx, returned 0x%lx \n", target_ctx_gregs[GREG_RDI].u64, (uint64_t) res);
    std::cout.flush();
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
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
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);

  if  (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){

    //Todo: Verify domain, type, protocol args.
    
    //Todo:  Generalize for better FD tracking
    int res = 3;
    printf("Setting socket FD to %d \n", res);
    
    ref<ConstantExpr> FDExpr = ConstantExpr::create(res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, FDExpr);

    //Fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    

  } else {
    printf("Found symbolic argument to model_socket \n");
    std::exit(EXIT_FAILURE);
  }
}


//This is here temporarily until we emit code without bswap assembly instructions
void Executor::model_htonl () {

  printf("Entering model_htonl at in interpCtr %lu \n", interpCtr);
  
  uint32_t res = htonl((uint32_t) target_ctx_gregs[GREG_RDI].u64);
  ref<ConstantExpr> FDExpr = ConstantExpr::create(res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, FDExpr);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
}




//Todo: Check input for symbolic args, or generalize to make not openssl-specific
void Executor::model_BIO_printf() {
  static int bio_printf_calls = 0;
  bio_printf_calls++;


  /*
  if (bio_printf_calls == 2) {
    fprintf(stderr, "Setting taseDebug to true \n");
    taseDebug = true;
    }*/
  
  
  printf("Entered bio_printf at interp Ctr %lu \n", interpCtr);
  fflush(stdout);
  
  char * errMsg = (char *) target_ctx_gregs[GREG_RSI].u64;
  printf("Entered bio_printf with message %s \n", errMsg);
  printf("Second arg as num is 0x%lx \n", target_ctx_gregs[GREG_RDX].u64);
  fflush(stdout);


  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

//Todo: Check input for symbolic args, or generalize to make not openssl-specific
void Executor::model_BIO_snprintf() {

  printf("Entered bio_snprintf at interp Ctr %lu \n", interpCtr);
  fflush(stdout);
  
  char * errMsg = (char *) target_ctx_gregs[GREG_RDX].u64;

  printf(" %s \n", errMsg);
  printf("First snprintf arg as int: %lu \n", target_ctx_gregs[GREG_RCX].u64);
  fflush(stdout);

  if (strcmp("error:%08lX:%s:%s:%s", errMsg) == 0) {

    printf( " %s \n", (char *) target_ctx_gregs[GREG_R8].u64);
    printf( " %s \n", (char *) target_ctx_gregs[GREG_R9].u64);
    fflush(stdout);
    
  }
  
  std::cerr << errMsg;
  
  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}


//time_t time(time_t *tloc);
// http://man7.org/linux/man-pages/man2/time.2.html
void Executor::model_time() {
  printf("Entering call to time at interpCtr %lu \n", interpCtr);
  fflush(stdout);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) ) {
    time_t res = time( (time_t *) target_ctx_gregs[GREG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);

    char * timeString = ctime(&res);
    printf("timeString is %s \n", timeString);
    printf("Size of timeVal is %lu \n", sizeof(time_t));
    fflush(stdout);
    
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("Found symbolic argument to model_time \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
}



//struct tm *gmtime(const time_t *timep);
//https://linux.die.net/man/3/gmtime
void Executor::model_gmtime() {
  printf("Entering call to gmtime at interpCtr %lu \n", interpCtr);
  fflush(stdout);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) ) {
    //Do call
    struct tm * res = gmtime( (time_t *) target_ctx_gregs[GREG_RDI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    char timeBuf[30];
    strftime(timeBuf, 30, "%Y-%m-%d %H:%M:%S", res);

    printf("gmtime result is %s \n", timeBuf);
    fflush(stdout);
    
    //If it doesn't exit, back returned struct with a memory object.
    ObjectPair OP;
    ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
      printf("model_gmtime result appears to have MO already backing it \n");
      fflush(stdout);
      
    } else {
      printf("Creating MO to back tm at 0x%lx with size 0x%lx \n", (uint64_t) res, sizeof(struct tm));
      fflush(stdout);
      MemoryObject * newMO = addExternalObject(*GlobalExecutionStatePtr, (void *) res, sizeof(struct tm), false);
      const ObjectState * newOSConst = GlobalExecutionStatePtr->addressSpace.findObject(newMO);
      ObjectState *newOS = GlobalExecutionStatePtr->addressSpace.getWriteable(newMO,newOSConst);
      newOS->concreteStore = (uint8_t *) res;
    }
    
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    //Do call
    int res = gettimeofday( (struct timeval *) target_ctx_gregs[GREG_RDI].u64, (struct timezone *) target_ctx_gregs[GREG_RSI].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

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
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    size_t nmemb = target_ctx_gregs[GREG_RDI].u64;
    size_t size  = target_ctx_gregs[GREG_RSI].u64;
    void * res = calloc(nmemb, size);

    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    size_t numBytes = size*nmemb;

    if (roundUpHeapAllocations)
      numBytes = roundUp(numBytes,8);
    
    printf("calloc at 0x%lx for 0x%lx bytes \n", (uint64_t) res, numBytes);
    std::cout.flush();
    //fprintf(heapMemLog, "CALLOC buf at 0x%lx - 0x%lx, size 0x%lx, interpCtr %lu \n", (uint64_t) res, ((uint64_t) res + numBytes -1), numBytes, interpCtr);
    //Make a memory object to represent the requested buffer
    MemoryObject * heapMem = addExternalObject(*GlobalExecutionStatePtr,res, numBytes , false );
    const ObjectState *heapOS = GlobalExecutionStatePtr->addressSpace.findObject(heapMem);
    ObjectState * heapOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(heapMem,heapOS);  
    heapOSWrite->concreteStore = (uint8_t *) res;

    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("Found symbolic argument to model_calloc \n");
    std::exit(EXIT_FAILURE);
  }
    
}




//void *realloc(void *ptr, size_t size);
//https://linux.die.net/man/3/realloc
//Todo: Set up additional memory objects if realloc adds extra space
void Executor::model_realloc() {
ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
 ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

 printf("Calling model_realloc \n");
 
 if  (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) ) {

   void * ptr = (void *) target_ctx_gregs[GREG_RDI].u64;
   size_t size = (size_t) target_ctx_gregs[GREG_RSI].u64;
   void * res = realloc(ptr,size);

   printf("Calling realloc on 0x%lx with size 0x%lx.  Ret val is 0x%lx \n", (uint64_t) ptr, (uint64_t) size, (uint64_t) res);
   if (roundUpHeapAllocations)
     size = roundUp(size, 8);
   
   ref<ConstantExpr> resultExpr = ConstantExpr::create( (uint64_t) res, Expr::Int64);
   target_ctx_gregs_OS->write(GREG_RAX * 8, resultExpr);


   std::cout.flush();
   //fprintf(heapMemLog, "REALLOC call on 0x%lx for size 0x%lx with return value 0x%lx. InterpCtr is %lu \n", (uint64_t) ptr, size, (uint64_t) res , interpCtr);

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
       printf("added MO for realloc at 0x%lx with size 0x%lx after orig location 0x%lx  \n", (uint64_t) res, size, (uint64_t) ptr);

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
	 printf("added MO for realloc at 0x%lx with size 0x%lx after orig size 0x%lx  \n", (uint64_t) res, size, origObjSize);
       }
     } else {
       printf("Error in realloc -- could not find original buffer info for ptr \n");
       std::cout.flush();
       std::exit(EXIT_FAILURE);
     }
   }
     
   //Fake a return
   uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
   target_ctx_gregs[GREG_RIP].u64 = retAddr;
   target_ctx_gregs[GREG_RSP].u64 += 8;
   
 } else {
    printf("Found symbolic argument to model_realloc \n");
    std::exit(EXIT_FAILURE);
 }
}



//http://man7.org/linux/man-pages/man3/malloc.3.html
void Executor::model_malloc() {
  static int times_model_malloc_called = 0;
  times_model_malloc_called++;
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);

  if (isa<ConstantExpr>(arg1Expr)) {
    size_t sizeArg = (size_t) target_ctx_gregs[GREG_RDI].u64;
    if (taseDebug)
      printf("Entered model_malloc for time %d with requested size %u \n",times_model_malloc_called, sizeArg);

    if (roundUpHeapAllocations) 
      sizeArg = roundUp(sizeArg, 8);
    /*
    if (sizeArg % 2 == 1) {
      printf("Found malloc request for odd num of bytes; adding 1 to requested size. \n");
      sizeArg++;
      }*/
    void * buf = malloc(sizeArg);
    if (taseDebug) {
      printf("Returned ptr at 0x%lx \n", (uint64_t) buf);
      std::cout.flush();
    }
    //fprintf(heapMemLog, "MALLOC buf at 0x%lx - 0x%lx, size 0x%x, interpCtr %lu \n", (uint64_t) buf, ((uint64_t) buf + sizeArg -1), sizeArg, interpCtr);
    //fflush(heapMemLog);
    //Make a memory object to represent the requested buffer
    MemoryObject * heapMem = addExternalObject(*GlobalExecutionStatePtr,buf, sizeArg, false );
    const ObjectState *heapOS = GlobalExecutionStatePtr->addressSpace.findObject(heapMem);
    ObjectState * heapOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(heapMem,heapOS);  
    heapOSWrite->concreteStore = (uint8_t *) buf;

    //Return pointer to malloc'd buffer in RAX
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) buf, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr); 

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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

extern bool skipFree;
void Executor::model_free() {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  if (isa<ConstantExpr>(arg1Expr)) {


    if (!skipFree) {
    
      void * freePtr = (void *) target_ctx_gregs[GREG_RDI].u64;
      printf("Calling model_free on addr 0x%lx \n", (uint64_t) freePtr);
      free(freePtr);

      //fprintf(heapMemLog, "FREE buf at 0x%lx, interpCtr is %lu \n", (uint64_t) freePtr, interpCtr);

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

    }
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    FILE * res = fopen( (char *) target_ctx_gregs[GREG_RDI].u64, (char *) target_ctx_gregs[GREG_RSI].u64);
    printf("Calling fopen on file %s \n", (char *) target_ctx_gregs[GREG_RDI].u64);
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr))
       ){

    FILE * res = fopen64( (char *) target_ctx_gregs[GREG_RDI].u64, (char *) target_ctx_gregs[GREG_RSI].u64);
    printf("Calling fopen64 on file %s \n", (char *) target_ctx_gregs[GREG_RDI].u64);
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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
  printf("Entering model_fclose at %lu \n", interpCtr);
  fflush(stdout);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){
    
    //We don't need to make any call
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) 
	) {
  
    size_t res = fread( (void *) target_ctx_gregs[GREG_RDI].u64, (size_t) target_ctx_gregs[GREG_RSI].u64, (size_t) target_ctx_gregs[GREG_RDX].u64, (FILE *) target_ctx_gregs[GREG_RCX].u64);
    
    //Return result
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

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
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  
}


//http://man7.org/linux/man-pages/man3/gethostbyname.3.html
//struct hostent *gethostbyname(const char *name);
//Todo -- check bytes of input for symbolic taint
void Executor::model_gethostbyname() {
  printf("Entering model_gethostbyname at interpCtr %lu \n", interpCtr);
  fflush(stdout);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){
    //Do the call
    printf("Calling model_gethostbyname on %s \n", (char *) target_ctx_gregs[GREG_RDI].u64);
    fflush(stdout);
    struct hostent * res = (struct hostent *) gethostbyname ((const char *) target_ctx_gregs[GREG_RDI].u64);

    //If it doesn't exit, back hostent struct with a memory object.
    ObjectPair OP;
    ref<ConstantExpr> addrExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    if (GlobalExecutionStatePtr->addressSpace.resolveOne(addrExpr, OP)) {
      printf("hostent result appears to have MO already backing it \n");
      fflush(stdout);
      
    } else {
      printf("Creating MO to back hostent at 0x%lx with size 0x%lx \n", (uint64_t) res, sizeof(hostent));
      fflush(stdout);
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
      
      
      printf("Mapping in buf at 0x%lx with size 0x%lx for h_addr_list", baseAddr, size);
      MemoryObject * listMO = addExternalObject(*GlobalExecutionStatePtr, (void *) baseAddr, size, false);
      const ObjectState * listOSConst = GlobalExecutionStatePtr->addressSpace.findObject(listMO);
      ObjectState * listOS = GlobalExecutionStatePtr->addressSpace.getWriteable(listMO, listOSConst);
      listOS->concreteStore = (uint8_t *) baseAddr;
      
    }

    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
    
    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
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
  fflush(stdout);
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(GREG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    int res = 0; //Pass success
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_setsockoptions -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE); 
  }
}

//No args for this one
void Executor::model___ctype_b_loc() {
  printf("Entering model__ctype_b_loc at interpCtr %lu \n", interpCtr);
  fflush(stdout);

  const unsigned short ** constRes = __ctype_b_loc();
  unsigned short ** res = const_cast<unsigned short **>(constRes);
  
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

  
}


//int32_t * * __ctype_tolower_loc(void);
//No args
//Todo -- allocate symbolic underlying results later for testing
void Executor::model___ctype_tolower_loc() {
  printf("Entering model__ctype_tolower_loc at interpCtr %lu \n", interpCtr);
  fflush(stdout);

  const int  ** constRes = __ctype_tolower_loc();
  int ** res = const_cast<int **>(constRes);
  
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  

}

//int fflush(FILE *stream);
//Todo -- Actually model this or provide a symbolic return status
void Executor::model_fflush(){

  printf("Entering model_fflush at %lu \n", interpCtr);
  fflush(stdout);
  
  std::cout.flush();
  //Get the input args per system V linux ABI.

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);

  if  (
       (isa<ConstantExpr>(arg1Expr))
       ){

    int res = 0;
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

  } else {
    printf("ERROR Found symbolic input to model_fflush \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }

}


//char *fgets(char *s, int size, FILE *stream);
//https://linux.die.net/man/3/fgets
void Executor::model_fgets() {
  printf("Entering model_fgets at %lu \n", interpCtr);
  fflush(stdout);
  
  std::cout.flush();
  //Get the input args per system V linux ABI.
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  
  if (
      (isa<ConstantExpr>(arg1Expr)) &&
      (isa<ConstantExpr>(arg2Expr)) &&
      (isa<ConstantExpr>(arg3Expr)) 
      ){
    //Do call
    char * res = fgets((char *) target_ctx_gregs[GREG_RDI].u64, (int) target_ctx_gregs[GREG_RSI].u64, (FILE *) target_ctx_gregs[GREG_RDX].u64);
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

    //Fake a return
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_fgets -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
    
  }
}

//Todo -- Inspect byte-by-byte for symbolic taint
void Executor::model_fwrite() {
  printf("Entering model_fwrite \n");

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  
  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr))
	) {
    
    size_t res = fwrite( (void *) target_ctx_gregs[GREG_RDI].u64, (size_t) target_ctx_gregs[GREG_RSI].u64, (size_t) target_ctx_gregs[GREG_RDX].u64, (FILE *) target_ctx_gregs[GREG_RCX].u64);
    
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
    target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;
  } else {
    printf("ERROR in model_fwrite -- symbolic args \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE); 
  } 
}

//http://man7.org/linux/man-pages/man2/signal.2.html
// We're modeling a client that receives no signals, so just
// bump RIP for now.
void Executor::model_signal() {
  printf("Entering model_signal \n");

  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
  target_ctx_gregs[GREG_RIP].u64 = retAddr;
  target_ctx_gregs[GREG_RSP].u64 += 8;

}

static void print_fd_set(int nfds, fd_set *fds) {
  int i;
  for (i = 0; i < nfds; i++) {
    printf(" %d", FD_ISSET(i, fds));
  }
  printf("\n");
}
/*
ref<Expr> Executor::make_bit_symbolic(uint8_t i) {
  
  ref<Expr> res = ConstantExpr::create


}
*/
#ifdef FD_ZERO
#undef FD_ZERO
#endif
#define FD_ZERO(p)        memset((char *)(p), 0, sizeof(*(p)))

//int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
//https://www.gnu.org/software/libc/manual/html_node/Waiting-for-I_002fO.html
//We examine fds 0 to ndfs-1.  Don't model the results of exceptfds, at least not yet.
//Todo: determine if we need to use kernel interface abi for this or any of the other i/o modeling functions
void Executor::model_select() {
  static int times_model_select_called = 0;
  times_model_select_called++;

  printf("Entering model_select for time %d \n", times_model_select_called);
  
  //Get the input args per system V linux ABI.
  int nfds = (int) target_ctx_gregs[GREG_RDI].u64; // int nfds
  fd_set * readfds = (fd_set *) target_ctx_gregs[GREG_RSI].u64; // fd_set * readfds
  fd_set * writefds = (fd_set *) target_ctx_gregs[GREG_RDX].u64; // fd_set * writefds
  //fd_set * exceptfds = (fd_set *) target_ctx_gregs[GREG_RCX].u64; // fd_set * exceptfds NOT USED
  //struct timeval * timeout = (struct timeval *) target_ctx_gregs[GREG_R8].u64;  // struct timeval * timeout  NOT USED
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(GREG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    
    printf("nfds is %d \n", nfds);
    printf("\n");
    printf("IN readfds  = ");
    print_fd_set(nfds, readfds);
    printf("IN writefds = ");
    print_fd_set(nfds, writefds);
    std::cout.flush();
    
    ref<Expr> orig_readfdExpr = tase_helper_read((uint64_t) &(readfds->fds_bits[0] ), 1) ;
    ref<Expr> orig_writefdExpr = tase_helper_read((uint64_t) &(writefds->fds_bits[0] ), 1);

    ref<Expr> all_bits_or = ConstantExpr::create(0, Expr::Int8);
    
    
    //READ
    if (times_model_select_called != 1) {
      //Per cliver, we don't want to simulate a client that somehow already has data to read in
      //from the socket on the first select.
      void * tmp1 = malloc(2);
      MemoryObject * tmpObjRead = addExternalObject( *GlobalExecutionStatePtr, (void *) tmp1, 2, false);
      const ObjectState * tmpObjReadOS = GlobalExecutionStatePtr->addressSpace.findObject(tmpObjRead);
      ObjectState * tmpObjReadOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(tmpObjRead,tmpObjReadOS);  
      tmpObjReadOSWrite->concreteStore = (uint8_t *) tmp1;
      
      tase_make_symbolic ((uint64_t) tmp1, 2, "select readfds mask");
      ref<Expr> rfdsMaskVar = tase_helper_read((uint64_t) tmp1, 1);
      ref<Expr> rfdsMaskExpr = AndExpr::create(rfdsMaskVar, orig_readfdExpr);
      tase_helper_write((uint64_t) &(readfds->fds_bits[0]), rfdsMaskExpr);

      all_bits_or = OrExpr::create(rfdsMaskExpr, all_bits_or);
      
    } else {
      tase_helper_write((uint64_t) &(readfds->fds_bits[0]), ConstantExpr::create(0, Expr::Int8));
    }


    //WRITE
    void * tmp2 = malloc(2);
    MemoryObject * tmpObjWrite = addExternalObject(*GlobalExecutionStatePtr, (void *) tmp2, 2, false);
    const ObjectState * tmpObjWriteOS = GlobalExecutionStatePtr->addressSpace.findObject(tmpObjWrite);
    ObjectState * tmpObjWriteOSWritable = GlobalExecutionStatePtr->addressSpace.getWriteable(tmpObjWrite,tmpObjWriteOS);
    tmpObjWriteOSWritable->concreteStore = (uint8_t *) tmp2;
    
    tase_make_symbolic((uint64_t) tmp2 , 2, "select writefds mask");
    ref<Expr> wfdsMaskVar = tase_helper_read((uint64_t) tmp2, 1);
    ref<Expr> wfdsMaskExpr = AndExpr::create(wfdsMaskVar, orig_writefdExpr);
    tase_helper_write((uint64_t) &(writefds->fds_bits[0]), wfdsMaskExpr);  

    all_bits_or = OrExpr::create(wfdsMaskExpr, all_bits_or);


    ref <ConstantExpr> Zero = ConstantExpr::create(0, Expr::Int8);
    ref <Expr> someFDPicked = NotExpr::create(EqExpr::create(all_bits_or, Zero));
    
    addConstraint(*GlobalExecutionStatePtr, someFDPicked);
    
    //ref<EqExpr> wfdsEqExpr = EqExpr::create(wfdsMaskExpr, 0);
    //ref<NotExpr> wfdsNotExpr = NotExpr::create(wfdsEqExpr);
    //addConstraint(*GlobalExecutionStatePtr, wfdsNotExpr );
    
    //RETURN VAL
    

    /*


    void * resTemp = malloc(8);
    MemoryObject * resMO = addExternalObject( *GlobalExecutionStatePtr, (void *) resTemp, 8, false);
    const ObjectState * resOS = GlobalExecutionStatePtr->addressSpace.findObject(resMO);
    ObjectState * resOSWritable = GlobalExecutionStatePtr->addressSpace.getWriteable(resMO,resOS);
    resOSWritable->concreteStore = (uint8_t *) resTemp;
    
    
    tase_make_symbolic((uint64_t) resTemp, 8, "select return val");
    ref<Expr> resExpr = tase_helper_read((uint64_t) (resTemp), 8);
    tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX].u64, resExpr);
    ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int64);
    ref<Expr> successExpr = SgtExpr::create (resExpr, zero);
    */
    
    tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX].u64, ConstantExpr::create(1, Expr::Int64)); 
    
    
    //addConstraint(*GlobalExecutionStatePtr, successExpr);

    printf("nfds is %d \n", nfds);
    printf("\n");
    printf("OUT readfds  = ");
    print_fd_set(nfds, readfds);
    printf("OUT writefds = ");
    print_fd_set(nfds, writefds);
    std::cout.flush();
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    return;
    
    
    bool debugSelect = false;
    if (debugSelect) {


      printf("nfds is %d \n", nfds);
      printf("\n");
      printf("IN readfds  = ");
      print_fd_set(nfds, readfds);
      printf("IN writefds = ");
      print_fd_set(nfds, writefds);
      std::cout.flush();

      if (times_model_select_called == 1) {
	printf("DEBUG: special casing select call 1 \n");
	FD_ZERO(readfds);
	FD_SET(0,writefds);
	
	printf("nfds is %d \n", nfds);
	printf("\n");
	printf("OUT readfds  = ");
	print_fd_set(nfds, readfds);
	printf("OUT writefds = ");
	print_fd_set(nfds, writefds);
	std::cout.flush();
	tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));

      }


      
      if (times_model_select_called == 2) {
	printf("DEBUG: special casing select call 2 \n");
	FD_ZERO(readfds);
	FD_SET(0,readfds);
	
	printf("nfds is %d \n", nfds);
	printf("\n");
	printf("OUT readfds  = ");
	print_fd_set(nfds, readfds);
	printf("OUT writefds = ");
	print_fd_set(nfds, writefds);
	std::cout.flush();
	tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));

      }

       if (times_model_select_called == 3) {
	printf("DEBUG: special casing select call 3 \n");	
	
	FD_ZERO(readfds);
	printf("nfds is %d \n", nfds);
	printf("\n");
	printf("OUT readfds  = ");
	print_fd_set(nfds, readfds);
	printf("OUT writefds = ");
	print_fd_set(nfds, writefds);
	std::cout.flush();
	tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));
       }

       if (times_model_select_called == 4) {
	 printf("DEBUG: special casing select call 4 \n");
	 
	 FD_ZERO(readfds);
	 FD_SET(0,readfds);
	 printf("nfds is %d \n", nfds);
	 printf("\n");
	 printf("OUT readfds  = ");
	 print_fd_set(nfds, readfds);
	 printf("OUT writefds = ");
	 print_fd_set(nfds, writefds);
	 std::cout.flush();
	 tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));

       }

       if (times_model_select_called == 5 ) {
	 printf("DEBUG: special casing select call 5 \n");
	 FD_ZERO(readfds);

	 printf("nfds is %d \n", nfds);
	 printf("\n");
	 printf("OUT readfds  = ");
	 print_fd_set(nfds, readfds);
	 printf("OUT writefds = ");
	 print_fd_set(nfds, writefds);
	 std::cout.flush();
	 tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));

       }

        if (times_model_select_called == 6 ) {
	 printf("DEBUG: special casing select call 6 \n");
	 FD_ZERO(readfds);
	 FD_SET(0,readfds);
	 printf("nfds is %d \n", nfds);
	 printf("\n");
	 printf("OUT readfds  = ");
	 print_fd_set(nfds, readfds);
	 printf("OUT writefds = ");
	 print_fd_set(nfds, writefds);
	 std::cout.flush();
	 tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), ConstantExpr::create(1, Expr::Int64));

       }
       
       
      	//fake a ret
	uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
	target_ctx_gregs[GREG_RIP].u64 = retAddr;
	target_ctx_gregs[GREG_RSP].u64 += 8;
	return;

	}
	/*
	
      printf("Forcing select return for debugging \n");
      //tase_helper_write((uint64_t) &(writefds->fds_bits[3]), ConstantExpr::create(1, Expr::Int8) );
      tase_make_symbolic((uint64_t) &(writefds->fds_bits[3]), 1, "WriteFDVal");
      ref<Expr> write_fd_val = tase_helper_read((uint64_t) &(writefds->fds_bits[3]), 1);
      ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
      ref <ConstantExpr> one = ConstantExpr::create(1, Expr::Int8);
      ref<Expr> equalsZeroExpr = EqExpr::create(write_fd_val,zero);
      ref<Expr> equalsOneExpr = EqExpr::create(write_fd_val, one);
      ref<Expr> equalsZeroOrOneExpr = OrExpr::create(equalsZeroExpr, equalsOneExpr);
      addConstraint(*GlobalExecutionStatePtr, equalsZeroOrOneExpr);

      tase_helper_write( (uint64_t) &(readfds->fds_bits[3]), ConstantExpr::create(0, Expr::Int8));


      tase_make_symbolic ( (uint64_t) &(target_ctx_gregs[GREG_RAX]), 8, "SelectReturnVal");
      ref<Expr> ret_val = tase_helper_read((uint64_t) &(target_ctx_gregs[GREG_RAX]), 8);
      ref<Expr> retEqualsZeroExpr = EqExpr::create(ret_val, zero);
      ref<Expr> retEqualsOneExpr  = EqExpr::create(ret_val, one);
      ref<Expr> retEqualsZeroOrOneExpr = OrExpr::create(retEqualsZeroExpr, retEqualsOneExpr);
      addConstraint(*GlobalExecutionStatePtr, retEqualsZeroOrOneExpr);
      
      //tase_helper_write( (uint64_t)  &(target_ctx_gregs[GREG_RAX]), ConstantExpr::create(1, Expr::Int64) );
      printf("nfds is %d \n", nfds);
      printf("\n");
      printf("OUT readfds  = ");
      print_fd_set(nfds, readfds);
      printf("OUT writefds = ");
      print_fd_set(nfds, writefds);
      std::cout.flush();

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;

      return;
    }
    
    //Go through each fd.  Really out to be able to handle a symbolic
    //indicator var for a given fds_bits entry, but not worried about that for now.
    for (uint64_t i = 0; i < nfds ; i++) {
      //if given fd is set in readfds, return sym val
      
      printf( "Read FD located at 0x%lx\n", (uint64_t) &(readfds->fds_bits[i]) );
      std::cout.flush();
      ref<Expr> isReadFDSetExpr = tase_helper_read((uint64_t) &(readfds->fds_bits[i]), Expr::Int8);
      //isReadFDSetExpr->dump();
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
      printf("Write fd located at 0x%lx \n", (uint64_t) &(writefds->fds_bits[i]) );
      std::cout.flush();
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

	tase_make_symbolic((uint64_t) &(readfds->fds_bits[j]), 1, "ReadFDVal");
        
	ref<Expr> fd_val = tase_helper_read((uint64_t) &(readfds->fds_bits[j]), 1);
	//Got to be a better way to write this... but we basically constrain the
	//sym var representing readfds[j] to be 0 or 1.
	//ref <Expr> readFDExpr = readFDResultBuf_OS->read(0,Expr::Int8);
	ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
	ref <ConstantExpr> one = ConstantExpr::create(1, Expr::Int8);
	ref<Expr> equalsZeroExpr = EqExpr::create(fd_val,zero);
	ref<Expr> equalsOneExpr = EqExpr::create(fd_val, one);
	ref<Expr> equalsZeroOrOneExpr = OrExpr::create(equalsZeroExpr, equalsOneExpr);
	addConstraint(*GlobalExecutionStatePtr, equalsZeroOrOneExpr);

	bitsSetExpr = AddExpr::create(bitsSetExpr, fd_val);
	
      } else {
	ref <ConstantExpr> zeroVal = ConstantExpr::create(0, Expr::Int8);
	tase_helper_write((uint64_t)&(readfds->fds_bits[j]), zeroVal);
      }

      if (make_writefds_symbolic[j]) {


	tase_make_symbolic((uint64_t) &(writefds->fds_bits[j]), 1, "WriteFDVal");

	ref<Expr> fd_val = tase_helper_read((uint64_t) &(writefds->fds_bits[j]), 1);

	//Got to be a better way to write this... but we basically constrain the
	//sym var representing writefds[j] to be 0 or 1.
        
	ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
	ref <ConstantExpr> one = ConstantExpr::create(1, Expr::Int8);
	ref<Expr> equalsZeroExpr = EqExpr::create(fd_val,zero);
	ref<Expr> equalsOneExpr = EqExpr::create(fd_val, one);
	ref<Expr> equalsZeroOrOneExpr = OrExpr::create(equalsZeroExpr, equalsOneExpr);
	addConstraint(*GlobalExecutionStatePtr, equalsZeroOrOneExpr);
        
	bitsSetExpr = AddExpr::create(bitsSetExpr, fd_val);
	
      } else {
	ref <ConstantExpr> zeroVal = ConstantExpr::create(0, Expr::Int8);
	tase_helper_write((uint64_t) &(writefds->fds_bits[j]), zeroVal);
      }
    }
    
    ref <ConstantExpr> zero = ConstantExpr::create(0, Expr::Int8);
    ref<Expr> successExpr = SgtExpr::create (bitsSetExpr, zero);
    //addConstraint(*GlobalExecutionStatePtr, successExpr);
    
    //For now, just model with a successful return.  But we could make this symbolic.
    printf("Attempting to write to RAX in model_select ... \n");
    if (isa<ConstantExpr> (bitsSetExpr))
      printf("RAX (bits set ) appears to be constant in model_select");
    std::cout.flush();

    tase_make_symbolic((uint64_t) &target_ctx_gregs[GREG_RAX], 8, "SelectReturn");

    
    //target_ctx_gregs_OS->write(GREG_RAX * 8, bitsSetExpr);
    //ref<ConstantExpr> offset = ConstantExpr::create(GREG_RAX * 8, Expr::Int16);  //Why 16?
    //target_ctx_gregs_OS->applyPsnOnWrite( offset , bitsSetExpr);
    
    std::cout.flush();
    //bump RIP and interpret next instruction
    //target_ctx_gregs[GREG_RIP].u64 = target_ctx_gregs[GREG_RIP].u64 +5;
    //printf("INTERPRETER: Exiting model_select \n");
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;

    */
    
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

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);

  if ( (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr)) ) { 

    int socket = (int) target_ctx_gregs[GREG_RDI].u64;
    struct sockaddr * addr = (struct sockaddr *) target_ctx_gregs[GREG_RSI].u64;
    socklen_t length = (socklen_t) target_ctx_gregs[GREG_RDX].u64;

    //Todo -- Generalize in case sockaddr struct isn't 14 bytes on all platfroms
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
      target_ctx_gregs_OS->write(GREG_RAX * 8, zeroResultExpr);
    } else {
      printf("ERROR: Unhandled model_connect failure-- unknown socket fd \n");
      std::exit(EXIT_FAILURE);
    }

    //bump RIP and interpret next instruction
    target_ctx_gregs[GREG_RIP].u64 = target_ctx_gregs[GREG_RIP].u64 +5;   
    
  } else {
     printf("ERROR: Found symbolic input to model_connect()");
     std::exit(EXIT_FAILURE);
  }
}

//-----------------------------CRYPTO SECTION--------------------


extern bool forceNativeRet;

// int tls1_generate_master_secret( SSL *s, unsigned char *out,
//             unsigned char *p, int len)
void Executor::model_tls1_generate_master_secret() {
ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); // SSL *s
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); // unsigned char * out
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64); // unsigned char * p
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64); // int len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) ) {

    printf("Entering model_tls1_generate_master_secret at interpctr %lu \n", interpCtr);
    fflush(stdout);
    if (enableMultipass == false) {
      printf("Will trap in ktest_master_secret further down for master secret \n");
      fflush(stdout);
      forceNativeRet = true;
      target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      return;
    }

    void * buf = (void *) target_ctx_gregs[GREG_RSI].u64;

    FILE * theFile = fopen("ssl.mastersecret", "rb");
    unsigned char tmp [48];
    fread(tmp, 1 , 48, theFile);
    printf("Printing    results of attempt to load master secret as binary... \n");
    for (int i = 0; i < 48; i++) {
      printf(" %2.2x", tmp[i]);
    }
    printf("\n");
        
    printf("PRINTING MASTER SECRET as hex \n");
    uint8_t * base = (uint8_t *) tmp;
    for (int i = 0; i < 48; i++)
      printf("%02x", *(base + i));
    printf("\n------------\n");
    
    memcpy (buf, tmp, 48); //Todo - use tase_helper read/write

    ref<ConstantExpr> res = ConstantExpr::create(SSL3_MASTER_SECRET_SIZE, Expr::Int64);
    tase_helper_write((uint64_t) &(target_ctx_gregs[GREG_RAX].u64), res);
    
    //fake a ret
    uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
    target_ctx_gregs[GREG_RIP].u64 = retAddr;
    target_ctx_gregs[GREG_RSP].u64 += 8;
    
  } else {
    printf("ERROR: symbolic arg passed to tls1_generate_master_secret \n");
    std::exit(EXIT_FAILURE);
  }
}

//Just here for debugging perf
void Executor::model_sha256_block_data_order() {
  printf("Entering model_SHA256_block_data_order \n");
  fflush(stdout);
  forceNativeRet = true;
  target_ctx_gregs[GREG_RIP].u64 += native_ret_off;

}

//Just here for debugging perf
void Executor::model_sha1_block_data_order() {
  printf("Entering model_SHA1_block_data_order \n");
  fflush(stdout);
  forceNativeRet = true;
  target_ctx_gregs[GREG_RIP].u64 += native_ret_off;

}

//Used to restore concrete values for buffers that are
//entirely made up of constant expressions
void Executor::rewriteConstants(uint64_t base, size_t size) {
  if (modelDebug) {
    printf("Rewriting constant array \n");
    fflush(stdout);
  }

  if (!(
	base > ((uint64_t) rodata_base_ptr)
	 &&
	base < (((uint64_t) rodata_base_ptr) + rodata_size)
	)
      ) {
    if (modelDebug) {
      printf("Base does not appear to be in rodata \n");
      fflush(stdout);
    }
  } else {
    if (modelDebug) {
      printf("Found base in rodata.  Returning from rewriteConstants without doing anything \n");
      fflush(stdout);
    }
    return;
  }
  
  for (size_t i = 0; i < size; i++) {

    //We're assuming
    //1. Every byte's 2-byte aligned buffer containing it has been mapped with a MO/OS at some point.
    //2. It's OK to duplicate some of these read/write ops
    uint64_t writeAddr;
    if( (base + i) %2 != 0)
      writeAddr = base + i -1;
    else
      writeAddr = base + i;
    
    ref<Expr> val = tase_helper_read(writeAddr, 2);
    tase_helper_write(writeAddr, val);

  }
  if (modelDebug) {
    printf("End result: \n");
    fflush(stdout);
    printBuf(stdout,(void *) base, size);
  }
}
  

//Model for int SHA1_Update(SHA_CTX *c, const void *data, size_t len);
//defined in crypto/sha/sha.h.
//Updated 04/30/2019
void Executor::model_SHA1_Update () {
  SHA1_Update_calls++;

  if (modelDebug) {
    printf("Calling model_SHA1_Update for time %d \n", SHA1_Update_calls);
  }
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //SHA_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    if (modelDebug) {
      printf("Entered model_SHA1Update for time %d \n", SHA1_Update_calls );
    }
    //Determine if SHA_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[GREG_RDI].u64;
    const void * data = (const void *) target_ctx_gregs[GREG_RSI].u64;
    size_t len = (size_t) target_ctx_gregs[GREG_RDX].u64;
    if (modelDebug) {
      printf("SHA1_Update_CTX is \n");
      printBuf(stdout,(void *) c, sizeof(SHA_CTX));
      printf("SHA1 data buf is \n");
      printBuf(stdout,(void *) data, len);
    }
    bool hasSymbolicInput = false;

    if (!isBufferEntirelyConcrete((uint64_t) c, sizeof(SHA_CTX)) || !isBufferEntirelyConcrete((uint64_t) data, len))
      hasSymbolicInput = true;

    if (hasSymbolicInput) {
      std::string nameString = "SHA1_Update_Output" + std::to_string(SHA1_Update_calls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      printf("MULTIPASS DEBUG: Found symbolic input to SHA1_Update \n");
      tase_make_symbolic((uint64_t) c, 20, name);


      //Can optionally return failure here if desired
      int res = 1; //Force success
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;

      
    } else { //Call natively

      //Deal with cases where buffer is entirely constant exprs
      rewriteConstants((uint64_t) c, sizeof(SHA_CTX));
      rewriteConstants((uint64_t) data, len);
      
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Did not find symbolic input to SHA1_Update \n");
      }
       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	forceNativeRet = true;
	target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      } else {
	 dont_model = true;
      }
      return;
      


      //Todo: provide SHA1_Update implementation for fast native execution     
    }
  } else {
    printf("ERROR: symbolic arg passed to model_SHA1_Update \n");
    std::exit(EXIT_FAILURE);
  }
}



//Model for int SHA1_Final(unsigned char *md, SHA_CTX *c)
//defined in crypto/sha/sha.h
void Executor::model_SHA1_Final() {
  SHA1_Final_calls++;
  printf("Calling model_SHA1_Final for time %d \n", SHA1_Final_calls);

  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //SHA_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[GREG_RDI].u64;
     SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[GREG_RSI].u64;
     bool hasSymbolicInput = false;

     if (modelDebug) {
       printf("SHA1_Final ctx is \n");
       printBuf(stdout,(void *) c, sizeof(SHA_CTX));
       printf("SHA1_Final md buf is \n");
       printBuf(stdout,(void *) md, SHA_DIGEST_LENGTH);
     }
     if (!isBufferEntirelyConcrete((uint64_t) c, 20) )
       hasSymbolicInput = true;
     
   
     if (hasSymbolicInput) {
       std::string nameString = "SHA1_Final_Output" + std::to_string(SHA1_Final_calls);
       const char * constCopy = nameString.c_str();
       char name [40];//Arbitrary number
       strncpy(name, constCopy, 40);
      
       printf("MULTIPASS DEBUG: Found symbolic input to SHA1_Final \n");

       tase_make_symbolic( (uint64_t) md, SHA_DIGEST_LENGTH, name);

       //Can optionally return failure here if desired
       int res = 1; //Force success
       ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
       target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
       
       //fake a ret
       uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
       target_ctx_gregs[GREG_RIP].u64 = retAddr;
       target_ctx_gregs[GREG_RSP].u64 += 8;
       
     } else {
       if (modelDebug) {
	 printf("MULTIPASS DEBUG: Did not find symbolic input to SHA1_Final \n");
       }
       //Deal with cases where buffer is entirely constant exprs
       rewriteConstants((uint64_t) c, sizeof(SHA_CTX));
       

       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	 forceNativeRet = true;
	 target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
       } else {
	 dont_model = true;
       }
       return;
       
       //Todo: Provide sha1_final native implementation for concrete execution
     }    
   } else {
     printf("ERROR: symbolic arg passed to model_SHA1_Final \n");
     std::exit(EXIT_FAILURE);
   }
}



//Model for int SHA256_Update(SHA256_CTX *c, const void *data, size_t len)
//defined in crypto/sha/sha.h.
//Updated 04/30/2019
void Executor::model_SHA256_Update () {
  SHA256_Update_calls++;
  printf("Calling model_SHA256_Update for time %d \n", SHA256_Update_calls);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //SHA256_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    //Determine if SHA256_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[GREG_RDI].u64;
    const void * data = (const void *) target_ctx_gregs[GREG_RSI].u64;
    size_t len = (size_t) target_ctx_gregs[GREG_RDX].u64;

    if (modelDebug) {
      printf("SHA256_Update_CTX is \n");
      printBuf(stdout,(void *) c, sizeof(SHA256_CTX));
      printf("SHA256 data buf is \n");
      printBuf(stdout,(void *) data, len);
    }
    
    bool hasSymbolicInput = false;
    if (!isBufferEntirelyConcrete((uint64_t ) c, sizeof(SHA256_CTX)) || !isBufferEntirelyConcrete( (uint64_t ) data, len))
      hasSymbolicInput = true;
    

    if (hasSymbolicInput) {

      printf("MULTIPASS DEBUG: Found symbolic input to SHA256_Update \n");
      std::string nameString = "SHA256_Update_Output" + std::to_string(SHA256_Update_calls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic( (uint64_t) c, 32, name);

      //Can optionally return failure here if desired
      int res = 1; //Force success
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else { //Call natively
      printf("MULTIPASS DEBUG: Did not find symbolic input to SHA256_Update \n");

      //Deal with cases where buffer is entirely constant exprs
      rewriteConstants((uint64_t) c, sizeof(SHA256_CTX));
      rewriteConstants((uint64_t) data, len);
      

       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	forceNativeRet = true;
	target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      } else {
	 dont_model = true;
      }
      return;

      
    }

  } else {
    printf("ERROR: symbolic arg passed to model_SHA256_Update \n");
    std::exit(EXIT_FAILURE);
  }
}

//Model for int SHA256_Final(unsigned char *md, SHA256_CTX *c)
//defined in crypto/sha/sha.h
void Executor::model_SHA256_Final() {
  SHA256_Final_calls++;
  printf("Calling model_SHA256_Final for time %d \n", SHA256_Final_calls);
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //SHA256_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[GREG_RDI].u64;
     SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[GREG_RSI].u64;

     if (modelDebug) {
       printf("SHA256_Final ctx is \n");
       printBuf(stdout,(void *) c, sizeof(SHA256_CTX));
       printf("SHA256_Final md buf is \n");
       printBuf(stdout,(void *) md, SHA_DIGEST_LENGTH);
     }
     bool hasSymbolicInput = false;

     if (!isBufferEntirelyConcrete((uint64_t) c, 32) )
       hasSymbolicInput = true;
     
     if (hasSymbolicInput) {
       std::string nameString = "SHA256_Final_Output" + std::to_string(SHA256_Final_calls);
       const char * constCopy = nameString.c_str();
       char name [40];//Arbitrary number
       strncpy(name, constCopy, 40);
      
       printf("MULTIPASS DEBUG: Found symbolic input to SHA256_Final \n");
       fflush(stdout);
       
       tase_make_symbolic((uint64_t) md, SHA_DIGEST_LENGTH, name);

       //Can optionally return failure here if desired
       int res = 1; //Force success
       ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
       target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
       
       //fake a ret
       uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
       target_ctx_gregs[GREG_RIP].u64 = retAddr;
       target_ctx_gregs[GREG_RSP].u64 += 8;
 
     } else {
       printf("MULTIPASS DEBUG: Did not find symbolic input to SHA256_Final \n");

       //Deal with cases where buffer is entirely constant exprs
       rewriteConstants((uint64_t) c, sizeof(SHA256_CTX));
       

       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	 forceNativeRet = true;
	 target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
       } else {
	 dont_model = true;
       }
       return;

      
     }
     
   } else {
     printf("ERROR: symbolic arg passed to model_SHA256_Final \n");
    std::exit(EXIT_FAILURE);
   }
}



//model for void AES_encrypt(const unsigned char *in, unsigned char *out,
//const AES_KEY *key);
//Updated 04/30/2019
void Executor::model_AES_encrypt () {
  AES_encrypt_calls++;

  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //const unsigned char *in
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //unsigned char * out
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64); //const AES_KEY * key

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    const unsigned char * in =  (const unsigned char *)target_ctx_gregs[GREG_RDI].u64;
    unsigned char * out = (unsigned char *) target_ctx_gregs[GREG_RSI].u64;
    const AES_KEY * key = (const AES_KEY *) target_ctx_gregs[GREG_RDX].u64;

    int AESBlockSize = 16; //Number of bytes in AES block    
    //printf("AES_encrypt %d debug -- dumping buffer inputs at round %d pass %d \n", AES_encrypt_calls, roundCount, passCount );
    //printf("key is \n");
    //printBuf(stdout,(void *) key, AESBlockSize);
    //rewriteConstants( (uint64_t) key, AESBlockSize);
    //printf("in is \n");
    //printBuf(stdout,(void *) in, AESBlockSize);
    //rewriteConstants( (uint64_t) in, AESBlockSize);
    //fflush(stdout);
    
    
   
    bool hasSymbolicDependency = false;
    
    //Check to see if any input bytes or the key are symbolic
    //Todo: Chase down any structs that AES_KEY points to if it's not a simple struct.
    //It's OK; struct holds no pointers.
    if (!isBufferEntirelyConcrete((uint64_t) in, AESBlockSize) || !isBufferEntirelyConcrete ((uint64_t) key, AESBlockSize) )
      hasSymbolicDependency = true;
    
    if (hasSymbolicDependency) {

      if (modelDebug) {
	printf("MULTIPASS DEBUG: Found symbolic input to AES_encrypt \n");
	fflush(stdout);
      }
      std::string nameString = "aes_Encrypt_output " + std::to_string(AES_encrypt_calls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic((uint64_t) out, AESBlockSize, name);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively, assuming no taint in registers
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Did not find symbolic input to AES_encrypt \n");
	fflush(stdout);
      }
      if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	forceNativeRet = true;
	target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      } else {
	dont_model = true;
      }
      return;

      //AES_Encrypt(in,out,key);  //Todo -- get native call for AES_Encrypt
    }
    
  } else {
    printf("ERROR: symbolic arg passed to model_AES_encrypt \n");
    std::exit(EXIT_FAILURE);
  } 
} 

//Model for
//void gcm_gmult_4bit(u64 Xi[2], const u128 Htable[16])
// in crypto/modes/gcm128.c
void Executor::model_gcm_gmult_4bit () {
  gcm_gmult_4bit_calls++;

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); // const u128 Htable[16]
  
  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr))
	) {
    
    u64 * XiPtr = (u64 *) target_ctx_gregs[GREG_RDI].u64;
    u128 * HtablePtr = (u128 *) target_ctx_gregs[GREG_RSI].u64;
    
    if (modelDebug) {
      printf("Entering model_gcm_gmult_4bit for time %d and dumping raw input as bytes \n", gcm_gmult_4bit_calls);
    }
    
    //printf("Xi inputs are \n");
    //printBuf(stdout,(void *) XiPtr, 16);
    //printf("Htable inputs are \n");
    //printBuf(stdout,(void *) HtablePtr, 196);

    
    //Todo: Double check the dubious ptr cast and figure out if we
    //are assuming any structs are packed
    bool hasSymbolicInput = false;

    if (!isBufferEntirelyConcrete((uint64_t) XiPtr, 16)) {
	hasSymbolicInput = true;
    }
    
    if (hasSymbolicInput) {
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Found symbolic input to gcm_gmult \n");
	fflush(stdout);
      }
      std::string nameString = "GCM_GMULT_output " + std::to_string(gcm_gmult_4bit_calls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic((uint64_t) XiPtr, 128, name);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Did not find symbolic input to gcm_gmult \n");
	fflush(stdout);
      }
       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	 forceNativeRet = true;
	 target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
       } else {
	 dont_model = true;
       }
       return;
      
    }
  } else {
    printf("ERROR: symbolic arg passed to model_gcm_gmult_4bit \n");
    std::exit(EXIT_FAILURE);
  }
}


//Model for 
//void gcm_ghash_4bit(u64 Xi[2],const u128 Htable[16],
//				const u8 *inp,size_t len)
//in crypto/modes/gcm128.c
//Todo: Check to see if we're incorrectly assuming that the Xi and Htable arrays are passed as ptrs in the abi.
void Executor::model_gcm_ghash_4bit () {
  gcm_ghash_4bit_calls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64); //const u128 Htable[16]
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64); // const u8 *inp
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) ) {

    u64 * XiPtr = (u64 *) target_ctx_gregs[GREG_RDI].u64;
    u128 * HtablePtr = (u128 *) target_ctx_gregs[GREG_RSI].u64;
    const u8 * inp = (const u8 *) target_ctx_gregs[GREG_RDX].u64;
    size_t len = (size_t) target_ctx_gregs[GREG_RCX].u64;
    //printf("Entering model_gcm_ghash_4bit for time %d and dumping args as raw bytes \n", gcm_ghash_4bit_calls);

    //printf("Xi inputs are \n");
    //printBuf(stdout,(void *) XiPtr, 16);
    //printf("Htable inputs are \n");
    //printBuf(stdout,(void *) HtablePtr, 196);
    //printf("inp is \n");
    //printBuf(stdout,(void *) inp, len);
    //printf("len is %lu \n", len);
    //std::cout.flush();
    
    //Todo: Double check the dubious ptr casts and figure out if we
    //are falsely assuming any structs or arrays are packed
    bool hasSymbolicInput = false;
    // Todo: Double check  if this is OK for different size_t values.
    if (!isBufferEntirelyConcrete((uint64_t) XiPtr, 16) || !isBufferEntirelyConcrete((uint64_t) inp, len) ) 
      hasSymbolicInput = true;
    
    if (hasSymbolicInput) {
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Found symbolic input to gcm_ghash \n");
	fflush(stdout);
      }
      std::string nameString = "GCM_GHASH_output " + std::to_string(gcm_ghash_4bit_calls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic ((uint64_t) XiPtr, sizeof(u64) * 2, name);
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively
      if (modelDebug) {
	printf("MULTIPASS DEBUG: Did not find symbolic input to gcm_ghash \n");
	fflush(stdout);
      }
       if (gprsAreConcrete() && !(exec_mode == INTERP_ONLY)) {
	forceNativeRet = true;
	target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      } else {
	 dont_model = true;
      }
       return;

    }
     
  } else {
    printf("ERROR: symbolic arg passed to model_gcm_ghash_4bit \n");
    std::exit(EXIT_FAILURE);
  }  
}

BIGNUM * Executor::BN_new_tase() {
  
  BIGNUM * result = (BIGNUM *) malloc(sizeof(BIGNUM));

  MemoryObject * BNMem = addExternalObject(*GlobalExecutionStatePtr,(void *) result, sizeof(BIGNUM), false );
  const ObjectState * BNOS = GlobalExecutionStatePtr->addressSpace.findObject(BNMem);
  ObjectState * BNOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(BNMem,BNOS);  
  BNOSWrite->concreteStore = (uint8_t *) result;
  
  result->flags=BN_FLG_MALLOCED;
  result->top=0;
  result->neg=0;
  result->dmax=0;
  result->d=NULL;

  return result;
}

EC_POINT * Executor::EC_POINT_new_tase(EC_GROUP * group) {

  EC_POINT * result = (EC_POINT *) malloc(sizeof(EC_POINT));

  MemoryObject * ECPMem = addExternalObject(*GlobalExecutionStatePtr,(void *) result, sizeof(EC_POINT), false );
  const ObjectState * ECPOS = GlobalExecutionStatePtr->addressSpace.findObject(ECPMem);
  ObjectState * ECPOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(ECPMem,ECPOS);  
  ECPOSWrite->concreteStore = (uint8_t *) result;

  //Set the group method
  result->meth = group->meth;

  //Init X
  BIGNUM * X = &(result->X);
  X->flags=BN_FLG_MALLOCED;
  X->top=0;
  X->neg=0;
  X->dmax=0;
  X->d=NULL;

  //Init Y
  BIGNUM * Y = &(result->Y);
  Y->flags=BN_FLG_MALLOCED;
  Y->top=0;
  Y->neg=0;
  Y->dmax=0;
  Y->d=NULL;

  //Init Z
  BIGNUM * Z = &(result->Z);
  Z->flags=BN_FLG_MALLOCED;
  Z->top=0;
  Z->neg=0;
  Z->dmax=0;
  Z->d=NULL;

  //Init  Z_is_one
  //Todo -- Should we make it symbolic?
  result->Z_is_one = 0;

  
  return result;
  
}

#define SYMBOLIC_BN_DMAX 64
void Executor::make_BN_symbolic(BIGNUM * bn, const char * symbol_name) {
  printf("Calling make_BN_symbolic at rip 0x%lx on addr 0x%lx \n", target_ctx_gregs[GREG_RIP].u64, (uint64_t) bn);
  fflush(stdout);
  if (bn->dmax > 0) {
    tase_make_symbolic((uint64_t) bn->d,  (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
  } else {
    bn->dmax = SYMBOLIC_BN_DMAX;
    void *buf = malloc((bn->dmax)*sizeof(bn->d[0]));
    
    MemoryObject * bufMem = addExternalObject(*GlobalExecutionStatePtr,(void *) buf,(bn->dmax)*sizeof(bn->d[0])  , false );
    const ObjectState * bufOS = GlobalExecutionStatePtr->addressSpace.findObject(bufMem);
    ObjectState * bufOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(bufMem,bufOS);  
    bufOSWrite->concreteStore = (uint8_t *) buf;
    
    tase_make_symbolic((uint64_t) buf, (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
    bn->d = (long unsigned int *) buf;
  }
  
  if (symbol_name == NULL) {
    symbol_name = "BN";
  }
  tase_make_symbolic((uint64_t) &(bn->neg), sizeof(int), symbol_name);
}

void Executor::make_EC_POINT_symbolic(EC_POINT* p) {
  make_BN_symbolic(&(p->X), "ECpointX");
  make_BN_symbolic(&(p->Y), "ECpointY");
  make_BN_symbolic(&(p->Z), "ECpointZ");
}


//Model for int EC_KEY_generate_key(EC_KEY *key)
// from crypto/ec/ec.h

//This is a little different because we have to reach into the struct
//and make its fields symbolic.
//Point of this function is to produce ephemeral key pair for Elliptic curve diffie hellman
//key exchange and eventually premaster secret generation during the handshake.

//EC_KEY struct has a private key k, which is a number between 1 and the size of the Elliptic curve subgroup
//generated by base point G.
//Public key is kG for the base point G.
//So k is an integer, and kG is a point (three coordinates in jacobian projection or two in affine projection)
//on the curve produced by "adding" G to itself k times.

//This model always produces symbolic output, regardless of the input
void Executor::model_EC_KEY_generate_key () {

  EC_KEY_generate_key_calls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64); //EC_KEY * key
  if ( (isa<ConstantExpr>(arg1Expr)) ) {
    
    EC_KEY * eckey = (EC_KEY *) target_ctx_gregs[GREG_RDI].u64;
     
    printf("Entering model_EC_KEY_generate_key for time %d \n", EC_KEY_generate_key_calls );
    if (enableMultipass) {
      printf("MULTIPASS DEBUG: Calling EC_KEY_generate_key with symbolic return \n"); 
      fflush(stdout);
      if (eckey->priv_key == NULL)
	eckey->priv_key = BN_new_tase();
      
      if (eckey->pub_key == NULL)
	eckey->pub_key = EC_POINT_new_tase(eckey->group);
      
      //Make private key (bignum) symbolic
      make_BN_symbolic(eckey->priv_key, "ECKEYprivate");
      
      //Make pub key (EC point) symbolic
      make_EC_POINT_symbolic(eckey->pub_key);
      
      //Fake a ret;
      //Can optionally return failure here if desired
      int res = 1; //Force success
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX], resExpr);
      
      //target_ctx_gregs_OS->write(GREG_RAX * 8, resExpr);
      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively
      printf("DEBUG: Calling EC_KEY_generate_key natively \n");
      fflush(stdout);
      forceNativeRet = true;
      target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      return; 
    } 
    
  } else {
    printf("ERROR: symbolic arg passed to model_EC_KEY_generate_key \n");
    std::exit(EXIT_FAILURE);
  }
}


// struct bignum_st
//         {
//         BN_ULONG *d;    /* Pointer to an array of 'BN_BITS2' bit chunks. */
//         int top;        /* Index of last used d +1. */
//         /* The next are internal book keeping for bn_expand. */
//         int dmax;       /* Size of the d array. */
//         int neg;        /* one if the number is negative */
//         int flags;
//         };


//Todo -- Properly make sure we're not assuming any pointers are concrete

bool Executor::is_symbolic_BIGNUM(BIGNUM * bn) {
  bool rv = false;
  
  if (!isBufferEntirelyConcrete((uint64_t) bn, sizeof(BIGNUM)) || !isBufferEntirelyConcrete((uint64_t) bn->d, bn->dmax))
    rv = true;
  else
    rv = false;

  printf("is_symbolic_BIGNUM returned %d at rip 0x%lx \n", rv, target_ctx_gregs[GREG_RIP].u64);
  fflush(stdout);

  return rv;
}


// struct ec_point_st {
//         const EC_METHOD *meth;

//         /* All members except 'meth' are handled by the method functions,                                                                
//          * even if they appear generic */

//         BIGNUM X;
//         BIGNUM Y;
//         BIGNUM Z; /* Jacobian projective coordinates:                                                                                    
//                    * (X, Y, Z)  represents  (X/Z^2, Y/Z^3)  if  Z != 0 */
//         int Z_is_one; /* enable optimized point arithmetics for special case */
// } /* EC_POINT */;


bool Executor::is_symbolic_EC_POINT(EC_POINT * pt) {

  bool rv = false;

  //Check entire struct first.  This includes pointers to the three BN coordinates
  if (!isBufferEntirelyConcrete((uint64_t) pt, sizeof(EC_POINT))) {
      rv = true;
      printf( "WARNING: is_symbolic_EC_POINT found symbolic data at rip 0x%lx \n",  target_ctx_gregs[GREG_RIP].u64);
      fflush(stdout);
      return rv;
  }
  
  /*
  if (is_symbolic_BIGNUM(&(pt->X)) || is_symbolic_BIGNUM(&(pt->Y)) || is_symbolic_BIGNUM(&(pt->Z)))
    rv = true;
  else
    rv = false;  
  */

  printf("is_symbolic_EC_POINT returned %d at rip 0x%lx \n", rv, target_ctx_gregs[GREG_RIP].u64);
  fflush(stdout);
  return rv;
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
  ECDH_compute_key_calls++;

  //static int model_ECDH_compute_key_calls = 0;
  //model_ECDH_compute_key_calls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(GREG_R8 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr))
	) {

    void * out = (void *) target_ctx_gregs[GREG_RDI].u64;
    size_t outlen = (size_t) target_ctx_gregs[GREG_RSI].u64;
    EC_POINT * pub_key = (EC_POINT *) target_ctx_gregs[GREG_RDX].u64;
    EC_KEY * eckey = (EC_KEY *) target_ctx_gregs[GREG_RCX].u64;
    
    bool hasSymbolicInputs = false;

    printf("Entering model_ECDH_compute_key for time %d \n", ECDH_compute_key_calls);
    
    if (is_symbolic_EC_POINT(pub_key) || is_symbolic_EC_POINT(eckey->pub_key) || is_symbolic_BIGNUM(eckey->priv_key))
      hasSymbolicInputs = true;


    if (hasSymbolicInputs) {
      printf("DEBUG: Calling ECDH_compute_key for time %d with symbolic input \n", ECDH_compute_key_calls);
      fflush(stdout);
      tase_make_symbolic( (uint64_t) out, outlen, "ecdh_compute_key_output");

      //return value is outlen
      //Todo -- determine if we really need to make the return value exactly size_t
      ref<ConstantExpr> returnVal = ConstantExpr::create(outlen, Expr::Int64);
      //target_ctx_gregs_OS->write(GREG_RAX * 8, returnVal);
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX], returnVal);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      
    } else {

      //Otherwise we're good to call natively
      printf("DEBUG: Calling ECDH_compute_key for time %d natively \n", ECDH_compute_key_calls);
      fflush(stdout);
      forceNativeRet = true;
      target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
      return; 
    }
      
  } else {
    printf("ERROR: model_ECDH_compute_key called with symbolic input args\n");
    std::exit(EXIT_FAILURE);
  }
}


void tase_print_BIGNUM(FILE * f, BIGNUM * bn) {
  
  fprintf(f,"Printing data in BIGNUM: \n");
  printBuf(f,(void *) bn->d, sizeof(BN_ULONG) * bn->dmax);
  
  fprintf(f,"\n Finished printing BIGNUM \n");
  fflush(stdout);
}
void tase_print_EC_POINT(FILE * f, EC_POINT * pt) {
  fprintf(f,"TASE printing ec_point \n");
  fflush(f);
  if (pt == NULL) {
    fprintf(f,"ec_point is NULL \n");
    return;
  }
     
  fprintf(f,"EC_METHOD is 0x%lx ", (uint64_t) pt->meth);
  fprintf(f,"X is \n");
  tase_print_BIGNUM(f,&(pt->X));
  fprintf(f,"Y is \n");
  tase_print_BIGNUM(f,&(pt->Y));
  fprintf(f,"Z is \n");
  tase_print_BIGNUM(f,&(pt->Z));
  fprintf(f,"Z_is_one is 0x%x", (uint32_t) pt->Z_is_one);
  fprintf(f,"\n Finished printing ec_point \n");
  fflush(f);
}

void tase_print_EC_KEY(FILE * f, EC_KEY * key) {
  fprintf(f,"Printing pub_key and priv_key fields in EC_KEY \n");
  fprintf(f,"pub_key: \n");
  tase_print_EC_POINT(f,(key->pub_key));
  fprintf(f,"priv_key: \n");
  if (key->priv_key != NULL) {
    tase_print_BIGNUM(f,(key->priv_key));
  } else {
    fprintf(f,"priv_key is NULL \n");
  }
  fprintf(f,"Finished printing EC_KEY \n");

}

//model for size_t EC_POINT_point2oct(const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form,
//        unsigned char *buf, size_t len, BN_CTX *ctx)
//Function defined in crypto/ec/ec_oct.c

//Todo: Double check this to see if we actually need to peek further into structs to see if they have symbolic
//taint
//The purpose of this function is to convert from an EC_POINT representation to an octet string encoding in buf.
//Todo: Check all the other args for symbolic taint, even though in practice it should just be the point

void Executor::model_EC_POINT_point2oct() {
  
  EC_POINT_point2oct_calls++;
  printf("Entering EC_POINT_point2oct at interpctr %lu \n", interpCtr);
  fflush(stdout);

  //#ifdef TASE_OPENSSL
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(GREG_RDI * 8, Expr::Int64);
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(GREG_RSI * 8, Expr::Int64);
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(GREG_RDX * 8, Expr::Int64);
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(GREG_RCX * 8, Expr::Int64);
  ref<Expr> arg5Expr = target_ctx_gregs_OS->read(GREG_R8 * 8, Expr::Int64);
  ref<Expr> arg6Expr = target_ctx_gregs_OS->read(GREG_R9 * 8, Expr::Int64);

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) &&
	(isa<ConstantExpr>(arg5Expr)) &&
	(isa<ConstantExpr>(arg6Expr))
	) {

    EC_GROUP * group = ( EC_GROUP *) target_ctx_gregs[GREG_RDI].u64;
    EC_POINT * point = ( EC_POINT *) target_ctx_gregs[GREG_RSI].u64;
    point_conversion_form_t form = (point_conversion_form_t) target_ctx_gregs[GREG_RDX].u64;
    unsigned char * buf = (unsigned char * ) target_ctx_gregs[GREG_RCX].u64;
    size_t len = (size_t) target_ctx_gregs[GREG_R8].u64;
    BN_CTX * ctx = (BN_CTX *) target_ctx_gregs[GREG_R9].u64;

    bool hasSymbolicInput = false;
    
    size_t field_len = BN_num_bytes(&group->field);
    size_t ret = (form == POINT_CONVERSION_COMPRESSED) ? 1 + field_len : 1 + 2*field_len;
    tase_print_EC_POINT(stdout,point);
    
    
    if (is_symbolic_EC_POINT(point))
      hasSymbolicInput = true;
    //Todo: See if there's a bug in our models or cliver's where we should be making
    //EC_POINT_point2oct ignore or examine the X/Y/Z fields bc of behavior for NULL buf
    if (hasSymbolicInput ) {

      printf("Entering EC_POINT_point2oct for time %d with symbolic input \n", EC_POINT_point2oct_calls);
      fflush(stdout);
   
      if (buf != NULL ) {
	tase_make_symbolic((uint64_t) buf, ret, "ECpoint2oct");
	printf("Returned from ECpoint2oct tase_make_symbolic call \n");
	std::cout.flush();
      } else {
	printf("Found special case to EC_POINT_point2oct with null buffer input. Returning size \n");
	std::cout.flush();
      }

      ref<ConstantExpr> returnVal = ConstantExpr::create(ret, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[GREG_RAX], returnVal);
      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[GREG_RSP].u64);
      target_ctx_gregs[GREG_RIP].u64 = retAddr;
      target_ctx_gregs[GREG_RSP].u64 += 8;
      printf("Returning from model_EC_POINT_point2oct \n");
      std::cout.flush();
      
    } else {
      //Otherwise we're good to call natively
       printf("Entering EC_POINT_point2oct for time %d and calling natively \n", EC_POINT_point2oct_calls);
       fflush(stdout);
       forceNativeRet = true;
       target_ctx_gregs[GREG_RIP].u64 += native_ret_off;
       return; 
     
    }

  } else {
    printf("ERROR: model_EC_POINT_point2oct called with symbolic input \n");
    std::exit(EXIT_FAILURE);
  }
    //#endif
    
}
