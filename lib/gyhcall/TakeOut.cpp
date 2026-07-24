#include "TakeOut.hpp"
#include "Analyze.hpp"
#include "type_hash.hpp"
#include "util.hpp"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Base64.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

namespace gyhcall {

llvm::json::Object
TakeOut::Config::dump_json() const
{
  llvm::json::Object jobj;
  jobj["Debug"] = mDebug;
  jobj["UseGRT"] = mUseGRT;
  jobj["UseFCP"] = mUseFCP;
  return jobj;
}

llvm::Error
TakeOut::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om ||                        //
      !om.map("Debug", mDebug) ||   //
      !om.map("UseGRT", mUseGRT) || //
      !om.map("UseFCP", mUseFCP))
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

namespace {

const unsigned char kModuleTemplate[] = {
#include "host.ll.chars"
};

std::unique_ptr<llvm::Module>
template_module(llvm::LLVMContext& ctx)
{
  ctx.setDiscardValueNames(false);
  auto memBuf = llvm::MemoryBuffer::getMemBufferCopy(
    { reinterpret_cast<const char*>(kModuleTemplate),
      sizeof(kModuleTemplate) });
  llvm::SMDiagnostic diag;
  auto ret = llvm::parseIR(*memBuf, diag, ctx);
  if (!ret) {
    diag.print("host.ll", llvm::outs());
    abort();
  }
  return ret;
}

} // namespace

std::unique_ptr<llvm::Module>
TakeOut::operator()(llvm::Module& mod)
{
  auto ret = template_module(mod.getContext());
  mModule = ret.get();
  mModule->setModuleIdentifier(mod.getModuleIdentifier());
  mModule->setDataLayout(mod.getDataLayout());
  if (auto flags = mod.getNamedMetadata("llvm.module.flags")) {
    auto hflags = mModule->getOrInsertNamedMetadata("llvm.module.flags");
    for (auto&& flag : flags->operands())
      hflags->addOperand(flag);
  }
  mModuleUidHex = llvm::utohexstr(mSymtbl.mUid, false, 8);

  gen_debug();

  llvm::DenseSet<llvm::Function*> blackList;
  for (auto&& [func, anno] : mBwList.mFuncs) {
    auto bw = anno.mTakeOut;
    if (!bw.has_value()) {
      mLogger(func, "忽略黑名单中的函数");
      continue;
    }
    if (!bw.value())
      blackList.insert(func);
    else if (mSymtbl.mFunctbl.find(func) == mSymtbl.mFunctbl.end())
      func->getContext().diagnose(DiagnosticString(
        ("'" + func->getName() + "' can't be taken out").str()));
  }
  for (auto&& [callee, bw] : mBwList.mCallees) {
    if (bw)
      // 包含黑名单调用的函数应该在 analyze 时就不会加入 Symtbl
      continue;
    // 给调用白名单里的函数创建对应的声明
    mWhiteCallees[callee] = llvm::Function::Create(callee->getFunctionType(),
                                                   callee->getLinkage(),
                                                   callee->getName(),
                                                   *mModule);
  }

  gen_functbl(gen_hfuncs(blackList), blackList);
  return ret;
}

void
TakeOut::gen_debug()
{
  mGyhDebug = mModule->getFunction("__gyh_debug__");
  assert(mGyhDebug != nullptr);
  mGyhCallback = mModule->getFunction("__gyh_callback__");
  assert(mGyhCallback != nullptr);

  const char* fmt;
  if (!mConfig.mUseGRT)
    fmt = "__gy_h_call_%d(%p, %p, %p, %p)\n";
  else
    fmt = "__gy_h_call_%d(%p, %p)\n";

  auto fmtString =
    llvm::ConstantDataArray::getString(mModule->getContext(), fmt);
  mGyhCallDebugFmt =
    new llvm::GlobalVariable(*mModule,
                             fmtString->getType(),
                             true,
                             llvm::GlobalValue::InternalLinkage,
                             fmtString,
                             "__gyh_call_debug_fmt__");

  fmt = "__gy_h_callback__(%p, %p, %p, %p)\n";
  fmtString = llvm::ConstantDataArray::getString(mModule->getContext(), fmt);
  mGyhCallbackDebugFmt =
    new llvm::GlobalVariable(*mModule,
                             fmtString->getType(),
                             true,
                             llvm::GlobalValue::InternalLinkage,
                             fmtString,
                             "__gyh_callback_debug_fmt__");
}

