#include "ucontext.h"

//5b5000:    55                       push   %rbp
void interp_fn_5b5000(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_RSP] = begin_gregs[REG_RSP] - 8;
  *(greg_t*)end_gregs[REG_RSP] = begin_gregs[REG_RBP];

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 1;
}


//5b5001:    48 89 e5                 mov    %rsp,%rbp
void interp_fn_5b5001(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_RBP] = begin_gregs[REG_RSP];

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 3;
}

//5b5004:    49 c7 c6 7b 00 00 00     mov    $0x7b,%r14
void interp_fn_5b5004(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_R14] = 0x7b;

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 7;
}

//5b500b:    49 c7 c7 7b 00 00 00     mov    $0x7b,%r15
void interp_fn_5b500b(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_R15] = 0x7b;

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 7;
}

//5b5012:    4c 03 64 24 10           add    0x10(%rsp),%r12
void interp_fn_5b5012(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_R12] = begin_gregs[REG_R12] + *(greg_t*)(begin_gregs[REG_RSP] + 0x10);
  // Affects Flags.

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 5;
}

//5b5017:    41 56                    push   %r14
void interp_fn_5b5017(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_RSP] = begin_gregs[REG_RSP] - 8;
  *(greg_t*)end_gregs[REG_RSP] = begin_gregs[REG_R14];

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 2;
}

//5b5019:    41 59                    pop    %r9
void interp_fn_5b5019(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_R9] = *(greg_t*)begin_gregs[REG_RSP];
  end_gregs[REG_RSP] = begin_gregs[REG_RSP] + 8;

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 2;
}

//5b501b:    49 ff c1                 inc    %r9
void interp_fn_5b501b(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  end_gregs[REG_R9] = begin_gregs[REG_R9] + 1;
  // Affects Flags.

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 3;
}

//5b501e:    48 ff 44 24 f8           incq   -0x8(%rsp)
void interp_fn_5b501e(ucontext_t* begin_ctx, ucontext_t* end_ctx) {
  const greg_t *begin_gregs = begin_ctx->uc_mcontext.gregs;
  greg_t *end_gregs = end_ctx->uc_mcontext.gregs;

  *(greg_t*)(begin_gregs[REG_RSP] - 0x8) += 1;

  end_gregs[REG_RIP] = begin_gregs[REG_RIP] + 5;
}
