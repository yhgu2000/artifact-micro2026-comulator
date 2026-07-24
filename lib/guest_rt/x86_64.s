.section .note.GNU-stack,"",@progbits
// prevent the stack from being executable, usual practice.

.text

// ========================================================================== //

.weak   __gyh_ctor__
__gyh_ctor__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // pass the address of cbFunc as arg2.
  mov     __gyh_callback_return__@GOTPCREL(%rip), %rsi

  // %rdi - arg1 - qlibUid
  // %rsi - arg2 - cbFunc
  movq    $50046, %rax
  syscall
  /** 50046 syscall will remember all the arguments,
   * then assigns an uint32_t index number to *%rdi.
   */

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_ctor__, %function

.weak   __gyh_dtor__
__gyh_dtor__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // %rdi - arg1 - qlibUid
  movq    $50096, %rax

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_dtor__, %function

.weak   __gyh_call__
__gyh_call__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // 注意: x86_64 的系统调用使用 r10 传递第四个参数 (历史遗留问题导致),
  // 但是我们遵循过程调用约定, 使用 rcx 传递第四个参数!

  // %rdi - arg1 - qlibUid
  // %rsi - arg2 - funcUid
  // %rdx - arg3 - retPad
  // %rcx - arg4 - argPad
  // %r8  - arg5 - extRefs
  // %r9  - arg6 - cbStubs
  movq    $50406, %rax
  syscall
  /** 50406 syscall will execute __gyh_hfunctbl__[%rdi],
   * behaving like the function body.
   */

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_call__, %function

.weak   __gyh_callback_return__
__gyh_callback_return__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // %rdi - arg1 - retPad
  // %rsi - arg2 - argPad
  // %rdx - arg3 - gfunc
  // %rcx - arg4 - cbStub
  call    *%rcx
  // callback stubs always return void.

  // pretent to be a normal function return.
  popq    %rbp

  movq    $50906, %rax
  syscall
  /** 50906 syscall will behave like a ret instruction:
   * 1. take the return address at *%rsp;
   * 2. increase %rsp by 8;
   * 3. set rip to the return address.
   */
.type   __gyh_callback_return__, %function

// ========================================================================== //
// GRT enabled
// ========================================================================== //

.weak   __gyh_ctor_grt__
__gyh_ctor_grt__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // pass the address of cbFunc as arg4.
  mov     __gyh_callback_return__@GOTPCREL(%rip), %rcx

  // %rdi - arg1 - qlibUid
  // %rsi - arg2 - extreftbl
  // %rdx - arg3 - cbstubtbl
  // %rcx - arg4 - cbFunc
  movq    $51046, %rax
  syscall
  /** 51046 syscall will remember all the arguments,
   * then assigns an uint32_t index number to *%rdi.
   */

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_ctor_grt__, %function

.weak   __gyh_call_grt__
__gyh_call_grt__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // %rdi - arg1 - qlibUid
  // %rsi - arg2 - funcUid
  // %rdx - arg3 - retPad
  // %rcx - arg4 - argPad
  movq    $51406, %rax
  syscall
  /** 51406 syscall will execute __gyh_hfunctbl__[%rdi],
   * behaving like the function body.
   */

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_call_grt__, %function

// ========================================================================== //
// FCP-CM enabled
// ========================================================================== //

.weak   __gyh_ctor_fcp_cm__
__gyh_ctor_fcp_cm__:
  // pretent to be a normal function call.
  pushq   %rbp
  movq    %rsp, %rbp

  // pass the address of cbFunc as arg6.
  mov     __gyh_callback_return__@GOTPCREL(%rip), %r9

  // %rdi - arg1 - qlibUid
  // %rsi - arg2 - extreftbl
  // %rdx - arg3 - cbstubtbl
  // %rcx - arg4 - gfunctbl
  // %r8  - arg5 - gfunctblSize
  // %r9  - arg6 - cbFunc
  movq    $52046, %rax
  syscall
  /** 52046 syscall will remember all the arguments,
   * then assigns an uint32_t index number to *%rdi.
   */

  // pretent to be a normal function return.
  popq    %rbp
  ret
.type   __gyh_ctor_fcp_cm__, %function
