#pragma once

#include "Config.hpp"
#include <llvm/IR/PassManager.h>

namespace gyhcall {

struct LtoPass : public llvm::PassInfoMixin<LtoPass>
{
  Config mConfig;
  std::string mConfigDir;

  LtoPass(Config config, std::string configDir)
    : mConfig(std::move(config))
    , mConfigDir(std::move(configDir))
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

  static bool isRequired() { return true; }
};

} // namespace gyhcall
