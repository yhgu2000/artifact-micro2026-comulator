#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/JSON.h>
#include <mutex>

namespace gyhcall {

/**
 * @brief jsonl 日志记录器接口
 */
class Logger
{
public:
  virtual ~Logger() noexcept = default;

  /**
   * @brief 记录一条 jsonl 日志, 默认实现什么也不做。
   *
   * @param msg 日志消息
   */
  virtual void operator()(const llvm::json::Value& msg);

  void operator()(const llvm::StringRef msg);

  void operator()(const llvm::Value* val, llvm::StringRef msg);

  void operator()(const llvm::Instruction* inst, llvm::StringRef msg);

  void operator()(const llvm::BasicBlock* bb, llvm::StringRef msg);

  void operator()(const llvm::Function* func, llvm::StringRef msg);

  void operator()(const llvm::Module* mod, llvm::StringRef msg);
};

/**
 * @brief 线程安全地将日志记录到文件
 */
class FileLogger : public Logger
{
public:
  ~FileLogger() noexcept override = default;

  FileLogger(llvm::StringRef filePath, std::error_code& ec)
    : mOutput(filePath, ec)
  {
  }

  void operator()(const llvm::json::Value& msg) override;

private:
  llvm::raw_fd_ostream mOutput;
  std::mutex mMutex;
};

/**
 * @brief 将日志用 llvm::Context::diagnose 记录
 */
class DiagnosticLogger : public Logger
{
public:
  ~DiagnosticLogger() noexcept override = default;

  DiagnosticLogger(llvm::LLVMContext& ctx)
    : mCtx(ctx)
  {
  }

  void operator()(const llvm::json::Value& msg) override;

private:
  llvm::LLVMContext& mCtx;
};

} // namespace gyhcall
