#include "gyh.h"
#include "cpu_loop-common.h"
#include "signal-common.h"
#include <dlfcn.h>
#include <time.h>

static const char* gyhcall_config_dir;

void
gyh_cpu_loop_init(CPUArchState* env)
{
  gyhcall_config_dir = getenv("GYHCALL_CONFIG_DIR");
  if (!gyhcall_config_dir) {
    fprintf(stderr, "environment 'GYHCALL_CONFIG_DIR' is not set.\n");
    abort();
  }
}

long long gyh_breakdown_emu_nsecs = 0;
long long gyh_breakdown_syscalls = 0;
long long gyh_breakdown_calls = 0;
long long gyh_breakdown_callbacks = 0;
long long gyh_breakdown_callback_returns = 0;

void
gyh_atexit(void)
{
  char* timing = getenv("GYHCALL_BREAKDOWN_FILE");
  if (!timing)
    return;
  FILE* file = fopen(timing, "w");
  fprintf(file,
          "emu_nsecs %lld\n"
          "syscalls %lld\n"
          "calls %lld\n"
          "callbacks %lld\n"
          "callback_returns %lld\n",
          gyh_breakdown_emu_nsecs,
          gyh_breakdown_syscalls,
          gyh_breakdown_calls,
          gyh_breakdown_callbacks,
          gyh_breakdown_callback_returns);
  fclose(file);
}

void
gyh_cpu_loop(CPUArchState* env)
{
  assert(!have_guest_base);
  gyh_cpu_loop_env = env;

  for (;;) {
    if (gyh_cpu_loop_ctx_to) {
      qemu_log_mask(GYH_LOG, "||=== cpu_loop >>>>> %p\n", gyh_cpu_loop_ctx_to);
      int ok = swapcontext(&gyh_cpu_loop_ctx, gyh_cpu_loop_ctx_to);
      assert(ok == 0);
    }

    struct timespec ts_begin, ts_end;
    timespec_get(&ts_begin, TIME_UTC);

    struct gyh_DoSyscall gyh_syscall;
    gyh_do_cpu(env, &gyh_syscall);

    timespec_get(&ts_end, TIME_UTC);
    gyh_breakdown_emu_nsecs +=
      (long long)(ts_end.tv_sec - ts_begin.tv_sec) * 1000000000 +
      (ts_end.tv_nsec - ts_begin.tv_nsec);

    switch (gyh_syscall.num) {
      case 0:
        continue;

      /* gyhcall */
      case GYH_NR__gyh_ctor__:
        gyh_ctor((uint32_t*)gyh_syscall.arg1, gyh_syscall.arg2);
        break;
      case GYH_NR__gyh_dtor__:
        gyh_dtor((uint32_t*)gyh_syscall.arg1);
        break;
      case GYH_NR__gyh_call__:
        ++gyh_breakdown_calls;
        gyh_call();
        break;
      case GYH_NR__gyh_callback_return__:
        ++gyh_breakdown_callback_returns;
        gyh_callback_return();
        break;

      /* gyhcall GRT */
      case GYH_NR__gyh_ctor_grt__:
        gyh_ctor_grt((uint32_t*)gyh_syscall.arg1,
                     (void*)gyh_syscall.arg2,
                     (void*)gyh_syscall.arg3,
                     gyh_syscall.arg4);
        break;
      case GYH_NR__gyh_call_grt__:
        ++gyh_breakdown_calls;
        gyh_call_grt();
        break;

      /* gyhcall FCP-CM */
      case GYH_NR__gyh_ctor_fcp_cm__:
        gyh_ctor_fcp_cm((uint32_t*)gyh_syscall.arg1,
                        (void*)gyh_syscall.arg2,
                        (void*)gyh_syscall.arg3,
                        (uint64_t**)gyh_syscall.arg4,
                        (uint32_t)gyh_syscall.arg5,
                        gyh_syscall.arg6);
        break;

      default:
        qemu_log_mask(
          GYH_LOG, "||=== Invalid gyh syscall: %d\n", gyh_syscall.num);
        abort();
    }
    ++gyh_breakdown_syscalls;
  }
}

