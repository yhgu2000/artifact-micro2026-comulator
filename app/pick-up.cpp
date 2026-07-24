#define OPT_I_IR
#define OPT_O_BW
#define OPT_OVERVIEW "pick up BwList from the LLVM IR"
#include "_main.hpp"
#include <gyhcall/PickUp.hpp>

using HRC = std::chrono::high_resolution_clock;

static int
main_body()
{
  auto conf = gIc.pick_up();
  (llvm::outs() << "picking up...").flush();

  gyhcall::DiagnosticLogger logger(gIirCTX);
  auto timing = HRC::now();
  gyhcall::PickUp(conf, gObw, logger)(*gIir);
  std::chrono::duration<double> cost = HRC::now() - timing;

  llvm::outs() << " DONE(" << cost.count() << "s)\n";
  return 0;
}