std::vector<llvm::Constant*>
TakeOut::gen_hfuncs(const llvm::DenseSet<llvm::Function*>& blackList)
{
  auto voidTy = llvm::Type::getVoidTy(mModule->getContext());
  auto ptrTy = llvm::PointerType::get(mModule->getContext(), 0);
  auto nullPtr = llvm::ConstantPointerNull::get(ptrTy);

  llvm::FunctionType* hfuncType;
  if (!mConfig.mUseGRT) {
    hfuncType =
      llvm::FunctionType::get(voidTy, { ptrTy, ptrTy, ptrTy, ptrTy }, false);
  } else {
    hfuncType = llvm::FunctionType::get(voidTy, { ptrTy, ptrTy }, false);
    mGyhExtreftbl =
      new llvm::GlobalVariable(*mModule,
                               ptrTy,
                               false,
                               llvm::GlobalValue::ExternalLinkage,
                               llvm::ConstantPointerNull::get(ptrTy),
                               "__gyh_extreftbl_" + mModuleUidHex);
    mGyhCbstubtbl =
      new llvm::GlobalVariable(*mModule,
                               ptrTy,
                               false,
                               llvm::GlobalValue::ExternalLinkage,
                               llvm::ConstantPointerNull::get(ptrTy),
                               "__gyh_cbstubtbl_" + mModuleUidHex);
  }

  std::vector<llvm::Constant*> hfuncs(mSymtbl.mFunctbl.size(), nullptr);

  if (!mConfig.mUseFCP) {
    for (auto& [func, info] : mSymtbl.mFunctbl) {
      if (blackList.contains(func)) {
        hfuncs[info.mUid] = nullPtr;
        continue;
      }
      auto hfunc =
        llvm::Function::Create(hfuncType,
                               llvm::GlobalValue::InternalLinkage,
                               "__gyh_call_" + std::to_string(info.mUid),
                               *mModule);
      hfunc->setDSOLocal(true);
      do_hstubcvt(func, hfunc, info);
      hfuncs[info.mUid] = hfunc;
    }
  }

  else {
    // FCP 优化下先创建对应的 hfuncs
    for (auto& [func, info] : mSymtbl.mFunctbl) {
      if (blackList.contains(func) || mWhiteCallees.contains(func))
        continue;
      auto hfunc = llvm::Function::Create(func->getFunctionType(),
                                          llvm::GlobalValue::InternalLinkage,
                                          func->getName(),
                                          *mModule);
      hfunc->setDSOLocal(true);
      mFcpFuncMap[func] = hfunc;
    }
    // 再创建 hstub 转发调用到 hfuncs
    for (auto& [func, info] : mSymtbl.mFunctbl) {
      if (blackList.contains(func)) {
        hfuncs[info.mUid] = nullPtr;
        continue;
      }
      auto hstub =
        llvm::Function::Create(hfuncType,
                               llvm::GlobalValue::InternalLinkage,
                               "__gyh_call_" + std::to_string(info.mUid),
                               *mModule);
      hstub->setDSOLocal(true);
      do_hstubcvt(func, hstub, info);
      hfuncs[info.mUid] = hstub;
    }
  }

  return hfuncs;
}

void
TakeOut::gen_functbl(std::vector<llvm::Constant*>&& hfuncs,
                     const llvm::DenseSet<llvm::Function*>& blackList)
{
  auto ptrTy = llvm::PointerType::get(mModule->getContext(), 0);

  auto hfunctblSizeInit = llvm::ConstantInt::get(
    llvm::Type::getInt64Ty(mModule->getContext()), hfuncs.size());
  new llvm::GlobalVariable(*mModule,
                           hfunctblSizeInit->getType(),
                           true,
                           llvm::GlobalValue::ExternalLinkage,
                           hfunctblSizeInit,
                           "__gyh_hfunctbl_size_" + mModuleUidHex);

  auto hfunctblInit = llvm::ConstantArray::get(
    llvm::ArrayType::get(ptrTy, hfuncs.size()), std::move(hfuncs));
  new llvm::GlobalVariable(*mModule,
                           hfunctblInit->getType(),
                           true,
                           llvm::GlobalValue::ExternalLinkage,
                           hfunctblInit,
                           "__gyh_hfunctbl_" + mModuleUidHex);

  if (mConfig.mUseFCPcm) {
    std::vector<llvm::Constant*> funcs(mSymtbl.mFunctbl.size(), nullptr);
    auto nullPtr = llvm::ConstantPointerNull::get(ptrTy);
    for (auto& [func, info] : mSymtbl.mFunctbl) {
      if (blackList.contains(func))
        funcs[info.mUid] = nullPtr;
      else
        funcs[info.mUid] = mFcpFuncMap.at(func);
    }

    auto functbl = llvm::ConstantArray::get(
      llvm::ArrayType::get(ptrTy, funcs.size()), std::move(funcs));
    mGyhFunctbl = new llvm::GlobalVariable(*mModule,
                                           functbl->getType(),
                                           true,
                                           llvm::GlobalValue::ExternalLinkage,
                                           functbl,
                                           "__gyh_functbl_" + mModuleUidHex);
  }
}

