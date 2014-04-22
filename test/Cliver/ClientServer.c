// RUN: %llvmgcc -B/usr/lib/x86_64-linux-gnu %s -DKTEST=\"%t1.ktest\" -o %t1
// RUN: %llvmgcc %s -DCLIENT -emit-llvm -g -c -o %t1.bc 
// RUN: %t1 > %t1.log
// RUN: grep -q "CLIENT: SUCCESS" %t1.log
// RUN: not grep -q "error" %t1.log
// RUN: %klee -cliver -use-threads=1 -posix-runtime -libc=uclibc -socket-log=%t1.ktest -all-external-warnings %t1.bc > %t.cliver.log

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

void server_run(int listen_fd) {
  int client_fd;
  char recv_buffer[1025];
  char send_buffer[1025];

  //int ticks = time(NULL);
  //snprintf(send_buffer, sizeof(send_buffer), "%.24s\r\n", ctime(&ticks));
  //write(connfd, send_buffer, strlen(send_buffer)); 

  if ((client_fd = accept(listen_fd, NULL, NULL)) < 0)
    goto exit_error;

  if (ktest_recv(client_fd, recv_buffer, sizeof(recv_buffer), 0) == -1)
    goto exit_error;

  printf("SERVER: recv = %s\n", recv_buffer);

  close(client_fd);
  close(listen_fd);

  printf("SERVER: SUCCESS\n");
  return;

exit_error:
  perror("server_run error");
  exit(1);

}

void client_init(int port, int *client_fd) {
  char* ip_address = "127.0.0.1";
  struct sockaddr_in server_address;

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

void client_run(int client_fd) {
  struct sockaddr_in server_address;
  char recv_buffer[256];
  char send_buffer[256];

  sprintf(send_buffer, "Hello, Server!");
  printf("CLIENT: send = %s\n", send_buffer);
  if (send(client_fd, send_buffer, strlen(send_buffer), 0) < 0)
    goto exit_error;
  
  //if (read(client_fd, recv_buffer, sizeof(recv_buffer)-1) < 0)
  //  goto error;

  //printf("CLIENT: read = %s\n", recv_buffer);

  close(client_fd);

  printf("CLIENT: SUCCESS\n");

  return;

exit_error:
  perror("client_run error");
  exit(1);
}

int main(int argc, char* argv[]) {
  int listen_fd=-1, client_fd=-1, port=0;

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


