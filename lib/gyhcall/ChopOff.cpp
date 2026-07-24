#include "ChopOff.hpp"
#include "Analyze.hpp"
#include "Logger.hpp"
#include "type_hash.hpp"
#include "util.hpp"
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Base64.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

namespace gyhcall {

namespace {

const unsigned char kModuleTemplate[] = {
#include "guest.ll.chars"
};

void
inject_module(llvm::Module& mod)
{
  auto& ctx = mod.getContext();
  ctx.setDiscardValueNames(false);

  // 加载 IR 模板
  auto memBuf = llvm::MemoryBuffer::getMemBufferCopy(
    { reinterpret_cast<char const*>(kModuleTemplate),
      sizeof(kModuleTemplate) });
  llvm::SMDiagnostic diag;
  auto mod2 = llvm::parseIR(*memBuf, diag, ctx);
  if (!mod2) {
    diag.print("guest.ll", llvm::outs());
    abort();
  }

  // 注入目标模块
  for (auto&& func : mod2->functions()) {
    llvm::Function::Create(
      func.getFunctionType(), func.getLinkage(), func.getName(), &mod)
      ->copyAttributesFrom(&func);
  }
}

} // namespace

llvm::json::Object
ChopOff::Config::dump_json() const
{
  llvm::json::Object jobj;
  jobj["Debug"] = mDebug;
  jobj["UseGRT"] = mUseGRT;
  jobj["UseFCPcm"] = mUseFCPcm;
  jobj["MinBasicBlocks"] = mMinBasicBlocks;
  jobj["MinInstructions"] = mMinInstructions;
  return jobj;
}

llvm::Error
ChopOff::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om ||                                        //
      !om.map("Debug", mDebug) ||                   //
      !om.map("UseGRT", mUseGRT) ||                 //
      !om.map("UseFCPcm", mUseFCPcm) ||             //
      !om.map("MinBasicBlocks", mMinBasicBlocks) || //
      !om.map("MinInstructions", mMinInstructions))
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

void
ChopOff::operator()(llvm::Module& mod)
{
  inject_module(mod);
  mModule = &mod;
  mModuleUidHex = llvm::utohexstr(mSymtbl.mUid, false, 8);

  gen_debug();
  gen_uid();

  mGyhCall = mModule->getFunction("__gyh_call__");
  assert(mGyhCall != nullptr);
  mGyhCallbackReturn = mModule->getFunction("__gyh_callback_return__");
  assert(mGyhCallbackReturn != nullptr);

  if (mConfig.mUseGRT) {
    mGyhCallGRT = mModule->getFunction("__gyh_call_grt__");
    assert(mGyhCallGRT != nullptr);
  }

  auto cbStubs = gen_cb_stubs();
  if (mConfig.mUseGRT)
    gen_grt(cbStubs);
  chop_gfuncs(cbStubs, calc_benefits());
  gen_ctor_dtor();
}

llvm::GlobalVariable*
ChopOff::gen_debug_fmt(const char* fmt)
{
  auto fmtString =
    llvm::ConstantDataArray::getString(mModule->getContext(), fmt);
  return new llvm::GlobalVariable(*mModule,
                                  fmtString->getType(),
                                  true,
                                  llvm::GlobalValue::InternalLinkage,
                                  fmtString,
                                  "__gyh_debug_fmt__");
}

void
ChopOff::gen_debug()
{
  mGyhDebug = mModule->getFunction("__gyh_debug__");
  assert(mGyhDebug != nullptr);
  mGyhCallDebugFmt =
    gen_debug_fmt(mConfig.mUseGRT ? "__g_yh_call_grt__(%p, %d, %p, %p)\n"
                                  : "__g_yh_call__(%p, %d, %p, %p, %p, %p)\n");
  mGyhCallbackDebugFmt = gen_debug_fmt("__g_yh_callback_%016llX(%p, %p, %p)\n");
}

