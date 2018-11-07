
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
#include <ucontext.h>
#include <iostream>
#include "klee/CVAssignment.h"
#include "klee/util/ExprUtil.h"
#include "tase/tase_constants.h"
#include "tase/TASEControl.h"
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <errno.h>
#include <cxxabi.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

extern gregset_t target_ctx_gregs;
extern gregset_t prev_ctx_gregs;
extern klee::Interpreter * GlobalInterpreter;
extern MemoryObject * target_ctx_gregs_MO;
extern ObjectState * target_ctx_gregs_OS;
extern ExecutionState * GlobalExecutionStatePtr;

enum testType : int {EXPLORATION, VERIFICATION};
extern enum testType test_type;

//Multipass
extern multipassRecord multipassInfo;
KTestObjectVector ktov;
extern bool enableMultipass;
extern void spinAndAwaitForkRequest();

int maxFDs = 10;
int openFD = 3;

KTestObject * peekNextKTestObject () {
  if (ktov.playback_index >= ktov.size){
    printf("Tried to peek for nonexistent KTestObject \n");
    std::exit(EXIT_FAILURE);
  }else {
    return &(ktov.objects[ktov.playback_index]);
  }
}


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
//int socket(int domain, int type, int protocol);
//http://man7.org/linux/man-pages/man2/socket.2.html
void Executor::model_socket() {

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
    ref<ConstantExpr> FDExpr = ConstantExpr::create(openFD, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, FDExpr);
    target_ctx_gregs[REG_RIP] += 5;
    openFD++;
  } else {
    printf("Found symbolic argument to model_socket \n");
    std::exit(EXIT_FAILURE);
  }
}

//Todo -- Determine if it's worthwhile to
//allocate memory object with page-aligned result

//http://man7.org/linux/man-pages/man3/malloc.3.html
void Executor::model_malloc() {
  
  ref<Expr> arg1Expr = target_ctx_gregs_OS->read(REG_RDI * 8, Expr::Int64);

  if (isa<ConstantExpr>(arg1Expr)) {
    size_t sizeArg = (size_t) target_ctx_gregs[REG_RDI];
    void * buf = malloc(sizeArg);
    printf("Entered model_malloc with requested size %d \n", sizeArg);
    std::cout.flush();

    //Make a memory object to represent the requested buffer
    MemoryObject * heapMem = addExternalObject(*GlobalExecutionStatePtr,buf, sizeArg, false );
    const ObjectState *heapOS = GlobalExecutionStatePtr->addressSpace.findObject(heapMem);
    ObjectState * heapOSWrite = GlobalExecutionStatePtr->addressSpace.getWriteable(heapMem,heapOS);  
    heapOSWrite->concreteStore = (uint8_t *) buf;

    //Return pointer to malloc'd buffer in RAX
    ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) buf, Expr::Int64);
    target_ctx_gregs_OS->write(REG_RAX * 8, resExpr); 

    //Ugly code to determine size of call inst
    uint64_t rip = target_ctx_gregs[REG_RIP]; 
    uint8_t callqOpc = 0xe8;
    uint16_t callIndOpc = 0x15ff;
    uint8_t * oneBytePtr = (uint8_t *) rip;
    uint8_t firstByte = *oneBytePtr;
    uint16_t firstTwoBytes = *( (uint16_t *) oneBytePtr);
    if (firstByte == callqOpc) {
      target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
    } else if (firstTwoBytes == callIndOpc) {
      target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +6;
    } else {
      printf("Can't determine call opcode \n");
      std::cout.flush();
      std::exit(EXIT_FAILURE);
    }
    
    printf("INTERPRETER: Exiting model_malloc \n"); 
    std::cout.flush();
  } else {
    printf("INTERPRETER: CALLING MODEL_MALLOC WITH SYMBOLIC ARGS.  NOT IMPLMENTED \n");
    target_ctx_gregs_OS->print();
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

    int filedes = (int) target_ctx_gregs[REG_RDI]; //fd
    //target_ctx_gregs[REG_RSI]; //address of dest buf  NOT USED YET
    //target_ctx_gregs[REG_RDX]; //max len to read in NOT USED YET
    
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

    int filedes = target_ctx_gregs[REG_RDI]; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI]; //address of dest buf 
    size_t size = target_ctx_gregs[REG_RDX]; //max len to read in
    
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
    target_ctx_gregs[REG_RIP] += 5;
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

    int filedes = target_ctx_gregs[REG_RDI]; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI]; //address of dest buf 
    size_t size = target_ctx_gregs[REG_RDX]; //max len to read in
    
    if (enableMultipass) {

      //BEGIN NEW VERIFICATION STAGE =====>
      //clear old multipass assignment info.

      multipassInfo.prevMultipassAssignment.clear();
      multipassInfo.currMultipassAssignment.clear();
      multipassInfo.messageNumber++;
      multipassInfo.passCount = 0;
      multipassInfo.roundRootPID = getpid();
      
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

    uint64_t numSymBytes = tls_predict_stdin_size(filedes, size);
    
    if (numSymBytes <= size) // redundant based on tls_predict_stdin_size's logic.
      tase_make_symbolic((uint64_t) buffer, numSymBytes, "stdinRead");
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

//http://man7.org/linux/man-pages/man2/signal.2.html
// We're modeling a client that receives no signals, so just
// bump RIP for now.
void Executor::model_signal() {
  printf("Entering model_signal \n");

  target_ctx_gregs[REG_RIP] += 5;

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

    int filedes = (int)  target_ctx_gregs[REG_RDI]; //fd
    void * buffer = (void *) target_ctx_gregs[REG_RSI]; //address of dest buf 
    size_t size = (size_t) target_ctx_gregs[REG_RDX]; //max len to read in
    
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

    
    target_ctx_gregs[REG_RIP] += 5;

    printf("Returning from model_write \n");

    if (test_type == EXPLORATION) {
      return;
    } else if (test_type == VERIFICATION) {
    
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
  int nfds = (int) target_ctx_gregs[REG_RDI]; // int nfds
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

    int socket = (int) target_ctx_gregs[REG_RDI];
    struct sockaddr * addr = (struct sockaddr *) target_ctx_gregs[REG_RSI];
    socklen_t length = (socklen_t) target_ctx_gregs[REG_RDX];

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
    target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;   
    
  } else {
     printf("ERROR: Found symbolic input to model_connect()");
     std::exit(EXIT_FAILURE);
  }
}
