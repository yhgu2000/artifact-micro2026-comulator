#include "BwList.hpp"
#include "util.hpp"

namespace gyhcall {

llvm::json::Object
BwList::FuncBw::dump_json() const
{
  llvm::json::Object ret;
  ret["outline"] = mOutline;
  ret["analyze"] = mAnalyze;
  ret["chop-off"] = mChopOff;
  ret["take-out"] = mTakeOut;
  return ret;
}

llvm::Error
BwList::FuncBw::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  if (!om.mapOptional("outline", mOutline) ||
      !om.mapOptional("analyze", mAnalyze) ||
      !om.mapOptional("chop-off", mChopOff) ||
      !om.mapOptional("take-out", mTakeOut))
    return llvm::make_error<Failure>();
  return llvm::Error::success();
}

llvm::json::Object
BwList::dump_json() const
{
  llvm::json::Object ret;

  llvm::json::Object funcs;
  for (auto&& [func, anno] : mFuncs)
    funcs[func->getName()] = anno.dump_json();
  ret["Funcs"] = std::move(funcs);

  llvm::json::Object callees;
  for (auto&& [func, bw] : mCallees)
    callees[func->getName()] = bw;
  ret["Callees"] = std::move(callees);

  llvm::json::Array fcpcmChop;
  for (auto&& func : mFCPcmChop)
    fcpcmChop.push_back(func->getName());
  ret["FCPcmChop"] = std::move(fcpcmChop);

  ret["FCPcmChopOthers"] = mFCPcmChopOthers;

  return ret;
}

llvm::Error
BwList::load_json(const llvm::json::Value& jval,
                  llvm::json::Path path,
                  llvm::Module& mod)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  auto& jobj = *jval.getAsObject();

  auto funcs = jobj.getObject("Funcs");
  auto funcsPath = path.field("Funcs");
  if (!funcs) {
    funcsPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [name, jval] : *funcs) {
    auto func = mod.getFunction(name);
    if (!func)
      continue;
    llvm::json::ObjectMapper om(jval, funcsPath);
    if (!om)
      return llvm::make_error<Failure>();
    FuncBw anno;
    if (auto err = anno.load_json(jval, funcsPath))
      return err;
    mFuncs[func] = anno;
  }

  auto callees = jobj.getObject("Callees");
  auto calleesPath = path.field("Callees");
  if (!callees) {
    calleesPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [name, jval] : *callees) {
    auto func = mod.getFunction(name);
    if (!func)
      continue;
    auto bw = jval.getAsBoolean();
    if (!bw) {
      calleesPath.report("expect a boolean");
      return llvm::make_error<Failure>();
    }
    mCallees[func] = bw.value();
  }

  auto fcpcmChop = jobj.getArray("FCPcmChop");
  auto fcpcmChopPath = path.field("FCPcmChop");
  if (!fcpcmChop) {
    fcpcmChopPath.report("expect an array");
    return llvm::make_error<Failure>();
  }
  for (auto&& [i, jval] : llvm::enumerate(*fcpcmChop)) {
    auto name = jval.getAsString();
    if (!name) {
      fcpcmChopPath.index(i).report("expect a string");
      return llvm::make_error<Failure>();
    }
    auto func = mod.getFunction(name.value());
    if (!func)
      continue;
    mFCPcmChop.insert(func);
  }

  if (!om.map("FCPcmChopOthers", mFCPcmChopOthers))
    return llvm::make_error<Failure>();

  return llvm::Error::success();
}

llvm::Error
BwList::to_file(llvm::StringRef path) const
{
  return dump_json_file(dump_json(), path);
}

llvm::Expected<BwList>
BwList::from_file(llvm::StringRef path, llvm::Module& mod)
{
  BwList ret;
  if (auto err = load_json_file(path, [&](auto& jval, auto path) {
        return ret.load_json(jval, path, mod);
      }))
    return err;
  return ret;
}

const BwList BwList::kNULL;

} // namespace gyhcall
