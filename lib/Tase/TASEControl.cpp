#include <stdio.h>
#include <fcntl.h>
#include <sys/sem.h>
#include <cstdlib>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <iostream>
#include <sys/prctl.h>
#include <time.h>
#include "klee/CVAssignment.h"
//#include "/playpen/humphries/zTASE/TASE/test/tase/include/tase/tase_interp.h"
#include "/playpen/humphries/zTASE/TASE/klee/lib/Core/Executor.h"

#include "klee/Internal/System/Time.h"


using namespace llvm;
using namespace klee;

extern std::stringstream workerIDStream;
extern bool dontFork;
extern bool taseDebug;
extern bool workerSelfTerminate;
extern void deserializeAssignments ( void * buf, int bufSize, klee::Executor * exec, CVAssignment * cv);

extern double interpreter_time;
extern double interp_setup_time;
extern double interp_run_time;
extern double interp_cleanup_time;
extern double interp_find_fn_time;
extern double mdl_time;
extern double psn_time;


//Perf debugging
extern double target_start_time;
extern uint64_t total_interp_returns;

typedef struct WorkerInfo {
  int pid;
  int branches;
  int round;
  int pass;
  int parent;
} WorkerInfo;


int QR_BYTE_LEN = 4096;
int QA_BYTE_LEN = 4096;
int MAX_WORKERS = 10;

bool taseManager = false;
int roundCount = 0;
int passCount = 0; //Pass ctr for current round of verification
int tase_branches = 0;
int multipassAssignmentSize = 16092;  //Totally arbitrary. Size of mmap'd multipass assignment buffer.
void * MPAPtr;  //Ptr to serialized multipass info for current round of verification
int * replayPIDPtr; //Ptr to the pid storing the replay PID for the current round of verification
int * replayLock ; //Lock to request replay.  1 is available, 0 unavailable.

int * latestRoundPtr; //Pointer to furthest round found so far in verification
int managerRoundCtr = 1;

CVAssignment  prevMPA;

void * ms_base;
int ms_size = 16384;

void * ms_QR_base;
int * ms_QR_size_ptr; //Pointer to num of workers in QR, not size in bytes
void * ms_QA_base;
int * ms_QA_size_ptr; //Pointer to num of workers in QA, not size in bytes

int * target_started_ptr;

int semID; //Global semaphore ID for sync

struct sembuf sem_lock = {0, -1, 0 | SEM_UNDO}; // {sem index, inc/dec, flags}
struct sembuf sem_unlock = {0, 1, 0 | SEM_UNDO};// SEM_UNDO added to release
//lock if process dies.

void get_sem_lock () {
  int res =  semop(semID, &sem_lock, 1);
  if (res == 0) {
    return;
  }
  else {
    printf("Error getting sem lock \n");
    std::cout.flush();
    perror("Error in get_sem_lock");
    std::exit(EXIT_FAILURE);
  }
}

void release_sem_lock () {
  int res = semop(semID, &sem_unlock,1);
  if (res == 0)
    return;
  else {
    perror("Error in release_sem_lock");
    std::exit(EXIT_FAILURE);
  }
}

int initialize_semaphore(int semKey) {
  semID = semget(semKey, 1, IPC_CREAT |IPC_EXCL | 0660 );
  if ( semID == -1) {
    perror("Error creating semaphore ");
    std::exit(EXIT_FAILURE);
  }
  //Todo -- Double check to see if we need to initialize
  //semaphore explicitly to 1.
 int res = semctl(semID, 0, SETVAL, 1);
 if (res == -1) {
   perror("Error initializing semaphore \n");
   std::exit(EXIT_FAILURE);
 }
 return semID;
}

static void printWorkerInfo(WorkerInfo * wi) {
  printf("<pid %d> <branches %d> <round %d> <pass %d> <parent %d> \n", wi->pid, wi->branches, wi->round, wi->pass, wi->parent);
}

