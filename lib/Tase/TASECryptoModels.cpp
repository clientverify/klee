

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

extern int loopCtr;

#include "/playpen/humphries/TASE/TASE/test/tase/include/tase/tase_interp.h"
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

//Multipass
extern multipassRecord multipassInfo;
extern KTestObjectVector ktov;
extern bool enableMultipass;
extern void spinAndAwaitForkRequest();

//Todo : fix these functions and remove traps

#ifdef TASE_OPENSSL

extern "C" {
  void RAND_add(const void * buf, int num, double entropy);
  int RAND_load_file(const char *filename, long max_bytes);
}
#include "/playpen/humphries/TASE/TASE/openssl/include/openssl/lhash.h"
#include "/playpen/humphries/TASE/TASE/openssl/include/openssl/crypto.h"
#include "/playpen/humphries/TASE/TASE/openssl/include/openssl/objects.h"
#include "/playpen/humphries/TASE/TASE/openssl/include/openssl/bio.h"
#include "/playpen/humphries/TASE/TASE/openssl/include/openssl/ssl.h"
#include "/playpen/humphries/TASE/TASE/openssl/crypto/x509/x509_vfy.h"
#include "/playpen/humphries/TASE/TASE/openssl/crypto/err/err.h"

extern "C" {
  int ssl3_connect(SSL *s);
  
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
    //It's OK; struct holds no pointers.
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


//int RAND_load_file(const char *filename, long max_bytes);
void Executor::model_RAND_load_file() {
  printf("Entering model_RAND_load_file \n");
  std::cout.flush();

  //Perform the call
  //int res = RAND_load_file((char *) target_ctx_gregs[REG_RDI], (long) target_ctx_gregs[REG_RSI]);
  int res = 1024;
  ref<ConstantExpr> resExpr = ConstantExpr::create((uint64_t) res, Expr::Int64);
  target_ctx_gregs_OS->write(REG_RAX * 8, resExpr);
  
  //Fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP]);
  target_ctx_gregs[REG_RIP] = retAddr;
  target_ctx_gregs[REG_RSP] += 8;
  printf("Exiting model_RAND_load_file \n");
  std::cout.flush();

}

void Executor::model_RAND_add() {

  printf("Entering model_RAND_add \n");
  std::cout.flush();
  //RAND_add((void *) target_ctx_gregs[REG_RDI], (int) target_ctx_gregs[REG_RSI], 0);

  //fake a ret
  uint64_t retAddr = *((uint64_t *) target_ctx_gregs[REG_RSP]);
  target_ctx_gregs[REG_RIP] = retAddr;
  target_ctx_gregs[REG_RSP] += 8;
}


//TODO: Determine if this is always a 4 byte return value in eax
//or if it's ever an 8 byte return into rax.
void Executor::model_random() {

  static int timesRandomIsCalled = 0;
  timesRandomIsCalled++;

  printf("INTERPRETER: calling model_random() \n");   

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
  
  target_ctx_gregs_OS->write(REG_RAX * 8,psnRand );
  
  /*
  //TODO: Make this more robust later for call instructions with more than 5 bytes.
  //TODO: Add asserts to make sure RIP and RSP aren't symbolic.
  */
  target_ctx_gregs[REG_RIP] = target_ctx_gregs[REG_RIP] +5;
  //printf("INTERPRETER: Exiting model_random \n");
  
  //printf("Ctx after modeling is ... \n");
  //printCtx(target_ctx_gregs);  
  
}
#endif