void
ChopOff::gen_uid()
{
  auto gyhUid = llvm::Constant::getIntegerValue(
    llvm::Type::getInt32Ty(mModule->getContext()),
    llvm::APInt(32, mSymtbl.mUid));
  mGyhQlibUid = new llvm::GlobalVariable(*mModule,
                                         gyhUid->getType(),
                                         false,
                                         llvm::GlobalValue::ExternalLinkage,
                                         gyhUid,
                                         "__gyh_qlib_uid_" + mModuleUidHex);
  mGyhQlibUid->setDSOLocal(true);
}

ChopOff::CbStubs
ChopOff::gen_cb_stubs()
{
  auto voidTy = llvm::Type::getVoidTy(mModule->getContext());
  auto ptrTy = llvm::PointerType::get(mModule->getContext(), 0);
  auto cbStubType =
    llvm::FunctionType::get(voidTy, { ptrTy, ptrTy, ptrTy }, false);

  CbStubs cbStubs;
  for (auto&& [hc, st] : mSymtbl.mCallbackStubs) {
    auto stubFunc = llvm::Function::Create(cbStubType,
                                           llvm::GlobalValue::WeakAnyLinkage,
                                           "__gyh_callback_" + hash2str(hc),
                                           *mModule);
    cbStubs.insert({ hc, stubFunc });

    assert(stubFunc->arg_size() == 3);
    auto argIter = stubFunc->arg_begin();
    auto& retPad = *argIter++;
    auto& argPad = *argIter++;
    auto& gfunc = *argIter++;

    // 设定参数属性, 鼓励编译器优化
    retPad.setName("retPad");
    retPad.addAttr(llvm::Attribute::NoAlias);
    retPad.addAttr(llvm::Attribute::NoCapture);
    retPad.addAttr(llvm::Attribute::NoUndef);
    retPad.addAttr(llvm::Attribute::WriteOnly);
    argPad.setName("argPad");
    argPad.addAttr(llvm::Attribute::NoAlias);
    argPad.addAttr(llvm::Attribute::NoCapture);
    argPad.addAttr(llvm::Attribute::NoUndef);
    argPad.addAttr(llvm::Attribute::ReadOnly);
    gfunc.setName("gfunc");
    gfunc.addAttr(llvm::Attribute::NoAlias);
    gfunc.addAttr(llvm::Attribute::NoCapture);
    gfunc.addAttr(llvm::Attribute::NoUndef);
    gfunc.addAttr(llvm::Attribute::ReadOnly);

    auto entry =
      llvm::BasicBlock::Create(mModule->getContext(), "entry", stubFunc);
    llvm::IRBuilder<> irb(entry);

    if (mConfig.mDebug) {
      irb.CreateCall(
        mGyhDebug,
        { mGyhCallbackDebugFmt, irb.getInt64(hc), &gfunc, &retPad, &argPad });
    }

    // 从参数结构体中提取参数
    llvm::SmallVector<llvm::Value*, 8> args;
    if (st.mArg) {
      auto argStruct = irb.CreateLoad(st.mArg, &argPad);
      for (auto&& [i, field] : llvm::enumerate(st.mArg->elements())) {
        auto arg = irb.CreateExtractValue(argStruct, i, "arg");
        args.push_back(arg);
      }
    }

    // 调用原始函数
    auto call = irb.CreateCall(st.mFunc, &gfunc, args);
    call->setAttributes(st.mAttrs);

    // 保存返回值
    if (st.mRet)
      irb.CreateStore(call, &retPad);

    irb.CreateRetVoid();
  }

  return cbStubs;
}

void
ChopOff::gen_grt(const ChopOff::CbStubs& cbStubs)
{
  auto ptrTy = llvm::PointerType::get(mModule->getContext(), 0);

  std::vector<llvm::Constant*> cbstubtbl(mSymtbl.mCallbackStubs.size(),
                                         nullptr);
  for (auto&& [hc, st] : mSymtbl.mCallbackStubs)
    cbstubtbl[st.mUid] = cbStubs.at(hc);
  auto cbstubtblTy = llvm::ArrayType::get(ptrTy, cbstubtbl.size());
  mGyhCbstubtbl =
    new llvm::GlobalVariable(*mModule,
                             cbstubtblTy,
                             true,
                             llvm::GlobalValue::InternalLinkage,
                             llvm::ConstantArray::get(cbstubtblTy, cbstubtbl),
                             "__gyh_cbstubtbl__");

  std::vector<llvm::Constant*> extreftbl(mSymtbl.mGreftbl.size(), nullptr);
  for (auto& [gref, info] : mSymtbl.mGreftbl)
    extreftbl[info.mUid] = gref;
  auto extreftblTy = llvm::ArrayType::get(ptrTy, extreftbl.size());
  mGyhExtreftbl =
    new llvm::GlobalVariable(*mModule,
                             extreftblTy,
                             true,
                             llvm::GlobalValue::InternalLinkage,
                             llvm::ConstantArray::get(extreftblTy, extreftbl),
                             "__gyh_extreftbl__");
}

