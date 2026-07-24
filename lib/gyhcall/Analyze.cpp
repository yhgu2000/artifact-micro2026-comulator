#include "Analyze.hpp"
#include "type_hash.hpp"
#include "util.hpp"
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/IRBuilder.h>

#define DEBUG_TYPE "gyhcall"
#include <llvm/Support/Debug.h>

namespace gyhcall {

llvm::json::Object
Analyze::Config::dump_json() const
{
  llvm::json::Object jobj;
  return jobj;
}

llvm::Error
Analyze::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

void
Analyze::operator()(llvm::Module& mod)
{
  for (auto&& func : mod) {
    std::optional<bool> bw;
    if (auto it = mBwList.mFuncs.find(&func); it != mBwList.mFuncs.end())
      bw = it->second.mAnalyze;
    if (bw.has_value() && !bw.value()) {
      mLogger(&func, "忽略黑名单中的函数");
      continue;
    }

    auto err = do_function(func);
    if (!err)
      continue;

    std::string s;
    llvm::raw_string_ostream so(s);
    so << err, mLogger(&func, s);

    if (bw.has_value() && bw.value())
      mod.getContext().diagnose(DiagnosticError(
        ("'" + func.getName() + "' analyze 未通过").str(), std::move(err)));
  }
}

namespace {

struct UnspportedFunction : public llvm::ErrorInfo<UnspportedFunction>
{
  static char ID;
  const char* mDesc{ nullptr };
  llvm::Function* mFunc{ nullptr };

  UnspportedFunction(const char* desc, llvm::Function* func)
    : mDesc(desc)
    , mFunc(func)
  {
  }
  static llvm::Error make(const char* desc, llvm::Function* func)
  {
    return llvm::make_error<UnspportedFunction>(desc, func);
  }

  void log(llvm::raw_ostream& os) const override
  {
    auto func = llvm::Function::Create(mFunc->getFunctionType(),
                                       mFunc->getLinkage(),
                                       mFunc->getName(),
                                       mFunc->getParent());
    func->setAttributes(mFunc->getAttributes());
    os << mDesc << ": " << *func;
    func->removeFromParent();
  }
  std::error_code convertToErrorCode() const override
  {
    return llvm::inconvertibleErrorCode();
  }
};
char UnspportedFunction::ID = 0;

struct UnspportedInstruction : public llvm::ErrorInfo<UnspportedInstruction>
{
  static char ID;
  const char* mDesc{ nullptr };
  llvm::Instruction* mIns{ nullptr };

  UnspportedInstruction(const char* desc, llvm::Instruction* ins)
    : mDesc(desc)
    , mIns(ins)
  {
  }
  static llvm::Error make(const char* desc, llvm::Instruction* ins)
  {
    return llvm::make_error<UnspportedInstruction>(desc, ins);
  }

  void log(llvm::raw_ostream& os) const override
  {
    os << mDesc << ": " << *mIns;
  }
  std::error_code convertToErrorCode() const override
  {
    return llvm::inconvertibleErrorCode();
  }
};
char UnspportedInstruction::ID = 0;

struct UnspportedAttribute : public llvm::ErrorInfo<UnspportedAttribute>
{
  static char ID;
  const char* mDesc{ nullptr };
  llvm::Attribute mAttr;

  UnspportedAttribute(const char* desc, llvm::Attribute attr)
    : mDesc(desc)
    , mAttr(attr)
  {
  }
  static llvm::Error make(const char* desc, llvm::Attribute attr)
  {
    return llvm::make_error<UnspportedAttribute>(desc, attr);
  }

