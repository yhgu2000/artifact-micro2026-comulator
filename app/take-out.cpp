#define OPT_I_IR
#define OPT_I_ST
#define OPT_O_IR
#define OPT_OVERVIEW "take out host functions"
#include "_main.hpp"
#include <gyhcall/TakeOut.hpp>

using HRC = std::chrono::high_resolution_clock;

static int
main_body()
{
  gyhcall::DiagnosticLogger logger(gIirCTX);

  auto conf = gIc.take_out();
  (llvm::outs() << "taking out...").flush();

  auto timing = HRC::now();
  gOir = gyhcall::TakeOut(conf, gIst, logger)(*gIir);
  std::chrono::duration<double> cost = HRC::now() - timing;

  llvm::outs() << " DONE(" << cost.count() << "s)\n";
  return 0;
}
