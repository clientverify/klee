#include <signal.h>

typedef void (*sighandler_t)(int);

unsigned int alarm(unsigned int seconds) {
  return 0;
}

sighandler_t signal(int signum, sighandler_t handler) {
  return 0;
}


