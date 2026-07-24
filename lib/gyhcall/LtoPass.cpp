#include "LtoPass.hpp"
#include "Analyze.hpp"
#include "ChopOff.hpp"
#include "Outline.hpp"
#include "PickUp.hpp"
#include "TakeOut.hpp"
#include "util.hpp"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/FileSystem.h>
#include <random>

namespace gyhcall {

llvm::PreservedAnalyses
LtoPass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  auto& ctx = mod.getContext();
  std::error_code ec;

  // 给模块分配唯一 ID 并创建数据目录
  std::uint32_t uid;
  std::string uidDir;
  static thread_local std::default_random_engine sRand(std::random_device{}());
  for (auto i = 0; i < 3; ++i) {
    uid = std::uniform_int_distribution<std::uint32_t>(0, UINT32_MAX)(sRand);
    uidDir = mConfigDir + "/guest/" + llvm::utohexstr(uid, false, 8);
    if (llvm::sys::fs::create_directories(uidDir, false))
      continue;
    goto FOR_ELSE;
  }
  ctx.diagnose(DiagnosticString("fail to create uid dir: " + uidDir));
  return llvm::PreservedAnalyses::all();
FOR_ELSE:

  auto logPath = uidDir + "/log.jsonl";
  FileLogger logger(logPath, ec);
  if (ec) {
    ctx.diagnose(DiagnosticString("fail to create log file '" + logPath +
                                  "': " + ec.message()));
    return llvm::PreservedAnalyses::all();
  }

  auto saveIR = [&](const char* name, llvm::Module& mod) -> bool {
    auto ext = mConfig.mLLorBC ? ".ll" : ".bc";
    auto path = uidDir + "/" + name + ext;
    llvm::raw_fd_ostream out(path, ec);
    if (ec) {
      ctx.diagnose(
        DiagnosticString("fail to save '" + path + "': " + ec.message()));
      return false;
    }
    if (mConfig.mLLorBC)
      mod.print(out, nullptr);
    else
      llvm::WriteBitcodeToFile(mod, out);
    return true;
  };

  // 保存 allin1
  if (!saveIR("allin1", mod))
    return llvm::PreservedAnalyses::all();
  logger("allin1 saved");

  // 执行 pick-up
  BwList bwList;
  {
    PickUp{ mConfig.pick_up(), bwList, logger }(mod);
    auto path = uidDir + "/bw-list.json";
    if (auto err = bwList.to_file(path)) {
      ctx.diagnose(
        DiagnosticError("fail to save '" + path + "'", std::move(err)));
      return llvm::PreservedAnalyses::all();
    }
    logger("bw-list saved");
  }

  // 执行 outline
  if (mConfig.mUsePFO) {
    auto conf = mConfig.outline();
    Outline{ conf, bwList, logger }(mod);
    if (!saveIR("outlined", mod))
      return llvm::PreservedAnalyses::all();
    logger("outlined saved");
  }

  // 执行 analyze
  Symtbl symtbl;
  symtbl.mUid = uid;
  {
    Analyze{ mConfig.analyze(), bwList, symtbl, logger }(mod);
    auto path = uidDir + "/symtbl.json";
    if (auto err = symtbl.to_file(path, ctx)) {
      ctx.diagnose(
        DiagnosticError("fail to save '" + path + "'", std::move(err)));
      return llvm::PreservedAnalyses::all();
    }
    logger("symtbl saved");
  }

  // 执行 take-out
  {
    auto hostMod = TakeOut{ mConfig.take_out(), bwList, symtbl, logger }(mod);
    assert(hostMod && "take-out returns unexpected nullptr");
    if (!saveIR("host", *hostMod))
      return llvm::PreservedAnalyses::all();
    logger("host saved");
  }

  // 执行 chop-off
  {
    ChopOff{ mConfig.chop_off(), bwList, symtbl, logger }(mod);
    if (!saveIR("guest", mod))
      return llvm::PreservedAnalyses::all();
    logger("guest saved");
  }

  return llvm::PreservedAnalyses::all();
}

} // namespace gyhcall
