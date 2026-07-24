#define _GNU_SOURCE
#include <alloca.h>
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <threads.h>
#include <ucontext.h>
#include <unistd.h>

// ========================================================================== //
// 客方
// ========================================================================== //

struct Qlib
{
  void* handle;
  uint32_t uid;
  void (**hfunctbl)();
  uint32_t hfunctblSize;
  void* extreftbl;
  void* cbstubtbl;
};

static struct Qlib sQlibs[1024] = { 0 };
static struct Qlib* const kQlibsEnd =
  sQlibs + sizeof(sQlibs) / sizeof(sQlibs[0]);

static struct Qlib*
gyh_ctor(uint32_t* qlibUid)
{
  char* configDir = getenv("GYHCALL_CONFIG_DIR");
  if (!configDir) {
    fprintf(stderr, "environment 'GYHCALL_CONFIG_DIR' is not set\n");
    abort();
  }

  struct Qlib* qlib;
  for (qlib = sQlibs; qlib < kQlibsEnd; qlib++) {
    if (qlib->handle == NULL)
      break;
  }
  if (qlib == kQlibsEnd) {
    fprintf(stderr, "fail to assign index for qlib %08X\n", *qlibUid);
    abort();
  }

  char qlibPath[1024];
  sprintf(qlibPath, "%s/host/%08X.qso", configDir, *qlibUid);
  qlib->handle = dlopen(qlibPath, RTLD_NOW);
  if (!qlib->handle) {
    fprintf(stderr, "%s", dlerror());
    abort();
  }
  qlib->uid = *qlibUid;

  char symbol[1024];
  sprintf(symbol, "__gyh_hfunctbl_%08X", qlib->uid);
  qlib->hfunctbl = (void (**)())dlsym(qlib->handle, symbol);
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

  *qlibUid = qlib - sQlibs;
  return qlib;
}

__attribute__((optnone, weak)) void
__gyh_ctor__(uint32_t* qlibUid)
{
  gyh_ctor(qlibUid);
}

