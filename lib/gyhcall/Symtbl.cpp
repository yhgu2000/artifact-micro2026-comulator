#include "Symtbl.hpp"
#include "type_hash.hpp"
#include "util.hpp"
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/Support/SourceMgr.h>

namespace gyhcall {

Symtbl::StubInfo::StubInfo(llvm::FunctionType* funcTy,
                           llvm::AttributeList attrs)
  : mFunc(funcTy)
  , mAttrs(attrs)
{
  mRet = funcTy->getReturnType();
  if (mRet->isVoidTy())
    mRet = nullptr;
  if (funcTy->getNumParams() > 0)
    mArg = llvm::StructType::get(funcTy->getContext(), funcTy->params());
  else
    mArg = nullptr;
}

llvm::json::Object
Symtbl::StubInfo::dump_json(llvm::Module& tmp) const
{
  llvm::json::Object ret;

  ret["Uid"] = mUid;

  std::string jstr;
  llvm::raw_string_ostream sout(jstr);
  auto func = llvm::Function::Create(
    mFunc, llvm::GlobalValue::ExternalLinkage, "stub", tmp);
  func->setAttributes(mAttrs);
  func->print(sout);
  func->eraseFromParent();
  ret["Func"] = std::move(jstr);

  return ret;
}

llvm::Error
Symtbl::StubInfo::load_json(const llvm::json::Value& jval,
                            llvm::json::Path path,
                            llvm::Module& mod)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();

  if (!om.map("Uid", mUid))
    return llvm::make_error<Failure>();

  std::string funcStr;
  if (!om.map("Func", funcStr))
    return llvm::make_error<Failure>();

  llvm::SMDiagnostic err;
  auto modPtr = llvm::parseAssemblyString(funcStr, err, mod.getContext());
  if (!modPtr) {
    path.field("Func").report("fail to parse IR");
    std::string s;
    llvm::raw_string_ostream sout(s);
    return llvm::createStringError(llvm::inconvertibleErrorCode(), s);
  }
  auto func = modPtr->getFunction("stub");
  if (!func) {
    path.field("Func").report("no function named 'stub'");
    return llvm::make_error<Failure>();
  }

  mFunc = func->getFunctionType();
  mAttrs = func->getAttributes();
  mRet = mFunc->getReturnType();
  if (mRet->isVoidTy())
    mRet = nullptr;
  if (mFunc->getNumParams() > 0)
    mArg = llvm::StructType::get(mFunc->getContext(), mFunc->params());
  else
    mArg = nullptr;

  return llvm::Error::success();
}

llvm::json::Object
Symtbl::FuncInfo::dump_json(llvm::Module& tmp) const
{
  llvm::json::Object ret;
  llvm::json::Array jarr;

  ret["Uid"] = mUid;

  auto typeHash = type_hash(mStubType->mFunc);
  assert(typeHash);
  ret["StubInfo"] = hash2str(*typeHash);

  for (auto&& v : mExtRefArgs)
    jarr.push_back(v->getName());
  ret["ExtRefArgs"] = std::move(jarr);

  for (auto&& hc : mCbStubArgs)
    jarr.push_back(hash2str(hc));
  ret["CbStubArgs"] = std::move(jarr);

  return ret;
}

llvm::Error
Symtbl::FuncInfo::load_json(const llvm::json::Value& jval,
                            llvm::json::Path path,
                            llvm::Module& mod,
                            Symtbl& st)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  auto& jobj = *jval.getAsObject();

  if (!om.map("Uid", mUid))
    return llvm::make_error<Failure>();

  std::string stubTypeStr;
  if (!om.map("StubInfo", stubTypeStr))
    return llvm::make_error<Failure>();

  auto typeHash = str2hash(stubTypeStr);
  if (!typeHash) {
    path.field("StubInfo").report("invalid type hash");
    return typeHash.takeError();
  }
  auto stubTypeIt = st.mCallingStubs.find(*typeHash);
  if (stubTypeIt == st.mCallingStubs.end()) {
    path.field("StubInfo").report("Type not found");
    return llvm::make_error<Failure>();
  }
  mStubType = &stubTypeIt->second;

  auto extRefArgs = jobj.getArray("ExtRefArgs");
  auto extRefArgsPath = path.field("ExtRefArgs");
  if (!extRefArgs) {
    extRefArgsPath.report("expect an array");
    return llvm::make_error<Failure>();
  }
  for (auto&& it : llvm::enumerate(*extRefArgs)) {
    auto name = it.value().getAsString();
    if (!name) {
      extRefArgsPath.index(it.index()).report("expect a string");
      return llvm::make_error<Failure>();
    }
    auto val = mod.getNamedValue(*name);
    if (!val) {
      extRefArgsPath.index(it.index()).report("Value not found");
      return llvm::make_error<Failure>();
    }
    mExtRefArgs.push_back(val);
  }

  auto cbStubArgs = jobj.getArray("CbStubArgs");
  auto cbStubArgsPath = path.field("CbStubArgs");
  if (!cbStubArgs) {
    cbStubArgsPath.report("expect an array");
    return llvm::make_error<Failure>();
  }
  for (auto&& it : llvm::enumerate(*cbStubArgs)) {
    auto name = it.value().getAsString();
    if (!name) {
      cbStubArgsPath.index(it.index()).report("expect a string");
      return llvm::make_error<Failure>();
    }
    auto typeHash = str2hash(*name);
    if (!typeHash) {
      cbStubArgsPath.index(it.index()).report("invalid type hash");
      return typeHash.takeError();
    }
    mCbStubArgs.push_back(*typeHash);
  }

  return llvm::Error::success();
}