ChopOff::Benefits
ChopOff::calc_benefits()
{
  Benefits benefits;
  for (auto it = mSymtbl.mFunctbl.begin(), itE = mSymtbl.mFunctbl.end();
       it != itE;
       ++it) {
    auto func = it->first;

    std::optional<bool> bw;
    if (auto it = mBwList.mFuncs.find(func); it != mBwList.mFuncs.end())
      bw = it->second.mChopOff;
    if (bw.has_value() && !bw.value()) {
      mLogger(func, "忽略黑名单中的函数");
      continue;
    }

    if (mConfig.mUseFCPcm) {
      if (func->hasPrefixData())
        mLogger(func, "忽略已有前缀数据的函数");
      else {
        auto i64Ty = llvm::Type::getInt64Ty(func->getContext());
        // 将魔数和函数号放到 gfunc 前缀数据中
        func->setPrefixData(llvm::ConstantArray::get(
          llvm::ArrayType::get(i64Ty, 2),
          { llvm::ConstantInt::get(i64Ty, Symtbl::kFCPcmMagic),
            llvm::ConstantInt::get(i64Ty, it->second.mUid) }));
      }

      // 如果在 FCP-CM 切除名单, 则只取出不切除
      if (mBwList.mFCPcmChop.contains(func) != mBwList.mFCPcmChopOthers)
        goto DONT_CHOP_OFF;
    }

    // 如果基本块或指令数过少, 直接模拟执行可能比一次客主切换调用更快
    if (func->size() > mConfig.mMinBasicBlocks &&
        func->getInstructionCount() > mConfig.mMinInstructions) {
      benefits.push_back(it);
      continue;
    }

  DONT_CHOP_OFF:
    if (bw.has_value() && bw.value())
      func->getContext().diagnose(
        DiagnosticString(("'" + func->getName() + "' 没有被 chop-off").str()));
  }
  return benefits;
}

