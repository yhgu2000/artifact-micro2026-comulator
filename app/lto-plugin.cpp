#include <csignal>
#include <gyhcall/LtoPass.hpp>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#define DEBUG_TYPE "gyhcall"
#include <llvm/Support/Debug.h>

namespace {

gyhcall::Config gConfig;
std::string gConfigDir;

void
register_pass_builder_callbacks(llvm::PassBuilder& pb)
{
  // pb.registerFullLinkTimeOptimizationEarlyEPCallback(
  pb.registerFullLinkTimeOptimizationLastEPCallback(
    [](llvm::ModulePassManager& mpm, llvm::OptimizationLevel ol) {
      mpm.addPass(gyhcall::LtoPass{ gConfig, gConfigDir });
    });
}

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo()
{
  auto env = std::getenv("GYHCALL_DEBUG_PLUGIN");
  if (env) {
    llvm::errs() << "waiting for debugger: " << getpid() << "\n";
    raise(SIGSTOP);
  }

  env = std::getenv("GYHCALL_CONFIG_DIR");
  if (env == nullptr) {
    llvm::errs() << "environment 'GYHCALL_CONFIG_DIR' is not set\n";
    return {};
  }
  if (*env == '\0') {
    return {
      .APIVersion = LLVM_PLUGIN_API_VERSION,
      .PluginName = "gyhcall",
      .PluginVersion = LLVM_VERSION_STRING,
      .RegisterPassBuilderCallbacks = [](llvm::PassBuilder&) {},
    };
  }
  gConfigDir = env;

  // 加载配置文件
  auto path = gConfigDir + "/gyhcall.config";
  auto configE = gyhcall::Config::from_file(path);
  if (!configE) {
    llvm::errs() << "fail to load '" << path << "': " << configE.takeError()
                 << "\n";
    return {};
  }
  gConfig = std::move(configE.get());

  return {
    .APIVersion = LLVM_PLUGIN_API_VERSION,
    .PluginName = "gyhcall",
    .PluginVersion = LLVM_VERSION_STRING,
    .RegisterPassBuilderCallbacks = register_pass_builder_callbacks,
  };
}
