#include "Config.hpp"
#include "util.hpp"

namespace gyhcall {

Analyze::Config
Config::analyze() const
{
  Analyze::Config cfg;

  return cfg;
}

ChopOff::Config
Config::chop_off() const
{
  ChopOff::Config cfg;

  cfg.mDebug = mDebug;
  cfg.mUseGRT = mUseGRT;
  cfg.mUseFCPcm = mUseFCPcm;
  cfg.mMinBasicBlocks = mMinBasicBlocks;
  cfg.mMinInstructions = mMinInstructions;

  return cfg;
}

GenQlib::Config
Config::gen_qlib() const
{
  GenQlib::Config cfg;

  return cfg;
}

Outline::Config
Config::outline() const
{
  Outline::Config cfg;

  return cfg;
}

PickUp::Config
Config::pick_up() const
{
  return mBwList;
}

TakeOut::Config
Config::take_out() const
{
  TakeOut::Config cfg;

  cfg.mDebug = mDebug;
  cfg.mUseGRT = mUseGRT;
  cfg.mUseFCP = mUseFCP;
  cfg.mUseFCPcm = mUseFCPcm;

  return cfg;
}

llvm::json::Object
Config::dump_json() const
{
  llvm::json::Object jobj;

  jobj["LLorBC"] = mLLorBC;
  jobj["Debug"] = mDebug;
  jobj["UsePFO"] = mUsePFO;
  jobj["UseGRT"] = mUseGRT;
  jobj["UseFCP"] = mUseFCP;
  jobj["UseFCPcm"] = mUseFCPcm;
  jobj["MinBasicBlocks"] = mMinBasicBlocks;
  jobj["MinInstructions"] = mMinInstructions;
  jobj["BwList"] = mBwList.dump_json();

  return jobj;
}

llvm::Error
Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  auto& jobj = *jval.getAsObject();

  if (!om.map("LLorBC", mLLorBC) || !om.map("Debug", mDebug) ||
      !om.map("UsePFO", mUsePFO) || !om.map("UseGRT", mUseGRT) ||
      !om.map("UseFCP", mUseFCP) || !om.map("UseFCPcm", mUseFCPcm) ||
      !om.map("MinBasicBlocks", mMinBasicBlocks) ||
      !om.map("MinInstructions", mMinInstructions))
    return llvm::make_error<Failure>();

  auto bwList = jobj.get("BwList");
  if (bwList && bwList->getAsObject()) {
    if (auto err = mBwList.load_json(*bwList, path.field("BwList")))
      return err;
  }

  return llvm::Error::success();
}

llvm::Error
Config::to_file(llvm::StringRef path) const
{
  return dump_json_file(dump_json(), path);
}

llvm::Expected<Config>
Config::from_file(llvm::StringRef path)
{
  Config ret;
  if (auto err = load_json_file(
        path, [&](auto& jval, auto path) { return ret.load_json(jval, path); }))
    return err;
  return ret;
}

} // namespace gyhcall
