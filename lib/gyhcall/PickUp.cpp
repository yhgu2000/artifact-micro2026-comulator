#include "PickUp.hpp"
#include "util.hpp"
#include <llvm/IR/IRBuilder.h>

namespace gyhcall {

llvm::json::Object
PickUp::Config::dump_json() const
{
  llvm::json::Object ret;

  llvm::json::Object funcs;
  for (auto&& [name, anno] : mFuncs)
    funcs[name] = anno.dump_json();
  ret["Funcs"] = std::move(funcs);

  llvm::json::Object callees;
  for (auto&& [name, bw] : mCallees)
    callees[name] = bw;
  ret["Callees"] = std::move(callees);

  llvm::json::Array fpcmChop;
  for (auto&& name : mFCPcmChop)
    fpcmChop.push_back(name);
  ret["FCPcmChop"] = std::move(fpcmChop);

  ret["FCPcmChopOthers"] = mFCPcmChopOthers;

  return ret;
}

llvm::Error
PickUp::Config::load_json(const llvm::json::Value& jval, llvm::json::Path path)
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
    BwList::FuncBw bw;
    if (auto err = bw.load_json(jval, funcsPath.field(name)))
      return err;
    mFuncs[name] = std::move(bw);
  }

  auto callees = jobj.getObject("Callees");
  auto calleesPath = path.field("Callees");
  if (!callees) {
    calleesPath.report("expect an object");
    return llvm::make_error<Failure>();
  }
  for (auto&& [name, jval] : *callees) {
    auto bw = jval.getAsBoolean();
    if (!bw.has_value()) {
      calleesPath.report("expect a boolean");
      return llvm::make_error<Failure>();
    }
    mCallees[name] = bw.value();
  }

  if (!om.map("FCPcmChop", mFCPcmChop))
    return llvm::make_error<Failure>();

  if (!om.map("FCPcmChopOthers", mFCPcmChopOthers))
    return llvm::make_error<Failure>();

  return llvm::Error::success();
}

llvm::Error
PickUp::Config::to_file(llvm::StringRef path) const
{
  return dump_json_file(dump_json(), path);
}

llvm::Expected<PickUp::Config>
PickUp::Config::from_file(llvm::StringRef path)
{
  Config ret;
  if (auto err = load_json_file(
        path, [&](auto& jval, auto path) { return ret.load_json(jval, path); }))
    return err;
  return ret;
}

void
PickUp::operator()(llvm::Module& mod)
{
  mModule = &mod;
  do_annotation();
  do_builtins();
  do_config();
}

void
PickUp::do_annotation()
{
  auto gv = mModule->getGlobalVariable("llvm.global.annotations");
  if (gv == nullptr)
    return;
  auto init = llvm::dyn_cast<llvm::ConstantArray>(gv->getInitializer());
  if (init == nullptr)
    return;
  auto elemTy = llvm::dyn_cast<llvm::StructType>(
    llvm::cast<llvm::ArrayType>(init->getType())->getElementType());
  if (elemTy == nullptr || elemTy->getNumElements() < 2)
    return;

  for (auto&& op : init->operands()) {
    auto s = llvm::cast<llvm::ConstantStruct>(op);
    auto func = llvm::dyn_cast<llvm::Function>(s->getOperand(0));
    if (func == nullptr)
      continue;
    auto info =
      llvm::cast<llvm::ConstantDataArray>(
        llvm::cast<llvm::GlobalVariable>(s->getOperand(1))->getInitializer())
        ->getAsCString();

    BwList::FuncBw anno;
    auto it = mResult.mFuncs.find(func);
    if (it != mResult.mFuncs.end())
      anno = it->second;

    if (info == "gyhcall:outline")
      anno.mOutline = true;
    else if (info == "gyhcall:no-outline")
      anno.mOutline = false;
    else if (info == "gyhcall:analyze")
      anno.mAnalyze = true;
    else if (info == "gyhcall:no-analyze")
      anno.mAnalyze = false;
    else if (info == "gyhcall:chop-off")
      anno.mChopOff = true;
    else if (info == "gyhcall:no-chop-off")
      anno.mChopOff = false;
    else if (info == "gyhcall:take-out")
      anno.mTakeOut = true;
    else if (info == "gyhcall:no-take-out")
      anno.mTakeOut = false;
    else
      continue;

    if (it == mResult.mFuncs.end())
      mResult.mFuncs.insert({ func, anno });
    else
      it->second = anno;
  }
}

