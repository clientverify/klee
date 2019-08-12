// RUN: echo 0

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

#ifdef KLEE
#include "klee/Internal/ADT/KTest.h"
#include "klee/klee.h"
unsigned klee_is_symbolic_buffer(const unsigned char *buf, size_t len);
#endif


#define BUF_SIZE 27 //number of bytes to allocate
char global_str[BUF_SIZE];

/* Modify contents of an existing memory object
 */
//The line numbers aren't working out very well on this function.
//It generates 4 allocs in klee, 2 for the arguements, one for
//the char declaration, and one for the int declaration.
//2 allocs for the arguements
void skip_me(char* modify_points_to, int size){
  //results in one alloc
  char set_to;
  set_to = 'a';
  //results in one alloc
  int i;
  for(i = 0; i < size; i++){
    modify_points_to[i] = set_to + i;
  }
}

//the following results in 6 calls to Executor::executeAlloc
//Presumably 2 are from the arguements (this happens if I ifdef
//out the entire function body.
//One is from the return, which shows up if I have only the return
//statement in the body

int main(int argc, char* argv[]) {
  //results in one call to alloc
  char* heap_str;
  //results in one call to alloc
  heap_str = (char*) malloc(BUF_SIZE);
  skip_me(heap_str, BUF_SIZE);
  heap_str[BUF_SIZE-1] = '\0';
  printf("heap string:   %s\n", heap_str);
  free(heap_str);

  //results in one call to alloc
  char stack_str[BUF_SIZE];
  skip_me(stack_str, BUF_SIZE);
  stack_str[BUF_SIZE-1] = '\0';
  printf("stack string:  %s\n", stack_str);

  skip_me(global_str, BUF_SIZE);
  global_str[BUF_SIZE-1] = '\0';
  printf("global string: %s\n", global_str);
  return 0;
}
