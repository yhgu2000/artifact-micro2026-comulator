#pragma once

#include <gyhcall/BwList.hpp>
#include <gyhcall/Config.hpp>
#include <gyhcall/Symtbl.hpp>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/SourceMgr.h>

namespace {

llvm::cl::OptionCategory gOptCat("gyhcall");

#ifndef NOPT_I_C
gyhcall::Config gIc;

llvm::cl::opt<std::string> gOptIc( //
  "i-c",
  llvm::cl::desc("input Config (JSON)"),
  llvm::cl::cat(gOptCat));

llvm::cl::opt<std::string> gOptIcBW( //
  "i-c-bw",
  llvm::cl::desc("input overriding Config BwList (JSON)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_I_IR
llvm::LLVMContext gIirCTX;
std::unique_ptr<llvm::Module> gIir;

llvm::cl::opt<std::string> gOptIir( //
  "i-ir",
  llvm::cl::desc("input LLVM IR (.bc/.ll omissible)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_I_ST
gyhcall::Symtbl gIst;

llvm::cl::opt<std::string> gOptIst( //
  "i-st",
  llvm::cl::desc("input Symtbl (.json)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_I_BW
gyhcall::BwList gIbw;

llvm::cl::opt<std::string> gOptIbw( //
  "i-bw",
  llvm::cl::desc("input BwList (.json)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_O_IR
std::unique_ptr<llvm::Module> gOir;

llvm::cl::opt<std::string> gOptOir( //
  "o-ir",
  llvm::cl::desc("output LLVM IR (.bc/.ll auto)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_O_ST
gyhcall::Symtbl gOst;

llvm::cl::opt<std::string> gOptOst( //
  "o-st",
  llvm::cl::desc("output Symtbl (.json)"),
  llvm::cl::cat(gOptCat));
#endif

#ifdef OPT_O_BW
gyhcall::BwList gObw;

llvm::cl::opt<std::string> gOptObw( //
  "o-bw",
  llvm::cl::desc("output BwList (.json)"),
  llvm::cl::cat(gOptCat));
#endif

} // namespace

static int
main_body();

int
main(int argc, char* argv[])
{
  llvm::InitLLVM initLLVM(argc, argv);
  if (!llvm::cl::ParseCommandLineOptions(argc, argv, OPT_OVERVIEW))
    return 1;

  // 先加载配置
#ifndef NOPT_I_C
  if (gOptIc.empty()) {
    if (auto env = std::getenv("GYHCALL_CONFIG_DIR")) {
      if (*env == '\0') {
        llvm::errs() << "[ERROR]: environment 'GYHCALL_CONFIG_DIR' is empty\n";
        return 2;
      }
      gOptIc = std::string(env) + "/gyhcall.config";
    } else {
      llvm::errs() << "[ERROR]: environment 'GYHCALL_CONFIG_DIR' is not set\n";
      return 2;
    }
  }
  llvm::errs() << "[INFO]: input Config: " << gOptIc << '\n';

  auto configE = gyhcall::Config::from_file(gOptIc);
  if (!configE) {
    llvm::errs() << "[ERROR]: fail to load Config: " << gOptIc
                 << configE.takeError() << '\n';
    return 2;
  }
  gIc = std::move(*configE);

  if (!gOptIcBW.empty()) {
    auto bwListE = gyhcall::PickUp::Config::from_file(gOptIcBW);
    if (!bwListE) {
      llvm::errs() << "[ERROR]: fail to load Config BwList: " << gOptIcBW
                   << bwListE.takeError() << '\n';
      return 2;
    }
    gIc.mBwList = std::move(*bwListE);
  }
#endif

  // 再打开输出

  std::error_code ec;

#ifdef OPT_O_IR
  {
    auto ext = gOptOir.substr(gOptOir.size() - 3);
    if (ext != ".bc" && ext != ".ll")
      gOptOir += gIc.mLLorBC ? ".ll" : ".bc";
  }
  llvm::raw_fd_ostream oIR(gOptOir, ec);
  if (ec) {
    llvm::errs() << "[ERROR]: fail to open '--o-ir': " << gOptOir << ": "
                 << ec.message() << '\n';
    return 3;
  }
  llvm::errs() << "[INFO]: output LLVM IR: " << gOptOir << '\n';
#endif

#ifdef OPT_O_ST
  llvm::raw_fd_ostream oST(gOptOst, ec);
  if (ec) {
    llvm::errs() << "[ERROR]: fail to open '--o-st': " << gOptOst << ": "
                 << ec.message() << '\n';
    return 4;
  }
  llvm::errs() << "[INFO]: output Symtbl: " << gOptOst << '\n';
#endif

#ifdef OPT_O_BW
  llvm::raw_fd_ostream oBW(gOptObw, ec);
  if (ec) {
    llvm::errs() << "[ERROR]: fail to open '--o-bw': " << gOptObw << ": "
                 << ec.message() << '\n';
    return 5;
  }
  llvm::errs() << "[INFO]: output BwList: " << gOptObw << '\n';
#endif

  // 最后再读取输入

#ifdef OPT_I_IR
  {
    std::string path;

#ifndef NOPT_I_C
    const char* exts[2];
    if (gIc.mLLorBC) // 根据配置决定优先级
      exts[0] = ".ll", exts[1] = ".bc";
    else
      exts[0] = ".bc", exts[1] = ".ll";
    if (llvm::sys::fs::exists(path = gOptIir))
      gOptIir = std::move(path);
    else if (llvm::sys::fs::exists(path = gOptIir + exts[0]))
      gOptIir = std::move(path);
    else if (llvm::sys::fs::exists(path = gOptIir + exts[1]))
      gOptIir = std::move(path);
    else {
      llvm::errs()
        << "[ERROR]: '--i-ir' (or with .bc/.ll extension) not exists: "
        << gOptIir << '\n';
      return 6;
    }
#else
    if (!llvm::sys::fs::exists(path = gOptIir)) {
      llvm::errs() << "[ERROR]: '--i-ir' not exists: " << path << '\n';
      return 6;
    }
#endif

    llvm::SMDiagnostic err;
    gIir = llvm::parseIRFile(gOptIir, err, gIirCTX);
    if (!gIir) {
      llvm::errs() << "[ERROR]: fail to load '--i-ir': " << path << '\n';
      err.print(nullptr, llvm::errs());
      return 6;
    }
    llvm::errs() << "[INFO]: load IR: " << (gOptIir = std::move(path)) << '\n';
  }
#endif

#ifdef OPT_I_ST
  {
    auto symtblE = gyhcall::Symtbl::from_file(gOptIst, *gIir);
    if (!symtblE) {
      llvm::errs() << "[ERROR]: fail to load Symtbl: " << gOptIst
                   << symtblE.takeError() << '\n';
      return 7;
    }
    gIst = std::move(*symtblE);
    llvm::errs() << "[INFO]: load Symtbl: " << gOptIst << '\n';
  }
#endif

#ifdef OPT_I_BW
  {
    auto symtblE = gyhcall::BwList::from_file(gOptIbw, *gIir);
    if (!symtblE) {
      llvm::errs() << "[ERROR]: fail to load BwList: " << gOptIbw
                   << symtblE.takeError() << '\n';
      return 8;
    }
    gIbw = std::move(*symtblE);
    llvm::errs() << "[INFO]: load BwList: " << gOptIbw << '\n';
  }
#endif

  // 执行主体过程
  if (auto ret = main_body())
    return ret;
  // 然后逆序输出

#ifdef OPT_O_BW
  oBW << gObw.dump_json();
  oBW.close();
#endif

#ifdef OPT_O_ST
  oST << gOst.dump_json(gIirCTX);
  oST.close();
#endif

#ifdef OPT_O_IR
  if (gOir) {
    if (gOptOir.substr(gOptOir.size() - 3) == ".bc")
      llvm::WriteBitcodeToFile(*gOir, oIR);
    else if (gOptOir.substr(gOptOir.size() - 3) == ".ll")
      gOir->print(oIR, nullptr);
    else
      assert(false); // impossible case
    llvm::errs() << "[INFO]: verifying output IR...\n";
    if (llvm::verifyModule(*gOir, &llvm::errs()))
      return 3;
    llvm::errs() << "[INFO]: output IR verified\n";
  }
#endif
}
