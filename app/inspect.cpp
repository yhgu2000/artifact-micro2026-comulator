#define NOPT_I_C
#define OPT_I_IR
#define OPT_OVERVIEW "inspect global names in LLVM IR"
#include "_main.hpp"

namespace {

llvm::cl::list<std::string> gOptNames(llvm::cl::Positional,
                                      llvm::cl::OneOrMore,
                                      llvm::cl::desc("NAMES"),
                                      llvm::cl::cat(gOptCat));

} // namespace

static int
main_body()
{
  for (auto&& name : gOptNames) {
    auto val = gIir->getNamedValue(name);
    if (!val) {
      llvm::outs() << "\nNOT FOUND: " << name << "\n\n";
      continue;
    }
    llvm::outs() << '\n' << *val << '\n';
  }
  return 0;
}
