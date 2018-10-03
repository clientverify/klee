#ifndef TASE_INTERP_H
#define TASE_INTERP_H

/* Number of each register in the `gregset_t' array.  */
#define REG_R8       0
#define REG_R9       1
#define REG_R10      2
#define REG_R11      3
#define REG_R12      4
#define REG_R13      5
#define REG_R14      6
#define REG_R15      7
#define REG_RDI      8
#define REG_RSI      9
#define REG_RBP     10
#define REG_RBX     11
#define REG_RDX     12
#define REG_RAX     13
#define REG_RCX     14
#define REG_RSP     15
#define REG_RIP     16
#define REG_EFL     17
/* Number of general registers.  */
#define NGREG       18
#define REG_SIZE     8

#define POISON_REFERENCE 0xDEADDEADDEADDEAD

#define SB_FLAG_INTRAN   0x1
#define SB_FLAG_MODELED  0x2

#ifndef IN_ASM

#include <stdint.h>
/* Type for general register.  */
typedef long long int greg_t;
/* Container for all general registers.  */
typedef greg_t gregset_t[NGREG];

extern uint8_t springboard_flags;

#else /* IN_ASM */

#define DEF_BYTE(name, value) .data ; .globl name ; .align 4; .type name,@object; name ## : ; .byte value; .size name, 1
#define IMM(name) $ ## name
#define SET_SB_FLAGS(flag) movb IMM(flag), springboard_flags(%rip)
#define GET_SB_FLAGS(reg)  movb springboard_flags(%rip), reg

/* Some compatibility declarations */
#define REG_Rdi     REG_RDI
#define REG_Rsi     REG_RSI
#define REG_Rbp     REG_RBP 
#define REG_Rbx     REG_RBX
#define REG_Rdx     REG_RDX
#define REG_Rax     REG_RAX
#define REG_Rcx     REG_RCX
#define REG_Rsp     REG_RSP
#define LABEL(fname,label)  fname ## _ ## label

/* Function prologue */
#define DEF_FUNC(name) .text ; .globl name ; .align  16, 0x90 ; .type name,@function; name ## : ; .cfi_startproc

/* Function epilogue */
#define END_FUNC(name) LABEL(name,func_end): ; .size name, LABEL(name,func_end)-name ; .cfi_endproc

/* Reference to registers in the shared context. */
#define CTX(reg_idx) target_ctx_gregs+reg_idx*REG_SIZE

/* Store a general purpose register into shared context.
 * CTX_STORE/CTX_LOAD expects to see a number (8-15) or a lower case
 * register name like ax, bx, si etc.
 */
#define CTX_STORE_REG(reg, reg_name) movq %r ## reg_name, CTX(reg)(%rip)
#define CTX_STORE(reg_name) CTX_STORE_REG(REG_R ## reg_name, reg_name) 
#define CTX_LOAD_REG(reg, reg_name) movq CTX(reg)(%rip), %r ## reg_name
#define CTX_LOAD(reg_name) CTX_LOAD_REG(REG_R ## reg_name, reg_name) 

#if defined(NOVERIFY) || defined(NOTSX)
#define CHECK_POISON
#else  /* NOVERIFY / NOTSX */
#define CHECK_POISON  ptest %xmm2, %xmm1; jne sb_bailout
#endif /* NOVERIFY / NOTSX */

#ifdef NOTSX
#define EXIT_TRAN
#define ENTER_TRAN(fallback)
#define INIT_XMM
#else /* NOTSX */
/* XMM2 should always just contain 1s.
 * XMM7 contains the reference taint value.
 * XMM1 accumulates taint.
 */
#define CLEAR(reg) pxor %xmm ## reg, %xmm ## reg

#define ACC_XMM(reg) pcmpeqw %xmm7, %xmm ## reg; por %xmm ## reg, %xmm1
#define EXIT_TRAN ACC_XMM(3); ACC_XMM(4); ACC_XMM(5); ACC_XMM(6); CHECK_POISON; xend

#define ENTER_TRAN(fallback) CLEAR(1); pcmpeqw %xmm2, %xmm2; xbegin fallback

/* r15 has no special meaning here - just using it as scratch. */
#define LOAD_INTO_XMM7(poison) movabsq IMM(poison), %r15; pinsrq  $0, %r15 ,%xmm7; pinsrq  $1, %r15 ,%xmm7
#define LOAD_POISON LOAD_INTO_XMM7(POISON_REFERENCE)
/* Clear poison registers and load poison reference register just to be safe.
 * Assumes r15 is dead and going to get clobbered.
 * entertran should initialize any remaining registers (XMM1 and XMM2).
 */
#define INIT_XMM CLEAR(3); CLEAR(4); CLEAR(5); CLEAR(6); LOAD_POISON

/* Shared code to return from interp_trap after transaction state has been decided. */
#define INTERP_EPILOGUE leaq CTX(REG_EFL)(%rip), %rsp; popfq; CTX_LOAD(ax); CTX_LOAD(sp)

#endif /* NOTSX */

#endif /* IN_ASM */

#endif /* TASE_INTERP_H */