  void log(llvm::raw_ostream& os) const override
  {
    os << mDesc << ": " << mAttr.getAsString();
  }
  std::error_code convertToErrorCode() const override
  {
    return llvm::inconvertibleErrorCode();
  }
};
char UnspportedAttribute::ID = 0;

llvm::Error
check_function_attributes(llvm::AttributeList attrList)
{
  static constexpr llvm::Attribute::AttrKind kAttrs[] = {
    // llvm::Attribute::NoReturn,
  };
  for (auto attr : kAttrs) {
    if (attrList.hasFnAttr(attr))
      return UnspportedAttribute::make("不支持的属性",
                                       attrList.getFnAttr(attr));
  }
  return llvm::Error::success();
}

} // namespace

llvm::Error
Analyze::do_function(llvm::Function& func)
{
  if (func.isDeclaration())
    return llvm::Error::success();

  if (func.isVarArg())
    return UnspportedFunction::make("不支持可变参数函数", &func);

  if (auto err = check_function_attributes(func.getAttributes()))
    return err;

  auto callingHash = function_hash(&func);
  if (!callingHash)
    return UnspportedFunction::make("无法哈希的函数签名", &func);

  std::remove_pointer_t<decltype(mFuncInfo)> funcInfo;
  mFuncInfo = &funcInfo;
  std::remove_pointer_t<decltype(mGrefsInFunc)> grefsInFunc;
  mGrefsInFunc = &grefsInFunc;
  decltype(mResult.mCallbackStubs) callbackStubs;
  // 先记在临时变量里, 等到确定当前函数能被卸载时再合并到最终结果

  for (auto&& bb : func) {
    for (auto&& ins : bb) {
      if (auto t = llvm::dyn_cast<llvm::IndirectBrInst>(&ins))
        // TODO
        return UnspportedInstruction::make("不支持间接跳转", &ins);

      if (auto t = llvm::dyn_cast<llvm::InvokeInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::ResumeInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::CatchSwitchInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::CatchReturnInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::CleanupReturnInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::LandingPadInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::CatchPadInst>(&ins))
        goto EXCEPTION;
      if (auto t = llvm::dyn_cast<llvm::CleanupPadInst>(&ins))
        goto EXCEPTION;
      goto NOT_EXCEPTION;
    EXCEPTION:
      // TODO
      return UnspportedInstruction::make("不支持异常处理", &ins);
    NOT_EXCEPTION:

      // 特殊处理函数调用
      if (auto t = llvm::dyn_cast<llvm::CallBase>(&ins)) {
        auto callee = t->getCalledOperand();
        if (llvm::isa<llvm::InlineAsm>(callee))
          return UnspportedInstruction::make("不支持内联汇编", &ins);

        if (auto gref = llvm::dyn_cast<llvm::GlobalValue>(callee)) {
          auto func = llvm::dyn_cast<llvm::Function>(gref);
          if (func) {
            if (auto it = mBwList.mCallees.find(func);
                it != mBwList.mCallees.end()) {
              if (it->second)
                goto DO_OPERANDS; // 调用白名单, 直接卸载.
              else
                return UnspportedInstruction::make("调用黑名单", &ins);
            }

            if (func->isIntrinsic())
              // 默认 Intrinsics 在主侧有等价的实现, 而那些不能被卸载的
              // Intrinsics 应当 (在 PickUp 阶段) 被加到 Callees
              // 黑名单里.
              goto DO_OPERANDS;
          }

          // 其他情况, 都要注册全局引用.
          if (grefsInFunc.insert(gref).second)
            funcInfo.mExtRefArgs.push_back(gref);
        }

        // 对于一般的情况, 都要注册回调类型.
        {
          auto callbackType = t->getFunctionType();
          if (callbackType->isVarArg())
            return UnspportedInstruction::make("不支持变参调用", t);
          auto callbackHash = call_site_hash(t);
          if (!callbackHash)
            return UnspportedInstruction::make("不可哈希的回调类型", t);
          auto [iter, isNew] =
            callbackStubs.try_emplace(*callbackHash, callbackType);
          if (isNew) {
            iter->second.mAttrs =
              filter_abi_attributes(t->getAttributes(), func.getContext());
            funcInfo.mCbStubArgs.push_back(*callbackHash);
          }
        }

      DO_OPERANDS:
        for (auto&& op : t->args())
          do_operand(op);
      }

      // 对于其它的指令, 我们只关心操作数是不是包含全局引用.
      else {
        for (auto&& op : ins.operands())
          do_operand(op);
      }
    }
  }

  // 添加调用类型
  auto [iter, isNew] =
    mResult.mCallingStubs.try_emplace(*callingHash, func.getFunctionType());
  if (isNew) {
    iter->second.mUid = mCallingCounter++;
    iter->second.mAttrs =
      filter_abi_attributes(func.getAttributes(), func.getContext());
  }
  funcInfo.mStubType = &iter->second;

  // 合并回调类型
  for (auto it = callbackStubs.begin(); it != callbackStubs.end();) {
    auto node = callbackStubs.extract(it++);
    auto ok = mResult.mCallbackStubs.insert(std::move(node));
    if (ok.inserted)
      ok.position->second.mUid = mCallbackCounter++;
  }

  // 加入到卸载函数表
  funcInfo.mUid = mFuncCounter++;
  mResult.mFunctbl.emplace(&func, std::move(funcInfo));

  // 合并全局引用
  for (auto&& gref : grefsInFunc) {
    Symtbl::GrefInfo grefInfo;
    auto [iter, isNew] = mResult.mGreftbl.try_emplace(gref);
    if (isNew)
      iter->second.mUid = mGrefCounter++;
  }

  return llvm::Error::success();
}

void
Analyze::do_operand(llvm::Value* op)
{
  // 这个函数收集操作数中的全局变量, 别名, 函数等外部引用.
  if (auto gv = llvm::dyn_cast<llvm::GlobalValue>(op)) {
    if (mGrefsInFunc->insert(gv).second)
      mFuncInfo->mExtRefArgs.push_back(gv);
    // 必须保证传参的顺序是确定的, 不会在两次编译之间改变: 我们采
    // 用函数内的引用顺序!
  }

  else if (auto ca = llvm::dyn_cast<llvm::ConstantAggregate>(op)) {
    for (auto&& op : ca->operands())
      do_operand(op);
  }

  else if (auto ce = llvm::dyn_cast<llvm::ConstantExpr>(op)) {
    for (auto&& op : ce->operands())
      do_operand(op);
  }
}

llvm::AnalysisKey Analyze::Pass::Key;

Analyze::Pass::Result
Analyze::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  Result result;
  std::unique_ptr<Logger> logger;
  if (!mLogPath.empty()) {
    std::error_code ec;
    logger = std::make_unique<FileLogger>(mLogPath, ec);
    if (ec) {
      mod.getContext().diagnose(
        DiagnosticString("fail to open log '" + mLogPath + "':" + ec.message(),
                         llvm::DS_Warning));
      logger.reset();
    }
  }
  if (!logger)
    logger = std::make_unique<DiagnosticLogger>(mod.getContext());
  Analyze analyze(mConfig, result, *logger);
  return result;
}

}; // namespace gyhcall
