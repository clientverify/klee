#ifndef TASE_H
#define TASE_H

extern void begin_target_inner();
extern void enter_tase(void (*fun)());
#define BUFFER_SIZE 256
#define STACK_SIZE 524288


#endif
