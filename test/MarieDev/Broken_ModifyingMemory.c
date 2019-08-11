// RUN: echo 0

#include <stdio.h>

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
  char stack_str[BUF_SIZE];
  skip_me(stack_str, BUF_SIZE);
  stack_str[BUF_SIZE-1] = '\0';
  printf("stack string:  %s\n", stack_str);

  return 0;
}
