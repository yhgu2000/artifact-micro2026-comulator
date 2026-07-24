#ifndef __GYH_H__
#define __GYH_H__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ucontext.h>

#include "qemu/osdep.h"
#include "qemu/units.h"

// #include "qapi/error.h"
#include "qemu.h"
#include "user-internals.h"

/**
 * @brief 在整个模拟开始前调用的初始化函数.
 */
void
gyh_cpu_loop_init(CPUArchState* env);

extern long long gyh_breakdown_emu_nsecs;
extern long long gyh_breakdown_syscalls;
extern long long gyh_breakdown_calls;
extern long long gyh_breakdown_callbacks;
extern long long gyh_breakdown_callback_returns;

/**
 * @brief 在被模拟的进程退出时调用的清理函数.
 */
void
gyh_atexit(void);

/**
 * @brief QEMU 模拟的主入口.
 *
 * QEMU 对这个函数的要求是它决不返回, 也就是说这个函数就描述了 QEMU 的整个
 * 模拟过程.
 *
 * @note
 * 我修改了 main.c 劫持了原来的 cpu_loop() 调用, 一旦 QEMU 完成初始化,
 * 就会反而进入这个函数执行, 如果在里面直接调 cpu_loop(), 那么 QEMU
 * 的行为就和之前完全一样了.
 */
void
gyh_cpu_loop(CPUArchState* env);

#define GYH_NR_start 50000

struct gyh_DoSyscall
{
  int num;
  abi_long arg1;
  abi_long arg2;
  abi_long arg3;
  abi_long arg4;
  abi_long arg5;
  abi_long arg6;
  abi_long arg7;
  abi_long arg8;
};

/**
 * @brief 模拟“一次” CPU 运行, 直到触发系统调用等中断.
 *
 * 如果触发了我们的系统调用, 将系统调用参数置入 gyh_syscall 中.
 * 否则将 gyh_syscall 中的参数置为 0.
 *
 * 为了避免对 QEMU 的模拟解释产生干扰, 我们的系统调用会统一在
 * gyh_cpu_loop 的循环结束时处理.
 *
 * @note
 * 这个函数是体系结构相关的, 写在 xxx-on.c 里, 其实现大部分就是抄直接
 * QEMU 的 cpu_loop() 里的循环体, 只不过按需修改了返回值.
 */
void
gyh_do_cpu(CPUArchState* env, struct gyh_DoSyscall* gyh_syscall);

// ========================================================================== //
// gyhcall
// ========================================================================== //

/**
 * @brief gyhlib.so 中代码调用的调试打印函数, 参数同 printf.
 */
void
__gyh_debug__(const char* fmt, ...);

/// 卸载到宿主侧的承接函数都拥有统一的签名
typedef void (*gyh_HFunc)(void* retPad,
                          void* argPad,
                          void* extRefPad,
                          void* cbStubPad);

/**
 * @brief gyhlib.so 中代码的回调触发函数
 *
 * 依次执行以下操作:
 *
 * 1. 保存 gyh_cpu_loop_env 的相关寄存器 (通常包括程序计数器、栈指针、
 *    基指针等系统调用被调方保存的寄存器);
 * 2. 保存当前模拟过程的上下文和 gyh_cpu_loop_env 的到客方栈上;
 * 3. 将上下文指针、func、retPad、argPad 按客方调用约定设置给 gyh_cpu_loop_env;
 * 4. 切换回 gyh_cpu_loop_ctx 继续执行模拟;
 * 5. 如果客方函数正常返回, 其会触发 callback_return 系统调用, 进而回到这里继续
 *    执行, 因此需要在最后恢复 gyh_cpu_loop_env 的相关寄存器状态.
 */
void
__gyh_callback__(void* cbStub, void* func, void* retPad, void* argPad);

#define GYH_NR__gyh_ctor__ 50046
#define GYH_NR__gyh_dtor__ 50096

#define GYH_NR__gyh_call__ 50406
#define GYH_NR__gyh_callback_return__ 50906

/// 当前 cpu_loop 的 env 参数, 用于跨上下文的参数传递
extern _Thread_local CPUArchState* gyh_cpu_loop_env;
/// 当前 cpu_loop 的执行上下文, 切换到它以继续执行模拟
extern _Thread_local ucontext_t gyh_cpu_loop_ctx;
/// 指示 cpu_loop 在下次循环开始时切换到其它上下文执行
extern _Thread_local ucontext_t* gyh_cpu_loop_ctx_to;
/// 主客回调时的客方入口地址, 在客方库加载时由客方传递过来
extern target_ulong gyh_cbFunc;

/**
 * @brief 处理 __gyh_ctor__ 系统调用.
 */
void
gyh_ctor(uint32_t* qlibUid, target_ulong cbFunc);

/**
 * @brief 处理 __gyh_dtor__ 系统调用.
 */
void
gyh_dtor(uint32_t* qlibUid);

/**
 * @brief 处理 __gyh_call__ 系统调用.
 *
 * 依次执行以下操作:
 *
 * 1. 从 env 中读出系统调用参数, 包括函数号、参数和返回值结构体地址等;
 * 2. 保存当前 cpu_loop 的模拟上下文;
 * 3. 创建 gyh_do_call 的上下文并设置 gyh_cpu_loop_ctx_to.
 *
 * 在下次 cpu_loop 开始时, 就会切换到 gyh_do_call 继续执行.
 *
 * 当前 cpu_loop 模拟过程的 CPUArchState* env 通过 gyh_cpu_loop_env 传递
 *
 * @note 体系相关
 */
void
gyh_call(void);

/**
 * @brief 真正执行 gyhcall 主侧调用.
 */
void
gyh_do_call(uint32_t* qlibUid,
            uint32_t funcUid,
            void* retPad,
            void* argPad,
            void* extRefs,
            void* cbStubs);

/**
 * @brief 处理 __gyh_callback_return__ 系统调用.
 *
 * 依次执行以下操作:
 *
 * 1. 从 env 中读出系统调用参数, 为一个 ucontext_t 指针;
 * 2. 设置 gyh_cpu_loop_ctx_to 为上述指针;
 * 3. 直接返回.
 *
 * 在下次 cpu_loop 开始时, 就会切换到 gyh_do_call 继续执行.
 *
 * 当前 cpu_loop 模拟过程的 CPUArchState* env 通过 gyh_cpu_loop_env 传递
 *
 * @note 体系相关
 */
void
gyh_callback_return(void);

// ========================================================================== //
// gyhcall GRT
// ========================================================================== //

/// GRT 优化的承接函数签名
typedef void (*gyh_HFuncGRT)(void* retPad, void* argPad);

#define GYH_NR__gyh_ctor_grt__ 51046
#define GYH_NR__gyh_call_grt__ 51406

void
gyh_ctor_grt(uint32_t* qlibUid,
             void* extreftbl,
             void* cbstubtbl,
             target_ulong cbFunc);

/**
 * @note 体系相关
 */
void
gyh_call_grt(void);

void
gyh_do_call_grt(uint32_t* qlibUid,
                uint32_t funcUid,
                void* retPad,
                void* argPad);

// ========================================================================== //
// gyhcall FCP-CM
// ========================================================================== //

#define GYH_NR__gyh_ctor_fcp_cm__ 52046

void
gyh_ctor_fcp_cm(uint32_t* qlibUid,
                void* extreftbl,
                void* cbstubtbl,
                uint64_t** gfunctbl,
                uint32_t gfunctblSize,
                target_ulong cbFunc);

#endif // __GYH_H__
