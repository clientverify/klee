// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DKLEE -DCLIENT -emit-llvm -g -c -o %t1.bc 
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: success" %t1.log
// RUN: grep -q "SERVER: success" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: rm -rf %t1.cliver-out
// RUN: %klee --output-dir=%t1.cliver-out -cliver -optimize=0 -posix-runtime -libc=uclibc -socket-log=%t1.ktest %t1.bc &> %t1.cliver.log
// RUN: grep -q "CLIENT: success" %t1.cliver.log
// RUN: rm -rf %t2.cliver-out
// RUN: %klee -cliver -cliver-mode=training -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc &> %t2.cliver.log
// RUN: grep -q "CLIENT: success" %t2.cliver.log
// RUN: rm -rf %t3.cliver-out
// RUN: %klee -cliver -use-clustering -cliver-mode=edit-dist-kprefix-row -optimize=0 -posix-runtime -libc=uclibc -output-dir=%t3.cliver-out -training-path-dir=%t2.cliver-out -socket-log=%t1.ktest %t1.bc &> %t3.cliver.log
// RUN: not grep -q "Recomputed kprefix" %t3.cliver.log
// RUN: grep -q "CLIENT: success" %t3.cliver.log

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

#include "klee/Internal/ADT/KTest.h"
#include "KTestSocket.inc"

#define BUFFER_SIZE 256
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

// Initialize echo server
void server_init(int *port, int *fd) {
  struct sockaddr_in server_address;

  if ((*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    goto exit_error;

  memset(&server_address, '0', sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(*port);

  if (bind(*fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    goto exit_error;

  if (listen(*fd, 1) != 0)
    goto exit_error;

  socklen_t len = sizeof(server_address);
  if (getsockname(*fd, (struct sockaddr *)&server_address, &len) == -1)
    goto exit_error;

  *port = ntohs(server_address.sin_port);

  return;
  
exit_error:
  perror("server_init error");
  exit(1);
}

// Run simple echo server
void server_run(int listen_fd) {
  int client_fd;
  int client_quit = 0; 
  char recv_buffer[BUFFER_SIZE];

  if ((client_fd = accept(listen_fd, NULL, NULL)) < 0)
    goto exit_error;

  while (!client_quit) {
    if (ktest_recv(client_fd, recv_buffer, sizeof(recv_buffer), 0) == -1)
      goto exit_error;

    printf("SERVER: recv: %s\n", recv_buffer);

    if (strncmp(recv_buffer, "QUIT", BUFFER_SIZE) == 0) {
      client_quit = 1;
    } else {
      if (ktest_send(client_fd, recv_buffer, strnlen(recv_buffer, BUFFER_SIZE)+1) == -1)
        goto exit_error;
    }
  }

  close(client_fd);
  close(listen_fd);

  printf("SERVER: success!\n");
  return;

exit_error:
  perror("server_run error");
  exit(1);

}

// Initialize simple client
void client_init(int port, int *client_fd) {
  char* ip_address = "127.0.0.1";
  struct sockaddr_in server_address;

  srand(time(NULL));

  if ((*client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    goto exit_error;

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) <= 0)
    goto exit_error;

  if (connect(*client_fd, &server_address, sizeof(server_address)) < 0)
    goto exit_error;

  return;

exit_error:
  perror("client_init error");
  exit(1);
}

void add_fruit_to_basket(char* basket, size_t max_size) {
  int fruit = abs(rand());

  fruit = fruit % 5;

  /*
  if (fruit == 0) 
    strncat(basket, "apple", max_size);
  else if (fruit == 1)
    strncat(basket, "orange", max_size);
  else if (fruit == 2)
    strncat(basket, "banana", max_size);
  else if (fruit == 3)
    strncat(basket, "grape", max_size);
  else if (fruit == 4)
    strncat(basket, "kiwi", max_size);
  */

  ///*
  switch (fruit % 5) {
    case 0: 
      strncat(basket, "apple", max_size);
      break;
    case 1: 
      strncat(basket, "orange", max_size);
      break;
    case 2: 
      strncat(basket, "banana", max_size);
      break;
    case 3: 
      strncat(basket, "grape", max_size);
      break;
    case 4: 
      strncat(basket, "kiwi", max_size);
      break;
  }
  //*/
}

void client_run(int client_fd) {
  int message_count = MESSAGE_COUNT; 
  int i;
  char send_buffer[BUFFER_SIZE];
  char recv_buffer[BUFFER_SIZE];
  struct timeval tv;
  memset(&tv, 0, sizeof(struct timeval));

  while (message_count-- > 0) {
    //select(0, NULL, NULL, NULL, &tv);
    send_buffer[0] = '\0';

    // Build random fruit string
    for (i=0; i<BASKET_SIZE; ++i)
      add_fruit_to_basket(send_buffer, BUFFER_SIZE);

    // Send fruit basket 
    printf("CLIENT: send: %s\n", send_buffer);
    if (send(client_fd, send_buffer, strnlen(send_buffer,BUFFER_SIZE)+1, 0) < 0)
      goto exit_error;

    // Receive echo
    if (recv(client_fd, recv_buffer, sizeof(recv_buffer), 0) < 0)
      goto exit_error;

    // Check echo
    if (strncmp(recv_buffer, send_buffer, BUFFER_SIZE) != 0)
      goto exit_error;
  }

  sprintf(send_buffer, "QUIT");
  printf("CLIENT: send: %s\n", send_buffer);
  if (send(client_fd, send_buffer, strnlen(send_buffer,BUFFER_SIZE)+1, 0) < 0)
    goto exit_error;
  
  close(client_fd);
  printf("CLIENT: success!\n");

  return;

exit_error:
  perror("client_run error");
  exit(1);
}

int main(int argc, char* argv[]) {
  int listen_fd=-1, client_fd=-1, port=0;

  if (argc > 1)
    port = atoi(argv[1]);

#if defined (CLIENT)
  client_init(port, &client_fd);
  client_run(client_fd);
#elif defined (SERVER)
  server_init(&port, &listen_fd);
  server_run(listen_fd);
  ktest_finish();
#else
  server_init(&port, &listen_fd);
  int pid = fork();
  if (pid == 0) {
    server_run(listen_fd);
    ktest_finish();
  } else {
    client_init(port, &client_fd);
    client_run(client_fd);
  }
#endif
  return 0;
}
