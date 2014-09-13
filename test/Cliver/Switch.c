// RUN: %llvmgcc %s -DKLEE -emit-llvm -g -c -o %t1.bc 
// RUN: rm -rf %t.cliver-out
// RUN: %klee -output-dir=%t.cliver-out -cliver -optimize=0 -posix-runtime -libc=uclibc %t1.bc &> %t1.cliver.log

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h> 

#define BUFFER_SIZE 4
#define MESSAGE_COUNT 2
#define BASKET_SIZE 2

// TODO: better symbolic model of random

//static unsigned long int srand_next = 1;
void srand(unsigned int seed) {
#ifdef KLEE
  //srand_next = seed;
#else
  srandom(seed);
#endif
}

int rand() {
#ifdef KLEE
  //srand_next = srand_next * 1103515245 + 12345;
  //return (unsigned int)(srand_next / 65536) % 32768;
  return klee_int("rand");
#else
  return random();
#endif
}

void add_fruit_to_basket(char* basket, size_t max_size) {
  switch (rand()) {
    case 0: 
      basket[0] = 'a'; basket[1] = 0;
      break;
    case 1: 
      basket[0] = 'b'; basket[1] = 0;
      break;
  }
}

int main(int argc, char* argv[]) {
  char basket[BUFFER_SIZE];
  memset(basket,0,BUFFER_SIZE);
  add_fruit_to_basket(basket, BUFFER_SIZE);
  return 0;
}