void
TakeOut::do_hstubcvt(llvm::Function* func,
                     llvm::Function* hstub,
                     const Symtbl::FuncInfo& info)
{
  auto entry = llvm::BasicBlock::Create(hstub->getContext(), "entry", hstub);
  llvm::IRBuilder<> irb(entry);

  // 在入口块生成调用转换的代码
  auto argIter = hstub->arg_begin();
  auto& retPad = *argIter++;
  auto& argPad = *argIter++;

  llvm::Argument* extRefArg;
  llvm::Argument* cbStubArg;
  if (!mConfig.mUseGRT) {
    assert(hstub->arg_size() == 4);
    extRefArg = &*argIter++, cbStubArg = &*argIter++;
  } else {
    assert(hstub->arg_size() == 2);
    extRefArg = nullptr, cbStubArg = nullptr;
  }

  if (mConfig.mDebug) {
    if (!mConfig.mUseGRT)
      irb.CreateCall(mGyhDebug,
                     { mGyhCallDebugFmt,
                       irb.getInt32(info.mUid),
                       &retPad,
                       &argPad,
                       extRefArg,
                       cbStubArg });
    else
      irb.CreateCall(
        mGyhDebug,
        { mGyhCallDebugFmt, irb.getInt32(info.mUid), &retPad, &argPad });
  }

  // 加载实际参数
  mHstubArgs.clear();
  if (info.mStubType->mArg) {
    auto args = irb.CreateLoad(info.mStubType->mArg, &argPad, "args");
    for (auto&& [i, arg] : llvm::enumerate(func->args()))
      mHstubArgs.push_back(irb.CreateExtractValue(args, i, arg.getName()));
  }

  // 加载外部引用表和回调桩表
  mExtRefsMap.clear(), mCbStubMap.clear();
  auto ptrTy = irb.getPtrTy();

  // 最基础的情况: 从 hstub 的后两个参数中加载
  if (!mConfig.mUseGRT) {
    if (!info.mExtRefArgs.empty()) {
      auto padType = llvm::ArrayType::get(ptrTy, info.mExtRefArgs.size());
      auto extRefPad = irb.CreateLoad(padType, extRefArg, true, "extRefPad");
      for (auto [i, extRef] : llvm::enumerate(info.mExtRefArgs)) {
        auto newVal = irb.CreateExtractValue(extRefPad, i, extRef->getName());
        mExtRefsMap[extRef] = newVal;
      }
    }
    if (!info.mCbStubArgs.empty()) {
      auto padType = llvm::ArrayType::get(ptrTy, info.mCbStubArgs.size());
      auto cbStubPad = irb.CreateLoad(padType, cbStubArg, true, "cbStubPad");
      for (auto [i, cbStub] : llvm::enumerate(info.mCbStubArgs)) {
        auto newVal =
          irb.CreateExtractValue(cbStubPad, i, llvm::utohexstr(cbStub));
        mCbStubMap[cbStub] = newVal;
      }
    }
  }

  // 使用全局引用表的情况: 从全局引用表指针指向的数组加载
  else if (!mConfig.mUseFCP) {
    for (auto extRef : info.mExtRefArgs) {
      auto it = mSymtbl.mGreftbl.find(extRef);
      assert(it != mSymtbl.mGreftbl.end());
      auto extreftbl = irb.CreateLoad(ptrTy, mGyhExtreftbl, "extreftbl");
      auto newValPtr = irb.CreateGEP(
        ptrTy, extreftbl, { irb.getInt64(it->second.mUid) }, "extRef", true);
      mExtRefsMap[extRef] = irb.CreateLoad(ptrTy, newValPtr, extRef->getName());
    }
    for (auto cbStub : info.mCbStubArgs) {
      auto it = mSymtbl.mCallbackStubs.find(cbStub);
      assert(it != mSymtbl.mCallbackStubs.end());
      auto cbstubtbl = irb.CreateLoad(ptrTy, mGyhCbstubtbl, "cbstubtbl");
      auto newValPtr = irb.CreateGEP(
        ptrTy, cbstubtbl, { irb.getInt64(it->second.mUid) }, "cbStub", true);
      mCbStubMap[cbStub] =
        irb.CreateLoad(ptrTy, newValPtr, llvm::utohexstr(cbStub));
    }
  }

  // 如果启用了快速调用路径: hstub 设置全局变量, 卸载函数自己加载引用
  else {
    auto it = mFcpFuncMap.find(func);
    assert(it != mFcpFuncMap.end());
    auto hfunc = it->second;

    auto ret = irb.CreateCall(hfunc, mHstubArgs);
    if (info.mStubType->mRet)
      irb.CreateStore(ret, &retPad);
    irb.CreateRet(nullptr);
    do_function(func, hfunc, info);

    // 在快速调用路径启用的情况下, 保留 llvm::CloneFunctionInto 对
    // hfunc 属性的修改. (原因是我发现 byval 属性的形参和实参不匹配
    // 会引发运行时段错误)
    // hfunc->setAttributes({});
    hfunc->setVisibility(llvm::GlobalValue::DefaultVisibility);

    // 避免 hstub 在遇到 fastcc 的 hfunc 时因函数体太简单而被优化成 unreachable
    hstub->addFnAttr(llvm::Attribute::OptimizeNone);
    hstub->addFnAttr(llvm::Attribute::NoInline);
    return;
  }

  // 设置返回值
  mHstubRetPad = &retPad;
  do_function(func, hstub, info);

  // llvm::CloneFunctionInto 会改变参数名和属性, 所以我们再重新设置属性
  // https://github.com/llvm/llvm-project/commit/8402a0fab09a2c3a1b5c2e23e2ababcb575709d7
  //? 在 LLVM 的最新版本里, CloneFunctionInto 已经被拆分为
  //? CloneFunctionBodyInto 和 CloneFunctionMetadataInto, 考虑在之后改用它们:
  hstub->setAttributes({});
  hstub->setVisibility(llvm::GlobalValue::DefaultVisibility);

  retPad.setName("retPad");
  argPad.setName("argPad");
  for (auto i : { &retPad, &argPad }) {
    i->addAttr(llvm::Attribute::NoAlias);
    i->addAttr(llvm::Attribute::NoCapture);
    i->addAttr(llvm::Attribute::NoUndef);
  }
  retPad.addAttr(llvm::Attribute::WriteOnly);
  argPad.addAttr(llvm::Attribute::ReadOnly);

  if (!mConfig.mUseGRT) {
    extRefArg->setName("extRefs");
    cbStubArg->setName("cbStubs");
    for (auto i : { extRefArg, cbStubArg }) {
      i->addAttr(llvm::Attribute::NoAlias);
      i->addAttr(llvm::Attribute::NoCapture);
      i->addAttr(llvm::Attribute::NoUndef);
      i->addAttr(llvm::Attribute::ReadOnly);
    }
  }
}