__attribute__((optnone, weak)) void
__gyh_dtor__(uint32_t* qlibUid)
{
  if (*qlibUid >= kQlibsEnd - sQlibs) {
    fprintf(stderr, "qlib[%d] is out of range\n", *qlibUid);
    abort();
  }

  struct Qlib* qlib = sQlibs + *qlibUid;
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

static void*
query_hfunc(uint32_t qlibUid, uint32_t funcUid)
{
  if (qlibUid >= kQlibsEnd - sQlibs) {
    fprintf(stderr, "qlib[%d] is out of range\n", qlibUid);
    abort();
  }
  struct Qlib* qlib = sQlibs + qlibUid;
  if (qlib->handle == NULL) {
    fprintf(stderr, "qlib[%d] is not set\n", qlibUid);
    abort();
  }
  if (funcUid >= qlib->hfunctblSize) {
    fprintf(
      stderr, "func[%d] is out of range of qlib '%08X'\n", funcUid, qlib->uid);
    abort();
  }
  return qlib->hfunctbl[funcUid];
}

struct GyhCallArgs
{
  ucontext_t callSite;
  void* hfunc;
  void* retPad;
  void* argPad;
  void* extRefs;
  void* cbStubs;
};

static void
gyh_call(struct GyhCallArgs* args)
{
  void (*hfunc)(void*, void*, void*, void*) = args->hfunc;
  void* retPad = args->retPad;
  void* argPad = args->argPad;
  void* extRefs = args->extRefs;
  void* cbStubs = args->cbStubs;
  hfunc(retPad, argPad, extRefs, cbStubs);
  setcontext(&args->callSite);
}

__attribute__((optnone, weak)) void
__gyh_call__(uint32_t* qlibUid,
             uint32_t funcUid,
             void* retPad,
             void* argPad,
             void* extRefs,
             void* cbStubs)
{
  ucontext_t callee;
  getcontext(&callee);
  static const int kFrameSize = 256;
  void* stackLow = alloca(kFrameSize);
  //* alloca 的参数不必真的是一个完整的栈大小，只需要能装下 makecontext 的
  //* 第一个栈帧即可，因为被调的栈就是当前的栈，当前栈的空间是足够的。
  //*
  //* 理论上讲 alloca(0) 也应该能工作，因为栈是从高向低增长的，只要知道高地址
  //* 就好了，但是实测小于某个值（估计是一个栈帧大小）的话就会报错，可能
  //* makecontext 里有一些检查。
  uintptr_t stackHigh =
    (uintptr_t)(stackLow + kFrameSize) & ~(uintptr_t)0xf; // 对齐到 16 字节
  callee.uc_stack.ss_sp = stackLow; // 注意这里赋的是低地址！
  callee.uc_stack.ss_size = stackHigh - (uintptr_t)stackLow;

  ucontext_t callSite;
  struct GyhCallArgs args;
  args.hfunc = query_hfunc(*qlibUid, funcUid);
  args.retPad = retPad;
  args.argPad = argPad;
  args.extRefs = extRefs;
  args.cbStubs = cbStubs;
  makecontext(&callee, (void (*)())gyh_call, 1, &args);

  int ok = swapcontext(&args.callSite, &callee);
  assert(ok == 0);
}

__attribute__((optnone, weak)) void
__gyh_callback_return__(void* retPad, void* argPad, void* gfunc, void* cbStub)
{
  void (*f)(void*, void*, void*) = cbStub;
  f(retPad, argPad, gfunc);
}

// === GRT ================================================================== //

static struct Qlib*
gyh_ctor_grt(uint32_t* qlibUid, void* extreftbl, void* cbstubtbl)
{
  struct Qlib* qlib = gyh_ctor(qlibUid);

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

__attribute__((optnone, weak)) void
__gyh_ctor_grt__(uint32_t* qlibUid, void* extreftbl, void* cbstubtbl)
{
  gyh_ctor_grt(qlibUid, extreftbl, cbstubtbl);
}

struct GyhCallGrtArgs
{
  ucontext_t callSite;
  void* hfunc;
  void* retPad;
  void* argPad;
};

static void
gyh_call_grt(struct GyhCallGrtArgs* args)
{
  void (*hfunc)(void*, void*) = args->hfunc;
  void* retPad = args->retPad;
  void* argPad = args->argPad;
  hfunc(retPad, argPad);
  setcontext(&args->callSite);
}

__attribute__((optnone, weak)) void
__gyh_call_grt__(uint32_t* qlibUid,
                 uint32_t funcUid,
                 void* retPad,
                 void* argPad)
{
  ucontext_t callee;
  getcontext(&callee);
  static const int kFrameSize = 256;
  void* stackLow = alloca(kFrameSize);
  //* 见 __gyh_call__ 的注释
  uintptr_t stackHigh = (uintptr_t)(stackLow + kFrameSize) & ~(uintptr_t)0xf;
  callee.uc_stack.ss_sp = stackLow;
  callee.uc_stack.ss_size = stackHigh - (uintptr_t)stackLow;

  ucontext_t callSite;
  struct GyhCallGrtArgs args;
  args.hfunc = query_hfunc(*qlibUid, funcUid);
  args.retPad = retPad;
  args.argPad = argPad;
  makecontext(&callee, (void (*)())gyh_call_grt, 1, &args);

  int ok = swapcontext(&args.callSite, &callee);
  assert(ok == 0);
}

// === FCP-CM =============================================================== //

static const uint64_t kFCPcmMagic = 0x6666200009068888;

__attribute__((optnone, weak)) void
__gyh_ctor_fcp_cm__(uint32_t* qlibUid,
                    void* extreftbl,
                    void* cbstubtbl,
                    uint64_t** gfunctbl,
                    uint32_t gfunctblSize)
{
  struct Qlib* qlib = gyh_ctor_grt(qlibUid, extreftbl, cbstubtbl);

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
}

// ========================================================================== //
// 主侧
// ========================================================================== //

__attribute__((optnone, weak)) void
__gyh_callback__(void* cbStub, void* gfunc, void* retPad, void* argPad)
{
  __gyh_callback_return__(retPad, argPad, gfunc, cbStub);
}
