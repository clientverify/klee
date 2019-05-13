
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
#include <fstream>

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
void printBuf(void * buf, int count)
{

  for (int i = 0; i < count; i++)
    printf("%02x", *((uint8_t *) buf + i));
  printf("\n\n");
  std::cout.flush();
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
  printf("Entering model_sprintf at RIP 0x%lx \n", target_ctx_gregs[REG_RIP].u64);
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

  printf("Entering model_vfprintf at RIP 0x%lx \n", target_ctx_gregs[REG_RIP].u64);
  
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
    printf("Creating MO to back errno at 0x%lx with size 0x%lx \n", (uint64_t) res, sizeof(int));
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

    printf("Entering model_ktest_master_secret \n");
    
    
    unsigned char * buf = (unsigned char *) target_ctx_gregs[REG_RDI].u64; 
    int num = (int) target_ctx_gregs[REG_RSI].u64;

    void * tmp = malloc(num);
    
    
    if (enableMultipass) {

      printf("Trying to read in master secret \n");
      
      
      //printf("Debug: Manually loading master secret \n");
      //char * secretString = "be02e96158f1de85a876435753db64188ea55a8163ef1df43298ded43de2e53a691024666c9d958105561054a34127e8";

      std::ifstream datfile;
      datfile.open("master_secret.dat");
      
      
      uint8_t endRes [48];
      std::string currLine;
      for (int i = 0; i < 48; i++) {
	std::getline(datfile, currLine);
	//printf("I see data at line %d as string %s \n",i,currLine.c_str());
	std::istringstream is(currLine);
	int tmp = 0;
	is >> tmp;
	//printf("I read the int input as %d \n",tmp);
	endRes[i] = (uint8_t) tmp;

      }
      printf("Printing results of attempt to load master secret... \n");
      for (int i = 0; i < 48; i++)
	printf("%02x",endRes[i]);
	   
      memcpy (buf, endRes, num); //Todo - use tase_helper read/write
	       
      //Todo: - Less janky io here.
      
    }else {
      ktest_master_secret_tase( (unsigned char *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);

      printf("PRINTING MASTER SECRET as hex \n");
      uint8_t * base = (uint8_t *) target_ctx_gregs[REG_RDI].u64;
      for (int i = 0; i < num; i++)
	printf("%02x", *(base + i));
      printf("\n------------\n");
      printf("PRINTING MASTER SECRET as uint8_t line-by-line \n");
      for (int i = 0; i < num; i++)
	printf("%u\n", (*(base +i)));
      printf("\n------------\n");
      
      
    }
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

  printf(" Found call to exit.  TASE should shutdown. \n");
  std::cout.flush();
  printf("IMPORTANT: Worker exiting from terminal path in round %d pass %d from model_exit \n", roundCount, passCount);
  std::cout.flush();
  worker_exit();
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
    
    bool debugMultipass = true;

    if (debugMultipass) {
	fprintf(modelLog, "MULTIPASS DEBUG: Entering call to ktest_writesocket round %d pass %d \n", roundCount, passCount);
	printf("MULTIPASS DEBUG: Entering ktest_writesocket at round %d pass %d \n", roundCount, passCount);
	std::cout.flush();
	fflush(modelLog);
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

      if (roundCount == 2) {
	/*
	workerIDStream << ".";
	workerIDStream << "MP_DBG";
	std::string pidString ;
	pidString = workerIDStream.str();
	//freopen(pidString.c_str(),"w",stdout);
	freopen(pidString.c_str(),"w",stderr);

	fprintf(stderr,"Opening new file for round 2 debugging \n");
	std::cout.flush();
	*/
      }
     
      //Basic structure comes from NetworkManager in klee repo of cliver.
      //Get log entry for c2s
      KTestObject *o = KTOV_next_object(&ktov, ktest_object_names[CLIENT_TO_SERVER]);
      if (o->numBytes != count) {
	fprintf(modelLog,"IMPORTANT: VERIFICATION ERROR: mismatch in replay size \n");
	printf("IMPORTANT: VERIFICATION ERROR - write buffer size mismatch: Worker exiting from terminal path in round %d pass %d \n", roundCount, passCount);
	std::cout.flush();
	worker_exit();
      }
      
      printf("Buffer in writesock call : \n");
      printBuf ((void *) buf, count);
      
      printf("Buffer in            log : \n");
      printBuf ((void *) o->bytes, count);
      
      
      //Create write condition
      klee::ref<klee::Expr> write_condition = klee::ConstantExpr::alloc(1, klee::Expr::Bool);
      /*
      printf("Checking wc buf of size %d in round %d pass %d \n",count, roundCount, passCount);
      printf("Printing each byte of output buf as expr: \n");
      std::cout.flush();
      fflush(stderr);
      */
      for (int i = 0; i < count; i++) {
	klee::ref<klee::Expr> val = tase_helper_read((uint64_t) buf + i, 1);
	/*
	if (!isa<ConstantExpr>(val) ) {
	  fprintf(stderr,"byte %d symbolic \n",i);
	} else {
	  fprintf(stderr,"byte %d concrete \n",i);
	}
	fflush(stderr);
	std::cout.flush();
	if(roundCount == 2)
	  val->dump();
	fflush(stderr);
	std::cout.flush();
	fprintf(stderr,"Printing write condition: \n");
	std::cout.flush();
	*/
	fflush(stderr);
	/* ref<klee::ConstantExpr> dbgExpr = klee::ConstantExpr::alloc(o->bytes[i], klee::Expr::Int8);
	dbgExpr->dump();
	ref<ConstantExpr> dbgExpr2 = klee::ConstantExpr::create(o->bytes[i], Expr::Int8);
	dbgExpr2->dump();*/
	klee::ref<klee::Expr> condition = klee::EqExpr::create(tase_helper_read((uint64_t) buf + i, 1),
							       klee::ConstantExpr::alloc(o->bytes[i], klee::Expr::Int8));
	//condition->dump();
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
      /*
      if (roundCount == 2 && passCount == 0) {
	fprintf(stderr,"Dumping entire write condition \n");
	std::cout.flush();
	write_condition->dump();
	fflush(stderr);
	}*/
      addConstraint(*GlobalExecutionStatePtr, write_condition);
      //Check validity of write condition

      get_sem_lock(); //Just for debugging 

      
      if (klee::ConstantExpr *CE = dyn_cast<klee::ConstantExpr>(write_condition)) {
	if (CE->isFalse()) {
	  fprintf(modelLog, "IMPORTANT: VERIFICATION ERROR: false write condition \n");
	  printf("IMPORTANT: VERIFICATION ERROR: false write condition. Worker exiting from terminal path in round %d pass %d \n", roundCount, passCount);
	  std::cout.flush();
	  worker_exit();
	}
      } else {
	bool result;
	//compute_false(GlobalExecutionStatePtr, write_condition, result);
	solver->mustBeFalse(*GlobalExecutionStatePtr, write_condition, result);
	
	if (result) {
	  fprintf(modelLog, "IMPORTANT: VERIFICATION ERROR: write condition determined false \n");
	  fprintf(stderr, "VERIFICATION ERROR: write condition determined false \n");
	  printf("IMPORTANT: VERIFICATION ERROR: false write condition. Worker exiting from terminal path in round %d pass %d \n", roundCount, passCount);
	  fflush(modelLog);
	  std::cout.flush();
	  worker_exit();
	} else {
	  fprintf(stderr,"Good news.  mustBeFalse(write_condition) returned false \n");
	  fflush(stderr);
	}
      }
      
      //Solve for multipass assignments
      CVAssignment currMPA;
      currMPA.clear();

     
      
      if (!isa<ConstantExpr>(write_condition)) {
	currMPA.solveForBindings(solver->solver, write_condition,GlobalExecutionStatePtr);
      }

      release_sem_lock();
      
      //print assignments
      printf("About to print assignments \n");
      std::cout.flush();
      
      currMPA.printAllAssignments(NULL);

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
      tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX], ConstantExpr::create(count, Expr::Int64));
      
      //RESET ROUND
      //-------------------------------------------
      //1. MMAP a new buffer storing the ID of the replay for the current round.
      //2. MMAP a new buffer for storing the assignments learned from the previous pass 
      multipass_reset_round(); //Sets up new buffer for MPA and destroys multipass child process
      
      //NEW ROUND
      //-------------------------------------------
      //1. Atomically create a new SIGSTOP'd replay process and deserialize the constraints
      multipass_start_round(this, false);  //Gets semaphore,sets prevMPA, and sets a replay child process up
	  
      
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

  printf("Entering model_ktest_readsocket for time %d \n", ktest_readsocket_calls);
  std::cout.flush();
  fprintf(modelLog,"Entering model_ktest_readsocket \n");
  fflush(modelLog);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) &&
       (isa<ConstantExpr>(arg3Expr))
       ){

    //Return result of call
    ssize_t res = ktest_readsocket_tase((int) target_ctx_gregs[REG_RDI].u64, (void *) target_ctx_gregs[REG_RSI].u64, (size_t) target_ctx_gregs[REG_RDX].u64);
    printf("Returned from ktest_readsocket_tase call \n");

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
  printf("Entering model_ktest_raw_read_stdin for time %d \n", ktest_raw_read_stdin_calls);
  fflush(stdout);
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

      printf("model_ktest_raw_read_stdin DBG: peekNextKTestObject returned %d bytes \n", len);
      fflush(stdout);
      
      if (len > buffsize) {
	fprintf(modelLog, "Buffer too large for ktest_raw_read stdin \n");
	fflush(modelLog);
	std::exit(EXIT_FAILURE);
      }


      int res = len;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX].u64, resExpr);
      tase_make_symbolic( (uint64_t) buf, len, "stdin");
      
      
    } else {
      
      
      //return result of call
      int res = ktest_raw_read_stdin_tase((void *) target_ctx_gregs[REG_RDI].u64, (int) target_ctx_gregs[REG_RSI].u64);
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX].u64, resExpr);
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

    fprintf(modelLog, "DBG: Pushed RV for model_ktest_select is 0x%lx \n", *((uint64_t *) target_ctx_gregs[REG_RSP].u64 ) );
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

