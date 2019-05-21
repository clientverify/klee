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
#include "/playpen/humphries/zTASE/TASE/klee/lib/Core/Executor.h"

extern std::stringstream workerIDStream;
extern bool dontFork;
extern void deserializeAssignments ( void * buf, int bufSize, klee::Executor * exec, CVAssignment * cv);


int QR_BYTE_LEN = 4096;
int QA_BYTE_LEN = 4096;
int MAX_WORKERS = 32;

bool taseManager = false;
int roundCount = 0;
int passCount = 0; //Pass ctr for current round of verification
int multipassAssignmentSize = 16092;  //Totally arbitrary. Size of mmap'd multipass assignment buffer.
void * MPAPtr;  //Ptr to serialized multipass info for current round of verification
int * replayPIDPtr; //Ptr to the pid storing the replay PID for the current round of verification
int * replayLock ; //Lock to request replay.  1 is available, 0 unavailable.

int * latestRoundPtr; //Pointer to furthest round found so far in verification

CVAssignment  prevMPA;

void * ms_base;
int ms_size = 16384;

int * ms_QR_base;
int * ms_QR_size_ptr;
int * ms_QA_base;
int * ms_QA_size_ptr;

int * target_started_ptr;

int semID; //Global semaphore ID for sync

struct sembuf sem_lock = {0, -1, 0 | SEM_UNDO}; // {sem index, inc/dec, flags}
struct sembuf sem_unlock = {0, 1, 0 | SEM_UNDO};// SEM_UNDO added to release
//lock if process dies.