static void printQ( int *base_size_ptr, void * basePtr) {
  int size = *base_size_ptr;
  if (size < 0) {
    printf("Error in printQ: size less than zero \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  printf("Printing queue: \n");
  if (size == 0)
    printf("Nothing in queue! \n");
  while (size > 0) {
    size--;
    WorkerInfo * worker = (WorkerInfo *) ( (uint64_t) basePtr + (uint64_t) (size * sizeof(WorkerInfo)));
    printWorkerInfo(worker);
  }
}

static void printQR() {
  printf("Printing QR: \n");
  printQ(ms_QR_size_ptr, ms_QR_base);
}

static void printQA() {
  printf("Printing QA: \n");
  printQ(ms_QA_size_ptr, ms_QA_base);
}

WorkerInfo * PidInQ(int pid, int * QSizePtr, void * basePtr) {
  int Qsize = *QSizePtr;
  for (int i = 0; i < Qsize; i++) {
    WorkerInfo * wi = (WorkerInfo *) ((uint64_t) basePtr + (uint64_t) (i * sizeof(WorkerInfo)) );
    if (wi->pid == pid)
      return wi;
  }
  return NULL;  
}

WorkerInfo * PidInQR(int pid) {
  return PidInQ(pid, ms_QR_size_ptr, ms_QR_base);
}

WorkerInfo * PidInQA(int pid) {
  return PidInQ(pid, ms_QA_size_ptr, ms_QA_base);
}

//Return pointer to worker in queue with earliest round, pass, branch.
WorkerInfo * getEarliestWorker(void * basePtr, int Qsize ) {
  
  //Initialization case
  WorkerInfo * earliest = (WorkerInfo *) (basePtr);
  if (basePtr == NULL || Qsize == 0) {
    fprintf(stderr,"getEarliestWorker called on empty/null queue \n");
    return NULL;
  }
  
  for (int i = 0; i < Qsize; i++) {
    WorkerInfo * currWorker = (WorkerInfo *) ((uint64_t) basePtr + (uint64_t)( i *(sizeof(WorkerInfo))));
    if (currWorker->round < earliest->round) {
      earliest = currWorker; //New earliest
    } else if (currWorker->round == earliest->round) { //Tie on round
      if (currWorker->pass < earliest->pass) { 
	earliest = currWorker; //New earliest
      } /* else if (currWorker->pass == earliest->pass) { //Tie on pass
	   if (currWorker->branches < earliest->branches) 
	   earliest = currWorker; //New earliest
	*/
    }
  }
  return earliest;
}


WorkerInfo * getLatestWorker(void * basePtr, int Qsize) {
  if (taseDebug) {
    printf("Manager calling getLatestWorker \n");
    fflush(stdout);
  }

  //Initialization case
  WorkerInfo * latest = (WorkerInfo *) (basePtr);
  if (basePtr == NULL || Qsize == 0) {
    fprintf(stderr,"getLatestWorker called on empty/null queue \n");
    return NULL;
  }

  for (int i = 0; i < Qsize; i++) {
    WorkerInfo * currWorker = (WorkerInfo *) ((uint64_t) basePtr + (uint64_t)( i *(sizeof(WorkerInfo))));
    if (currWorker->round > latest->round) {
      latest = currWorker; //New latest
    } else if (currWorker->round == latest->round) { //Tie on round
      if (currWorker->pass > latest->pass) {
	latest = currWorker; //New latest
      } /* else if (currWorker->pass == latest->pass) { //Tie on pass
	   if (currWorker->branches > latest->branches)
	   latest = currWorker; //New latest
	*/
    }
  }
  return latest;
}

void removeFromQ(WorkerInfo * worker, int * sizePtr, void * basePtr) {
  
  int size = *sizePtr;
  WorkerInfo * found = PidInQ(worker->pid, sizePtr, basePtr);
  if (found == NULL) {
    fprintf(stderr,"Error: Trying to remove pid %d not in Q \n", worker->pid);
    std::exit(EXIT_FAILURE);
  }
  memset((void *) found, 0, sizeof(WorkerInfo));
  //Slide values down
  WorkerInfo * edgePtr = (WorkerInfo *) ((uint64_t) basePtr + (size -1) * sizeof(WorkerInfo));
  WorkerInfo * searchPtr = found;
  WorkerInfo * tmpPtr;
  while (searchPtr != edgePtr) {
    tmpPtr = searchPtr;
    searchPtr++;
    memcpy ((void *) (tmpPtr), searchPtr, sizeof(WorkerInfo));
  }
  memset((void *) searchPtr, 0, sizeof(WorkerInfo));
  *sizePtr = *sizePtr -1;
}

void removeFromQR(WorkerInfo * worker) {

  if (worker == NULL) {
    fprintf(stderr,"ERROR: calling removeFromQR on null ptr \n");
    fflush(stdout);
    std::exit(EXIT_FAILURE);
  }
  removeFromQ(worker, ms_QR_size_ptr, ms_QR_base);

}

void removeFromQA(WorkerInfo * worker) {
  if (worker == NULL) {
    fprintf(stderr,"ERROR: calling removeFromQA on null ptr \n");
    fflush(stdout);
    std::exit(EXIT_FAILURE);
  }
  removeFromQ(worker, ms_QA_size_ptr, ms_QA_base);
}

void addToQ(WorkerInfo * worker, int * sizePtr, void * basePtr) {
  void * dst = (void *) ((uint64_t) basePtr + (*sizePtr) * sizeof(WorkerInfo));
  void * src = (void *) worker;
  memcpy(dst ,  src , sizeof(WorkerInfo));
  *sizePtr = *sizePtr +1;
}

void addToQR(WorkerInfo * worker) {
  addToQ(worker, ms_QR_size_ptr, ms_QR_base);
}

void addToQA(WorkerInfo * worker) {
  addToQ(worker, ms_QA_size_ptr, ms_QA_base);
}

void QRtoQA(WorkerInfo * worker) {
  WorkerInfo tmp;  
  memcpy ((void *) &tmp, (void *) worker, sizeof(WorkerInfo));
  removeFromQR (worker);
  addToQA(&tmp);
}

void QAtoQR(WorkerInfo * worker) {
  WorkerInfo tmp;
  memcpy((void *) &tmp, (void *) worker, sizeof(WorkerInfo));
  removeFromQA (worker);
  addToQR(&tmp);
}


void select_workers () {

  if (*ms_QR_size_ptr == MAX_WORKERS) {

    //Get earliest worker by round, pass, branch

    WorkerInfo * earliestWorkerQR = getEarliestWorker(ms_QR_base, *ms_QR_size_ptr);

    if (earliestWorkerQR->round < managerRoundCtr ) {
      int res = kill(earliestWorkerQR->pid, SIGKILL);
      if (res == -1) {
	perror("Error sigstopping in select_workers \n");
	fprintf(stderr, "Issue delivering SIGSTOP to pid %d \n", earliestWorkerQR->pid);
      } else {
	printf("Manager kicking pid from QR %d; pid is in round %d when latest round is %d \n", earliestWorkerQR->pid, earliestWorkerQR->round, managerRoundCtr);
      }
      QRtoQA(earliestWorkerQR);
    }
    
    /*
    int res = kill(earliestWorker->pid, SIGSTOP);
    if (taseDebug) {
      printf("Manager starting waitpid call for QRtoQA  \n");
      fflush(stdout);
    }
    while (true) {
      int status;
      int res = waitpid(earliestWorker->pid, &status, WUNTRACED);
      if (WIFSTOPPED(status)) {
	QRtoQA(earliestWorker);
	break;
      } else if (WIFEXITED(status)) {
	removeFromQR(earliestWorker);
      }
    }
    */
  }

  if (*ms_QR_size_ptr < MAX_WORKERS && *ms_QA_size_ptr != 0) {

    printQA();
    printQR();

    WorkerInfo * latestWorker = getLatestWorker(ms_QA_base, *ms_QA_size_ptr);
    /*
    int check = waitpid(latestWorker->pid, &check, WUNTRACED | WCONTINUED | WNOHANG);
    if (check == -1){
      perror("Error in dummy check \n ");
      printf("Error in dummy check for pid %d \n ", latestWorker->pid);
    } else {
      printf("Dummy check appears to be ok for pid %d \n", latestWorker->pid);
    }
    if (WIFSTOPPED(check))
      printf("DBG: pid is stopped \n");
    if(WIFCONTINUED(check))
      printf("DBG: pid is continued \n");
    if (WIFEXITED(check))
      printf("DBG: pid is exited \n");
    fflush(stdout);
    */
    if (latestWorker->round < managerRoundCtr && *ms_QR_size_ptr > 0) {
      printf("No workers in QA in latest round %d.  Not moving to QR. \n", managerRoundCtr);
    } else {
      int res = kill(latestWorker->pid, SIGCONT);
      if (res == -1){
	perror("Error during kill sigcont \n");
	printf("Error during kill sigcont \n");
	fflush(stdout);
      } 
      //printf("Manager starting waitpid call for QAtoQR on pid %d \n", latestWorker->pid);
      //fflush(stdout);
      //while (true) {
      // int status =0;
      // res = waitpid(latestWorker->pid,&status, WUNTRACED | WCONTINUED );
      //Todo -- comment check back in
      //if (res == -1)
      //perror("ERROR: manager can't waitpid");
      //if (WIFCONTINUED(status)) {
      QAtoQR(latestWorker);
      //break;
      //} else if (WIFEXITED(status)) {
      //removeFromQA(latestWorker);
      //break;
      //}
      //printf("status after waitpid: %d \n", status);
      //}
      //printf("Manager finished waitpid call for QAtoQR \n");
    }
  }
}


void manage_workers () {
  get_sem_lock();
  if (managerRoundCtr < *latestRoundPtr) {
    managerRoundCtr = *latestRoundPtr;
    double curr_time = util::getWallTime();
    double diff = curr_time - target_start_time;
    fprintf(stderr,"Manager sees new round %d starting at time %lf \n", managerRoundCtr, diff);    
  }

  //Exit case
  if (*ms_QA_size_ptr == 0 && *ms_QR_size_ptr == 0 && *target_started_ptr == 1) {
    //fprintf(stderr,"Manager found empty QA and QR \n");
    //std::exit(EXIT_SUCCESS);
  }
  
  if (*ms_QR_size_ptr > MAX_WORKERS) {
    fprintf(stderr,"ERROR: found more workers than expected in manage_workers \n");
    std::exit(EXIT_FAILURE);
  }

  select_workers();
  release_sem_lock();
}

//Todo -- Any more cleanup needed?
void worker_exit() {
  if (taseManager != true) {
    printf("WARNING: worker_exit called without taseManager \n");
    std::cout.flush();
    std::exit(EXIT_SUCCESS);
  } else {    
    printf("Worker %d attempting to exit and remove self from QR \n", getpid());
    std::cout.flush();
    get_sem_lock();
    removeFromQR(PidInQR(getpid()));
    release_sem_lock();
    std::exit(EXIT_SUCCESS);
  }
}

int tase_fork(int parentPID, uint64_t rip) {
  tase_branches++;
  double curr_time = util::getWallTime();
  printf("PID %d entering tase_fork at rip 0x%lx %lf seconds after analysis started \n", parentPID, rip, curr_time - target_start_time);
  
  std::cout.flush();
  if (dontFork) {
    printf("Forking is disabled.  Shutting down \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
  if (taseManager) {
    get_sem_lock();
    printf("TASE FORKING! \n");
    if (roundCount < *latestRoundPtr && workerSelfTerminate)  {
      printf("Worker %d is in round %d when latest round is %d. Worker exiting. \n", getpid(), roundCount, *latestRoundPtr );
      fflush(stdout);
      removeFromQR(PidInQR(getpid()));
      release_sem_lock();
      std::exit(EXIT_SUCCESS);
    }
    int trueChildPID = ::fork();
    if (trueChildPID == -1) {
      printf("Error during forking \n");
      perror("Fork error \n");
    }
    if (trueChildPID != 0) {
      printf("Parent PID %d forked off child %d at rip 0x%lx for TRUE branch \n", parentPID, trueChildPID, rip);
      fflush(stdout);
      //Block until child has sigstop'd.
      while (true) {
	int status;
	int res = waitpid(trueChildPID, &status, WUNTRACED);
	if (WIFSTOPPED(status))
	  break;
      }
      WorkerInfo wi;
      wi.pid = trueChildPID;
      wi.round = roundCount;
      wi.pass  = passCount;
      wi.branches = tase_branches;
      wi.parent = parentPID;
      addToQA(&wi);
    } else  {     
      raise(SIGSTOP);


      if (taseDebug) {
	get_sem_lock();
	if (PidInQR(getpid()) == NULL)
	  fprintf(stderr,"Error: Fork pid %d running but not in QR ", getpid());
	release_sem_lock();
      }

      
      return 1;// Go back to path exploration
    } 

    int falseChildPID = ::fork();
    if (falseChildPID == -1) {
      printf("Error during forking \n");
      perror("Fork error \n");
    }
    if (falseChildPID != 0) {
      printf("Parent PID %d forked off child %d at rip 0x%lx for FALSE branch \n", parentPID, falseChildPID, rip );
      fflush(stdout);
      //Block until child has sigstop'd
      while (true) {
	int status;
	int res = waitpid(falseChildPID, &status, WUNTRACED);
	if (res == -1)
	  perror("Waitpid error in tase_fork()");
	if (WIFSTOPPED(status))
	  break;
      }
      
      WorkerInfo wi;
      wi.pid = falseChildPID;
      wi.round = roundCount;
      wi.pass = passCount;
      wi.branches = tase_branches;
      wi.parent = parentPID;
      addToQA(&wi);
    } else {
      raise(SIGSTOP);
      if (taseDebug) {
	get_sem_lock();
	if (PidInQR(getpid()) == NULL)
	  fprintf(stderr,"Error: Fork pid %d running but not in QR ", getpid());
	release_sem_lock();
      }
      
      return 0; //Go back to path exploration
    }    
    printQR();
    printQA();
    printf("control debug: Parent PID %d exiting tase_fork after producing child PIDs %d (true) and %d (false) from rip 0x%lx \n", parentPID, trueChildPID, falseChildPID, rip);
    printf("Exiting tase_fork \n");
    fflush(stdout);
    removeFromQR(PidInQR(getpid()));
    release_sem_lock();
    std::exit(EXIT_SUCCESS);
  } else {
    int pid = ::fork();
    return pid;
  }
}

void tase_exit() {
  if (taseManager) {
    get_sem_lock();
    removeFromQR(PidInQR(getpid()));
    printf("PID %d calling tase_exit \n", getpid());  
    std::cout.flush();
    release_sem_lock();
    std::exit(EXIT_SUCCESS); //Releases semaphore
  } else {
    printf("PID %d calling tase_exit \n", getpid());

    std::cout.flush();
    std::exit(EXIT_SUCCESS);
  }
}

void initManagerStructures() {
   
   initialize_semaphore(getpid());
   ms_base = mmap(NULL, ms_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);

   ms_QR_base =  ( (int *) ms_base) + 1024;
   ms_QR_size_ptr = (int *)(ms_QR_base -4);
   *ms_QR_size_ptr = 0;
   
   ms_QA_base =  ((int*) ms_base) + 2048;
   ms_QA_size_ptr = (int *)(ms_QA_base -4);
   *ms_QA_size_ptr = 0;

   int res = prctl(PR_SET_CHILD_SUBREAPER, 1);
   if (res == -1) {
     perror("Subreaper err ");
     fprintf(stderr, "Exiting due to reaper error in initManagerStructures \n");
     std::exit(EXIT_FAILURE);
   }
     
   target_started_ptr = ((int *) ms_base) + 3072;
   *target_started_ptr = 0; //Switches to 1 when we execute target

   latestRoundPtr = ((int *) ms_base) + 100;
   *latestRoundPtr = 0;
 }

//Fill a buffer with string for the time
void TASE_get_time_string (char * buf) {
  time_t theTime;
  time(&theTime);

  struct tm * tstruct= localtime(&theTime);
  strftime(buf, 80, "%T",tstruct);
  
}

void multipass_start_round (klee::Executor * theExecutor, bool isReplay) {
  printf("Hit top of multipass_start_round \n");
  std::cout.flush();
  get_sem_lock();

  if (roundCount < *latestRoundPtr  && workerSelfTerminate)  {
    removeFromQR(PidInQR(getpid()));
    printf("Worker %d is in round %d when latest round is %d. Worker exiting. \n", getpid(), roundCount, *latestRoundPtr );
    fflush(stdout);
    std::exit(EXIT_SUCCESS);
  } else {
    printf("Worker sees latest round as %d; updating to %d \n", *latestRoundPtr, roundCount);
    *latestRoundPtr = roundCount;
  }

  printf("IMPORTANT: Starting round %d pass %d of verification \n", roundCount, passCount);
  std::cout.flush();
  //Make backup of self
  int childPID = ::fork();
  if (childPID == -1) {
    printf("Error during forking \n");
    perror("Fork error \n");
  }
  if (childPID != 0) {
    *replayPIDPtr = childPID;
    //Block until child has sigstop'd
    while (true) {
      int status;
      int res = waitpid(childPID, &status, WUNTRACED);
      if (res == -1)
	  perror("Waitpid error in multipass_start_round()");
      if (WIFSTOPPED(status))
	break;
    }    

    if (isReplay) {

      //Pick up roundCount and passcount
      WorkerInfo * myInfo = PidInQR(getpid());
      if (myInfo != NULL) {
	roundCount = myInfo->round;
	passCount = myInfo->pass;
      } else {
	printf("ERROR: could not find self in QR \n");
	fflush(stdout);
	std::exit(EXIT_FAILURE);
      }
      
      //Pickup assignment info if necessary
      if (*(uint8_t *) MPAPtr != 0) {
	printf("Attempting to deserialize MP Assignments \n");
	std::cout.flush();
	if (*replayLock != 0)
	  printf("IMPORTANT: Error: control debug - replay lock has value %d in multipass_start_round \n", *replayLock);
	std::cout.flush();
	prevMPA.clear();
	deserializeAssignments(MPAPtr, multipassAssignmentSize, theExecutor, &prevMPA);
	memset(MPAPtr, 0, multipassAssignmentSize); //Wipe out the multipass assignment to be safe
	printf("Printing assignments AFTER deserialization \n");
	prevMPA.printAllAssignments(NULL);
	*replayLock = 1;
	
      } else {
	printf( "IMPORTANT: ERROR: control debug: deserializing from empty buf 0x%lx for replay pid %d  \n", (uint64_t) MPAPtr,  getpid());
	std::cout.flush();
      }
    }
    std::cout.flush();
    release_sem_lock();
  } else {    
    raise(SIGSTOP);

    int i = getpid();
    workerIDStream << ".";
    workerIDStream << i;
    std::string pidString ;
    pidString = workerIDStream.str();
    if (pidString.size() > 250) {
      printf("Cycling log name due to large size \n");
      workerIDStream.str("");
      workerIDStream << "Monitor.Wrapped.";
      workerIDStream << i;
      pidString = workerIDStream.str();
      printf("Cycled log name is %s \n", pidString.c_str());
    }


    printf("Before freopen, new string for log is %s \n", pidString.c_str());
    
    FILE * res = freopen(pidString.c_str(),"w",stdout);

    if (res == NULL) {
      printf("ERROR: Couldn't open file for new replay pid %d \n", i);
      perror("Error opening file during replay");
      fprintf(stderr, "ERROR opening new file for child process logging for pid %d \n", i);
      worker_exit();
    } else 
      printf("Worker %d opened file for logging \n", i);

    printf("Resetting interp time counters  %lf seconds after analysis began \n", util::getWallTime() - target_start_time );
    interpreter_time = 0.0;
    interp_setup_time = 0.0;
    interp_run_time = 0.0;
    interp_cleanup_time = 0.0;
    interp_find_fn_time = 0.0;
    
    mdl_time = 0.0;
    psn_time = 0.0;
    
    fflush(stdout);
    static int ctr = 0;
    while (true) {
      printf("Trying to get sem lock \n");
      fflush(stdout);
      get_sem_lock(); 
      if ( (PidInQR(getpid()) !=NULL) && *replayLock == 0) {
	break;
      } else {
	ctr++;
	if(PidInQR(i) == NULL && ctr == 1)
	  fprintf(stderr,"Error: replay pid %d running but not in QR \n", getpid());
	release_sem_lock();
      }
    }
    
    char buf [80];
    TASE_get_time_string(buf);

    double curr_time = util::getWallTime();
    double elapsed_time = curr_time - target_start_time;
    
    printf("IMPORTANT: control debug:  replaying round %d for pass %d at time %s with replay pid %d.  %lf seconds since analysis started \n", roundCount, passCount, buf, getpid(), elapsed_time );
    if(!(PidInQR(i) != NULL)) 
      printf("IMPORTANT: Error: control debug: Process %d executing without pid in QR at time %s \n", i, buf);
    std::cout.flush();
    release_sem_lock();
    
    multipass_start_round(theExecutor, true);
    printf("Returned from multipass_start_round \n");
    std::cout.flush();
  }    
}


void multipass_replay_round (void * assignmentBufferPtr, CVAssignment * mpa, int * pidPtr)  {

  printf("Entering multipass_replay_round \n");
  fflush(stdout);
  
  while(true) {//Is this actually needed?  get_sem_lock() should block until semaphore is available
 
    get_sem_lock();
    if (roundCount < *latestRoundPtr && workerSelfTerminate)  {
      removeFromQR(PidInQR(getpid()));
      printf("Worker %d is in round %d when latest round is %d. Worker exiting. \n", getpid(), roundCount, *latestRoundPtr );
      fflush(stdout);
      release_sem_lock();
      std::exit(EXIT_SUCCESS);
    }
    printf("Value of replay lock is %d \n", *replayLock);
    if (PidInQA(*pidPtr) != NULL)
      printf("replay pid %d is in QA \n", *pidPtr);
    else
      printf("replay pid %d isn't in QA \n", *pidPtr);

    if (PidInQR(*pidPtr) != NULL)
      printf("replay pid %d is in QR \n", *pidPtr);
    else
      printf("replay pid %d isn't in QR \n", *pidPtr);

    if (*((uint8_t *) assignmentBufferPtr) != 0) 
      printf("Assignment buf is not zero \n");
    else
      printf("Assignment buf is zero \n");
    fflush(stdout);
    
    if (*replayLock != 1  || (PidInQA(*pidPtr) != NULL) || (PidInQR(*pidPtr) != NULL) ||  *((uint8_t *) assignmentBufferPtr) != 0)  {
      release_sem_lock();  //Spin and try again after pending replay fully executes

    } else {
      
      if (*replayLock != 1) {
	printf("IMPORTANT: control debug: Error - replayLock has unexpected value %d \n", *replayLock);
      } else {
	*replayLock = 0;
	if (PidInQA(*pidPtr) != NULL)
	  printf("ERROR: control debug: replay pid is somehow already in QA \n");

	double curr_time = util::getWallTime();
	double elapsed_time = curr_time - target_start_time;
	
	printf("mp_replay_round: control debug: replayLock obtained. Inserting replayPid %d into QA.  %lf seconds elapsed since target analysis started \n", *pidPtr,  elapsed_time);
	std::cout.flush();
      }
      std::cout.flush();
      WorkerInfo wi;
      wi.pid = *pidPtr;
      wi.round = roundCount;
      wi.pass = passCount + 1;
      wi.branches = tase_branches;
      wi.parent = getpid();
      addToQA(&wi);  //Move replay child pid into QA
      if ( *((uint8_t *) assignmentBufferPtr) != 0) { //Move latest assignments into shared mem
	printf("ERROR: control debug: see data in assignment buffer when trying to serialize new constraints \n");
	std::cout.flush();
      }
      printf("Printing assignments BEFORE serialization \n");
      mpa->printAllAssignments(NULL);
      double elapsed_time = util::getWallTime() - target_start_time;
      printf(" control debug: Serializing to buf 0x%lx at time %lf for replay pid %d in round %d \n", (uint64_t) assignmentBufferPtr, elapsed_time, *pidPtr, roundCount);
      mpa->serializeAssignments(assignmentBufferPtr, multipassAssignmentSize);
      std::cout.flush();
      removeFromQR(PidInQR(getpid()));
      release_sem_lock();
      std::exit(EXIT_SUCCESS);
    }
  }
}

void  multipass_reset_round() {

  passCount = 0;
  roundCount++;
  MPAPtr = mmap(NULL, multipassAssignmentSize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
  printf("control debug: MPAPtr is located at 0x%lx for round %d \n", (uint64_t) MPAPtr, roundCount);
  std::cout.flush();
  memset(MPAPtr, 0, multipassAssignmentSize);
  replayPIDPtr = (int *) mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
  *replayPIDPtr = 0;
  replayLock = (int *) mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
  *replayLock = 1;
}