//Just for debugging perf
/*
 */

// void * memset (void * buf, int val, size_t number)
void Executor::model_memset() {

  void * buf = (void *) target_ctx_gregs[REG_RDI].u64;
  int val = (int) target_ctx_gregs[REG_RSI].u64;
  size_t num = (size_t) target_ctx_gregs[REG_RDX].u64;

  void * res = memset(buf,val,num);
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

  
  //Fake a return
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
  target_ctx_gregs[REG_RIP].u64 = retAddr;
  target_ctx_gregs[REG_RSP].u64 += 8;

}

//Just for debugging
//void *memcpy(void *dest, const void *src, size_t n);
void Executor::model_memcpy() {
  fprintf(modelLog,"Entering model_memcpy: Moving 0x%lx bytes from 0x%lx to 0x%lx \n",(size_t) target_ctx_gregs[REG_RDX].u64,  (void *) target_ctx_gregs[REG_RSI].u64 ,  (void*) target_ctx_gregs[REG_RDI].u64  );
  fflush(modelLog);
  printf("Entering model_memcpy: Moving 0x%lx bytes from 0x%lx to 0x%lx \n",(size_t) target_ctx_gregs[REG_RDX].u64,  (void *) target_ctx_gregs[REG_RSI].u64 ,  (void*) target_ctx_gregs[REG_RDI].u64  );
  std::cout.flush();

  
  void * dst = (void *) target_ctx_gregs[REG_RDI].u64;
  void * src = (void *) target_ctx_gregs[REG_RSI].u64;
  size_t num = (size_t) target_ctx_gregs[REG_RDX].u64;
    
  void * res;
  if (isBufferEntirelyConcrete((uint64_t) src, num) && isBufferEntirelyConcrete((uint64_t) dst, num)) 
    printf("Found entirely concrete buffer to buffer copy \n ");
  else
    printf("Found some symbolic taint in mempcy buffers \n");

  
  printf("Memcpy dbg -- printing source buffer as raw bytes \n");
  printBuf(src, num);

  printf("Memcpy dbg -- printing dest   buffer as raw bytes \n");
  printBuf(dst, num);
    
  std::cout.flush();
  
  
  std::cout.flush();
  for (int i = 0; i < num; i++) {
    ref<Expr> srcByte = tase_helper_read( ((uint64_t) src)+i, 1);
    tase_helper_write (((uint64_t)dst +i), srcByte);
  }
  res = dst;
  
    
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

    char * buf = (char *) target_ctx_gregs[REG_RDI].u64;
    int num    = (int) target_ctx_gregs[REG_RSI].u64;

    if (enableMultipass) {
      tase_make_symbolic((uint64_t) buf, num, "rng");
       //Double check this
      int res = num;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
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

  printf("Calling model_ktest_RAND_pseudo_bytes \n");
  fprintf(modelLog,"Calling model_ktest_RAND_PSEUDO_bytes at %lu \n", interpCtr);
  
  if  (
       (isa<ConstantExpr>(arg1Expr)) &&
       (isa<ConstantExpr>(arg2Expr)) ) {

    char * buf = (char *) target_ctx_gregs[REG_RDI].u64;
    int num   = (int) target_ctx_gregs[REG_RSI].u64;
    
    //return result of call

    if (enableMultipass) {

      tase_make_symbolic((uint64_t) buf, num, "prng");

      //Double check this
      int res = num;
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
      
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

    printf("Called getenv on 0x%lx, returned 0x%lx \n", target_ctx_gregs[REG_RDI].u64, (uint64_t) res);
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
      fprintf(modelLog,"Creating MO to back tm at 0x%lx with size 0x%x \n", (uint64_t) res, sizeof(struct tm));
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
    
    printf("calloc at 0x%lx for 0x%lx bytes \n", (uint64_t) res, numBytes);
    std::cout.flush();
    fprintf(heapMemLog, "CALLOC buf at 0x%lx - 0x%lx, size 0x%lx, interpCtr %lu \n", (uint64_t) res, ((uint64_t) res + numBytes -1), numBytes, interpCtr);
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

   printf("Calling realloc on 0x%lx with size 0x%lx.  Ret val is 0x%lx \n", (uint64_t) ptr, (uint64_t) size, (uint64_t) res);
   if (roundUpHeapAllocations)
     size = roundUp(size, 8);
   
   ref<ConstantExpr> resultExpr = ConstantExpr::create( (uint64_t) res, Expr::Int64);
   target_ctx_gregs_OS->write(REG_RAX * 8, resultExpr);


   std::cout.flush();
   fprintf(heapMemLog, "REALLOC call on 0x%lx for size 0x%lx with return value 0x%lx. InterpCtr is %lu \n", (uint64_t) ptr, size, (uint64_t) res , interpCtr);

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
      printf("Returned ptr at 0x%lx \n", (uint64_t) buf);
      std::cout.flush();
    }
    fprintf(heapMemLog, "MALLOC buf at 0x%lx - 0x%lx, size 0x%x, interpCtr %lu \n", (uint64_t) buf, ((uint64_t) buf + sizeArg -1), sizeArg, interpCtr);
    fflush(heapMemLog);
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

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);
  if (isa<ConstantExpr>(arg1Expr)) {


    if (!skipFree) {
    
      void * freePtr = (void *) target_ctx_gregs[REG_RDI].u64;
      printf("Calling model_free on addr 0x%lx \n", (uint64_t) freePtr);
      free(freePtr);

      fprintf(heapMemLog, "FREE buf at 0x%lx, interpCtr is %lu \n", (uint64_t) freePtr, interpCtr);

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
      fprintf(modelLog,"Creating MO to back hostent at 0x%lx with size 0x%lx \n", (uint64_t) res, sizeof(hostent));
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


static void print_fd_set(int nfds, fd_set *fds) {
  int i;
  for (i = 0; i < nfds; i++) {
    printf(" %d", FD_ISSET(i, fds));
  }
  printf("\n");
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


    printf("nfds is %d \n", nfds);
    printf("\n");
    printf("IN readfds  = ");
    print_fd_set(nfds, readfds);
    printf("IN writefds = ");
    print_fd_set(nfds, writefds);
    std::cout.flush();


    
    bool debugSelect = true;
    if (debugSelect) {
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


      tase_make_symbolic ( (uint64_t) &(target_ctx_gregs[REG_RAX]), 8, "SelectReturnVal");
      ref<Expr> ret_val = tase_helper_read((uint64_t) &(target_ctx_gregs[REG_RAX]), 8);
      ref<Expr> retEqualsZeroExpr = EqExpr::create(ret_val, zero);
      ref<Expr> retEqualsOneExpr  = EqExpr::create(ret_val, one);
      ref<Expr> retEqualsZeroOrOneExpr = OrExpr::create(retEqualsZeroExpr, retEqualsOneExpr);
      addConstraint(*GlobalExecutionStatePtr, retEqualsZeroOrOneExpr);
      
      //tase_helper_write( (uint64_t)  &(target_ctx_gregs[REG_RAX]), ConstantExpr::create(1, Expr::Int64) );




      printf("nfds is %d \n", nfds);
      printf("\n");
      printf("OUT readfds  = ");
      print_fd_set(nfds, readfds);
      printf("OUT writefds = ");
      print_fd_set(nfds, writefds);
      std::cout.flush();

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;

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

	/*
	void * readFDResultBuf = malloc(4); //Technically this only needs to be a bit or byte
	//Get the MO, then call executeMakeSymbolic()
	MemoryObject * readFDResultBuf_MO = memory->allocateFixed( (uint64_t) readFDResultBuf ,4,NULL);
	std::string nameString = "readFDSymVar " + std::to_string(times_model_select_called) + " " + std::to_string(j);
	readFDResultBuf_MO->name = nameString;
	executeMakeSymbolic(*GlobalExecutionStatePtr, readFDResultBuf_MO, nameString);
	const ObjectState *constreadFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.findObject(readFDResultBuf_MO);
	ObjectState * readFDResultBuf_OS = GlobalExecutionStatePtr->addressSpace.getWriteable(readFDResultBuf_MO,constreadFDResultBuf_OS);
	
	*/
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

    tase_make_symbolic((uint64_t) &target_ctx_gregs[REG_RAX], 8, "SelectReturn");

    /*
    target_ctx_gregs_OS->write(REG_RAX * 8, bitsSetExpr);
    ref<ConstantExpr> offset = ConstantExpr::create(REG_RAX * 8, Expr::Int16);  //Why 16?
    target_ctx_gregs_OS->applyPsnOnWrite( offset , bitsSetExpr);
    */
    std::cout.flush();
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

//-----------------------------CRYPTO SECTION--------------------
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

extern bool forceNativeRet;
/*
int AES_encrypt_calls;
int ECDH_compute_key_calls;
int EC_POINT_point2oct_calls;
int EC_KEY_generate_key_calls;
int SHA1_Update_calls;
int SHA1_Final_calls;
int SHA256_Update_calls;
int SHA256_Final_calls;
int gcm_gmult_4bit_calls;
int gcm_ghash_4bit_calls;

*/



//Model for int SHA1_Update(SHA_CTX *c, const void *data, size_t len);
//defined in crypto/sha/sha.h.
//Updated 04/30/2019
void Executor::model_SHA1_Update () {
  static int timesSHA1UpdateIsCalled = 0;
  timesSHA1UpdateIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //SHA_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {


    printf("Entered model_SHA1Update for time %d \n", timesSHA1UpdateIsCalled );
    //Determine if SHA_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[REG_RDI].u64;
    const void * data = (const void *) target_ctx_gregs[REG_RSI].u64;
    size_t len = (size_t) target_ctx_gregs[REG_RDX].u64;
    
    bool hasSymbolicInput = false;

    if (!isBufferEntirelyConcrete((uint64_t) c, sizeof(SHA_CTX)) || !isBufferEntirelyConcrete((uint64_t) data, len))
      hasSymbolicInput = true;

    if (hasSymbolicInput) {
      std::string nameString = "SHA1_Update_Output" + std::to_string(timesSHA1UpdateIsCalled);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
       fprintf(modelLog,"MULTIPASS DEBUG: Found symbolic input to SHA1_Update \n");
      tase_make_symbolic((uint64_t) c, 20, name);


      //Can optionally return failure here if desired
      int res = 1; //Force success
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;

      
    } else { //Call natively
       fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to SHA1_Update \n");
       forceNativeRet = true;
       target_ctx_gregs[REG_RIP].u64 += 17;

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
  static int timesSHA1FinalIsCalled = 0;
  timesSHA1FinalIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //SHA_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[REG_RDI].u64;
     SHA_CTX * c = (SHA_CTX *) target_ctx_gregs[REG_RSI].u64;
     bool hasSymbolicInput = false;

     if (!isBufferEntirelyConcrete((uint64_t) c, 20) )
       hasSymbolicInput = true;
     
   
     if (hasSymbolicInput) {
       std::string nameString = "SHA1_Final_Output" + std::to_string(timesSHA1FinalIsCalled);
       const char * constCopy = nameString.c_str();
       char name [40];//Arbitrary number
       strncpy(name, constCopy, 40);
      
       fprintf(modelLog,"MULTIPASS DEBUG: Found symbolic input to SHA1_Final \n");

       
       tase_make_symbolic( (uint64_t) md, SHA_DIGEST_LENGTH, name);

       //Can optionally return failure here if desired
       int res = 1; //Force success
       ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
       target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
       
       //fake a ret
       uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
       target_ctx_gregs[REG_RIP].u64 = retAddr;
       target_ctx_gregs[REG_RSP].u64 += 8;
       
     } else {
       fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to SHA1_Final \n");
       forceNativeRet = true;
       target_ctx_gregs[REG_RIP].u64 += 17;

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
  static int timesSHA256UpdateIsCalled = 0;
  timesSHA256UpdateIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //SHA256_CTX * c
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const void * data
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    //Determine if SHA256_CTX or data have symbolic values.
    //If not, run the underlying function.

    SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[REG_RDI].u64;
    const void * data = (const void *) target_ctx_gregs[REG_RSI].u64;
    size_t len = (size_t) target_ctx_gregs[REG_RDX].u64;
    
    bool hasSymbolicInput = false;
    if (!isBufferEntirelyConcrete((uint64_t ) c, sizeof(SHA256_CTX)) || !isBufferEntirelyConcrete( (uint64_t ) data, len))
      hasSymbolicInput = true;
    

    if (hasSymbolicInput) {

      std::string nameString = "SHA256_Update_Output" + std::to_string(timesSHA256UpdateIsCalled);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic( (uint64_t) c, 32, name);

      //Can optionally return failure here if desired
      int res = 1; //Force success
      ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
      target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      
    } else { //Call natively
      fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to SHA256_Update \n");
      forceNativeRet = true;
      target_ctx_gregs[REG_RIP].u64 += 17;
      //todo: provide sha256_update native implementation
      
    }

  } else {
    printf("ERROR: symbolic arg passed to model_SHA256_Update \n");
    std::exit(EXIT_FAILURE);
  }
}

//Model for int SHA256_Final(unsigned char *md, SHA256_CTX *c)
//defined in crypto/sha/sha.h
void Executor::model_SHA256_Final() {
  static int timesSHA256FinalIsCalled = 0;
  timesSHA256FinalIsCalled++;

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //unsigned char *md
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //SHA256_CTX *c

   if (  (isa<ConstantExpr>(arg1Expr)) &&
	 (isa<ConstantExpr>(arg2Expr)) ) {

     unsigned char * md = (unsigned char *) target_ctx_gregs[REG_RDI].u64;
     SHA256_CTX * c = (SHA256_CTX *) target_ctx_gregs[REG_RSI].u64;
     
     bool hasSymbolicInput = false;

     if (!isBufferEntirelyConcrete((uint64_t) c, 32) )
       hasSymbolicInput = true;
     
     if (hasSymbolicInput) {
       std::string nameString = "SHA256_Final_Output" + std::to_string(timesSHA256FinalIsCalled);
       const char * constCopy = nameString.c_str();
       char name [40];//Arbitrary number
       strncpy(name, constCopy, 40);
      
       fprintf(modelLog,"MULTIPASS DEBUG: Found symbolic input to SHA256_Final \n");
       
       tase_make_symbolic((uint64_t) md, SHA_DIGEST_LENGTH, name);

       //Can optionally return failure here if desired
       int res = 1; //Force success
       ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
       target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
       
       //fake a ret
       uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
       target_ctx_gregs[REG_RIP].u64 = retAddr;
       target_ctx_gregs[REG_RSP].u64 += 8;
 
     } else {
        fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to SHA256_Final \n");
	forceNativeRet = true;
	target_ctx_gregs[REG_RIP].u64 += 17;
       //Todo: provide fast native sha256_final implementation
      
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
  static int timesModelAESEncryptIsCalled = 0;
  timesModelAESEncryptIsCalled++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //const unsigned char *in
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //unsigned char * out
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); //const AES_KEY * key

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr))
	) {

    const unsigned char * in =  (const unsigned char *)target_ctx_gregs[REG_RDI].u64;
    unsigned char * out = (unsigned char *) target_ctx_gregs[REG_RSI].u64;
    const AES_KEY * key = (const AES_KEY *) target_ctx_gregs[REG_RDX].u64;


     int AESBlockSize = 16; //Number of bytes in AES block
    
    printf("AES_encrypt %d debug -- dumping buffer inputs at round %d pass %d \n", timesModelAESEncryptIsCalled, roundCount, passCount );

    printf("key is \n");
    printBuf((void *) key, AESBlockSize);
    printf("in is \n");
    printBuf((void *) in, AESBlockSize);
    
    
    
   
    bool hasSymbolicDependency = false;
    
    //Check to see if any input bytes or the key are symbolic
    //Todo: Chase down any structs that AES_KEY points to if it's not a simple struct.
    //It's OK; struct holds no pointers.
    if (!isBufferEntirelyConcrete((uint64_t) in, AESBlockSize) || !isBufferEntirelyConcrete ((uint64_t) key, AESBlockSize) )
      hasSymbolicDependency = true;
    
    if (hasSymbolicDependency) {
      printf("MULTIPASS DEBUG: Found symbolic input to AES_encrypt \n");
      fprintf(modelLog, "MULTIPASS DEBUG: Found symbolic input to AES_encrypt \n");
      fflush(modelLog);
      fflush(stdout);
      std::string nameString = "aes_Encrypt_output " + std::to_string(timesModelAESEncryptIsCalled);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic((uint64_t) out, AESBlockSize, name);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively
      printf("MULTIPASS DEBUG: Did not find symbolic input to AES_encrypt \n");
      fflush(stdout);
      fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to AES_encrypt \n");
      forceNativeRet = true;
      target_ctx_gregs[REG_RIP].u64 += 17;
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
  static int modelGCMGMULT4bitCalls = 0;
  modelGCMGMULT4bitCalls++;

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); // const u128 Htable[16]
  
  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr))
	) {
    
    u64 * XiPtr = (u64 *) target_ctx_gregs[REG_RDI].u64;
    u128 * HtablePtr = (u128 *) target_ctx_gregs[REG_RSI].u64;

    printf("Entering model_gcm_gmult_4bit for time %d and dumping raw input as bytes \n", modelGCMGMULT4bitCalls);

    printf("Xi inputs are \n");
    printBuf((void *) XiPtr, 16);
    printf("Htable inputs are \n");
    printBuf((void *) HtablePtr, 196);

    
    //Todo: Double check the dubious ptr cast and figure out if we
    //are assuming any structs are packed
    bool hasSymbolicInput = false;

    if (!isBufferEntirelyConcrete((uint64_t) XiPtr, 16)) {
	hasSymbolicInput = true;
    }
    
    if (hasSymbolicInput) {
       fprintf(modelLog,"MULTIPASS DEBUG: Found symbolic input to gcm_gmult \n");
       printf("MULTIPASS DEBUG: Found symbolic input to gcm_gmult \n");
       std::cout.flush();
      std::string nameString = "GCM_GMULT_output " + std::to_string(modelGCMGMULT4bitCalls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic((uint64_t) XiPtr, 128, name);

      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      
    } else {
       //Otherwise we're good to call natively
      fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to gcm_gmult \n");
      printf("MULTIPASS DEBUG: Did not find symbolic input to gcm_gmult \n");
      forceNativeRet = true;
      target_ctx_gregs[REG_RIP].u64 += 17;
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
  static int modelGCMGHASH4bitCalls = 0;
  modelGCMGHASH4bitCalls++;
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //u64 Xi[2]
  ref<Expr> arg2Expr = target_ctx_gregs_OS->read(REG_RSI * 8, Expr::Int64); //const u128 Htable[16]
  ref<Expr> arg3Expr = target_ctx_gregs_OS->read(REG_RDX * 8, Expr::Int64); // const u8 *inp
  ref<Expr> arg4Expr = target_ctx_gregs_OS->read(REG_RCX * 8, Expr::Int64); //size_t len

  if (  (isa<ConstantExpr>(arg1Expr)) &&
	(isa<ConstantExpr>(arg2Expr)) &&
	(isa<ConstantExpr>(arg3Expr)) &&
	(isa<ConstantExpr>(arg4Expr)) ) {

    u64 * XiPtr = (u64 *) target_ctx_gregs[REG_RDI].u64;
    u128 * HtablePtr = (u128 *) target_ctx_gregs[REG_RSI].u64;
    const u8 * inp = (const u8 *) target_ctx_gregs[REG_RDX].u64;
    size_t len = (size_t) target_ctx_gregs[REG_RCX].u64;
    printf("Entering model_gcm_ghash_4bit for time %d and dumping args as raw bytes \n", modelGCMGHASH4bitCalls);

    printf("Xi inputs are \n");
    printBuf((void *) XiPtr, 16);
    printf("Htable inputs are \n");
    printBuf((void *) HtablePtr, 196);
    printf("inp is \n");
    printBuf((void *) inp, len);
    printf("len is %lu \n", len);
    std::cout.flush();
    
    //Todo: Double check the dubious ptr casts and figure out if we
    //are falsely assuming any structs or arrays are packed
    bool hasSymbolicInput = false;
    // Todo: Double check  if this is OK for different size_t values.
    if (!isBufferEntirelyConcrete((uint64_t) XiPtr, 16) || !isBufferEntirelyConcrete((uint64_t) inp, len) ) 
      hasSymbolicInput = true;
    
    if (hasSymbolicInput) {
      fprintf(modelLog,"MULTIPASS DEBUG: Found symbolic input to gcm_ghash \n");
      printf("MULTIPASS DEBUG: Found symbolic input to gcm_ghash \n");
      std::cout.flush();
      std::string nameString = "GCM_GHASH_output " + std::to_string(modelGCMGHASH4bitCalls);
      const char * constCopy = nameString.c_str();
      char name [40];//Arbitrary number
      strncpy(name, constCopy, 40);
      
      tase_make_symbolic ((uint64_t) XiPtr, sizeof(u64) * 2, name);
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      
    } else {
      //Otherwise we're good to call natively
      fprintf(modelLog,"MULTIPASS DEBUG: Did not find symbolic input to gcm_ghash \n");
      printf("MULTIPASS DEBUG: Did not find symbolic input to gcm_ghash \n");
      std::cout.flush();
      forceNativeRet = true;
      target_ctx_gregs[REG_RIP].u64 += 17;
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
  fprintf(modelLog, "Calling make_BN_symbolic at rip 0x%lx on addr 0x%lx \n", target_ctx_gregs[REG_RIP].u64, (uint64_t) bn);
  fflush(modelLog);
  if (bn->dmax > 0) {
    //char *buf = (char *)malloc((bn->dmax)*sizeof(bn->d[0]));
    //klee_make_symbolic(buf, (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
    tase_make_symbolic((uint64_t) bn->d,  (bn->dmax)*sizeof(bn->d[0]), "BNbuf");
    //memcpy(bn->d, buf, bn->dmax);
    //free(buf);
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
void Executor::model_EC_KEY_generate_key () {

  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64); //EC_KEY * key

   if ( (isa<ConstantExpr>(arg1Expr)) ) {

     EC_KEY * eckey = (EC_KEY *) target_ctx_gregs[REG_RDI].u64;
     bool makeECKeySymbolic = true;


     if (enableMultipass) {
       fprintf(modelLog,"MULTIPASS DEBUG: Calling EC_KEY_generate_key with symbolic return \n"); 
       
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
       tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX], resExpr);
       
       //target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
       
       //fake a ret
       uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
       target_ctx_gregs[REG_RIP].u64 = retAddr;
       target_ctx_gregs[REG_RSP].u64 += 8;
       
     } else {
       //Otherwise we're good to call natively
       fprintf(modelLog,"DEBUG: Calling EC_KEY_generate_key natively \n");
       forceNativeRet = true;
       target_ctx_gregs[REG_RIP].u64 += 17;
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

  fprintf(modelLog, "is_symbolic_BIGNUM returned %d at rip 0x%lx \n", rv, target_ctx_gregs[REG_RIP].u64);
  fflush(modelLog);
  printf( "is_symbolic_BIGNUM returned %d at rip 0x%lx \n", rv, target_ctx_gregs[REG_RIP].u64);
  std::cout.flush();
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
      fprintf(modelLog, "WARNING: is_symbolic_EC_POINT found symbolic data at rip 0x%lx \n",  target_ctx_gregs[REG_RIP].u64);
      fflush(modelLog);
      return rv;
  }
  
  /*
  if (is_symbolic_BIGNUM(&(pt->X)) || is_symbolic_BIGNUM(&(pt->Y)) || is_symbolic_BIGNUM(&(pt->Z)))
    rv = true;
  else
    rv = false;  
  */

  fprintf(modelLog, "is_symbolic_EC_POINT returned %d at rip 0x%lx \n", rv, target_ctx_gregs[REG_RIP].u64);
  fflush(modelLog);
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

    void * out = (void *) target_ctx_gregs[REG_RDI].u64;
    size_t outlen = (size_t) target_ctx_gregs[REG_RSI].u64;
    EC_POINT * pub_key = (EC_POINT *) target_ctx_gregs[REG_RDX].u64;
    EC_KEY * eckey = (EC_KEY *) target_ctx_gregs[REG_RCX].u64;
    
    bool hasSymbolicInputs = false;

    if (is_symbolic_EC_POINT(pub_key) || is_symbolic_EC_POINT(eckey->pub_key) || is_symbolic_BIGNUM(eckey->priv_key))
      hasSymbolicInputs = true;


    if (hasSymbolicInputs) {

      fprintf(modelLog,"DEBUG: Calling ECDH_compute_key with symbolic input \n");
      
      tase_make_symbolic( (uint64_t) out, outlen, "ecdh_compute_key_output");

      //return value is outlen
      //Todo -- determine if we really need to make the return value exactly size_t
      ref<ConstantExpr> returnVal = ConstantExpr::create(outlen, Expr::Int64);
      //target_ctx_gregs_OS->write(REG_RAX * 8, returnVal);
      tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX], returnVal);


      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      
    } else {
      
      //Otherwise we're good to call natively
       fprintf(modelLog,"DEBUG: Calling ECDH_compute_key natively \n");
       forceNativeRet = true;
       target_ctx_gregs[REG_RIP].u64 += 17;
       return; 
    }
      
  } else {
    printf("ERROR: model_ECDH_compute_key called with symbolic input args\n");
    std::exit(EXIT_FAILURE);
  }
}





enum runType : int {INTERP_ONLY, MIXED};
//extern std::string project;
extern enum runType exec_mode;
bool point2oct_hack = false;


void tase_print_BIGNUM(BIGNUM bn) {
  printf("Printing BIGNUM \n");
  for (int i = 0; i < sizeof(BIGNUM); i++) {
    printf("%x",  *( ((uint8_t*) &(bn)) +i));
  }
  printf("\n Finished printing BIGNUM \n");
}
void tase_print_EC_POINT(EC_POINT * pt) {
  printf("TASE printing ec_point \n");
  std::cout.flush();
  if (pt == NULL) {
    printf("ec_point is NULL \n");
    return;
  }
    
  
  printf("EC_METHOD is 0x%lx ", (uint64_t) pt->meth);
  printf("X is \n");
  tase_print_BIGNUM(pt->X);
  printf("Y is \n");
  tase_print_BIGNUM(pt->Y);
  printf("Z is \n");
  tase_print_BIGNUM(pt->Z);
  printf("Z_is_one is 0x%x", (uint32_t) pt->Z_is_one);
  printf("\n Finished printing ec_point \n");
  std::cout.flush();
}


//model for size_t EC_POINT_point2oct(const EC_GROUP *group, const EC_POINT *point, point_conversion_form_t form,
//        unsigned char *buf, size_t len, BN_CTX *ctx)
//Function defined in crypto/ec/ec_oct.c

//Todo: Double check this to see if we actually need to peek further into structs to see if they have symbolic
//taint
//The purpose of this function is to convert from an EC_POINT representation to an octet string encoding in buf.
//Todo: Check all the other args for symbolic taint, even though in practice it should just be the point

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

    EC_GROUP * group = ( EC_GROUP *) target_ctx_gregs[REG_RDI].u64;
    EC_POINT * point = ( EC_POINT *) target_ctx_gregs[REG_RSI].u64;
    point_conversion_form_t form = (point_conversion_form_t) target_ctx_gregs[REG_RDX].u64;
    unsigned char * buf = (unsigned char * ) target_ctx_gregs[REG_RCX].u64;
    size_t len = (size_t) target_ctx_gregs[REG_R8].u64;
    BN_CTX * ctx = (BN_CTX *) target_ctx_gregs[REG_R9].u64;

    bool hasSymbolicInput = false;
    size_t field_len = BN_num_bytes(&group->field);
    size_t ret = (form == POINT_CONVERSION_COMPRESSED) ? 1 + field_len : 1 + 2*field_len;

    printf("Entering EC_POINT_point2oct \n");
    std::cout.flush();
    
    tase_print_EC_POINT(point);
    fprintf(modelLog,"Entering EC_POINT_point2oct at interpctr %lu \n", interpCtr);
    fflush(modelLog);
    
    if (is_symbolic_EC_POINT(point))
      hasSymbolicInput = true;
    //Todo: See if there's a bug in our models or cliver's where we should be making
    //EC_POINT_point2oct ignore or examine the X/Y/Z fields bc of behavior for NULL buf
    if (hasSymbolicInput ) {

      /*
      if (buf == NULL && point2oct_hack) {
	printf("HACK: Using interpreter hack for point2oct with symbolic inputs and null buf \n");
	std::cout.flush();
	//Just interpret until we debug this
	target_ctx_gregs[REG_RIP].u64 += 17;
	exec_mode = INTERP_ONLY;
	return; 

	}*/
      fprintf(modelLog,"DEBUG: Found symbolic input to EC_POINT_point2oct \n");
      
      if (buf != NULL ) {
	tase_make_symbolic((uint64_t) buf, ret, "ECpoint2oct");
	printf("Returned from ECpoint2oct tase_make_symbolic call \n");
	std::cout.flush();
      } else {
	printf("Found special case to EC_POINT_point2oct with null buffer input. Returning size \n");
	std::cout.flush();
      }

      
      ref<ConstantExpr> returnVal = ConstantExpr::create(ret, Expr::Int64);
      tase_helper_write((uint64_t) &target_ctx_gregs[REG_RAX], returnVal);
      printf("DBG 123 \n");
      std::cout.flush();

      
      //fake a ret
      uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP].u64);
      target_ctx_gregs[REG_RIP].u64 = retAddr;
      target_ctx_gregs[REG_RSP].u64 += 8;
      printf("Returning from model_EC_POINT_point2oct \n");
      std::cout.flush();
      
    } else {
      //Otherwise we're good to call natively
       fprintf(modelLog,"DEBUG: Calling EC_POINT_point2oct natively \n");
       forceNativeRet = true;
       target_ctx_gregs[REG_RIP].u64 += 17;
       return; 
     
    }

  } else {
    printf("ERROR: model_EC_POINT_point2oct called with symbolic input \n");
    std::exit(EXIT_FAILURE);
  }
}
