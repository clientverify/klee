// RUN: echo 0

#include <stdio.h>
#include <stdlib.h>

#ifdef KLEE
#include "klee/Internal/ADT/KTest.h"
#include "klee/klee.h"
unsigned klee_is_symbolic_buffer(const unsigned char *buf, size_t len);
#endif


#define BUF_SIZE 27 //number of bytes to allocate
char global_str[BUF_SIZE];

/* Modify contents of an existing memory object
 */
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

int main(int argc, char* argv[]) {
  //results in one call to alloc
  char* heap_str;
  //results in one call to alloc
  heap_str = (char*) malloc(BUF_SIZE);
  skip_me(heap_str, BUF_SIZE);
  heap_str[BUF_SIZE-1] = '\0';
  printf("heap string:   %s\n", heap_str);
  free(heap_str);

  skip_me(global_str, BUF_SIZE);
  global_str[BUF_SIZE-1] = '\0';
  printf("global string: %s\n", global_str);
  return 0;
}