// ========================================================================== //
// gyhcall
// ========================================================================== //

void
__gyh_debug__(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  if (unlikely(qemu_loglevel_mask(GYH_LOG))) {
    FILE* f = qemu_log_trylock();
    if (f) {
      va_list ap;
      va_start(ap, fmt);
      vfprintf(f, fmt, ap);
      va_end(ap);
      qemu_log_unlock(f);
    }
  }
  va_end(args);
}

_Thread_local CPUArchState* gyh_cpu_loop_env = NULL;
_Thread_local ucontext_t gyh_cpu_loop_ctx;
_Thread_local ucontext_t* gyh_cpu_loop_ctx_to = NULL;
target_ulong gyh_cbFunc = 0;

struct gyh_Qlib
{
  void* handle;
  uint32_t uid;
  void** hfunctbl;
  uint32_t hfunctblSize;
  void* extreftbl;
  void* cbstubtbl;
};

/// 运行时 qso 加载表
#define gyh_qlibs_size 1024
static struct gyh_Qlib gyh_qlibs[gyh_qlibs_size];

static struct gyh_Qlib*
gyh_do_ctor(uint32_t* qlibUid, target_ulong cbFunc)
{
  if (gyh_cbFunc == 0)
    gyh_cbFunc = cbFunc;
  else if (gyh_cbFunc != cbFunc) {
    fprintf(stderr,
            "cbFunc old %08X != new %08X\n",
            (uint32_t)gyh_cbFunc,
            (uint32_t)cbFunc);
    abort();
  }

  size_t qlibIdx;
  for (qlibIdx = 0; qlibIdx < gyh_qlibs_size; ++qlibIdx) {
    if (gyh_qlibs[qlibIdx].handle == NULL)
      break;
  }
  if (qlibIdx == gyh_qlibs_size) {
    fprintf(stderr, "fail to assign index for qlib %08X\n", *qlibUid);
    abort();
  }
  struct gyh_Qlib* qlib = gyh_qlibs + qlibIdx;

  char qlibPath[1024];
  sprintf(qlibPath, "%s/host/%08X.qso", gyhcall_config_dir, *qlibUid);
  qlib->handle = dlopen(qlibPath, RTLD_NOW);
  if (!qlib->handle) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  qlib->uid = *qlibUid;

  char symbol[1024];
  sprintf(symbol, "__gyh_hfunctbl_%08X", qlib->uid);
  qlib->hfunctbl = dlsym(qlib->handle, symbol);
  if (!qlib->hfunctbl) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  sprintf(symbol, "__gyh_hfunctbl_size_%08X", qlib->uid);
  uint32_t* hfunctblSize = (uint32_t*)dlsym(qlib->handle, symbol);
  if (!hfunctblSize) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  qlib->hfunctblSize = *hfunctblSize;

  *qlibUid = qlibIdx;
  return qlib;
}

void
gyh_ctor(uint32_t* qlibUid, target_ulong cbFunc)
{
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_ctor__<%08X>(%p, %p)\n",
                *qlibUid,
                qlibUid,
                (void*)cbFunc);

  gyh_do_ctor(qlibUid, cbFunc);
}

void
gyh_dtor(uint32_t* qlibUid)
{
  qemu_log_mask(GYH_LOG, "||=== __gyh_dtor__<%08X>(%p)\n", *qlibUid, qlibUid);

  if (*qlibUid >= gyh_qlibs_size) {
    fprintf(stderr, "qlib[%d] is out of range\n", *qlibUid);
    abort();
  }

  struct gyh_Qlib* qlib = gyh_qlibs + *qlibUid;
  if (qlib->handle == NULL) {
    fprintf(stderr, "qlib[%d] is not set\n", *qlibUid);
    abort();
  }

  if (dlclose(qlib->handle)) {
    fprintf(stderr, "%s\n", dlerror());
    abort();
  }

  *qlibUid = qlib->uid;
}