void
TakeOut::do_function(llvm::Function* func,
                     llvm::Function* hfunc,
                     const Symtbl::FuncInfo& info)
{
  llvm::BasicBlock* entry;
  if (!mConfig.mUseFCP)
    entry = &hfunc->getEntryBlock();
  else
    entry = llvm::BasicBlock::Create(hfunc->getContext(), "entry", hfunc);
  llvm::IRBuilder<> irb(entry);
  auto ptrTy = irb.getPtrTy();

  llvm::ValueToValueMapTy vmap; // 只用来替换函数的参数
  if (!mConfig.mUseFCP) {
    assert(func->arg_size() == mHstubArgs.size());
    for (auto&& [fr, to] : llvm::zip(func->args(), mHstubArgs))
      vmap[&fr] = to;
  }

  else {
    assert(func->arg_size() == hfunc->arg_size());
    for (auto&& [fr, to] : llvm::zip(func->args(), hfunc->args()))
      vmap[&fr] = &to;

    // 根据在 hstub 中预设的, 指向两表的全局指针加载当前函数涉及的引用
    auto extRefTbl = irb.CreateLoad(ptrTy, mGyhExtreftbl, "extRefTbl");
    for (auto extRef : info.mExtRefArgs) {
      auto it = mSymtbl.mGreftbl.find(extRef);
      assert(it != mSymtbl.mGreftbl.end());
      auto newValPtr = irb.CreateGEP(
        ptrTy, extRefTbl, { irb.getInt64(it->second.mUid) }, "extRef", true);
      mExtRefsMap[extRef] = irb.CreateLoad(ptrTy, newValPtr, extRef->getName());
    }
    auto cbStubTbl = irb.CreateLoad(ptrTy, mGyhCbstubtbl, "cbStubTbl");
    for (auto cbStub : info.mCbStubArgs) {
      auto it = mSymtbl.mCallbackStubs.find(cbStub);
      assert(it != mSymtbl.mCallbackStubs.end());
      auto newValPtr = irb.CreateGEP(
        ptrTy, cbStubTbl, { irb.getInt64(it->second.mUid) }, "cbStub", true);
      mCbStubMap[cbStub] =
        irb.CreateLoad(ptrTy, newValPtr, llvm::utohexstr(cbStub));
    }

    //! 不能将 FcpFunc 简单丢到 vmap 里利用 CloneFunctionInto 实现替换.
    //! 因为这会同时替换掉函数体内对函数的取指操作 (例如 &foo), 而这种操作要
    //! 保持客方的语义, 即取的是客方函数的地址! 因此, 我们智能针对性地对直接调
    //! 用语句应用 FCP 优化.
  }

  // 复制函数体, 使用 LLVM 内置的函数复制工具以尽可能避免没考虑到的情况
  llvm::SmallVector<llvm::ReturnInst*, 8> rets;
  llvm::CloneFunctionInto(
    hfunc, func, vmap, llvm::CloneFunctionChangeType::DifferentModule, rets);
  // 我们不借助 vmap, 而是自己处理 mExtRefsMap 的替换, 因为这些替换可能
  // 会将常量表达式里的全局变量替换为局部变量, 这时这些常量表达式需要被展开
  // 为等价的普通指令, 只能我们自己来做.

  // TODO
  //! CloneFunctionInto 这个函数会重复地复制调试信息, 从而导致模块急剧膨胀.
  //! 未来务必要解决这个问题! 思路是 "做减法" -- 复制整个模块,
  //! 然后清理其中不需要的对象.

  auto attrs = hfunc->getAttributes();
  auto attrsNew = attrs.removeFnAttributes(hfunc->getContext());
  hfunc->setAttributes(attrsNew);

  // 在函数入口处生成一个无条件跳转指令到原入口对应的新基本块
  auto newEntryIt = vmap.find(&func->getEntryBlock());
  assert(newEntryIt != vmap.end());
  auto newEntry = llvm::cast<llvm::BasicBlock>(newEntryIt->second);
  mEntryTerminator = irb.CreateBr(newEntry);
  irb.SetInsertPoint(mEntryTerminator);

  if (!mConfig.mUseFCP) {
    // 返回指令转为 store 指令
    for (auto&& ret : rets) {
      if (auto retv = ret->getReturnValue()) {
        auto irb = llvm::IRBuilder<>(ret);
        irb.CreateStore(retv, mHstubRetPad);
        irb.CreateRetVoid();
        ret->eraseFromParent();
        // 不能直接用 setOperand(0, nullptr) 将返回值置空!
      }
    }
  }

  // 处理所有指令, 应用 extRefs 和 cbStubs 替换
  std::remove_pointer_t<decltype(mToReplace)> toReplace;
  mToReplace = &toReplace; // 那些映射后还需要再替换一遍的指令
  for (auto it = hfunc->begin(); ++it != hfunc->end();) { // 跳过入口块
    auto ins = &*(it->begin());
    while (ins != nullptr)
      ins = do_instruction(ins);
  }
  for (auto [oldIns, newIns] : toReplace) {
    if (newIns)
      oldIns->replaceAllUsesWith(newIns);
    oldIns->eraseFromParent();
  }
}

