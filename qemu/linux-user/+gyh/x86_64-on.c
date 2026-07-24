#include "qemu/osdep.h"

#include "qemu.h"
#include "qemu/timer.h"
#include "user-internals.h"

#include "cpu_loop-common.h"
#include "signal-common.h"
#include "user-mmap.h"

#include "gyh.h"

static bool
write_ok_or_segv(CPUX86State* env, abi_ptr addr, size_t len)
{
  /*
   * For all the vsyscalls, NULL means "don't write anything" not
   * "write it at address 0".
   */
  if (addr == 0 || access_ok(env_cpu(env), VERIFY_WRITE, addr, len)) {
    return true;
  }

  env->error_code = PG_ERROR_W_MASK | PG_ERROR_U_MASK;
  force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MAPERR, addr);
  return false;
}

/*
 * Since v3.1, the kernel traps and emulates the vsyscall page.
 * Entry points other than the official generate SIGSEGV.
 */
static void
emulate_vsyscall(CPUX86State* env)
{
  int syscall;
  abi_ulong ret;
  uint64_t caller;

  /*
   * Validate the entry point.  We have already validated the page
   * during translation to get here; now verify the offset.
   */
  switch (env->eip & ~TARGET_PAGE_MASK) {
    case 0x000:
      syscall = TARGET_NR_gettimeofday;
      break;
    case 0x400:
      syscall = TARGET_NR_time;
      break;
    case 0x800:
      syscall = TARGET_NR_getcpu;
      break;
    default:
      goto sigsegv;
  }

  /*
   * Validate the return address.
   * Note that the kernel treats this the same as an invalid entry point.
   */
  if (get_user_u64(caller, env->regs[R_ESP])) {
    goto sigsegv;
  }

  /*
   * Validate the pointer arguments.
   */
  switch (syscall) {
    case TARGET_NR_gettimeofday:
      if (!write_ok_or_segv(
            env, env->regs[R_EDI], sizeof(struct target_timeval)) ||
          !write_ok_or_segv(
            env, env->regs[R_ESI], sizeof(struct target_timezone))) {
        return;
      }
      break;
    case TARGET_NR_time:
      if (!write_ok_or_segv(env, env->regs[R_EDI], sizeof(abi_long))) {
        return;
      }
      break;
    case TARGET_NR_getcpu:
      if (!write_ok_or_segv(env, env->regs[R_EDI], sizeof(uint32_t)) ||
          !write_ok_or_segv(env, env->regs[R_ESI], sizeof(uint32_t))) {
        return;
      }
      break;
    default:
      g_assert_not_reached();
  }

  /*
   * Perform the syscall.  None of the vsyscalls should need restarting.
   */
  get_task_state(env_cpu(env))->orig_ax = syscall;
  ret = do_syscall(env,
                   syscall,
                   env->regs[R_EDI],
                   env->regs[R_ESI],
                   env->regs[R_EDX],
                   env->regs[10],
                   env->regs[8],
                   env->regs[9],
                   0,
                   0);
  g_assert(ret != -QEMU_ERESTARTSYS);
  g_assert(ret != -QEMU_ESIGRETURN);
  if (ret == -TARGET_EFAULT) {
    goto sigsegv;
  }
  env->regs[R_EAX] = ret;

  /* Emulate a ret instruction to leave the vsyscall page.  */
  env->eip = caller;
  env->regs[R_ESP] += 8;
  return;

sigsegv:
  force_sig(TARGET_SIGSEGV);
}

static bool
maybe_handle_vm86_trap(CPUX86State* env, int trapnr)
{
  return false;
}

