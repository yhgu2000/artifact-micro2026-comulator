#include "cli-tools.hpp"
#include <llvm/Support/FileSystem.h>

bool
expand_inputs(const llvm::cl::list<std::string>& inputs,
              std::vector<std::string>& paths)
{
  bool hasError = false;
  for (auto&& input : inputs) {
    std::error_code ec;
    llvm::sys::fs::directory_iterator it(input, ec);
    if (ec) {
      if (ec == std::errc::not_a_directory)
        paths.push_back(input);
      else {
        llvm::outs() << "Fail to open directory: " << input << '\n';
        hasError = true;
      }
      continue;
    }
    for (decltype(it) end; it != end; it.increment(ec)) {
      if (ec) {
        llvm::outs() << "Fail to iterate directory: " << input << '\n';
        hasError = true;
        break;
      }
      paths.push_back(it->path());
    }
  }
  return hasError;
}

std::string
abspath(llvm::StringRef path)
{
  llvm::SmallVector<char, 256> absPath;
  absPath.append(path.begin(), path.end());
  llvm::sys::fs::make_absolute(absPath);
  return { absPath.begin(), absPath.end() };
}
