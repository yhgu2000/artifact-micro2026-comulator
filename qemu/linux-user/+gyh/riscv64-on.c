#include "qemu/osdep.h"

#include "qemu.h"
#include "user-internals.h"

#include "cpu_loop-common.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "semihosting/common-semi.h"
#include "signal-common.h"

#include "gyh.h"

void
gyh_do_cpu(CPUArchState* env, struct gyh_DoSyscall* gyh_syscall)
{
  CPUState* cs = env_cpu(env);
  int trapnr;
  target_ulong ret;

  cpu_exec_start(cs);
  trapnr = cpu_exec(cs);
  cpu_exec_end(cs);
  process_queued_cpu_work(cs);

  switch (trapnr) {
    case EXCP_INTERRUPT:
      /* just indicate that signals should be handled asap */
      break;
    case EXCP_ATOMIC:
      cpu_exec_step_atomic(cs);
      break;
    case RISCV_EXCP_U_ECALL:
      env->pc += 4;
      if (env->gpr[xA7] == TARGET_NR_riscv_flush_icache) {
        /* riscv_flush_icache_syscall is a no-op in QEMU as
           self-modifying code is automatically detected */
        ret = 0;
      } else {
        target_ulong syscall_num =
          env->gpr[(env->elf_flags & EF_RISCV_RVE) ? xT0 : xA7];
        if (syscall_num == 94) // exit_group
          gyh_atexit();
        if (syscall_num > GYH_NR_start) {
          gyh_syscall->num = syscall_num;
          gyh_syscall->arg1 = env->gpr[xA0];
          gyh_syscall->arg2 = env->gpr[xA1];
          gyh_syscall->arg3 = env->gpr[xA2];
          gyh_syscall->arg4 = env->gpr[xA3];
          gyh_syscall->arg5 = env->gpr[xA4];
          gyh_syscall->arg6 = env->gpr[xA5];
          break;
        }
        ret = do_syscall(env,
                         syscall_num,
                         env->gpr[xA0],
                         env->gpr[xA1],
                         env->gpr[xA2],
                         env->gpr[xA3],
                         env->gpr[xA4],
                         env->gpr[xA5],
                         0,
                         0);
      }
      if (ret == -QEMU_ERESTARTSYS) {
        env->pc -= 4;
      } else if (ret != -QEMU_ESIGRETURN) {
        env->gpr[xA0] = ret;
      }
      if (cs->singlestep_enabled) {
        goto gdbstep;
      }
      break;
    case RISCV_EXCP_ILLEGAL_INST:
      force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->pc);
      break;
    case RISCV_EXCP_BREAKPOINT:
    case EXCP_DEBUG:
    gdbstep:
      force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
      break;
    case RISCV_EXCP_SEMIHOST:
      do_common_semihosting(cs);
      env->pc += 4;
      break;
    default:
      EXCP_DUMP(
        env, "\nqemu: unhandled CPU exception %#x - aborting\n", trapnr);
      exit(EXIT_FAILURE);
  }

  process_pending_signals(env);
}

// ========================================================================== //
// gyhcall
// ========================================================================== //

void
__gyh_callback__(void* cbStub, void* func, void* retPad, void* argPad)
{
  //* 这里的代码是在客方的栈上工作！
  CPUArchState* env = gyh_cpu_loop_env;
  assert(gyh_cpu_loop_ctx_to == NULL);

  // 我们需要在这里实现调用 cbFunc(retPad, argPad, func, cbStub) 。
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_callback__(%p, %p, %p, %p)\n",
                cbStub,
                func,
                retPad,
                argPad);

  // 先传递 cbFunc 的参数，然后模拟客方 call cbfunc 的语义。
  env->gpr[10] = (target_ulong)retPad; /* a0 */
  env->gpr[11] = (target_ulong)argPad; /* a1 */
  env->gpr[12] = (target_ulong)func;   /* a2 */
  env->gpr[13] = (target_ulong)cbStub; /* a3 */

  // 我们要设置客方的 sp，让客方将 gyhcall 期间的栈空间使用视为一整块
  // 栈帧。由于 sp 是被调方保存寄存器，在修改它之前我们需要先保存它。
  target_ulong sp = env->gpr[2]; /* sp */

  // 在栈上申请一块空间：
  // --H--| ... | ctx | ... |--L--
  //      ^ 当前栈帧用到这里了
  //                        ^ alloca 返回的地址
  //                  ^ RISC-V 要求这里对齐到 16 字节
  // 因此分配的大小为：
  static const int alloca_size = 16                    // 用于对齐
                                 + sizeof(ucontext_t); // 保存 ctx
  env->gpr[2] = ((target_ulong)alloca(alloca_size) + 16) & ~(target_ulong)0xf;

  // cbFunc 结束时，我们需要回到这里继续执行主侧代码。因此，我们保存当前
  // 执行上下文到客方栈上。cbFunc 会在结束时触发 callback_return 系统
  // 调用再回到这个函数里运行。
  ucontext_t* ctx = (ucontext_t*)env->gpr[2];

  // jal 指令的第一步，是保存返回地址到 ra
  env->gpr[1] = env->pc; /* ra */

  // 然后，跳转到 cbFunc 的第一条指令
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_callback__ at %p, from %p\n",
                (void*)(env->gpr[2]),
                (void*)(env->pc));
  env->pc = gyh_cbFunc;

  // 切换到 cpu_loop 里继续模拟执行客方代码。
  qemu_log_mask(GYH_LOG, "||=== cpu_loop <<<<< %p\n", &ctx);
  ++gyh_breakdown_callbacks;
  swapcontext(ctx, &gyh_cpu_loop_ctx);
  // 如果客方代码不返回，就算跑飞了，也没事，因为这里的所有局部变量都在客方栈上，
  // 而且卸载到主侧的函数对主侧而言是无副作用的，不会破坏主侧的状态完整性。

  // 如果客方代码运行返回，那么 cbFunc 会通过 callback_return 回到主侧，
  // 然后切换到此处继续执行。
  gyh_cpu_loop_ctx_to = NULL; // 切换已经完成，重置它，这很重要！

  // 恢复 sp 为 callback 前的值。
  env->gpr[2] = sp; /* sp */
}