void
gyh_do_call(uint32_t* qlibUid,
            uint32_t funcUid,
            void* retPad,
            void* argPad,
            void* extRefs,
            void* cbStubs)
{
  //* 这里的代码是在客方的栈上工作！
  ucontext_t* ctx;
  ctx = gyh_cpu_loop_ctx_to;  // 保存之前的上下文, 把全局指针空出来
  gyh_cpu_loop_ctx_to = NULL; // 切换已经完成, 重置它, 这很重要！

  if (*qlibUid >= gyh_qlibs_size) {
    fprintf(stderr, "qlib[%d] is out of range\n", *qlibUid);
    abort();
  }
  struct gyh_Qlib* qlib = gyh_qlibs + *qlibUid;
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_call__<%08X,%u>(%p, %p, %p, %p)\n",
                qlib->uid,
                funcUid,
                retPad,
                argPad,
                extRefs,
                cbStubs);

  if (funcUid > qlib->hfunctblSize) {
    fprintf(stderr,
            "funcUid (%u) is out of range (%u) for '%08X'\n",
            funcUid,
            qlib->hfunctblSize,
            qlib->uid);
    abort();
  }

  gyh_HFunc func = qlib->hfunctbl[funcUid];
  func(retPad, argPad, extRefs, cbStubs);

  // 如果 func 运行返回，就回到 CPU 循环里继续模拟执行客方代码。
  qemu_log_mask(GYH_LOG, "||=== cpu_loop <<<<< %p\n", ctx);
  setcontext(&gyh_cpu_loop_ctx);
}

// ========================================================================== //
// gyhcall GRT
// ========================================================================== //

static struct gyh_Qlib*
gyh_do_ctor_grt(uint32_t* qlibUid,
                void* extreftbl,
                void* cbstubtbl,
                target_ulong cbFunc)
{
  struct gyh_Qlib* qlib = gyh_do_ctor(qlibUid, cbFunc);

  char symbol[1024];
  sprintf(symbol, "__gyh_extreftbl_%08X", qlib->uid);
  void** extreftblPtr = dlsym(qlib->handle, symbol);
  if (!extreftblPtr) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  qlib->extreftbl = *extreftblPtr = extreftbl;

  sprintf(symbol, "__gyh_cbstubtbl_%08X", qlib->uid);
  void** cbstubtblPtr = dlsym(qlib->handle, symbol);
  if (!cbstubtblPtr) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  qlib->cbstubtbl = *cbstubtblPtr = cbstubtbl;

  return qlib;
}

void
gyh_ctor_grt(uint32_t* qlibUid,
             void* extreftbl,
             void* cbstubtbl,
             target_ulong cbFunc)
{
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_ctor_grt__<%08X>(%p, %p, %p)\n",
                *qlibUid,
                qlibUid,
                extreftbl,
                cbstubtbl);

  gyh_do_ctor_grt(qlibUid, extreftbl, cbstubtbl, cbFunc);
}

void
gyh_do_call_grt(uint32_t* qlibUid, uint32_t funcUid, void* retPad, void* argPad)
{
  //* 这里的代码是在客方的栈上工作！
  ucontext_t* ctx;
  ctx = gyh_cpu_loop_ctx_to;  // 保存之前的上下文, 把全局指针空出来
  gyh_cpu_loop_ctx_to = NULL; // 切换已经完成, 重置它, 这很重要！

  if (*qlibUid >= gyh_qlibs_size) {
    fprintf(stderr, "qlib[%d] is out of range\n", *qlibUid);
    abort();
  }
  struct gyh_Qlib* qlib = gyh_qlibs + *qlibUid;
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_call_grt__<%08X,%u>(%p, %p)\n",
                qlib->uid,
                funcUid,
                retPad,
                argPad);

  if (funcUid > qlib->hfunctblSize) {
    fprintf(stderr,
            "funcUid (%u) is out of range (%u) for '%08X'\n",
            funcUid,
            qlib->hfunctblSize,
            qlib->uid);
    abort();
  }

  gyh_HFuncGRT func = qlib->hfunctbl[funcUid];
  func(retPad, argPad);

  // 如果 func 运行返回，就回到 CPU 循环里继续模拟执行客方代码。
  qemu_log_mask(GYH_LOG, "||=== cpu_loop <<<<< %p\n", ctx);
  setcontext(&gyh_cpu_loop_ctx);
}

