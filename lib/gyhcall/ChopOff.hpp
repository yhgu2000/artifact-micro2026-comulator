#pragma once

#include "BwList.hpp"
#include "Logger.hpp"
#include "Symtbl.hpp"
#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

class ChopOff
{
public:
  struct Pass;

  struct Config
  {
    /// 是否生成调试代码
    bool mDebug{ false };

    /// 是否使用全局引用表
    bool mUseGRT{ false };

    /// 是否启用跨模块快速调用路径
    bool mUseFCPcm{ false };

    /// 执行切除的最小基本块数
    unsigned mMinBasicBlocks = 2;

    /// 执行切除的最小指令数
    unsigned mMinInstructions = 10;

    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };

  const Config& mConfig;
  const BwList& mBwList{ BwList::kNULL };
  const Symtbl& mSymtbl;
  Logger& mLogger;

  ChopOff(const Config& config, const Symtbl& symtbl, Logger& logger)
    : mConfig(config)
    , mSymtbl(symtbl)
    , mLogger(logger)
  {
  }

  ChopOff(const Config& config,
          const BwList& bwList,
          const Symtbl& symtbl,
          Logger& logger)
    : mConfig(config)
    , mBwList(bwList)
    , mSymtbl(symtbl)
    , mLogger(logger)
  {
  }

  void operator()(llvm::Module& mod);

private:
  llvm::Module* mModule{ nullptr };
  std::string mModuleUidHex;

  llvm::Function* mGyhDebug{ nullptr };
  llvm::GlobalVariable* mGyhCallDebugFmt{ nullptr };
  llvm::GlobalVariable* mGyhCallbackDebugFmt{ nullptr };

  llvm::GlobalVariable* mGyhQlibUid{ nullptr };
  llvm::Function* mGyhCtor{ nullptr };
  llvm::Function* mGyhCtorHHH{ nullptr };
  llvm::Function* mGyhDtor{ nullptr };
  llvm::Function* mGyhDtorHHH{ nullptr };
  llvm::Function* mGyhCall{ nullptr };
  llvm::Function* mGyhCallbackReturn{ nullptr };

  llvm::GlobalVariable* mGyhExtreftbl{ nullptr };
  llvm::GlobalVariable* mGyhCbstubtbl{ nullptr };
  llvm::Function* mGyhCtorGRT{ nullptr };
  llvm::Function* mGyhCallGRT{ nullptr };

  llvm::GlobalVariable* mGyhGfunctbl{ nullptr };
  llvm::Function* mGyhCtorFCPcm{ nullptr };

  llvm::GlobalVariable* gen_debug_fmt(const char* fmt);
  void gen_debug();
  void gen_uid();
  using CbStubs = llvm::DenseMap<llvm::hash_code, llvm::Function*>;
  CbStubs gen_cb_stubs();
  void gen_grt(const CbStubs& cbStubs);
  using Benefits = std::vector<decltype(Symtbl::mFunctbl)::const_iterator>;
  Benefits calc_benefits();
  void chop_gfuncs(const CbStubs& cbStubs, const Benefits& benefits);
  void gen_ctor_dtor();
};

struct ChopOff::Pass : public llvm::PassInfoMixin<ChopOff>
{
  struct Config : public ChopOff::Config
  {
    std::string mLogPath;

    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };

  Config mConfig;

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

  static bool isRequired() { return true; }
};

} // namespace gyhcall