void
gyh_do_cpu(CPUX86State* env, struct gyh_DoSyscall* gyh_syscall)
{
  CPUState* cs = env_cpu(env);
  int trapnr;
  abi_ulong ret;

  cpu_exec_start(cs);
  trapnr = cpu_exec(cs);
  cpu_exec_end(cs);
  process_queued_cpu_work(cs);

  switch (trapnr) {
    case 0x80:
      /* linux syscall from int $0x80 */
      get_task_state(cs)->orig_ax = env->regs[R_EAX];
      if (env->regs[8] == 252)
        gyh_atexit();
      if (env->regs[R_EAX] > GYH_NR_start) {
        gyh_syscall->num = env->regs[R_EAX];
        gyh_syscall->arg1 = env->regs[R_EBX];
        gyh_syscall->arg2 = env->regs[R_ECX];
        gyh_syscall->arg3 = env->regs[R_EDX];
        gyh_syscall->arg4 = env->regs[R_ESI];
        gyh_syscall->arg5 = env->regs[R_EDI];
        gyh_syscall->arg6 = env->regs[R_EBP];
        break;
      }
      ret = do_syscall(env,
                       env->regs[R_EAX],
                       env->regs[R_EBX],
                       env->regs[R_ECX],
                       env->regs[R_EDX],
                       env->regs[R_ESI],
                       env->regs[R_EDI],
                       env->regs[R_EBP],
                       0,
                       0);
      if (ret == -QEMU_ERESTARTSYS) {
        env->eip -= 2;
      } else if (ret != -QEMU_ESIGRETURN) {
        env->regs[R_EAX] = ret;
      }
      break;
    case EXCP_SYSCALL:
      /* linux syscall from syscall instruction.  */
      get_task_state(cs)->orig_ax = env->regs[R_EAX];
      if (env->regs[R_EAX] == 231) // exit_group
        gyh_atexit();
      if (env->regs[R_EAX] > GYH_NR_start) {
        gyh_syscall->num = env->regs[R_EAX];
        gyh_syscall->arg1 = env->regs[R_EDI];
        gyh_syscall->arg2 = env->regs[R_ESI];
        gyh_syscall->arg3 = env->regs[R_EDX];
        // 我们遵循过程调用约定, 使用 %rcx 传递第四个参数!
        gyh_syscall->arg4 = env->regs[R_ECX];
        gyh_syscall->arg5 = env->regs[8];
        gyh_syscall->arg6 = env->regs[9];
        break;
      }
      ret = do_syscall(env,
                       env->regs[R_EAX],
                       env->regs[R_EDI],
                       env->regs[R_ESI],
                       env->regs[R_EDX],
                       env->regs[10],
                       env->regs[8],
                       env->regs[9],
                       0,
                       0);
      if (ret == -QEMU_ERESTARTSYS) {
        env->eip -= 2;
      } else if (ret != -QEMU_ESIGRETURN) {
        env->regs[R_EAX] = ret;
      }
      break;
    case EXCP_VSYSCALL:
      emulate_vsyscall(env);
      break;
    case EXCP0B_NOSEG:
    case EXCP0C_STACK:
      force_sig(TARGET_SIGBUS);
      break;
    case EXCP0D_GPF:
      /* XXX: potential problem if ABI32 */
      if (maybe_handle_vm86_trap(env, trapnr)) {
        break;
      }
      force_sig(TARGET_SIGSEGV);
      break;
    case EXCP0E_PAGE:
      force_sig_fault(TARGET_SIGSEGV,
                      (env->error_code & PG_ERROR_P_MASK ? TARGET_SEGV_ACCERR
                                                         : TARGET_SEGV_MAPERR),
                      env->cr[2]);
      break;
    case EXCP00_DIVZ:
      if (maybe_handle_vm86_trap(env, trapnr)) {
        break;
      }
      force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV, env->eip);
      break;
    case EXCP01_DB:
      if (maybe_handle_vm86_trap(env, trapnr)) {
        break;
      }
      force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->eip);
      break;
    case EXCP03_INT3:
      if (maybe_handle_vm86_trap(env, trapnr)) {
        break;
      }
      force_sig(TARGET_SIGTRAP);
      break;
    case EXCP04_INTO:
    case EXCP05_BOUND:
      if (maybe_handle_vm86_trap(env, trapnr)) {
        break;
      }
      force_sig(TARGET_SIGSEGV);
      break;
    case EXCP06_ILLOP:
      force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->eip);
      break;
    case EXCP_INTERRUPT:
      /* just indicate that signals should be handled asap */
      break;
    case EXCP_DEBUG:
      force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->eip);
      break;
    case EXCP_ATOMIC:
      cpu_exec_step_atomic(cs);
      break;
    default:
      EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
      abort();
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
  env->regs[R_EDI] = (target_ulong)retPad;
  env->regs[R_ESI] = (target_ulong)argPad;
  env->regs[R_EDX] = (target_ulong)func;
  env->regs[R_ECX] = (target_ulong)cbStub;

  // 我们要设置客方的 rsp，让客方将 gyhcall 期间的栈空间使用视为一整块
  // 栈帧。由于 rsp 是被调方保存寄存器，在修改它之前我们需要先保存它。
  target_ulong rsp = env->regs[R_ESP];

  // 在栈上申请一块空间：
  // --H--| ... | ctx | rip | ... |--L--
  //      ^ 当前栈帧用到这里了
  //                              ^ alloca 返回的地址
  //                  ^ ABI 要求我们将这里对齐到 16 字节
  // 因此分配的大小为：
  static const int alloca_size = 16                   // 用于对齐
                                 + sizeof(ucontext_t) // 保存 ctx
                                 + 8;                 // 保存 rip
  env->regs[R_ESP] =
    ((target_ulong)alloca(alloca_size) + 24) & ~(target_ulong)0xf;

  // cbFunc 结束时，我们需要回到这里继续执行主侧代码。因此，我们保存当前
  // 执行上下文到客方栈上。cbFunc 会在结束时触发 callback_return 系统
  // 调用再回到这个函数里运行。
  ucontext_t* ctx = (ucontext_t*)env->regs[R_ESP];

  // call 指令语义的第一步，就是下推栈指针，预留保存程序指针的空间。
  env->regs[R_ESP] -= sizeof(env->eip);

  // 然后，保存当前的程序指针到栈上。
  *(target_ulong*)(env->regs[R_ESP]) = env->eip;

  // 最后，跳转到 cbFunc 第一条指令：push %rbp 会使栈帧再度 16 字节对齐。
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_callback__ at %p, from %p\n",
                (void*)(env->regs[R_ESP]),
                (void*)(env->eip));
  env->eip = gyh_cbFunc;

  // 切换到 cpu_loop 里继续模拟执行客方代码。
  qemu_log_mask(GYH_LOG, "||=== cpu_loop <<<<< %p\n", ctx);
  ++gyh_breakdown_callbacks;
  swapcontext(ctx, &gyh_cpu_loop_ctx);
  // 如果客方代码不返回，就算跑飞了，也没事，因为这里的所有局部变量都在客方栈上，
  // 而且卸载到主侧的函数对主侧而言是无副作用的，不会破坏主侧的状态完整性。

  // 如果客方代码运行返回，那么 cbFunc 会通过 callback_return 回到主侧，
  // 然后切换到此处继续执行。
  gyh_cpu_loop_ctx_to = NULL; // 切换已经完成，重置它，这很重要！

  // 恢复 rsp 为 callback 前的值。
  env->regs[R_ESP] = rsp;
}