llvm::Instruction*
TakeOut::do_instruction(llvm::Instruction* ins)
{
  mInstruction = ins;

  // 函数调用需要特殊处理
  if (auto call = llvm::dyn_cast<llvm::CallBase>(ins)) {
    call->dropUnknownNonDebugMetadata();
    //! 诸如 !callee 等的元数据里面可能包含全局符号引用,
    //! 因为我们对元数据的处理还不完善，所以把这类元数据都丢弃掉.

    for (auto& arg : call->args()) {
      auto newArg = do_operand(arg);
      arg.set(newArg);
    }

    if (auto func = call->getCalledFunction()) {
      if (auto it = mWhiteCallees.find(func); it != mWhiteCallees.end()) {
        call->setCalledFunction(it->second);
        return call->getNextNode();
      }

      if (func->isIntrinsic()) {
        llvm::IRBuilder<> irb(ins);
        auto newIns = irb.CreateIntrinsic(
          func->getReturnType(),
          func->getIntrinsicID(),
          llvm::SmallVector<llvm::Value*>{ call->arg_begin(), call->arg_end() },
          nullptr, // TODO 保留 Fast Math Flags
          ins->getName());

        // 处理返回值
        if (func->getReturnType()->isVoidTy())
          mToReplace->push_back({ call, nullptr });
        else
          mToReplace->push_back({ ins, newIns });

        // 复制元数据
        newIns->copyMetadata(*ins);
        return call->getNextNode();
      }

      if (mConfig.mUseFCP) {
        auto it = mFcpFuncMap.find(func);
        if (it != mFcpFuncMap.end()) {
          call->setCalledFunction(it->second);
          return call->getNextNode();
        }
      }
    }
    call->setCalledOperand(do_operand(call->getCalledOperand()));
    if (mConfig.mUseFCPcm)
      return do_fcp_cm(call); // 它会返回下一条指令

    // 函数调用都需要转换参数和返回值形式然后交给运行时处理, 即使是
    // 模块内部链接的函数也一样, 因为被调函数可能引用了其它全局符号,
    // 在没有全局引用表的情况下, 被调函数不知道这些间接全局符号引用.

    auto retVal = do_callback(call); // 它会返回返回值的替代
    mToReplace->push_back({ call, retVal });
  }

  // 一般的指令只需简单替换操作数中的全局引用即可
  else {
    for (auto& op : ins->operands())
      op.set(do_operand(op));
  }

  return ins->getNextNode();
}

