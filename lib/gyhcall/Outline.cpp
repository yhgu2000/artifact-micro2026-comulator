#include "Outline.hpp"
#include "type_hash.hpp"
#include "util.hpp"

#define DEBUG_TYPE "gyhcall"
#include <llvm/Support/Debug.h>

namespace gyhcall {

llvm::json::Object
Outline::Config::dump_json() const
{
  llvm::json::Object jobj;
  return jobj;
}

llvm::Error
Outline::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

namespace {

struct CheckFunction
{
  /// 需要外联的调用指令
  std::vector<llvm::Instruction*> mToOutline;
  /// 需要补外联的指令
  std::vector<llvm::Instruction*> mToNegOutline;
};

CheckFunction
check_function(llvm::Function& func)
{
  CheckFunction ret;

  //* 这里的代码与 Analyze 高度相关

  for (auto&& bb : func) {
    for (auto&& ins : bb) {
      if (auto t = llvm::dyn_cast<llvm::IndirectBrInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::InvokeInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::ResumeInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::CatchSwitchInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::CatchReturnInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::CleanupReturnInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::LandingPadInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::CatchPadInst>(&ins))
        ret.mToNegOutline.push_back(&ins);
      else if (auto t = llvm::dyn_cast<llvm::CleanupPadInst>(&ins))
        ret.mToNegOutline.push_back(&ins);

      else if (auto t = llvm::dyn_cast<llvm::CallBase>(&ins)) {
        auto callee = t->getCalledOperand();
        if (llvm::isa<llvm::InlineAsm>(callee)) {
          ret.mToNegOutline.push_back(&ins); // 内联汇编通常与当前栈帧强关联
          continue;
        }

        auto callbackType = t->getFunctionType();
        if (callbackType->isVarArg()) {
          // 老实说, 我们只能对变参调用这一种情况做外联
          ret.mToOutline.push_back(&ins);
          continue;
        }

        if (callee->getName() == "_setjmp") {
          ret.mToNegOutline.push_back(&ins);
          continue;
        }

        // 其它一般情况的调用指令都不用管
      }

      // 其它指令没什么特殊的, 不用管
    }
  }

  return ret;
}

} // namespace

void
Outline::operator()(llvm::Module& mod)
{
  for (auto&& func : mod) {
    std::optional<bool> bw;
    if (auto it = mBwList.mFuncs.find(&func); it != mBwList.mFuncs.end())
      bw = it->second.mOutline;
    if (bw.has_value() && !bw.value())
      continue;

    if (func.isVarArg())
      continue; // 变参函数不会被卸载, 因此无需外联

    auto check = check_function(func);
    if (bw.has_value() && bw.value()) {
      if (check.mToOutline.empty() && check.mToNegOutline.empty())
        mod.getContext().diagnose(DiagnosticString("没有可以外联的指令"));
    }

    if (!check.mToNegOutline.empty()) {
      continue; // TODO: 补外联
    }

    for (auto* ins : check.mToOutline) {
      auto outliner = gen_outliner(*ins);
      if (outliner == nullptr)
        continue;
      llvm::IRBuilder<> irb(ins);
      llvm::SmallVector<llvm::Value*, 16> ops;
      for (auto&& op : ins->operands())
        ops.push_back(op.operator->());
      auto newIns = irb.CreateCall(outliner, ops, ins->getName());
      ins->replaceAllUsesWith(newIns);
      ins->eraseFromParent();
    }
  }
  for (auto&& [hash, func] : mOutliners)
    mod.getFunctionList().insert(mod.end(), func);
}

/**
 * @brief 生成指令对应的外联函数
 *
 * @param ins 指令
 * @return llvm::Function* 孤儿外联函数, 需要再添加到模块中
 */
llvm::Function*
Outline::gen_outliner(llvm::Instruction& ins)
{
  llvm::SmallVector<llvm::Type*, 16> argTys;
  for (auto&& op : ins.operands())
    argTys.push_back(op->getType());
  auto funcTy = llvm::FunctionType::get(ins.getType(), argTys, false);
  auto hash = type_hash(funcTy);
  if (!hash.has_value())
    return nullptr;

  auto& func = mOutliners.getOrInsertDefault(hash.value());
  if (func)
    return func;
  func = llvm::Function::Create(funcTy,
                                llvm::GlobalValue::WeakAnyLinkage,
                                "__gyh_outliner_" + hash2str(hash.value()));

  auto bb = llvm::BasicBlock::Create(func->getContext(), "entry", func);
  auto clone = ins.clone();
  clone->insertInto(bb, bb->begin());
  for (auto&& [idx, arg] : llvm::enumerate(func->args()))
    clone->setOperand(idx, &arg);
  if (funcTy->getReturnType()->isVoidTy())
    llvm::ReturnInst::Create(func->getContext(), bb);
  else
    llvm::ReturnInst::Create(func->getContext(), clone, bb);

  // 避免函数再被内联回去
  func->addFnAttr(llvm::Attribute::NoInline);
  func->addFnAttr(llvm::Attribute::OptimizeNone);
  return func;
}

llvm::PreservedAnalyses
Outline::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  // TODO
  return llvm::PreservedAnalyses::all();
}

} // namespace gyhcall
