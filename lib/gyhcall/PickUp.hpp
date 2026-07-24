#pragma once

#include "BwList.hpp"
#include "Logger.hpp"
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

class PickUp
{
public:
  struct Pass;

  struct Config
  {
    /// 函数黑白名单
    llvm::StringMap<BwList::FuncBw> mFuncs;

    /// 调用黑白名单
    llvm::StringMap<bool> mCallees;

    /// FCP-CM 切除名单
    std::vector<std::string> mFCPcmChop;
    /// 反选 FCP-CM 切除名单
    bool mFCPcmChopOthers = false;

    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);

    llvm::Error to_file(llvm::StringRef path) const;
    static llvm::Expected<Config> from_file(llvm::StringRef path);
  };

  const Config& mConfig;
  BwList& mResult;
  Logger& mLogger;

  PickUp(const Config& config, BwList& result, Logger& logger)
    : mConfig(config)
    , mResult(result)
    , mLogger(logger)
  {
  }

  void operator()(llvm::Module& mod);

private:
  llvm::Module* mModule{ nullptr };

  void do_annotation();
  void do_builtins();
  void do_config();
};

struct PickUp::Pass : public llvm::AnalysisInfoMixin<PickUp::Pass>
{
  using Result = BwList;
  static llvm::AnalysisKey Key;

  struct Config : public PickUp::Config
  {};

  Config mConfig;
  std::string mLogPath;

  Result run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam);
};

} // namespace gyhcall