void get_sem_lock () {
  int res =  semop(semID, &sem_lock, 1);
  if (res == 0) {
    //printf("Obtained sem lock \n");
    std::cout.flush();
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

static void printQ( int *base_size_ptr, int * base_ptr) {
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
    printf("%d\n", *(base_ptr + size));
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

bool PidInQR(int pid) {
  int QR_size = *ms_QR_size_ptr;

  for (int i = 0; i < QR_size; i++) {
    if ( *(ms_QR_base + i) == pid)
      return true;
  }
  return false;  
}

bool PidInQA(int pid) {
  int QA_size= *ms_QA_size_ptr;
  for (int i = 0; i < QA_size ; i++) {
    if ( *(ms_QA_base +i)  == pid)
      return true;
  }
  return false;
}

/* MUST be called ONLY when lock is obtained */
static void remove_dead_workers (int * QR_size_ptr, int * QR_base_ptr) {
  printf("Entering remove_dead_workers \n");
  int QR_index = *QR_size_ptr;
  int valid_QR_size = *QR_size_ptr;
  printf("DEBUG: QR size is %d \n", QR_index);
  printf("DEBUG: Before remove_dead_workers, QR is \n");
  printQR();

  //Check each worker in QR
  while (QR_index > 0) {
    QR_index--;
    int * curr_entry_ptr = QR_base_ptr + QR_index;
    int pid = *curr_entry_ptr;
    int res;

    printf("Checking pid %d \n", pid);
    
    res = kill(pid,0);
    printf("res is %d \n", res);
    
    if (res == -1) {
      printf("Found exited pid %d \n",pid);
      int * index_ptr = valid_QR_size + QR_base_ptr -1;
      while (index_ptr != curr_entry_ptr) {
	*(index_ptr -1) = *index_ptr;
	index_ptr--;
      }
      valid_QR_size--;
    }
  }

  printf("After remove_dead_workers valid_QR_size is %d \n",valid_QR_size);
  *QR_size_ptr = valid_QR_size;
  printQR();
  return;
}

void manage_workers () {
  get_sem_lock();
  
  int QA_size_init = *ms_QA_size_ptr;
  int QR_size_init = *ms_QR_size_ptr;

  //Exit case
  if (QA_size_init == 0 && QR_size_init == 0 && *target_started_ptr == 1) {
    printf("Manager found empty QA and QR \n");
    std::cout.flush();

    release_sem_lock();
    std::exit(EXIT_SUCCESS);

  }
  
  if (QR_size_init > MAX_WORKERS) {
    printf("ERROR: found more workers than expected in manage_workers \n");
    std::exit(EXIT_FAILURE);
  }
  
  //printf("Entering manage_workers --\n");
  //printQR();
  //printQA();
  
  //remove_dead_workers(ms_QR_size_ptr,ms_QR_base); For now, depend on tase_exit
  
  int QR_size_updated = *ms_QR_size_ptr;
  
  if (QR_size_updated < MAX_WORKERS) {
    
    if (QA_size_init > 0 ) {
      //grab another process and run it
      int newWorkerPID = *(ms_QA_base + QA_size_init  -1);
      *ms_QA_size_ptr = QA_size_init -1;
      printf("control debug: Manager moving pid %d from QA into QR \n", newWorkerPID);
      std::cout.flush();
      
      int res = kill(newWorkerPID,SIGCONT);//May need to stick in a loop
      if (res == -1) {
	printf("Trying to sigcont %d \n",newWorkerPID);
	std::cout.flush();
	perror("ERROR: manager can't sigcont ");
	std::exit(EXIT_FAILURE);
      }

      printf("Manager starting waitpid call \n");
      std::cout.flush();
      while (true) {
	int status;
	res = waitpid(newWorkerPID,&status, WUNTRACED | WCONTINUED );
	if (res == -1)
	  perror("ERROR: manager can't waitpid");
	if (WIFCONTINUED(status) || WIFEXITED(status))
	  break;
      }
      printf("Manager finished waitpid call \n");
      std::cout.flush();
      //Add new worker to QR
      *(ms_QR_base + QR_size_updated) = newWorkerPID;
      *ms_QR_size_ptr = QR_size_updated + 1;    
    }
  }

  if (*target_started_ptr == 1) {
    //printf("Manager sees %d in QR, %d in QA \n", *ms_QR_size_ptr, *ms_QA_size_ptr);
    std::cout.flush();
    if (*ms_QR_size_ptr == 0 && *ms_QA_size_ptr == 0) {
      printf("Manager sees empty queues \n");
      std::cout.flush();
      std::exit(EXIT_SUCCESS);
    }
  }
    
  //printf("Leaving manage_workers -- \n");
  //printQR();
  //printQA();
  
  release_sem_lock();
}

static void remove_self_from_QR () {

    //First, find self in QR
    int * searchPtr = ms_QR_base;
    int mypid = getpid();
    int i = 0;
    while (i < QR_BYTE_LEN) {
      if (*searchPtr == mypid)
	break;
      else {
	i+= sizeof(int);
	searchPtr++;
      }
    }
    if (i >= QR_BYTE_LEN){
      printf("ERROR: tase_fork couldn't find self \n");
      std::exit(EXIT_FAILURE);
    }

    //Remove self
    int QR_size = * ms_QR_size_ptr;
    int * QR_edge_ptr = (int *) ms_QR_base + QR_size -1;
    *searchPtr = 0;

    //Slide values down stack
    while (searchPtr < QR_edge_ptr) {
      *searchPtr = *(searchPtr +1);
      searchPtr++;
    }

    //Update size of QR
    QR_size--;
    *ms_QR_size_ptr = QR_size;
}

void QA_insert_PID(int PID) {

  int QA_size= *ms_QA_size_ptr;
  QA_size++;
  if (QA_size * sizeof(int) > QA_BYTE_LEN) {
    printf("FATAL ERROR: Too many process in QA \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  *(ms_QA_base + QA_size -1) = PID;
  *ms_QA_size_ptr = QA_size;
  

}

//Todo -- Any more cleanup needed?
void worker_exit() {
  if (taseManager != true) {
    printf("WARNING: worker_exit called without taseManager \n");
    std::cout.flush();
  } else {
    
    get_sem_lock();
    
        
    remove_self_from_QR();
    fflush(stdout);
    std::exit(EXIT_SUCCESS);//Releases semaphore
  }
}

int tase_fork(int parentPID, uint64_t rip) {
  printf("PID %d entering tase_fork at rip 0x%lx \n", parentPID, rip);
  std::cout.flush();
  if (dontFork) {
    printf("Forking is disabled.  Shutting down \n");
    std::cout.flush();
    std::exit(EXIT_FAILURE);
  }
  
  if (taseManager) {
    
    get_sem_lock();
    printf("TASE FORKING! \n");

    if (roundCount < *latestRoundPtr)  {
      remove_self_from_QR();
      printf("Worker %d is in round %d when latest round is %d. Worker exiting. \n", getpid(), roundCount, *latestRoundPtr );
      fflush(stdout);
      std::exit(EXIT_SUCCESS);
    }

    
    int trueChildPID = ::fork();
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
      QA_insert_PID(trueChildPID);

    } else  {
     
      raise(SIGSTOP);
      return 1;// Go back to path exploration
    } 

    int falseChildPID = ::fork();
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

      QA_insert_PID(falseChildPID);

    } else {

      raise(SIGSTOP);
      return 0; //Go back to path exploration
    }
    
    //Remove self from running queue

    printQR();
    remove_self_from_QR();
    printf("control debug: Parent PID %d exiting tase_fork after producing child PIDs %d (true) and %d (false) from rip 0x%lx \n", parentPID, trueChildPID, falseChildPID, rip);
    printf("Exiting tase_fork \n");
    std::cout.flush();
    //Exit, and release semaphore
    std::exit(EXIT_SUCCESS);
    
  } else {
    int pid = ::fork();
    return pid;
  }
}

void tase_exit() {
  if (taseManager) {
    get_sem_lock();
    remove_self_from_QR();
    
    printf("PID %d calling tase_exit \n", getpid());
    
    std::cout.flush();
    std::exit(EXIT_SUCCESS);
    
    release_sem_lock();
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
   if (res == -1)
     perror("Subreaper err ");
     
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

  if (roundCount < *latestRoundPtr)  {
    remove_self_from_QR();
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
	char buf2 [80];
	TASE_get_time_string(buf2);
	printf( "IMPORTANT: ERROR: control debug: deserializing from empty buf 0x%lx at time %s for replay pid %d  \n", (uint64_t) MPAPtr, buf2, getpid());
	std::cout.flush();
      }
 
    }

    std::cout.flush();
    release_sem_lock();
  } else {

    passCount++;
    raise(SIGSTOP);

    int i = getpid();
    workerIDStream << ".";
    workerIDStream << i;
    std::string pidString ;
    pidString = workerIDStream.str();
    freopen(pidString.c_str(),"w",stdout);
    
    while (true) {
      
      get_sem_lock(); 
      if (PidInQR(i) && *replayLock == 0) {
	break;
      } else {
	release_sem_lock();
      }
    }
    
    char buf [80];
    TASE_get_time_string(buf);
    
    printf("IMPORTANT: control debug:  replaying round %d for pass %d at time %s with replay pid %d \n", roundCount, passCount, buf, getpid());
    if(!PidInQR(i)) 
      printf("IMPORTANT: Error: control debug: Process %d executing without pid in QR at time %s \n", i, buf);
    std::cout.flush();
    release_sem_lock();
    
    multipass_start_round(theExecutor, true);
    printf("Returned from multipass_start_round \n");
    std::cout.flush();
  }    
}
/*
void multipass_start_round (klee::Executor * theExecutor) {
  printf("Hit top of multipass_start_round \n");
  std::cout.flush();
  
  get_sem_lock();
  printf("IMPORTANT: Starting round %d pass %d of verification \n", roundCount, passCount);
  std::cout.flush();
  //Make backup of self
  int childPID = ::fork();
 
  if (childPID != 0) {
    *replayPIDPtr = childPID;
    //Block until child has sigstop'd
    while (true) {

      int status;
      int res = waitpid(childPID, &status, WUNTRACED);
      if (WIFSTOPPED(status))
	break;
    }    
  } else {
    passCount++;
    raise(SIGSTOP);


    int i = getpid();
    workerIDStream << ".";
    workerIDStream << i;
    std::string pidString ;
    pidString = workerIDStream.str();
    freopen(pidString.c_str(),"w",stdout);
    
    while (true) {
      
      get_sem_lock(); 
      if (PidInQR(i) && *replayLock == 0) {
	break;
      } else {
	release_sem_lock();
      }
    }
    

    time_t theTime;
    time(&theTime);
    char buf [80];
    struct tm * tstruct = localtime(&theTime);
    strftime(buf, 80, "%T", tstruct);
    
    printf("IMPORTANT: control debug:  replaying round %d for pass %d at time %s with replay pid %d \n", roundCount, passCount, buf, getpid());
    if(!PidInQR(i)) 
      printf("IMPORTANT: Error: control debug: Process %d executing without pid in QR at time %s \n", i, buf);
    std::cout.flush();
    
    multipass_start_round(theExecutor);
    printf("Returned from multipass_start_round \n");
    std::cout.flush();




    




    
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
      time_t theTime2;
      time(&theTime2);
      char buf2 [80];
      struct tm * tstruct2= localtime(&theTime2);
      strftime(buf2, 80, "%T",tstruct2);
      printf( "IMPORTANT: ERROR: control debug: deserializing from empty buf 0x%lx at time %s for replay pid %d  \n", (uint64_t) MPAPtr, buf2, getpid());
      std::cout.flush();
    }
  }
  
  std::cout.flush();  
  release_sem_lock();
}
*/


void multipass_replay_round (void * assignmentBufferPtr, CVAssignment * mpa, int * pidPtr)  {

  while(true) {//Is this actually needed?  get_sem_lock() should block until semaphore is available
 
    get_sem_lock();
    if (roundCount < *latestRoundPtr)  {
      remove_self_from_QR();
      printf("Worker %d is in round %d when latest round is %d. Worker exiting. \n", getpid(), roundCount, *latestRoundPtr );
      fflush(stdout);
      std::exit(EXIT_SUCCESS);
    }
    
    if (*replayLock != 1  || PidInQA(*pidPtr) || PidInQR(*pidPtr) ||  *((uint8_t *) assignmentBufferPtr) != 0)  {
      release_sem_lock();  //Spin and try again after pending replay fully executes

    } else {
      
      
      if (*replayLock != 1) {
	printf("IMPORTANT: control debug: Error - replayLock has unexpected value %d \n", *replayLock);
      } else {
	*replayLock = 0;
	if (PidInQA(*pidPtr))
	  printf("ERROR: control debug: replay pid is somehow already in QA \n");
	time_t theTime;
	time(&theTime);
	char buf [80];
	struct tm * tstruct= localtime(&theTime);
	strftime(buf, 80, "%T",tstruct);
	printf("mp_replay_round: control debug: replayLock obtained. Inserting replayPid %d into QA  at time %s \n", *pidPtr, buf);
	std::cout.flush();
	
      }
      std::cout.flush();
      QA_insert_PID(*pidPtr); //Move replay child pid into QA
      if ( *((uint8_t *) assignmentBufferPtr) != 0) { //Move latest assignments into shared mem
	printf("ERROR: control debug: see data in assignment buffer when trying to serialize new constraints at time % \n");
	std::cout.flush();
      }
      printf("Printing assignments BEFORE serialization \n");
      mpa->printAllAssignments(NULL);
      time_t theTime2;
      time(&theTime2);
      char buf2 [80];
      struct tm * tstruct2= localtime(&theTime2);
      strftime(buf2, 80, "%T",tstruct2);
      printf(" control debug: Serializing to buf 0x%lx at time %s for replay pid %d in round %d \n", (uint64_t) assignmentBufferPtr, buf2, *pidPtr, roundCount);
      mpa->serializeAssignments(assignmentBufferPtr, multipassAssignmentSize);
      std::cout.flush();
      remove_self_from_QR();
      std::exit(EXIT_SUCCESS);//Exit implicitly releases the semaphore
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
