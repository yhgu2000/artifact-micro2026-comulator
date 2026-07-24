#include "qemu/osdep.h"

#include "qemu.h"
#include "user-internals.h"

#include "cpu_loop-common.h"
#include "qemu/guest-random.h"
#include "semihosting/common-semi.h"
#include "signal-common.h"
#include "target/arm/cpu-features.h"
#include "target/arm/syndrome.h"

#include "gyh.h"

void
gyh_do_cpu(CPUArchState* env, struct gyh_DoSyscall* gyh_syscall)
{
  CPUState* cs = env_cpu(env);
  int trapnr, ec, fsc, si_code, si_signo;
  abi_long ret;

  cpu_exec_start(cs);
  trapnr = cpu_exec(cs);
  cpu_exec_end(cs);
  process_queued_cpu_work(cs);

  switch (trapnr) {
    case EXCP_SWI:
      /* On syscall, PSTATE.ZA is preserved, PSTATE.SM is cleared. */
      aarch64_set_svcr(env, 0, R_SVCR_SM_MASK);
      if (env->xregs[8] == 94) // exit_group
        gyh_atexit();
      if (env->xregs[8] > GYH_NR_start) {
        gyh_syscall->num = env->xregs[8];
        gyh_syscall->arg1 = env->xregs[0];
        gyh_syscall->arg2 = env->xregs[1];
        gyh_syscall->arg3 = env->xregs[2];
        gyh_syscall->arg4 = env->xregs[3];
        gyh_syscall->arg5 = env->xregs[4];
        gyh_syscall->arg6 = env->xregs[5];
        break;
      }
      ret = do_syscall(env,
                       env->xregs[8],
                       env->xregs[0],
                       env->xregs[1],
                       env->xregs[2],
                       env->xregs[3],
                       env->xregs[4],
                       env->xregs[5],
                       0,
                       0);
      if (ret == -QEMU_ERESTARTSYS) {
        env->pc -= 4;
      } else if (ret != -QEMU_ESIGRETURN) {
        env->xregs[0] = ret;
      }
      break;
    case EXCP_INTERRUPT:
      /* just indicate that signals should be handled asap */
      break;
    case EXCP_UDEF:
      force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->pc);
      break;
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
      ec = syn_get_ec(env->exception.syndrome);
      switch (ec) {
        case EC_DATAABORT:
        case EC_INSNABORT:
          /* Both EC have the same format for FSC, or close enough. */
          fsc = extract32(env->exception.syndrome, 0, 6);
          switch (fsc) {
            case 0x04 ... 0x07: /* Translation fault, level {0-3} */
              si_signo = TARGET_SIGSEGV;
              si_code = TARGET_SEGV_MAPERR;
              break;
            case 0x09 ... 0x0b: /* Access flag fault, level {1-3} */
            case 0x0d ... 0x0f: /* Permission fault, level {1-3} */
              si_signo = TARGET_SIGSEGV;
              si_code = TARGET_SEGV_ACCERR;
              break;
            case 0x11: /* Synchronous Tag Check Fault */
              si_signo = TARGET_SIGSEGV;
              si_code = TARGET_SEGV_MTESERR;
              break;
            case 0x21: /* Alignment fault */
              si_signo = TARGET_SIGBUS;
              si_code = TARGET_BUS_ADRALN;
              break;
            default:
              g_assert_not_reached();
          }
          break;
        case EC_PCALIGNMENT:
          si_signo = TARGET_SIGBUS;
          si_code = TARGET_BUS_ADRALN;
          break;
        default:
          g_assert_not_reached();
      }
      force_sig_fault(si_signo, si_code, env->exception.vaddress);
      break;
    case EXCP_DEBUG:
    case EXCP_BKPT:
      force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
      break;
    case EXCP_SEMIHOST:
      do_common_semihosting(cs);
      env->pc += 4;
      break;
    case EXCP_YIELD:
      /* nothing to do here for user-mode, just resume guest code */
      break;
    case EXCP_ATOMIC:
      cpu_exec_step_atomic(cs);
      break;
    default:
      EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
      abort();
  }

  /* Check for MTE asynchronous faults */
  if (unlikely(env->cp15.tfsr_el[0])) {
    env->cp15.tfsr_el[0] = 0;
    force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MTEAERR, 0);
  }

  process_pending_signals(env);
  /* Exception return on AArch64 always clears the exclusive monitor,
   * so any return to running guest code implies this.
   */
  env->exclusive_addr = -1;
}