void
ChopOff::chop_gfuncs(const ChopOff::CbStubs& cbStubs, const Benefits& benefits)
{
  auto ptrTy = llvm::PointerType::get(mModule->getContext(), 0);

  for (auto&& it : benefits) {
    auto func = it->first;
    auto& info = it->second;

    auto prefixData = func->hasPrefixData() ? func->getPrefixData() : nullptr;
    func->deleteBody();
    if (prefixData)
      func->setPrefixData(prefixData);

    auto entry = llvm::BasicBlock::Create(func->getContext(), "entry", func);
    llvm::IRBuilder<> irb(entry);

    // 准备返回值
    llvm::Value* retPad;
    if (info.mStubType->mRet == nullptr)
      retPad = llvm::ConstantPointerNull::get(ptrTy);
    else
      retPad = irb.CreateAlloca(info.mStubType->mRet, nullptr, "retPad");

    // 准备参数结构体
    llvm::Value* argPad;
    if (info.mStubType->mArg == nullptr)
      argPad = llvm::ConstantPointerNull::get(ptrTy);
    else {
      auto argsType = info.mStubType->mArg;
      llvm::Value* argsVal = llvm::UndefValue::get(argsType);
      for (auto&& [i, arg] : llvm::enumerate(func->args()))
        argsVal = irb.CreateInsertValue(argsVal, &arg, { unsigned(i) });
      argPad = irb.CreateAlloca(argsType, nullptr, "argPad");
      irb.CreateStore(argsVal, argPad);
    }

    if (!mConfig.mUseGRT) {
      llvm::Value* extRefArg;
      llvm::Value* cbStubArg;

      // 准备外部引用
      if (info.mExtRefArgs.empty())
        extRefArg = llvm::ConstantPointerNull::get(ptrTy);
      else {
        auto padType =
          llvm::ArrayType::get(irb.getPtrTy(), info.mExtRefArgs.size());
        llvm::Value* val = llvm::UndefValue::get(padType);
        for (auto&& [i, extRef] : llvm::enumerate(info.mExtRefArgs))
          val = irb.CreateInsertValue(val, extRef, { unsigned(i) });
        extRefArg = irb.CreateAlloca(padType, nullptr, "extRefPad");
        irb.CreateStore(val, extRefArg);
      }

      // 准备回调桩表
      if (info.mCbStubArgs.empty())
        cbStubArg = llvm::ConstantPointerNull::get(ptrTy);
      else {
        auto padType =
          llvm::ArrayType::get(irb.getPtrTy(), info.mCbStubArgs.size());
        llvm::Value* val = llvm::UndefValue::get(padType);
        for (auto&& [i, cbStub] : llvm::enumerate(info.mCbStubArgs)) {
          auto it = cbStubs.find(cbStub);
          assert(it != cbStubs.end());
          val = irb.CreateInsertValue(val, it->second, { unsigned(i) });
        }
        cbStubArg = irb.CreateAlloca(padType, nullptr, "cbStubPad");
        irb.CreateStore(val, cbStubArg);
      }

      // 触发客主调用
      if (mConfig.mDebug) {
        irb.CreateCall(mGyhDebug,
                       { mGyhCallDebugFmt,
                         mGyhQlibUid,
                         irb.getInt32(info.mUid),
                         retPad,
                         argPad,
                         extRefArg,
                         cbStubArg });
      }
      irb.CreateCall(mGyhCall,
                     { mGyhQlibUid,
                       irb.getInt32(info.mUid),
                       retPad,
                       argPad,
                       extRefArg,
                       cbStubArg });
    }

    else {
      // 触发 GRT 客主调用
      if (mConfig.mDebug)
        irb.CreateCall(mGyhDebug,
                       { mGyhCallDebugFmt,
                         mGyhQlibUid,
                         irb.getInt32(info.mUid),
                         retPad,
                         argPad });
      irb.CreateCall(mGyhCallGRT,
                     { mGyhQlibUid, irb.getInt32(info.mUid), retPad, argPad });
    }

    // 获取返回值
    if (info.mStubType->mRet == nullptr)
      irb.CreateRetVoid();
    else {
      auto retVal = irb.CreateLoad(info.mStubType->mRet, retPad, "ret");
      irb.CreateRet(retVal);
    }
  }
}

