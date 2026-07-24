#pragma once

#include "BwList.hpp"
#include "Logger.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <vector>

namespace gyhcall {

class Outline
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
  Logger& mLogger;

  Outline(const Config& config, Logger& logger)
    : mConfig(config)
    , mLogger(logger)
  {
  }

  Outline(const Config& config, const BwList& bwlist, Logger& logger)
    : mConfig(config)
    , mBwList(bwlist)
    , mLogger(logger)
  {
  }

  void operator()(llvm::Module& mod);

private:
  llvm::DenseMap<llvm::hash_code, llvm::Function*> mOutliners;

  llvm::Function* gen_outliner(llvm::Instruction& ins);
};

struct Outline::Pass : public llvm::PassInfoMixin<Outline::Pass>
{
  struct Config : public Outline::Config
  {};

  Config mConfig;
  std::string mLogPath;

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

  static bool isRequired() { return true; }
};

} // namespace gyhcall
