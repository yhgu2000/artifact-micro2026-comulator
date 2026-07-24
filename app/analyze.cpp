#define OPT_I_IR
#define OPT_O_ST
#define OPT_OVERVIEW "analyze LLVM IR for the Symtbl"
#include "_main.hpp"
#include <gyhcall/Analyze.hpp>

using HRC = std::chrono::high_resolution_clock;

static int
main_body()
{
  auto conf = gIc.analyze();
  (llvm::outs() << "analyzing...").flush();

  gyhcall::DiagnosticLogger logger(gIirCTX);
  auto timing = HRC::now();
  gyhcall::Analyze(conf, gOst, logger)(*gIir);
  std::chrono::duration<double> cost = HRC::now() - timing;

  llvm::outs() << " DONE(" << cost.count() << "s)\n";
  return 0;
}