llvm::Instruction*
TakeOut::do_callback(llvm::CallBase* call)
{
  llvm::IRBuilder<> irb(call);
  auto stackSave = irb.CreateStackSave();

  // 检出回调桩
  auto cbStubHash = call_site_hash(call);
  if (!cbStubHash)
    mLogger(call, "无法哈希的回调签名"), abort();
  auto cbStub = mCbStubMap.at(*cbStubHash);

  // 准备返回值
  llvm::Value* retPad;
  auto retType = call->getType();
  if (retType->isVoidTy())
    retPad = llvm::ConstantPointerNull::get(irb.getPtrTy());
  else
    retPad = irb.CreateAlloca(call->getType(), nullptr, "retPad");

  // 准备参数结构体
  llvm::Value* argPad;
  llvm::SmallVector<llvm::Type*, 8> argTypes;
  for (auto& arg : call->args())
    argTypes.push_back(arg->getType());
  if (argTypes.empty())
    argPad = llvm::ConstantPointerNull::get(irb.getPtrTy());
  else {
    auto argsType = llvm::StructType::get(call->getContext(), argTypes);
    llvm::Value* argsVal = llvm::UndefValue::get(argsType);
    for (auto&& [i, arg] : llvm::enumerate(call->args()))
      argsVal = irb.CreateInsertValue(argsVal, arg, { unsigned(i) });
    argPad = irb.CreateAlloca(argsType, nullptr, "argPad");
    irb.CreateStore(argsVal, argPad);
  }

  if (mConfig.mDebug) {
    irb.CreateCall(mGyhDebug,
                   { mGyhCallbackDebugFmt,
                     cbStub,
                     call->getCalledOperand(),
                     retPad,
                     argPad });
  }

  // 调用回调桩
  irb.CreateCall(mGyhCallback,
                 { cbStub, call->getCalledOperand(), retPad, argPad });

  // 处理返回值
  llvm::Instruction* ret;
  if (retType->isVoidTy())
    ret = nullptr;
  else
    ret = irb.CreateLoad(call->getType(), retPad, call->getName());

  irb.CreateStackRestore(stackSave);
  return ret;
}

llvm::Instruction*
TakeOut::do_fcp_cm(llvm::CallBase* call)
{
  // 将原基本块从调用点处分裂为上下两块
  auto bb = call->getParent();
  auto topBB = bb->splitBasicBlockBefore(call, "gyh_fcp_cm.top");

  // 在左边创建一个新基本块, 插入到前面防止继续遍历到
  auto leftBB =
    llvm::BasicBlock::Create(topBB->getContext(), "gyh_fcp_cm.left");
  leftBB->insertInto(bb->getParent(), bb);
  // 原调用指令 call 移动到右块
  auto rightBB = bb->splitBasicBlockBefore(std::next(call->getIterator()),
                                           "gyh_fcp_cm.right");

  // 在上基本块中检查 FCP-CM 魔数
  llvm::Value* hfuncAddr;
  {
    llvm::IRBuilder<> irb(topBB);
    auto oldTerm = topBB->getTerminator();
    irb.SetInsertPoint(oldTerm);

    auto callee = call->getCalledOperand();
    auto calleePtr = irb.CreateBitCast(callee, irb.getPtrTy());
    auto magic = irb.CreateLoad(
      irb.getInt64Ty(),
      irb.CreateGEP(irb.getInt64Ty(), calleePtr, { irb.getInt64(-2) }));
    hfuncAddr = irb.CreateLoad(
      irb.getInt64Ty(),
      irb.CreateGEP(irb.getInt64Ty(), calleePtr, { irb.getInt64(-1) }));

    auto magicExpected =
      irb.CreateXor(irb.getInt64(Symtbl::kFCPcmMagic), hfuncAddr);
    auto cond = irb.CreateICmpEQ(magic, magicExpected);
    irb.CreateCondBr(cond, leftBB, rightBB);
    oldTerm->eraseFromParent();
  }

  // 如果检查通过, 则直接调用目标函数
  llvm::CallBase* leftRet;
  {
    llvm::IRBuilder<> irb(leftBB);
    auto hfunc = irb.CreateIntToPtr(hfuncAddr, irb.getPtrTy());
    leftRet = reinterpret_cast<llvm::CallBase*>(call->clone());
    leftRet->insertInto(leftBB, leftBB->end());
    leftRet->setCalledOperand(hfunc);
    irb.CreateBr(bb);
  }

  // 否则执行一般的解释重入回调
  llvm::Instruction* rightRet;
  {
    assert(call->getParent() == rightBB);
    rightRet = do_callback(call);
  }

  if (rightRet == nullptr) {
    mToReplace->push_back({ call, rightRet });
  } else {
    llvm::IRBuilder<> irb(bb, bb->getFirstInsertionPt());
    auto phi = irb.CreatePHI(rightRet->getType(), 2, call->getName());
    phi->addIncoming(leftRet, leftBB);
    phi->addIncoming(rightRet, rightBB);
    mToReplace->push_back({ call, phi });
  }

  return &*(bb->begin());
}