void
ChopOff::gen_ctor_dtor()
{
  auto torFuncTy = llvm::FunctionType::get(
    llvm::Type::getVoidTy(mModule->getContext()), {}, false);

  mGyhCtorHHH = llvm::Function::Create(torFuncTy,
                                       llvm::GlobalValue::ExternalLinkage,
                                       "__gyh_ctor_" + mModuleUidHex,
                                       *mModule);
  {
    auto entry =
      llvm::BasicBlock::Create(mModule->getContext(), "entry", mGyhCtorHHH);
    llvm::IRBuilder<> irb(entry);
    if (!mConfig.mUseGRT) {
      mGyhCtor = mModule->getFunction("__gyh_ctor__");
      assert(mGyhCtor != nullptr);

      if (mConfig.mDebug)
        irb.CreateCall(mGyhDebug,
                       { gen_debug_fmt("__g_yh_ctor__(%p)\n"), mGyhQlibUid });
      irb.CreateCall(mGyhCtor, { mGyhQlibUid });
    }

    else if (!mConfig.mUseFCPcm) {
      mGyhCtorGRT = mModule->getFunction("__gyh_ctor_grt__");
      assert(mGyhCtorGRT != nullptr);

      if (mConfig.mDebug)
        irb.CreateCall(mGyhDebug,
                       { gen_debug_fmt("__g_yh_ctor_grt__(%p, %p, %p)\n"),
                         mGyhQlibUid,
                         mGyhExtreftbl,
                         mGyhCbstubtbl });
      irb.CreateCall(mGyhCtorGRT,
                     { mGyhQlibUid, mGyhExtreftbl, mGyhCbstubtbl });
    }

    else {
      mGyhCtorFCPcm = mModule->getFunction("__gyh_ctor_fcp_cm__");
      assert(mGyhCtorFCPcm != nullptr);

      std::vector<llvm::Constant*> gfuncs(mSymtbl.mFunctbl.size(), nullptr);
      for (auto&& [func, info] : mSymtbl.mFunctbl)
        gfuncs[info.mUid] = func;
      auto gfunctblInit = llvm::ConstantArray::get(
        llvm::ArrayType::get(irb.getPtrTy(), mSymtbl.mFunctbl.size()), gfuncs);
      mGyhGfunctbl =
        new llvm::GlobalVariable(*mModule,
                                 gfunctblInit->getType(),
                                 true,
                                 llvm::GlobalValue::InternalLinkage,
                                 gfunctblInit,
                                 "__gyh_gfunctbl__");

      if (mConfig.mDebug)
        irb.CreateCall(
          mGyhDebug,
          { gen_debug_fmt("__g_yh_ctor_fcp_cm__(%p, %p, %p, %p, %d)\n"),
            mGyhQlibUid,
            mGyhExtreftbl,
            mGyhCbstubtbl,
            mGyhGfunctbl,
            irb.getInt32(mSymtbl.mFunctbl.size()) });
      irb.CreateCall(mGyhCtorFCPcm,
                     { mGyhQlibUid,
                       mGyhExtreftbl,
                       mGyhCbstubtbl,
                       mGyhGfunctbl,
                       irb.getInt32(mSymtbl.mFunctbl.size()) });
    }
    irb.CreateRetVoid();
  }
  llvm::appendToGlobalCtors(*mModule, mGyhCtorHHH, 0);

  mGyhDtorHHH = llvm::Function::Create(torFuncTy,
                                       llvm::GlobalValue::ExternalLinkage,
                                       "__gyh_dtor_" + mModuleUidHex,
                                       *mModule);
  {
    mGyhDtor = mModule->getFunction("__gyh_dtor__");
    assert(mGyhDtor != nullptr);

    auto entry =
      llvm::BasicBlock::Create(mModule->getContext(), "entry", mGyhDtorHHH);
    llvm::IRBuilder<> irb(entry);

    if (mConfig.mDebug)
      irb.CreateCall(mGyhDebug,
                     { gen_debug_fmt("__g_yh_dtor__(%p)\n"), mGyhQlibUid });
    irb.CreateCall(mGyhDtor, { mGyhQlibUid });
    irb.CreateRetVoid();
  }
  llvm::appendToGlobalDtors(*mModule, mGyhDtorHHH, 0);
}

llvm::PreservedAnalyses
ChopOff::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  auto& analysis = mam.getResult<Analyze::Pass>(mod);
  std::unique_ptr<Logger> logger;
  if (!mConfig.mLogPath.empty()) {
    std::error_code ec;
    logger = std::make_unique<FileLogger>(mConfig.mLogPath, ec);
    if (ec) {
      mod.getContext().diagnose(DiagnosticString(
        "fail to open log '" + mConfig.mLogPath + "':" + ec.message(),
        llvm::DS_Warning));
      logger.reset();
    }
  }
  if (!logger)
    logger = std::make_unique<DiagnosticLogger>(mod.getContext());
  ChopOff chopOff(mConfig, analysis, *logger);
  chopOff(mod);

  auto pa = llvm::PreservedAnalyses::none();
  pa.preserve<Analyze::Pass>();
  return pa;
}

llvm::json::Object
ChopOff::Pass::Config::dump_json() const
{
  auto jobj = ChopOff::Config::dump_json();
  jobj["LogPath"] = mLogPath;
  return jobj;
}

llvm::Error
ChopOff::Pass::Config::load_json(const llvm::json::Value& jval,
                                 llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om || !om.map("LogPath", mLogPath))
    return llvm::make_error<Failure>();
  return ChopOff::Config::load_json(jval, path);
}

} // namespace gyhcall
