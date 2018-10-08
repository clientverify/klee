#include <stdio.h>
#include <fcntl.h>
#include <sys/sem.h>
#include <cstdlib>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>


bool taseManager = true;

void * ms_base;
int ms_size = 16384;

int * ms_QR_base;
int * ms_QR_size_ptr;
int * ms_QA_base;
int * ms_QA_size_ptr;

int semID; //Global semaphore ID for sync

struct sembuf sem_lock = {0, -1, 0 | SEM_UNDO}; // {sem index, inc/dec, flags}
struct sembuf sem_unlock = {0, 1, 0 | SEM_UNDO};// SEM_UNDO added to release
//lock if process dies.

void get_sem_lock () {
  int res =  semop(semID, &sem_lock, 1);
  if (res == 0)
    return;
  else {
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

void manage_workers () {

  

}

int tase_fork() {
 
  if (taseManager) {

    get_sem_lock();
     printf("TASE FORKING! \n");
    int trueChildPID = ::fork();
    if (trueChildPID != 0) {
      
      //Block until child has sigstop'd.
      while (true) {
	int status;
	int res = waitpid(trueChildPID, &status, WUNTRACED);
	if (WIFSTOPPED(status))
	  break;
      }
      //Q_Available.insert(trueChildPID); 
      int QA_size= *ms_QA_size_ptr;
      QA_size++;
      *(ms_QA_base + QA_size) = trueChildPID;
      *ms_QA_size_ptr = QA_size;
      
    } else  {
      raise(SIGSTOP);
      return 1;// Go back to path exploration
    } 

    int falseChildPID = ::fork();
    if (falseChildPID != 0) {
      //Block until child has sigstop'd
      while (true) {
	int status;
	int res = waitpid(falseChildPID, &status, WUNTRACED);
	if (WIFSTOPPED(status))
	  break;
      }
      //Q_Available.insert(falseChildPID);
       int QA_size= *ms_QA_size_ptr;
       QA_size++;
       *(ms_QA_base + QA_size) = falseChildPID;
       *ms_QA_size_ptr = QA_size;
      
    } else {
      raise(SIGSTOP);
      return 0; //Go back to path exploration
    }
    
    //Remove self from running queue

    //First, find self in QR
    int * searchPtr = ms_QR_base;
    int mypid = getpid();
    int i = 0;
    while (i < 4096) {
      if (*searchPtr == mypid)
	break;
      else {
	i+= 4;
	searchPtr++;
      }
    }
    if (i == 4096){
      printf("ERROR: tase_fork couldn't find self \n");
      std::exit(EXIT_FAILURE);
    }

    //Remove self
    int QR_size = * ms_QR_size_ptr;
    int * QR_edge_ptr = (int *) ms_QR_base + QR_size;
    *searchPtr = 0;

    //Slide values down stack
    while (searchPtr < QR_edge_ptr) {
      *searchPtr = *(searchPtr +1);
      searchPtr++;
    }

    //Update size of QR
    QR_size--;
    *ms_QR_size_ptr = QR_size;

    printf("Exiting tase_fork \n");
    //Exit, and release semaphore
    std::exit(EXIT_SUCCESS);
    
  } else {
    int pid = ::fork();
    return pid;
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
   
 }