llvm::json::Object
Symtbl::GrefInfo::dump_json(llvm::Module& tmp) const
{
  llvm::json::Object ret;

  ret["Uid"] = mUid;

  return ret;
}

llvm::Error
Symtbl::GrefInfo::load_json(const llvm::json::Value& jval,
                            llvm::json::Path path,
                            llvm::Module& mod,
                            Symtbl& st)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();

  if (!om.map("Uid", mUid))
    return llvm::make_error<Failure>();

  return llvm::Error::success();
}

llvm::json::Object
Symtbl::dump_json(llvm::LLVMContext& ctx) const
{
  llvm::json::Object ret;
  llvm::json::Object jobj;

  llvm::Module tmpMod({}, ctx);

  ret["Uid"] = mUid;

  for (auto&& [hc, st] : mCallingStubs)
    jobj[hash2str(hc)] = st.dump_json(tmpMod);
  ret["CallingStubs"] = std::move(jobj);

  for (auto&& [hc, st] : mCallbackStubs)
    jobj[hash2str(hc)] = st.dump_json(tmpMod);
  ret["CallbackStubs"] = std::move(jobj);

  for (auto&& [func, qfunc] : mFunctbl)
    jobj[func->getName()] = qfunc.dump_json(tmpMod);
  ret["Functbl"] = std::move(jobj);

  for (auto&& [gref, info] : mGreftbl)
    jobj[gref->getName()] = info.dump_json(tmpMod);
  ret["Greftbl"] = std::move(jobj);

  return ret;
}

llvm::Error
Symtbl::load_json(const llvm::json::Value& jval,
                  llvm::json::Path path,
                  llvm::Module& mod)
{
  llvm::json::ObjectMapper om(jval, path);
  if (!om)
    return llvm::make_error<Failure>();
  auto& jobj = *jval.getAsObject();

  if (!om.map("Uid", mUid))
    return llvm::make_error<Failure>();

  auto callingStubs = jobj.getObject("CallingStubs");
  auto callingStubsPath = path.field("CallingStubs");
  if (!callingStubs) {
    callingStubsPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [k, v] : *callingStubs) {
    auto typeHash = str2hash(k);
    if (!typeHash) {
      callingStubsPath.field(k).report("invalid type hash");
      return typeHash.takeError();
    }
    StubInfo stubType;
    if (auto err = stubType.load_json(v, callingStubsPath.field(k), mod))
      return err;
    mCallingStubs.emplace(*typeHash, std::move(stubType));
  }

  auto callbackTypes = jobj.getObject("CallbackTypes");
  auto callbackTypesPath = path.field("CallbackTypes");
  if (!callbackTypes) {
    callbackTypesPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [k, v] : *callbackTypes) {
    auto typeHash = str2hash(k);
    if (!typeHash) {
      callbackTypesPath.field(k).report("invalid type hash");
      return typeHash.takeError();
    }
    StubInfo stubType;
    if (auto err = stubType.load_json(v, callbackTypesPath.field(k), mod))
      return err;
    mCallbackStubs.emplace(*typeHash, std::move(stubType));
  }

  auto functbl = jobj.getObject("Functbl");
  auto functblPath = path.field("Functbl");
  if (!functbl) {
    functblPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [k, v] : *functbl) {
    auto func = mod.getFunction(k);
    if (!func) {
      functblPath.field(k).report("Function not found");
      continue;
    }
    FuncInfo info;
    if (auto err = info.load_json(v, functblPath.field(k), mod, *this))
      return err;
    mFunctbl.emplace(func, std::move(info));
  }

  auto greftbl = jobj.getObject("Greftbl");
  auto greftblPath = path.field("Greftbl");
  if (!greftbl) {
    greftblPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [k, v] : *greftbl) {
    auto gref = mod.getNamedValue(k);
    if (!gref) {
      greftblPath.field(k).report("GlobalValue not found");
      continue;
    }
    GrefInfo info;
    if (auto err = info.load_json(v, greftblPath.field(k), mod, *this))
      return err;
    mGreftbl.emplace(gref, std::move(info));
  }

  return llvm::Error::success();
}

llvm::Error
Symtbl::to_file(llvm::StringRef path, llvm::LLVMContext& ctx) const
{
  return dump_json_file(dump_json(ctx), path);
}

llvm::Expected<Symtbl>
Symtbl::from_file(llvm::StringRef path, llvm::Module& mod)
{
  Symtbl ret;
  if (auto err = load_json_file(path, [&](auto& jval, auto path) {
        return ret.load_json(jval, path, mod);
      }))
    return err;
  return ret;
}

llvm::AttributeList
filter_abi_attributes(llvm::AttributeList attrList, llvm::LLVMContext& ctx)
{
  llvm::AttributeList ret;

  // attrList.getFnAttrs();
  // 经查, llvm 的函数属性里没有和调用处 ABI 强相关的, 所以不管它
  auto num = attrList.getNumAttrSets();
  if (num == 0)
    return ret;
  num -= 1;
  static_assert(llvm::AttributeList::ReturnIndex == 0);
  static_assert(llvm::AttributeList::FirstArgIndex == 1);

  for (unsigned i = 0; i < num; ++i) {
    llvm::AttrBuilder ab(ctx);
    for (auto&& attr : attrList.getAttributes(i)) {
      ab.addAttribute(attr);
      // 参数和返回值属性里将近一半都是和 ABI 相关,
      // 少部分剩下的只是优化作用也没其它影响, 考虑到总数也不多,
      // 我们就直接全部保留好了.
    }
    ret = ret.addAttributesAtIndex(ctx, i, ab);
  }
  return ret;
}

} // namespace gyhcall