llvm::Value*
TakeOut::do_operand(llvm::Value* op)
{
  static bool sDebug = false;
  if (sDebug) {
    llvm::outs() << (void*)op << *op << "\n\n";
    for (auto&& [k, v] : mExtRefsMap) {
      llvm::outs() << (void*)k << *k << '\n';
      llvm::outs() << (void*)v << " -> " << *v << '\n';
    }
  }

  // 如果操作数是对全局符号的引用, 那就直接替换掉
  auto it = mExtRefsMap.find(op);
  if (it != mExtRefsMap.end())
    return it->second;

  if (llvm::isa<llvm::GlobalValue>(op) &&
      llvm::isa<llvm::DbgValueInst>(mInstruction)) {
    return llvm::PoisonValue::get(op->getType());
  }

  // 常量表达式需要展开成普通表达式, 并递归替换其中的引用
  if (auto c = llvm::dyn_cast<llvm::Constant>(op)) {
    llvm::SmallVector<llvm::Value*, 16> newOps;
    bool hasNew = false;
    for (auto&& op : c->operands()) {
      auto newOp = do_operand(op);
      hasNew = hasNew || newOp != op;
      newOps.push_back(newOp);
    }
    if (!hasNew)
      return c;

    if (auto gv = llvm::dyn_cast<llvm::GlobalValue>(c)) {
      assert(false && "unexpected GlobalValue in Constant");
    }

    auto insertBefore =
      llvm::isa<llvm::PHINode>(mInstruction) ? mEntryTerminator : mInstruction;
    // PHI 节点必须在入口块开头, 所以我们没有办法,
    // 只能把常量表达式展开后的指令放到入口块里, 不过 LLVM IR
    // 对常量表达式的限制可以保证这种将局部的 "变量"
    // 提升到开头的做法是一定正确的.

    if (auto ca = llvm::dyn_cast<llvm::ConstantAggregate>(c)) {
      llvm::IRBuilder<> irb(insertBefore);
      llvm::Value* val = llvm::UndefValue::get(ca->getType());

      if (llvm::isa<llvm::ConstantStruct>(ca) ||
          llvm::isa<llvm::ConstantArray>(ca)) {
        for (auto [i, newOp] : llvm::enumerate(newOps))
          val = irb.CreateInsertValue(val, newOp, { unsigned(i) }, "const");
      }

      else if (llvm::isa<llvm::ConstantVector>(ca)) {
        for (auto [i, newOp] : llvm::enumerate(newOps))
          val = irb.CreateInsertElement(val, newOp, i, "const");
      }

      else
        assert(false && "unknown ConstantAggregate type");
      return val;
    }

    if (auto ce = llvm::dyn_cast<llvm::ConstantExpr>(c)) {
      auto ins = ce->getAsInstruction(insertBefore);
      for (auto [i, newOp] : llvm::enumerate(newOps))
        ins->setOperand(i, newOp);
      return ins;
    }
  }

  else if (auto m = llvm::dyn_cast<llvm::MetadataAsValue>(op)) {
    auto md = do_metadata(m->getMetadata());
    if (md != m->getMetadata())
      return llvm::MetadataAsValue::get(m->getContext(), md);
  }

  // 其它没有包含全局引用的情况不需要任何处理
  return op;
}

