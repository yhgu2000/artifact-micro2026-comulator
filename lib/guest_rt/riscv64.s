.section .note.GNU-stack,"",@progbits
# prevent the stack from being executable, usual practice.

.text

# ============================================================================ #

.weak   __gyh_ctor__
__gyh_ctor__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # pass the address of cbFunc as arg2.
  la      a1, __gyh_callback_return__

  # a0 - arg1 - qlibUid
  # a1 - arg2 - cbFunc
  li      a7, 50046
  ecall
  # 50046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%a0.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_ctor__, %function

.weak   __gyh_dtor__
__gyh_dtor__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # a0 - arg1 - qlibUid
  li      a7, 50096
  ecall

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_dtor__, %function

.weak   __gyh_call__
__gyh_call__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # a0 - arg1 - qlibUid
  # a1 - arg2 - funcUid
  # a2 - arg3 - retPad
  # a3 - arg4 - argPad
  # a4 - arg5 - extRefs
  # a5 - arg6 - cbStubs
  li      a7, 50406
  ecall
  # 50406 syscall will execute __gyh_hfunctbl__[%a0],
  # behaving like the function body.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_call__, %function

.weak   __gyh_callback_return__
__gyh_callback_return__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # a0 - arg1 - retPad
  # a1 - arg2 - argPad
  # a2 - arg3 - gfunc
  # a3 - arg4 - cbStub
  jalr    a3
  # callback stubs always return void.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16

  li      a7, 50906
  ecall
  # 50906 syscall will behave like a ret instruction:
  # 1. set pc to the return address in ra.
.type   __gyh_callback_return__, %function

# ============================================================================ #
# GRT enabled
# ============================================================================ #

.weak   __gyh_ctor_grt__
__gyh_ctor_grt__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # pass the address of cbFunc as arg4.
  la      a3, __gyh_callback_return__

  # a0 - arg1 - qlibUid
  # a1 - arg2 - extreftbl
  # a2 - arg3 - cbstubtbl
  # a3 - arg4 - cbFunc
  li      a7, 51046
  ecall
  # 51046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%a0.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_ctor_grt__, %function

.weak   __gyh_call_grt__
__gyh_call_grt__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # a0 - arg1 - qlibUid
  # a1 - arg2 - funcUid
  # a2 - arg3 - retPad
  # a3 - arg4 - argPad
  li      a7, 51406
  ecall
  # 51406 syscall will execute __gyh_hfunctbl__[%a0],
  # behaving like the function body.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_call_grt__, %function

# ============================================================================ #
# FCP-CM enabled
# ============================================================================ #

.weak   __gyh_ctor_fcp_cm__
__gyh_ctor_fcp_cm__:
  # pretent to be a normal function call.
  addi    sp, sp, -16
  sd      ra, 8(sp)
  sd      s0, 0(sp)
  mv      s0, sp

  # pass the address of cbFunc as arg6.
  la      a5, __gyh_callback_return__

  # a0 - arg1 - qlibUid
  # a1 - arg2 - extreftbl
  # a2 - arg3 - cbstubtbl
  # a3 - arg4 - gfunctbl
  # a4 - arg5 - gfunctblSize
  # a5 - arg6 - cbFunc
  li      a7, 52046
  ecall
  # 52046 syscall will remember all the arguments,
  # then assigns an uint32_t index number to *%a0.

  # pretent to be a normal function return.
  ld      s0, 0(sp)
  ld      ra, 8(sp)
  addi    sp, sp, 16
  ret
.type   __gyh_ctor_fcp_cm__, %function
