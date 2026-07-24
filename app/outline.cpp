#define OPT_I_IR
#define OPT_O_IR
#define OPT_OVERVIEW "outline un-offloadable code from gfuncs"
#include "_main.hpp"
#include <gyhcall/Outline.hpp>

using HRC = std::chrono::high_resolution_clock;

static int
main_body()
{
  gyhcall::DiagnosticLogger logger(gIirCTX);

  auto conf = gIc.outline();
  (llvm::outs() << "outlining...").flush();

  auto timing = HRC::now();
  gyhcall::Outline{ conf, logger }(*gIir);
  std::chrono::duration<double> cost = HRC::now() - timing;

  llvm::outs() << " DONE(" << cost.count() << "s)\n";
  gOir = std::move(gIir);
  return 0;
}
