/**
 * @brief 用于命令行程序编写的工具
 */

#pragma once

#include <llvm/Support/CommandLine.h>
#include <vector>

/**
 * @brief 将 inputs 中的目录展开为其中包含的所有子路径。
 *
 * @param inputs 输入参数列表。
 * @param paths 输出的路径列表，结果追加到其后。
 * @return 如果有错误，返回 true；否则返回 false。
 */
bool
expand_inputs(const llvm::cl::list<std::string>& inputs,
              std::vector<std::string>& paths);

/**
 * @brief 获取 path 相对于当前工作目录的绝对路径。
 */
std::string
abspath(llvm::StringRef path);