void
PickUp::do_builtins()
{
  static constexpr auto kSortedWhites = []() constexpr {
    std::array ret = {
      //* Code Generator Intrinsics
      // llvm::Intrinsic::returnaddress,
      // llvm::Intrinsic::addressofreturnaddress,
      // llvm::Intrinsic::sponentry,
      // llvm::Intrinsic::stackaddress,
      // llvm::Intrinsic::frameaddress,
      // llvm::Intrinsic::swift_async_context_addr,
      // llvm::Intrinsic::localescape,
      // llvm::Intrinsic::localrecover,
      // llvm::Intrinsic::seh_try_begin,
      // llvm::Intrinsic::seh_try_end,
      // llvm::Intrinsic::seh_scope_begin,
      // llvm::Intrinsic::seh_scope_end,
      // llvm::Intrinsic::read_register,
      // llvm::Intrinsic::read_volatile_register,
      // llvm::Intrinsic::write_register,
      llvm::Intrinsic::stacksave,
      llvm::Intrinsic::stackrestore,
      // llvm::Intrinsic::structured_gep,
      // llvm::Intrinsic::get_dynamic_area_offset,
      // llvm::Intrinsic::prefetch,
      // llvm::Intrinsic::pcmarker,
      // llvm::Intrinsic::readcyclecounter,
      // llvm::Intrinsic::readsteadycounter,
      // llvm::Intrinsic::clear_cache,
      // llvm::Intrinsic::instrprof_increment,
      // llvm::Intrinsic::instrprof_increment_step,
      // llvm::Intrinsic::instrprof_callsite,
      // llvm::Intrinsic::instrprof_timestamp,
      // llvm::Intrinsic::instrprof_cover,
      // llvm::Intrinsic::instrprof_value_profile,
      // llvm::Intrinsic::instrprof_mcdc_parameters,
      // llvm::Intrinsic::instrprof_mcdc_tvbitmap_update,
      // llvm::Intrinsic::thread_pointer,
      // llvm::Intrinsic::call_preallocated_setup,
      // llvm::Intrinsic::call_preallocated_arg,
      // llvm::Intrinsic::call_preallocated_teardown,

      //* Standard C/C++ Library Intrinsics
      llvm::Intrinsic::abs,
      llvm::Intrinsic::smax,
      llvm::Intrinsic::smin,
      llvm::Intrinsic::umax,
      llvm::Intrinsic::umin,
      // llvm::Intrinsic::scmp,
      // llvm::Intrinsic::ucmp,
      llvm::Intrinsic::memcpy,
      llvm::Intrinsic::memcpy_inline,
      llvm::Intrinsic::memmove,
      llvm::Intrinsic::memset,
      llvm::Intrinsic::memset_inline,
      // llvm::Intrinsic::experimental.memset.pattern,
      llvm::Intrinsic::sqrt,
      llvm::Intrinsic::powi,
      llvm::Intrinsic::sin,
      llvm::Intrinsic::cos,
      // llvm::Intrinsic::tan,
      // llvm::Intrinsic::asin,
      // llvm::Intrinsic::acos,
      // llvm::Intrinsic::atan,
      // llvm::Intrinsic::atan2,
      // llvm::Intrinsic::sinh,
      // llvm::Intrinsic::cosh,
      // llvm::Intrinsic::tanh,
      // llvm::Intrinsic::sincos,
      // llvm::Intrinsic::sincospi,
      // llvm::Intrinsic::modf,
      llvm::Intrinsic::pow,
      llvm::Intrinsic::exp,
      llvm::Intrinsic::exp2,
      llvm::Intrinsic::exp10,
      llvm::Intrinsic::ldexp,
      llvm::Intrinsic::frexp,
      llvm::Intrinsic::log,
      llvm::Intrinsic::log10,
      llvm::Intrinsic::log2,
      llvm::Intrinsic::fma,
      llvm::Intrinsic::fabs,

      //* Floating-point min/max intrinsics comparison
      llvm::Intrinsic::minnum,
      llvm::Intrinsic::maxnum,
      llvm::Intrinsic::minimum,
      llvm::Intrinsic::maximum,
      // llvm::Intrinsic::minimumnum,
      // llvm::Intrinsic::maximumnum,
      llvm::Intrinsic::copysign,
      llvm::Intrinsic::floor,
      llvm::Intrinsic::ceil,
      llvm::Intrinsic::trunc,
      llvm::Intrinsic::rint,
      llvm::Intrinsic::nearbyint,
      llvm::Intrinsic::round,
      llvm::Intrinsic::roundeven,
      llvm::Intrinsic::lround,
      llvm::Intrinsic::llround,
      llvm::Intrinsic::lrint,
      llvm::Intrinsic::llrint,

      //* Constrained Floating-Point Intrinsics

      llvm::Intrinsic::experimental_constrained_fadd,
      llvm::Intrinsic::experimental_constrained_fsub,
      llvm::Intrinsic::experimental_constrained_fmul,
      llvm::Intrinsic::experimental_constrained_fdiv,
      llvm::Intrinsic::experimental_constrained_frem,
      llvm::Intrinsic::experimental_constrained_fma,
      llvm::Intrinsic::experimental_constrained_fptoui,
      llvm::Intrinsic::experimental_constrained_fptosi,
      llvm::Intrinsic::experimental_constrained_uitofp,
      llvm::Intrinsic::experimental_constrained_sitofp,
      llvm::Intrinsic::experimental_constrained_fptrunc,
      llvm::Intrinsic::experimental_constrained_fpext,
      llvm::Intrinsic::experimental_constrained_fcmp,
      llvm::Intrinsic::experimental_constrained_fcmps,
      llvm::Intrinsic::experimental_constrained_fmuladd,

      //* Constrained libm-equivalent Intrinsics

      llvm::Intrinsic::experimental_constrained_sqrt,
      llvm::Intrinsic::experimental_constrained_pow,
      llvm::Intrinsic::experimental_constrained_powi,
      llvm::Intrinsic::experimental_constrained_ldexp,
      llvm::Intrinsic::experimental_constrained_sin,
      llvm::Intrinsic::experimental_constrained_cos,
      // llvm::Intrinsic::experimental_constrained_tan,
      // llvm::Intrinsic::experimental_constrained_asin,
      // llvm::Intrinsic::experimental_constrained_acos,
      // llvm::Intrinsic::experimental_constrained_atan,
      // llvm::Intrinsic::experimental_constrained_atan2,
      // llvm::Intrinsic::experimental_constrained_sinh,
      // llvm::Intrinsic::experimental_constrained_cosh,
      // llvm::Intrinsic::experimental_constrained_tanh,
      llvm::Intrinsic::experimental_constrained_exp,
      llvm::Intrinsic::experimental_constrained_exp2,
      llvm::Intrinsic::experimental_constrained_log,
      llvm::Intrinsic::experimental_constrained_log10,
      llvm::Intrinsic::experimental_constrained_log2,
      llvm::Intrinsic::experimental_constrained_rint,
      llvm::Intrinsic::experimental_constrained_lrint,
      llvm::Intrinsic::experimental_constrained_llrint,
      llvm::Intrinsic::experimental_constrained_nearbyint,
      llvm::Intrinsic::experimental_constrained_maxnum,
      llvm::Intrinsic::experimental_constrained_minnum,
      llvm::Intrinsic::experimental_constrained_maximum,
      llvm::Intrinsic::experimental_constrained_minimum,
      llvm::Intrinsic::experimental_constrained_ceil,
      llvm::Intrinsic::experimental_constrained_floor,
      llvm::Intrinsic::experimental_constrained_round,
      llvm::Intrinsic::experimental_constrained_roundeven,
      llvm::Intrinsic::experimental_constrained_lround,
      llvm::Intrinsic::experimental_constrained_llround,
      llvm::Intrinsic::experimental_constrained_trunc,
      llvm::Intrinsic::experimental_noalias_scope_decl,

      //* Bit Manipulation Intrinsics
      llvm::Intrinsic::bitreverse,
      llvm::Intrinsic::bswap,
      llvm::Intrinsic::ctpop,
      llvm::Intrinsic::ctlz,
      llvm::Intrinsic::cttz,
      llvm::Intrinsic::fshl,
      llvm::Intrinsic::fshr,
      // llvm::Intrinsic::clmul,

      //* Arithmetic with Overflow Intrinsics
      llvm::Intrinsic::sadd_with_overflow,
      llvm::Intrinsic::uadd_with_overflow,
      llvm::Intrinsic::ssub_with_overflow,
      llvm::Intrinsic::usub_with_overflow,
      llvm::Intrinsic::smul_with_overflow,
      llvm::Intrinsic::umul_with_overflow,

      //* Saturation Arithmetic Intrinsics
      llvm::Intrinsic::sadd_sat,
      llvm::Intrinsic::uadd_sat,
      llvm::Intrinsic::ssub_sat,
      llvm::Intrinsic::usub_sat,
      llvm::Intrinsic::sshl_sat,
      llvm::Intrinsic::ushl_sat,

      //* Fixed Point Arithmetic Intrinsics
      llvm::Intrinsic::smul_fix,
      llvm::Intrinsic::umul_fix,
      llvm::Intrinsic::smul_fix_sat,
      llvm::Intrinsic::umul_fix_sat,
      llvm::Intrinsic::sdiv_fix,
      llvm::Intrinsic::udiv_fix,
      llvm::Intrinsic::sdiv_fix_sat,
      llvm::Intrinsic::udiv_fix_sat,

      //* Specialized Arithmetic Intrinsics
      llvm::Intrinsic::canonicalize,
      llvm::Intrinsic::fmuladd,

      //* Vector Reduction Intrinsics
      llvm::Intrinsic::vector_reduce_add,
      llvm::Intrinsic::vector_reduce_fadd,
      llvm::Intrinsic::vector_reduce_mul,
      llvm::Intrinsic::vector_reduce_fmul,
      llvm::Intrinsic::vector_reduce_and,
      llvm::Intrinsic::vector_reduce_or,
      llvm::Intrinsic::vector_reduce_xor,
      llvm::Intrinsic::vector_reduce_smax,
      llvm::Intrinsic::vector_reduce_smin,
      llvm::Intrinsic::vector_reduce_umax,
      llvm::Intrinsic::vector_reduce_umin,
      llvm::Intrinsic::vector_reduce_fmax,
      llvm::Intrinsic::vector_reduce_fmin,
      llvm::Intrinsic::vector_reduce_fmaximum,
      llvm::Intrinsic::vector_reduce_fminimum,

      //* Vector Partial Reduction Intrinsics
      // llvm::Intrinsic::vector_partial_reduce_add,
      // llvm::Intrinsic::vector_partial_reduce_fadd,

      //* Vector Manipulation Intrinsics
      llvm::Intrinsic::vector_insert,
      llvm::Intrinsic::vector_extract,
      // llvm::Intrinsic::vector_reverse,
      // llvm::Intrinsic::vector_deinterleave2 / 3 / 4 / 5 / 6 / 7 / 8,
      // llvm::Intrinsic::vector_interleave2 / 3 / 4 / 5 / 6 / 7 / 8,
      // llvm::Intrinsic::vector_splice_left,
      // llvm::Intrinsic::vector_splice_right,
      // llvm::Intrinsic::stepvector,

      //* Matrix Intrinsics
      llvm::Intrinsic::matrix_transpose,
      llvm::Intrinsic::matrix_multiply,
      llvm::Intrinsic::matrix_column_major_load,
      llvm::Intrinsic::matrix_column_major_store,

      //* Saturating floating-point to integer conversions
      llvm::Intrinsic::fptoui_sat,
      llvm::Intrinsic::fptosi_sat,

      //* Floating-Point Conversion Intrinsics
      llvm::Intrinsic::fptrunc_round,
      // llvm::Intrinsic::convert_to_arbitrary_fp,
      // llvm::Intrinsic::convert_from_arbitrary_fp,

      //* Debugger Intrinsics
      llvm::Intrinsic::dbg_assign,
      llvm::Intrinsic::dbg_declare,
      llvm::Intrinsic::dbg_label,
      llvm::Intrinsic::dbg_value,

      //* Vector Predication Intrinsics
      llvm::Intrinsic::vp_select,
      llvm::Intrinsic::vp_merge,
      llvm::Intrinsic::vp_add,
      llvm::Intrinsic::vp_sub,
      llvm::Intrinsic::vp_mul,
      llvm::Intrinsic::vp_sdiv,
      llvm::Intrinsic::vp_udiv,
      llvm::Intrinsic::vp_srem,
      llvm::Intrinsic::vp_urem,
      llvm::Intrinsic::vp_ashr,
      llvm::Intrinsic::vp_lshr,
      llvm::Intrinsic::vp_shl,
      llvm::Intrinsic::vp_or,
      llvm::Intrinsic::vp_and,
      llvm::Intrinsic::vp_xor,
      llvm::Intrinsic::vp_abs,
      llvm::Intrinsic::vp_smax,
      llvm::Intrinsic::vp_smin,
      llvm::Intrinsic::vp_umax,
      llvm::Intrinsic::vp_umin,
      llvm::Intrinsic::vp_copysign,
      llvm::Intrinsic::vp_minnum,
      llvm::Intrinsic::vp_maxnum,
      llvm::Intrinsic::vp_minimum,
      llvm::Intrinsic::vp_maximum,
      llvm::Intrinsic::vp_fadd,
      llvm::Intrinsic::vp_fsub,
      llvm::Intrinsic::vp_fmul,
      llvm::Intrinsic::vp_fdiv,
      llvm::Intrinsic::vp_frem,
      llvm::Intrinsic::vp_fneg,
      llvm::Intrinsic::vp_fabs,
      llvm::Intrinsic::vp_sqrt,
      llvm::Intrinsic::vp_fma,
      llvm::Intrinsic::vp_fmuladd,
      llvm::Intrinsic::vp_reduce_add,
      llvm::Intrinsic::vp_reduce_fadd,
      llvm::Intrinsic::vp_reduce_mul,
      llvm::Intrinsic::vp_reduce_fmul,
      llvm::Intrinsic::vp_reduce_and,
      llvm::Intrinsic::vp_reduce_or,
      llvm::Intrinsic::vp_reduce_xor,
      llvm::Intrinsic::vp_reduce_smax,
      llvm::Intrinsic::vp_reduce_smin,
      llvm::Intrinsic::vp_reduce_umax,
      llvm::Intrinsic::vp_reduce_umin,
      llvm::Intrinsic::vp_reduce_fmax,
      llvm::Intrinsic::vp_reduce_fmin,
      // llvm::Intrinsic::vp_reduce_fmaximum,
      // llvm::Intrinsic::vp_reduce_fminimum,
      llvm::Intrinsic::get_active_lane_mask,
      // llvm::Intrinsic::loop_dependence_war_mask,
      // llvm::Intrinsic::loop_dependence_raw_mask,
      llvm::Intrinsic::experimental_vp_splice,
      llvm::Intrinsic::experimental_vp_reverse,
      llvm::Intrinsic::vp_load,
      // llvm::Intrinsic::vp_load_ff,
      llvm::Intrinsic::vp_store,
      llvm::Intrinsic::experimental_vp_strided_load,
      llvm::Intrinsic::experimental_vp_strided_store,
      llvm::Intrinsic::vp_gather,
      llvm::Intrinsic::vp_scatter,
      llvm::Intrinsic::vp_trunc,
      llvm::Intrinsic::vp_zext,
      llvm::Intrinsic::vp_sext,
      llvm::Intrinsic::vp_fptrunc,
      llvm::Intrinsic::vp_fpext,
      llvm::Intrinsic::vp_fptoui,
      llvm::Intrinsic::vp_fptosi,
      llvm::Intrinsic::vp_uitofp,
      llvm::Intrinsic::vp_sitofp,
      llvm::Intrinsic::vp_ptrtoint,
      llvm::Intrinsic::vp_inttoptr,
      llvm::Intrinsic::vp_fcmp,
      llvm::Intrinsic::vp_icmp,
      llvm::Intrinsic::vp_ceil,
      llvm::Intrinsic::vp_floor,
      llvm::Intrinsic::vp_rint,
      llvm::Intrinsic::vp_nearbyint,
      llvm::Intrinsic::vp_round,
      llvm::Intrinsic::vp_roundeven,
      llvm::Intrinsic::vp_roundtozero,
      // llvm::Intrinsic::vp_lrint,
      // llvm::Intrinsic::vp_llrint,
      llvm::Intrinsic::vp_bitreverse,
      llvm::Intrinsic::vp_bswap,
      llvm::Intrinsic::vp_ctpop,
      llvm::Intrinsic::vp_ctlz,
      llvm::Intrinsic::vp_cttz,
      // llvm::Intrinsic::vp_cttz_elts,
      // llvm::Intrinsic::vp_sadd_sat,
      // llvm::Intrinsic::vp_uadd_sat,
      // llvm::Intrinsic::vp_ssub_sat,
      // llvm::Intrinsic::vp_usub_sat,
      llvm::Intrinsic::vp_fshl,
      llvm::Intrinsic::vp_fshr,
      llvm::Intrinsic::vp_is_fpclass,

      //* Masked Vector Load and Store Intrinsics
      llvm::Intrinsic::masked_load,
      llvm::Intrinsic::masked_store,

      //* Masked Vector Gather and Scatter Intrinsics
      llvm::Intrinsic::masked_gather,
      llvm::Intrinsic::masked_scatter,

      //* Masked Vector Expanding Load and Compressing Store Intrinsics
      llvm::Intrinsic::masked_expandload,
      llvm::Intrinsic::masked_compressstore,

      //* Memory Use Markers
      llvm::Intrinsic::lifetime_start,
      llvm::Intrinsic::lifetime_end,
      llvm::Intrinsic::invariant_start,
      llvm::Intrinsic::invariant_end,
      llvm::Intrinsic::launder_invariant_group,
      llvm::Intrinsic::strip_invariant_group,

      //* Floating-Point Test Intrinsics
      llvm::Intrinsic::is_fpclass,

      //* General Intrinsics
      llvm::Intrinsic::var_annotation,
      llvm::Intrinsic::ptr_annotation,
      llvm::Intrinsic::annotation,
      llvm::Intrinsic::objectsize,
      llvm::Intrinsic::expect,
      llvm::Intrinsic::expect_with_probability,
      llvm::Intrinsic::assume,
      llvm::Intrinsic::donothing,
      llvm::Intrinsic::is_constant,
      llvm::Intrinsic::ptrmask,

      //* guard
      llvm::Intrinsic::IndependentIntrinsics(UINT_MAX),
    };

    for (std::size_t i = ret.size(); i >= 1; --i) {
      for (std::size_t j = 1; j < i; ++j) {
        if (ret[j - 1] > ret[j]) {
          auto tmp = ret[j - 1];
          ret[j - 1] = ret[j], ret[j] = tmp;
        }
      }
    }
    return ret;
  }();

  for (auto&& func : mModule->getFunctionList()) {
    auto intrinsic = func.getIntrinsicID();
    if (intrinsic == llvm::Intrinsic::not_intrinsic)
      continue;
    if (intrinsic != *llvm::lower_bound(kSortedWhites, intrinsic)) {
      mLogger(&func, "忽略非白名单的内建函数");
      mResult.mCallees[&func] = false;
    }
  }
}

