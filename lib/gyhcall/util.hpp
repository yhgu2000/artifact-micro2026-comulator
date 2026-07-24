#pragma once

#include <cstdint>
#include <functional>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

/**
 * @brief 单纯表示失败的错误类, 无任何额外信息
 */
struct Failure : public llvm::ErrorInfo<Failure>
{
  static char ID;

  void log(llvm::raw_ostream& os) const override;
  std::error_code convertToErrorCode() const override;
};

/**
 * @brief 包含一条自定义消息的诊断信息类
 */
struct DiagnosticString : public llvm::DiagnosticInfo
{
  std::string mMsg;

  /**
   * @param msg 自定义消息
   * @param ds 诊断信息的严重程度, 默认为 llvm::DS_Error
   */
  DiagnosticString(std::string msg,
                   llvm::DiagnosticSeverity ds = llvm::DS_Error)
    : llvm::DiagnosticInfo(llvm::getNextAvailablePluginDiagnosticKind(), ds)
    , mMsg(std::move(msg))
  {
  }

  void print(llvm::DiagnosticPrinter& dp) const override;
};

/**
 * @brief 将 llvm::Error 转换为诊断信息类, 附带一条自定义消息
 */
struct DiagnosticError : public llvm::DiagnosticInfo
{
  std::string mMsg;
  llvm::Error mErr;

  /**
   * @param msg 自定义消息
   * @param err llvm::Error 对象, 必须已被检查过
   * @param ds 诊断信息的严重程度, 默认为 llvm::DS_Error
   */
  DiagnosticError(std::string msg,
                  llvm::Error&& err,
                  llvm::DiagnosticSeverity ds = llvm::DS_Error)
    : llvm::DiagnosticInfo(llvm::getNextAvailablePluginDiagnosticKind(), ds)
    , mMsg(std::move(msg))
    , mErr(std::move(err))
  {
  }

  void print(llvm::DiagnosticPrinter& dp) const override;
};

llvm::Error
dump_json_file(const llvm::json::Value& jval, llvm::StringRef path);

llvm::Error
load_json_file(llvm::StringRef path,
               const std::function<llvm::Error(llvm::json::Value& jval,
                                               llvm::json::Path path)>& loader);

} // namespace gyhcall

namespace llvm::json {

bool
fromJSON(const llvm::json::Value& E, std::uint32_t& Out, llvm::json::Path P);

bool
fromJSON(const llvm::json::Value& E, std::uint8_t& Out, llvm::json::Path P);

bool
fromJSON(const llvm::json::Value& E, std::uint16_t& Out, llvm::json::Path P);

bool
fromJSON(const llvm::json::Value& E, std::int8_t& Out, llvm::json::Path P);

bool
fromJSON(const llvm::json::Value& E, std::int16_t& Out, llvm::json::Path P);

bool
fromJSON(const llvm::json::Value& E, std::int32_t& Out, llvm::json::Path P);

} // namespace llvm
