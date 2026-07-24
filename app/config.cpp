#include <gyhcall/Config.hpp>
#include <llvm/Support/InitLLVM.h>

int
main(int argc, char* argv[])
{
  llvm::InitLLVM initLLVM(argc, argv);
  if (!llvm::cl::ParseCommandLineOptions(argc, argv, "show default config"))
    return 1;

  gyhcall::Config config;
  llvm::outs() << llvm::json::Value(config.dump_json()) << '\n';
  return 0;
}
