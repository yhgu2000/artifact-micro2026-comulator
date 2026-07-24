#pragma once

#include "BwList.hpp"
#include "Logger.hpp"
#include "Symtbl.hpp"
#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

class Analyze
{
public:
  struct Pass;

  struct Config
  {
    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };

  const Config& mConfig;
  const BwList& mBwList{ BwList::kNULL };
  Symtbl& mResult;
  Logger& mLogger;

  Analyze(const Config& config, Symtbl& result, Logger& logger)
    : mConfig(config)
    , mResult(result)
    , mLogger(logger)
  {
  }

  Analyze(const Config& config,
          const BwList& bwList,
          Symtbl& result,
          Logger& logger)
    : mConfig(config)
    , mBwList(bwList)
    , mResult(result)
    , mLogger(logger)
  {
  }

  void operator()(llvm::Module& mod);

private:
  unsigned mCallingCounter{ 0 };
  unsigned mCallbackCounter{ 0 };
  unsigned mFuncCounter{ 0 };
  unsigned mGrefCounter{ 0 };
  llvm::Error do_function(llvm::Function& func);

  Symtbl::FuncInfo* mFuncInfo{ nullptr };
  llvm::SmallPtrSet<llvm::GlobalValue*, 16>* mGrefsInFunc{ nullptr };
  void do_operand(llvm::Value* op);
};

struct Analyze::Pass : public llvm::AnalysisInfoMixin<Analyze::Pass>
{
  using Result = Symtbl;
  static llvm::AnalysisKey Key;

  struct Config : public Analyze::Config
  {};

  Config mConfig;
  std::string mLogPath;

  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam);
};

} // namespace gyhcall