llvm::Metadata*
TakeOut::do_metadata(llvm::Metadata* md)
{
  static bool sDebug = false;
  if (sDebug) {
    llvm::outs() << (void*)md << *md << "\n";
  }

  if (auto vam = llvm::dyn_cast<llvm::ValueAsMetadata>(md)) {
    auto newVal = do_operand(vam->getValue());
    if (newVal != vam->getValue()) {
      if (auto c = llvm::dyn_cast<llvm::Constant>(newVal))
        return llvm::ConstantAsMetadata::get(c);
      if (llvm::isa<llvm::LocalAsMetadata>(vam))
        return llvm::LocalAsMetadata::get(newVal);
      if (llvm::isa<llvm::GlobalValue>(newVal))
        return llvm::ValueAsMetadata::get(
          llvm::PoisonValue::get(newVal->getType()));
      return llvm::ValueAsMetadata::get(newVal);
    }
  }
  // else if(auto dbg = llvm::dyn_cast<llvm::DIVariable>(md)) {
  //   if(auto dbgGlobal = llvm::dyn_cast<llvm::DIGlobalVariable>(md)){
  //     dbgGlobal->print(llvm::errs());;
  //     llvm::errs()<<"\n";
  //     // auto globalVal = dbgGlobal->e
  //   }
  // }

  else if (auto dbgArglist = llvm::dyn_cast<llvm::DIArgList>(md)) {
    bool changed = false;
    llvm::SmallVector<llvm::ValueAsMetadata*, 4> newArgs;
    for (llvm::ValueAsMetadata* vam : dbgArglist->getArgs()) {
      auto newVal = do_operand(vam->getValue());
      // TODO:
      if (llvm::isa<llvm::GlobalValue>(newVal)) {
        newVal = llvm::PoisonValue::get(newVal->getType());
        changed = true;
      } else if (newVal != vam->getValue()) {
        changed = true;
      }
      newArgs.push_back(llvm::ValueAsMetadata::get(newVal));
    }
    // 只有当参数发生变化时才创建新的 DIArgList
    if (!changed)
      return md;

    llvm::DIArgList* newArgList =
      llvm::DIArgList::get(mInstruction->getContext(), newArgs);
    if (llvm::DILocation* loc = mInstruction->getDebugLoc().get())
      mInstruction->setDebugLoc(llvm::DebugLoc(loc));
    return newArgList;
  }

  return md;
}

//? 由于 llvm::MDNode 的存在, llvm::Metadata 可能形成带环的图解构,
//? 下面的代码旨在实现完整的替换操作, 但是 Metadata 可能非常多, 遍历起
//? 来非常耗时, 实践中可能不存在间接 ValueAsMetadata 的情况, 暂时用上面的.

// static void
// search_metadata(llvm::Metadata* md,
//                 llvm::DenseMap<llvm::Metadata*, llvm::Metadata*>& map)
// {
//   if (!md)
//     return;
//   if (map.contains(md))
//     return;
//   map.insert(std::make_pair(md, nullptr));
//   if (auto node = llvm::dyn_cast<llvm::MDNode>(md)) {
//     for (auto& op : node->operands())
//       search_metadata(op.get(), map);
//   }
// }
//
// llvm::Metadata*
// TakeOut::do_metadata(llvm::Metadata* md)
// {
//   llvm::DenseMap<llvm::Metadata*, llvm::Metadata*> map;
//   search_metadata(md, map);
//
//   bool changed = false;
//   for (auto& [k, v] : map) {
//     if (auto vam = llvm::dyn_cast<llvm::ValueAsMetadata>(k)) {
//       auto newVal = do_operand(vam->getValue());
//
//       if (newVal == vam->getValue())
//         continue;
//       if (auto c = llvm::dyn_cast<llvm::Constant>(newVal))
//         v = llvm::ConstantAsMetadata::get(c);
//       else if (llvm::isa<llvm::LocalAsMetadata>(vam))
//         v = llvm::LocalAsMetadata::get(newVal);
//       else
//         v = llvm::ValueAsMetadata::get(newVal);
//       changed = true;
//     }
//   }
//
//   while (changed) {
//     // TODO
//   }
//
//   return map[md];
// }

llvm::PreservedAnalyses
TakeOut::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  std::error_code ec;
  llvm::raw_fd_ostream out(mOutputPath, ec);
  if (ec) {
    mod.getContext().diagnose(DiagnosticString(
      "fail to take out '" + mOutputPath + "': " + ec.message()));
    return llvm::PreservedAnalyses::all();
  }

  auto& analysis = mam.getResult<Analyze::Pass>(mod);
  std::unique_ptr<Logger> logger;
  if (!mLogPath.empty()) {
    std::error_code ec2;
    logger = std::make_unique<FileLogger>(mLogPath, ec2);
    if (ec2) {
      mod.getContext().diagnose(DiagnosticString(
        "fail to open log '" + mLogPath + "': " + ec2.message(),
        llvm::DS_Warning));
      logger.reset();
    }
  }
  if (!logger)
    logger = std::make_unique<DiagnosticLogger>(mod.getContext());
  TakeOut takeOut(mConfig, analysis, *logger);
  auto hmod = takeOut(mod);

  if (mOutText)
    hmod->print(out, nullptr);
  else
    llvm::WriteBitcodeToFile(*hmod, out);
  return llvm::PreservedAnalyses::all();
}

} // namespace gyhcall