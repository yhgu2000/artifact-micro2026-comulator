#include "GenQlib.hpp"
#include "util.hpp"
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>

namespace gyhcall {

llvm::json::Object
GenQlib::Config::dump_json() const
{
  llvm::json::Object jobj;
  return jobj;
}

llvm::Error
GenQlib::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

llvm::PreservedAnalyses
GenQlib::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  return llvm::PreservedAnalyses::all();
}

} // namespace gyhcall