void
gyh_call(void)
{
  CPUArchState* env = gyh_cpu_loop_env;

  target_ulong sp = env->gpr[2]; /* sp */
  // RISC-V 规定栈指针始终 16 字节对齐
  assert((sp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* ra = *(void**)(sp + 8);
  qemu_log_mask(GYH_LOG, "||=== __gyh_call__ at %p, from %p\n", (void*)sp, ra);

  static _Thread_local ucontext_t ctx;
  // 进入 gyh_do_call 的上下文基于当前上下文修改而来
  getcontext(&ctx);

  // 这里的（128）应该设为 gyh_do_call 需要的栈帧大小，执行 gyh_do_call
  // 相当于继续执行客方的被调函数。
  ctx.uc_link = NULL;
  ctx.uc_stack.ss_sp = (void*)(sp - 64);
  ctx.uc_stack.ss_size = 64;
  makecontext(&ctx,
              (void*)gyh_do_call,
              6,
              (uint64_t*)env->gpr[10], /* a0 */
              (uint32_t)env->gpr[11],  /* a1 */
              (void*)env->gpr[12],     /* a2 */
              (void*)env->gpr[13],     /* a3 */
              (void*)env->gpr[14],     /* a4 */
              (void*)env->gpr[15]);    /* a5 */

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

void
gyh_callback_return(void)
{
  CPUArchState* env = gyh_cpu_loop_env;

  // 我们需要在这里模拟客方 ret 指令的语义。
  env->pc = env->gpr[1]; /* ra */
  // ret 指令会检查 sp 是否 16 字节对齐。
  assert((env->gpr[2] & 0xf) == 0); /* sp */

  // 这时的 rsp 应该就指向此前 __gyh_callback__ 保存的主侧上下文了。
  ucontext_t* ctx = (ucontext_t*)env->gpr[2]; /* sp */
  qemu_log_mask(GYH_LOG, "||=== gyh_callback_return(%p)\n", ctx);

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = ctx;
  // 等下次 cpu_loop 开始时就会切换到 __gyh_callback__ 里继续运行了。
}

// ========================================================================== //
// gyhcall GRT
// ========================================================================== //

void
gyh_call_grt(void)
{
  CPUArchState* env = gyh_cpu_loop_env;

  target_ulong sp = env->gpr[2]; /* sp */
  // RISC-V 规定栈指针始终 16 字节对齐
  assert((sp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* ra = *(void**)(sp + 8);
  qemu_log_mask(
    GYH_LOG, "||=== __gyh_call_grt__ at %p, from %p\n", (void*)sp, ra);

  static _Thread_local ucontext_t ctx;
  // 进入 gyh_do_call 的上下文基于当前上下文修改而来
  getcontext(&ctx);

  // 这里的（128）应该设为 gyh_do_call 需要的栈帧大小，执行 gyh_do_call
  // 相当于继续执行客方的被调函数。
  ctx.uc_link = NULL;
  ctx.uc_stack.ss_sp = (void*)(sp - 64);
  ctx.uc_stack.ss_size = 64;
  makecontext(&ctx,
              (void*)gyh_do_call_grt,
              4,
              (uint64_t*)env->gpr[10], /* a0 */
              (uint32_t)env->gpr[11],  /* a1 */
              (void*)env->gpr[12],     /* a2 */
              (void*)env->gpr[13]);    /* a3 */

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

// ========================================================================== //
// gyhcall FCP-CM
// ========================================================================== //
