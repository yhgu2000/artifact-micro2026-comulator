#include "Logger.hpp"
#include "util.hpp"

namespace gyhcall {

void
Logger::operator()(const llvm::json::Value& msg)
{
}

void
Logger::operator()(const llvm::StringRef msg)
{
  llvm::json::Object jobj;
  jobj["msg"] = msg.str();

  this->operator()(std::move(jobj));
}

void
Logger::operator()(const llvm::Value* val, llvm::StringRef msg)
{
  std::string valRepr;
  if (val != nullptr) {
    llvm::raw_string_ostream out(valRepr);
    out << *val << '\n';
  }

  llvm::json::Object jobj;
  jobj["msg"] = msg.str();
  if (val != nullptr)
    jobj["val"] = valRepr;

  this->operator()(std::move(jobj));
}

void
Logger::operator()(const llvm::Instruction* inst, llvm::StringRef msg)
{
  const llvm::Module* mod;
  const llvm::Function* func;
  const llvm::BasicBlock* bb;
  std::string instRepr;
  if (inst == nullptr) {
    mod = nullptr;
    func = nullptr;
    bb = nullptr;
  } else {
    mod = inst->getModule();
    func = inst->getFunction();
    bb = inst->getParent();
    llvm::raw_string_ostream out(instRepr);
    out << *inst << '\n';
  }

  llvm::json::Object jobj;
  jobj["msg"] = msg.str();
  if (mod != nullptr)
    jobj["mod"] = mod->getName().str();
  if (func != nullptr)
    jobj["func"] = func->getName().str();
  if (bb != nullptr)
    jobj["bb"] = bb->getName().str();
  if (inst != nullptr)
    jobj["inst"] = instRepr;

  this->operator()(std::move(jobj));
}

void
Logger::operator()(const llvm::BasicBlock* bb, llvm::StringRef msg)
{
  const llvm::Module* mod;
  const llvm::Function* func;
  if (bb == nullptr) {
    mod = nullptr;
    func = nullptr;
  } else {
    func = bb->getParent();
    mod = bb->getModule();
  }

  llvm::json::Object jobj;
  jobj["msg"] = msg.str();
  if (mod != nullptr)
    jobj["mod"] = mod->getName().str();
  if (func != nullptr)
    jobj["func"] = func->getName().str();
  if (bb != nullptr)
    jobj["bb"] = bb->getName().str();

  this->operator()(std::move(jobj));
}

void
Logger::operator()(const llvm::Function* func, llvm::StringRef msg)
{
  const llvm::Module* mod;
  if (func == nullptr) {
    mod = nullptr;
  } else {
    mod = func->getParent();
  }

  llvm::json::Object jobj;
  jobj["msg"] = msg.str();
  if (mod != nullptr)
    jobj["mod"] = mod->getName().str();
  if (func != nullptr)
    jobj["func"] = func->getName().str();

  this->operator()(std::move(jobj));
}

void
Logger::operator()(const llvm::Module* mod, llvm::StringRef msg)
{
  llvm::json::Object jobj;
  jobj["msg"] = msg.str();
  if (mod != nullptr)
    jobj["mod"] = mod->getName().str();

  this->operator()(std::move(jobj));
}

void
FileLogger::operator()(const llvm::json::Value& msg)
{
  std::scoped_lock<std::mutex> lock(mMutex);
  mOutput << msg << '\n';
}

void
DiagnosticLogger::operator()(const llvm::json::Value& msg)
{
  std::string json;
  llvm::raw_string_ostream out(json);
  out << msg;
  mCtx.diagnose(DiagnosticString(std::move(json), llvm::DS_Remark));
}

} // namespace gyhcall
