#pragma once

#include <llvm/IR/PassManager.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

class GenQlib
{
public:
  struct Pass;

  struct Config
  {
    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };
};

struct GenQlib::Pass : public llvm::PassInfoMixin<GenQlib>
{
  struct Config : public GenQlib::Config
  {};

  Config mConfig;
  std::string mLogPath;

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

  static bool isRequired() { return true; }
};

} // namespace gyhcall
