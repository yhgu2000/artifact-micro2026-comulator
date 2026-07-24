#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

struct BwList
{
  /**
   * @brief 对于每项成员:
   *
   * - 如果为 false, 则禁用掉对应的处理环节;
   * - 如果为 true, 则在对应环节的处理失败时报错;
   * - 如果未指定, 则默认该怎么做就怎么做.
   */
  struct FuncBw
  {
    std::optional<bool> mOutline;
    std::optional<bool> mAnalyze;
    std::optional<bool> mChopOff;
    std::optional<bool> mTakeOut;

    llvm::json::Object dump_json() const;
    llvm::Error load_json(const llvm::json::Value& jval, llvm::json::Path path);
  };

  /// 函数黑白名单
  llvm::DenseMap<llvm::Function*, FuncBw> mFuncs;

  /// 如果值为 false, 则认为相关的调用指令无法被卸载;
  /// 否则将相关指令的被调操作数保留不变, 直接卸载到主侧.
  llvm::DenseMap<llvm::Function*, bool> mCallees;

  /// FCP-CM 切除名单, 这些函数只加前缀而不切除函数体
  llvm::DenseSet<llvm::Function*> mFCPcmChop;
  /// 如果为 True, 则对不在名单中的函数应用 FCP-CM 切除
  bool mFCPcmChopOthers = false;

  llvm::json::Object dump_json() const;
  llvm::Error load_json(const llvm::json::Value& jval,
                        llvm::json::Path path,
                        llvm::Module& mod);

  llvm::Error to_file(llvm::StringRef path) const;
  static llvm::Expected<BwList> from_file(llvm::StringRef path,
                                          llvm::Module& mod);

  /// 空的黑白名单, 不做任何事情
  static const BwList kNULL;
};

} // namespace gyhcall
