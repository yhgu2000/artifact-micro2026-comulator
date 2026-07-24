#pragma once

#include "BwList.hpp"
#include "Logger.hpp"
#include "Symtbl.hpp"
#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

class TakeOut
{
public:
  struct Pass;

  struct Config
  {
    /// 是否生成调试代码。
    bool mDebug{ false };

    /// 是否使用全局引用表。
    bool mUseGRT{ false };

    /// 是否使用快速调用路径。
    bool mUseFCP{ false };

    /// 是否启用跨模块快速调用路径
    bool mUseFCPcm{ false };

    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };

  const Config& mConfig;
  const BwList& mBwList{ BwList::kNULL };
  const Symtbl& mSymtbl;
  Logger& mLogger;

  TakeOut(const Config& config, const Symtbl& symtbl, Logger& logger)
    : mConfig(config)
    , mSymtbl(symtbl)
    , mLogger(logger)
  {
  }

  TakeOut(const Config& config,
          const BwList& bwList,
          const Symtbl& symtbl,
          Logger& logger)
    : mConfig(config)
    , mBwList(bwList)
    , mSymtbl(symtbl)
    , mLogger(logger)
  {
  }

  std::unique_ptr<llvm::Module> operator()(llvm::Module& mod);

private:
  llvm::Module* mModule{ nullptr };
  std::string mModuleUidHex;

  llvm::Function* mGyhDebug{ nullptr };
  llvm::GlobalVariable* mGyhCallDebugFmt{ nullptr };
  llvm::GlobalVariable* mGyhCallbackDebugFmt{ nullptr };

  llvm::Function* mGyhCallback{ nullptr };

  llvm::GlobalVariable* mGyhExtreftbl{ nullptr };
  llvm::GlobalVariable* mGyhCbstubtbl{ nullptr };

  llvm::GlobalVariable* mGyhFunctbl{ nullptr };

  void gen_debug();
  std::vector<llvm::Constant*> gen_hfuncs(
    const llvm::DenseSet<llvm::Function*>& blackList);
  void gen_functbl(std::vector<llvm::Constant*>&& hfuncs,
                   const llvm::DenseSet<llvm::Function*>& blackList);

private:
  llvm::DenseMap<llvm::Function*, llvm::Function*> mWhiteCallees;
  llvm::DenseMap<llvm::Function*, llvm::Function*> mFcpFuncMap;

  void do_hstubcvt(llvm::Function* func,
                   llvm::Function* hstub,
                   const Symtbl::FuncInfo& info);
  llvm::SmallVector<llvm::Value*> mHstubArgs;
  llvm::Value* mHstubRetPad{ nullptr };
  llvm::SmallDenseMap<llvm::Value*, llvm::Value*> mExtRefsMap;
  llvm::SmallDenseMap<llvm::hash_code, llvm::Value*> mCbStubMap;

  void do_function(llvm::Function* func,
                   llvm::Function* hfunc,
                   const Symtbl::FuncInfo& info);
  llvm::SmallVector<std::pair<llvm::Instruction*, llvm::Instruction*>, 32>*
    mToReplace;
  llvm::Instruction* mEntryTerminator{ nullptr };

  llvm::Instruction* do_instruction(llvm::Instruction* ins);
  llvm::Instruction* mInstruction{ nullptr };

  llvm::Instruction* do_callback(llvm::CallBase* call);
  llvm::Instruction* do_fcp_cm(llvm::CallBase* call);

  llvm::Value* do_operand(llvm::Value* op);

  llvm::Metadata* do_metadata(llvm::Metadata* md);
};

struct TakeOut::Pass : public llvm::PassInfoMixin<TakeOut>
{
  struct Config : public TakeOut::Config
  {};

  Config mConfig;
  std::string mLogPath;
  std::string mOutputPath;
  bool mOutText{ false };

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

  static bool isRequired() { return true; }
};

} // namespace gyhcall
