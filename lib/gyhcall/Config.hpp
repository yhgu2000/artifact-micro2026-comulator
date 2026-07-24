#pragma once

#include "Analyze.hpp"
#include "ChopOff.hpp"
#include "GenQlib.hpp"
#include "Outline.hpp"
#include "PickUp.hpp"
#include "TakeOut.hpp"

namespace gyhcall {

struct Config
{
  /// IR 输出模式, True 输出文本格式 (.ll), False 输出字节码 (.bc)
  bool mLLorBC = false;
  /// 是否生成调试代码
  bool mDebug{ false };

  /// 启用部分函数卸载优化
  bool mUsePFO = false;
  /// 启用全局引用表优化
  bool mUseGRT = false;
  /// 启用快速调用路径优化
  bool mUseFCP = false;
  /// 启用跨模块快速调用路径优化
  bool mUseFCPcm = false;

  /// 函数卸载的基本块数阈值
  unsigned mMinBasicBlocks = 2;
  /// 函数卸载的指令数阈值
  unsigned mMinInstructions = 10;

  /// 卸载黑白名单
  PickUp::Config mBwList;

  /// {{@ 获取各环节的配置项
  Analyze::Config analyze() const;
  ChopOff::Config chop_off() const;
  GenQlib::Config gen_qlib() const;
  Outline::Config outline() const;
  PickUp::Config pick_up() const;
  TakeOut::Config take_out() const;
  /// @}}

  llvm::json::Object dump_json() const;
  llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);

  llvm::Error to_file(llvm::StringRef path) const;
  static llvm::Expected<Config> from_file(llvm::StringRef path);
};

} // namespace gyhcall
