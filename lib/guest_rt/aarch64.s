.section .note.GNU-stack,"",%progbits
# prevent the stack from being executable, usual practice.

.text

# ============================================================================ #

.weak   __gyh_ctor__
__gyh_ctor__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # pass the address of cbFunc as arg2.
  adrp    x1, :got:__gyh_callback_return__
  ldr     x1, [x1, :got_lo12:__gyh_callback_return__]

  # x0 - arg1 - qlibUid
  # x1 - arg2 - cbFunc
  mov     w8, #50046
  svc     #0
  # 50046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%x0.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_ctor__, %function

.weak   __gyh_dtor__
__gyh_dtor__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # x0 - arg1 - qlibUid
  mov     w8, #50096
  svc     #0

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_dtor__, %function

.weak   __gyh_call__
__gyh_call__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # x0 - arg1 - qlibUid
  # x1 - arg2 - funcUid
  # x2 - arg3 - retPad
  # x3 - arg4 - argPad
  # x4 - arg5 - extRefs
  # x5 - arg6 - cbStubs
  mov     w8, #50406
  svc     #0
  # 50406 syscall will execute __gyh_hfunctbl__[%x0],
  # behaving like the function body.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_call__, %function

.weak   __gyh_callback_return__
__gyh_callback_return__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # x0 - arg1 - retPad
  # x1 - arg2 - argPad
  # x2 - arg3 - gfunc
  # x3 - arg4 - cbStub
  blr     x3
  # callback stubs always return void.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16

  mov     w8, #50906
  svc     #0
  # 50906 syscall will behave like a ret instruction:
  # 1. set pc to the return address in lr/x30.
.type   __gyh_callback_return__, %function

# ============================================================================ #
# GRT enabled
# ============================================================================ #

.weak   __gyh_ctor_grt__
__gyh_ctor_grt__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # pass the address of cbFunc as arg4.
  adrp    x3, :got:__gyh_callback_return__
  ldr     x3, [x3, #:got_lo12:__gyh_callback_return__]

  # x0 - arg1 - qlibUid
  # x1 - arg2 - extreftbl
  # x2 - arg3 - cbstubtbl
  # x3 - arg4 - cbFunc
  mov     w8, #51046
  svc     #0
  # 51046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%x0.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_ctor_grt__, %function

.weak   __gyh_call_grt__
__gyh_call_grt__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # x0 - arg1 - qlibUid
  # x1 - arg2 - funcUid
  # x2 - arg3 - retPad
  # x3 - arg4 - argPad
  mov     w8, #51406
  svc     #0
  # 51406 syscall will execute __gyh_hfunctbl__[%x0],
  # behaving like the function body.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_call_grt__, %function

# ============================================================================ #
# FCP-CM enabled
# ============================================================================ #

.weak   __gyh_ctor_fcp_cm__
__gyh_ctor_fcp_cm__:
  # pretent to be a normal function call.
  stp     fp, lr, [sp, -16]!
  mov     fp, sp

  # pass the address of cbFunc as arg6.
  adrp    x5, :got:__gyh_callback_return__
  ldr     x5, [x5, :got_lo12:__gyh_callback_return__]

  # x0 - arg1 - qlibUid
  # x1 - arg2 - extreftbl
  # x2 - arg3 - cbstubtbl
  # x3 - arg4 - gfunctbl
  # x4 - arg5 - gfunctblSize
  # x5 - arg6 - cbFunc
  mov     w8, #52046
  svc     #0
  # 52046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%x0.

  # pretent to be a normal function return.
  ldp     fp, lr, [sp], 16
  ret
.type   __gyh_ctor_fcp_cm__, %function