void
gyh_call(void)
{
  CPUArchState* env = gyh_cpu_loop_env;

  target_ulong rsp = env->regs[R_ESP];
  // 根据 __gyh_call__ 的签名，客方在 call 指令**之前**的 rsp 应该是 16
  // 字节对齐的。call 指令将 rip 压入栈，而我们用汇编实现的 __gyh_call__
  // 的首条指令为 pushq %rbp，然后再触发 enter_calling，所以到这里时，rsp
  // 就是 16 字节对齐的。
  assert((rsp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* rip = *(void**)(rsp + 8);
  qemu_log_mask(
    GYH_LOG, "||=== __gyh_call__ at %p, from %p\n", (void*)rsp, rip);

  static _Thread_local ucontext_t ctx;
  // 进入 gyh_do_call 的上下文基于当前上下文修改而来
  getcontext(&ctx);

  // 这里的（128）应该设为 gyh_do_call 需要的栈帧大小，执行 gyh_do_call
  // 相当于继续执行客方的被调函数，因此 gyh_do_call 第一条指令会把主侧的 rbp
  // push 到这里（客方）的 (*rsp) 里。
  ctx.uc_link = NULL;
  ctx.uc_stack.ss_sp = (void*)(rsp - 64);
  ctx.uc_stack.ss_size = 64;
  makecontext(&ctx,
              (void*)gyh_do_call,
              6,
              (uint64_t*)env->regs[R_EDI],
              (uint32_t)env->regs[R_ESI],
              (void*)env->regs[R_EDX],
              (void*)env->regs[R_ECX],
              (void*)env->regs[R_R8],
              (void*)env->regs[R_R9]);

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

void
gyh_callback_return(void)
{
  CPUArchState* env = gyh_cpu_loop_env;

  // 我们需要在这里模拟客方 ret 指令的语义。
  env->eip = *(target_ulong*)(env->regs[R_ESP]);
  env->regs[R_ESP] += sizeof(env->eip);

  // 这时的 rsp 应该就指向此前 __gyh_callback__ 保存的主侧上下文了。
  ucontext_t* ctx = (ucontext_t*)(env->regs[R_ESP]);
  qemu_log_mask(GYH_LOG, "||=== gyh_callback_return to %p\n", ctx);

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

  target_ulong rsp = env->regs[R_ESP];
  // 根据 __gyh_call__ 的签名，客方在 call 指令**之前**的 rsp 应该是 16
  // 字节对齐的。call 指令将 rip 压入栈，而我们用汇编实现的 __gyh_call__
  // 的首条指令为 pushq %rbp，然后再触发 enter_calling，所以到这里时，rsp
  // 就是 16 字节对齐的。
  assert((rsp & 0xf) == 0);
  // 高 8 字节里保存了返回客方调用者继续运行的程序指针。
  void* rip = *(void**)(rsp + 8);
  qemu_log_mask(
    GYH_LOG, "||=== __gyh_call_grt__ at %p, from %p\n", (void*)rsp, rip);

  static _Thread_local ucontext_t ctx;
  // 进入 gyh_do_call 的上下文基于当前上下文修改而来
  getcontext(&ctx);

  // 这里的（128）应该设为 gyh_do_call 需要的栈帧大小，执行 gyh_do_call
  // 相当于继续执行客方的被调函数，因此 gyh_do_call 第一条指令会把主侧的 rbp
  // push 到这里（客方）的 (*rsp) 里。
  ctx.uc_link = NULL;
  ctx.uc_stack.ss_sp = (void*)(rsp - 64);
  ctx.uc_stack.ss_size = 64;
  makecontext(&ctx,
              (void*)gyh_do_call_grt,
              4,
              (uint64_t*)env->regs[R_EDI],
              (uint32_t)env->regs[R_ESI],
              (void*)env->regs[R_EDX],
              (void*)env->regs[R_ECX]);

  assert(gyh_cpu_loop_ctx_to == NULL);
  gyh_cpu_loop_ctx_to = &ctx;
  // 等下次 cpu_loop 开始是就会进入到上面的 gyh_do_call 运行了。
}

// ========================================================================== //
// gyhcall FCP-CM
// ========================================================================== //
