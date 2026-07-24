#include "util.hpp"
#include <climits>
#include <llvm/IR/DiagnosticPrinter.h>

namespace gyhcall {

char Failure::ID = 0;

void
Failure::log(llvm::raw_ostream& os) const
{
}

std::error_code
Failure::convertToErrorCode() const
{
  return llvm::inconvertibleErrorCode();
}

void
DiagnosticString::print(llvm::DiagnosticPrinter& dp) const
{
  dp << mMsg;
}

void
DiagnosticError::print(llvm::DiagnosticPrinter& dp) const
{
  std::string s;
  llvm::raw_string_ostream sout(s);
  sout << mErr;
  dp << mMsg << ": " << s;
}

llvm::Error
dump_json_file(const llvm::json::Value& jval, llvm::StringRef path)
{
  std::error_code ec;
  llvm::raw_fd_ostream os(path, ec);
  if (ec)
    return llvm::createFileError(path, ec);
  os << jval;
  return llvm::Error::success();
}

llvm::Error
load_json_file(llvm::StringRef path,
               const std::function<llvm::Error(llvm::json::Value& jval,
                                               llvm::json::Path path)>& loader)
{
  auto mbE = llvm::MemoryBuffer::getFile(path);
  if (!mbE)
    return llvm::createFileError(path, mbE.getError());
  auto jvalE = llvm::json::parse(mbE.get()->getBuffer());
  if (!jvalE)
    return jvalE.takeError();
  llvm::json::Path::Root root;
  if (auto err = loader(*jvalE, root)) {
    std::string s;
    llvm::raw_string_ostream os(s);
    os << err << '\n';
    root.printErrorContext(*jvalE, os);
    return llvm::createStringError(llvm::inconvertibleErrorCode(), s);
  }
  return llvm::Error::success();
}

} // namespace gyhcall

namespace llvm::json {

bool
fromJSON(const llvm::json::Value& E, std::uint32_t& Out, llvm::json::Path P)
{
  if (auto S = E.getAsUINT64()) {
    if (*S <= UINT32_MAX) {
      Out = *S;
      return true;
    }
  }
  P.report("expect uint32_t");
  return false;
}

bool
fromJSON(const llvm::json::Value& E, std::uint8_t& Out, llvm::json::Path P)
{
  if (auto S = E.getAsUINT64()) {
    if (*S <= UINT8_MAX) {
      Out = *S;
      return true;
    }
  }
  P.report("expect uint8_t");
  return false;
}

bool
fromJSON(const llvm::json::Value& E, std::uint16_t& Out, llvm::json::Path P)
{
  if (auto S = E.getAsUINT64()) {
    if (*S <= UINT16_MAX) {
      Out = *S;
      return true;
    }
  }
  P.report("expect uint16_t");
  return false;
}

bool
fromJSON(const llvm::json::Value& E, std::int8_t& Out, llvm::json::Path P)
{
  if (auto S = E.getAsUINT64()) {
    if (*S <= static_cast<uint64_t>(INT8_MAX)) {
      Out = *S;
      return true;
    }
  }
  P.report("expect int8_t");
  return false;
}

bool
fromJSON(const llvm::json::Value& E, std::int16_t& Out, llvm::json::Path P)
{
  if (auto S = E.getAsUINT64()) {
    if (*S <= static_cast<uint64_t>(INT16_MAX)) {
      Out = *S;
      return true;
    }
  }
  P.report("expect int16_t");
  return false;
}

// bool
// fromJSON(const llvm::json::Value& E, std::int32_t& Out, llvm::json::Path P)
// {
//   if (auto S = E.getAsUINT64()) {
//     if (*S <= static_cast<uint64_t>(INT32_MAX)) {
//       Out = *S;
//       return true;
//     }
//   }
//   P.report("expect int32_t");
//   return false;
// }

} // namespace llvm