// ========================================================================== //
// gyhcall
// ========================================================================== //

void
__gyh_callback__(void* cbStub, void* func, void* retPad, void* argPad)
{
  //* 这里的代码是在客方的栈上工作！
  CPUArchState* env = gyh_cpu_loop_env;
  assert(env->aarch64);
  assert(gyh_cpu_loop_ctx_to == NULL);

  // 我们需要在这里实现调用 cbFunc(retPad, argPad, func, cbStub) 。
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_callback__(%p, %p, %p, %p)\n",
                cbStub,
                func,
                retPad,
                argPad);

  // 先传递 cbFunc 的参数，然后模拟客方 blr cbfunc 的语义。
  env->xregs[0] = (target_ulong)retPad;
  env->xregs[1] = (target_ulong)argPad;
  env->xregs[2] = (target_ulong)func;
  env->xregs[3] = (target_ulong)cbStub;

  // 我们要设置客方的 sp，让客方将 gyhcall 期间的栈空间使用视为一整块
  // 栈帧。由于 sp 是被调方保存寄存器，在修改它之前我们需要先保存它。
  target_ulong sp = env->xregs[31];

  // 在栈上申请一块空间：
  // --H--| ... | ctx | ... |--L--
  //      ^ 当前栈帧用到这里了
  //                        ^ alloca 返回的地址
  //                  ^ aapcs64 要求这里对齐到 128 位
  // 因此分配的大小为：
  static const int alloca_size = 16                    // 用于对齐
                                 + sizeof(ucontext_t); // 保存 ctx
  env->xregs[31] =
    ((target_ulong)alloca(alloca_size) + 16) & ~(target_ulong)0xf;

  // cbFunc 结束时，我们需要回到这里继续执行主侧代码。因此，我们保存当前
  // 执行上下文到客方栈上。cbFunc 会在结束时触发 callback_return 系统
  // 调用再回到这个函数里运行。
  ucontext_t* ctx = (ucontext_t*)env->xregs[31];

  // blr 指令的第一步，是保存 pc 到 lr/r30 。
  env->xregs[30] = env->pc;

  // 然后，跳转到 cbFunc 的第一条指令：stp fp, lr, [sp, -16]!
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_callback__ at %p, from %p\n",
                (void*)(env->xregs[31]),
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
  env->xregs[31] = sp;
}

void
gyh_call(void)
{
  CPUArchState* env = gyh_cpu_loop_env;
  assert(env->aarch64);

  target_ulong sp = env->xregs[31];
  // aapcs64 规定栈指针始终 128 位对齐
  assert((sp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* lr = *(void**)(sp + 8);
  qemu_log_mask(GYH_LOG, "||=== __gyh_call__ at %p, from %p\n", (void*)sp, lr);

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
              (uint64_t*)env->xregs[0],
              (uint32_t)env->xregs[1],
              (void*)env->xregs[2],
              (void*)env->xregs[3],
              (void*)env->xregs[4],
              (void*)env->xregs[5]);

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

void
gyh_callback_return(void)
{
  CPUArchState* env = gyh_cpu_loop_env;
  assert(env->aarch64);

  // 我们需要在这里模拟客方 ret 指令的语义。
  env->pc = env->xregs[30];
  // ret 指令和 br lr 的区别在于 ret 会检查 sp 是否 128 位对齐。
  assert((env->xregs[31] & 0xf) == 0);

  // 这时的 rsp 应该就指向此前 __gyh_callback__ 保存的主侧上下文了。
  ucontext_t* ctx = (ucontext_t*)env->xregs[31];
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
  assert(env->aarch64);

  target_ulong sp = env->xregs[31];
  // aapcs64 规定栈指针始终 128 位对齐
  assert((sp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* lr = *(void**)(sp + 8);
  qemu_log_mask(
    GYH_LOG, "||=== __gyh_call_grt__ at %p, from %p\n", (void*)sp, lr);

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
              (uint64_t*)env->xregs[0],
              (uint32_t)env->xregs[1],
              (void*)env->xregs[2],
              (void*)env->xregs[3]);

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

// ========================================================================== //
// gyhcall FCP-CM
// ========================================================================== //