void
PickUp::do_config()
{
  for (auto&& [name, anno] : mConfig.mFuncs) {
    auto func = mModule->getFunction(name);
    if (func == nullptr)
      continue;
    // if (auto linkage = func->getLinkage();
    //     linkage != llvm::GlobalValue::LinkageTypes::ExternalLinkage &&
    //     linkage != llvm::GlobalValue::LinkageTypes::WeakAnyLinkage) {
    //   mLogger(func, "忽略非外部链接的同名函数");
    //   continue;
    // }
    mResult.mFuncs[func] = anno;
  }

  for (auto&& [name, bw] : mConfig.mCallees) {
    auto func = mModule->getFunction(name);
    if (func == nullptr)
      continue;
    // if (auto linkage = func->getLinkage();
    //     linkage != llvm::GlobalValue::LinkageTypes::ExternalLinkage ||
    //     linkage != llvm::GlobalValue::LinkageTypes::WeakAnyLinkage) {
    //   mLogger(func, "忽略非外部链接的调用");
    //   continue;
    // }
    mResult.mCallees[func] = bw;
  }

  for (auto&& name : mConfig.mFCPcmChop) {
    auto func = mModule->getFunction(name);
    if (func == nullptr)
      continue;
    mResult.mCallees[func] = true;
  }

  mResult.mFCPcmChopOthers = mConfig.mFCPcmChopOthers;
}

llvm::AnalysisKey PickUp::Pass::Key;

PickUp::Pass::Result
PickUp::Pass::run(llvm::Module& mod, llvm::ModuleAnalysisManager& mam)
{
  Result result;
  std::unique_ptr<Logger> logger;
  if (!mLogPath.empty()) {
    std::error_code ec;
    logger = std::make_unique<FileLogger>(mLogPath, ec);
    if (ec) {
      mod.getContext().diagnose(
        DiagnosticString("fail to open log '" + mLogPath + "':" + ec.message(),
                         llvm::DS_Warning));
      logger.reset();
    }
  }
  if (!logger)
    logger = std::make_unique<DiagnosticLogger>(mod.getContext());
  PickUp analyze(mConfig, result, *logger);
  return result;
}

} // namespace gyhcall
