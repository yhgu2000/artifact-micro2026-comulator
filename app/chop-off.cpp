#include <gyhcall/ChopOff.hpp>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/InitLLVM.h>

#define OPT_I_IR
#define OPT_I_ST
#define OPT_O_IR
#define OPT_OVERVIEW "chop off guest functions"
#include "_main.hpp"

using HRC = std::chrono::high_resolution_clock;

static int
main_body()
{
  auto conf = gIc.chop_off();
  (llvm::outs() << "chopping off...").flush();

  gyhcall::DiagnosticLogger logger(gIirCTX);
  auto timing = HRC::now();
  gyhcall::ChopOff(conf, gIst, logger)(*gIir);
  std::chrono::duration<double> cost = HRC::now() - timing;

  llvm::outs() << " DONE(" << cost.count() << "s)\n";
  gOir = std::move(gIir);
  return 0;
}