// ========================================================================== //
// gyhcall FCP-CM
// ========================================================================== //

static const uint64_t kFCPcmMagic = 0x6666200009068888;

static struct gyh_Qlib*
gyh_do_ctor_fcp_cm(uint32_t* qlibUid,
                   void* extreftbl,
                   void* cbstubtbl,
                   uint64_t** gfunctbl,
                   uint32_t gfunctblSize,
                   target_ulong cbFunc)
{

  struct gyh_Qlib* qlib =
    gyh_do_ctor_grt(qlibUid, extreftbl, cbstubtbl, cbFunc);

  char symbol[1024];
  sprintf(symbol, "__gyh_functbl_%08X", qlib->uid);
  void** functblPtr = dlsym(qlib->handle, symbol);
  if (!functblPtr) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }

  uintptr_t pageSize = sysconf(_SC_PAGE_SIZE);
  uintptr_t pageBegin = 0, pageEnd = 0;
  for (uint32_t i = 0; i < gfunctblSize; ++i) {
    uint64_t* magic = gfunctbl[i] - 2;
    if (*magic != kFCPcmMagic) {
      fprintf(stderr,
              "bad magic (%016lX) for gfunctbl[%d]: %p\n",
              *magic,
              i,
              gfunctbl[i]);
      abort();
    }
    uint64_t* gfuncId = gfunctbl[i] - 1;

    if ((uintptr_t)magic < pageBegin || (uintptr_t)gfunctbl[i] >= pageEnd) {
      // // 复原之前代码段权限
      // if ((pageEnd - pageBegin) != 0) {
      //   if (mprotect((void*)pageBegin, pageSize, PROT_READ | PROT_EXEC)) {
      //     perror("mprotect");
      //     abort();
      //   }
      // }
      // 设置当前代码段为可读写
      pageBegin = (uintptr_t)magic & ~(pageSize - 1);
      pageEnd = ((uintptr_t)gfunctbl[i] + pageSize) & ~(pageSize - 1);
      mprotect((void*)pageBegin,
               pageEnd - pageBegin,
               PROT_READ | PROT_WRITE | PROT_EXEC);
    }

    *gfuncId = (uint64_t)functblPtr[i];
    *magic = kFCPcmMagic ^ *gfuncId;
  }

  // // 复原之前代码段权限
  // if ((pageEnd - pageBegin) != 0) {
  //   if (mprotect((void*)pageBegin, pageSize, PROT_READ | PROT_EXEC)) {
  //     perror("mprotect");
  //     abort();
  //   }
  // }

  return qlib;
}

void
gyh_ctor_fcp_cm(uint32_t* qlibUid,
                void* extreftbl,
                void* cbstubtbl,
                uint64_t** gfunctbl,
                uint32_t gfunctblSize,
                target_ulong cbFunc)
{
  qemu_log_mask(GYH_LOG,
                "||=== __gyh_ctor_fcp_cm__<%08X>(%p, %p, %p, %p, %u)\n",
                *qlibUid,
                qlibUid,
                extreftbl,
                cbstubtbl,
                gfunctbl,
                gfunctblSize);

  gyh_do_ctor_fcp_cm(
    qlibUid, extreftbl, cbstubtbl, gfunctbl, gfunctblSize, cbFunc);
}
