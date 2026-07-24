#define NOPT_I_C
#define OPT_I_IR
#define OPT_OVERVIEW "compare hashes of globals in LLVM IR and check uniqueness"
#include "_main.hpp"
#include <gyhcall/type_hash.hpp>

namespace {

llvm::cl::opt<std::string> gOptCmpIR(llvm::cl::Positional,
                                     llvm::cl::desc("compared LLVM IR"),
                                     llvm::cl::cat(gOptCat));
std::unique_ptr<llvm::Module> gCmpIR;

llvm::raw_ostream&
operator<<(llvm::raw_ostream& os, llvm::Attribute attr)
{
  os << attr.getAsString();
  return os;
}

llvm::raw_ostream&
operator<<(llvm::raw_ostream& os, llvm::AttributeSet attrSet)
{
  for (auto attr : attrSet)
    os << attr << " ";
  return os;
}

llvm::raw_ostream&
print(llvm::raw_ostream& os, llvm::AttributeList attrList, const char* indent)
{
  auto it = attrList.begin();
  if (it == attrList.end())
    return os << "\n";
  os << *it << "\n";
  for (auto itE = attrList.end(); it != itE; ++it)
    os << indent << *it << "\n";
  return os;
}

using TAPair = std::pair<llvm::Type*, llvm::AttributeList>;

std::optional<llvm::hash_code>
hash_ta_pair(TAPair* taPair, const llvm::GlobalValue& gv)
{
  std::optional<llvm::hash_code> ret;
  if (auto func = llvm::dyn_cast<llvm::Function>(&gv)) {
    if ((ret = gyhcall::function_hash(func))) {
      taPair->first = func->getFunctionType();
      taPair->second = func->getAttributes();
    }
  } else {
    if ((ret = gyhcall::type_hash(gv.getValueType()))) {
      taPair->first = gv.getValueType();
      taPair->second = llvm::AttributeList();
    }
  }
  return ret;
}

llvm::raw_ostream&
print_ta_pair(llvm::raw_ostream& os, const TAPair& a, const TAPair& b)
{
  os << "  - " << *a.first << "\n"
     << "    " << *b.first << "\n";
  if (!a.second.isEmpty() || !b.second.isEmpty()) {
    os << "  - ", print(os, a.second, "      ");
    os << "    ", print(os, b.second, "      ");
  }
  return os;
}

llvm::DenseMap<llvm::hash_code, TAPair> gHashes;

struct Result
{
  uint32_t mNormal;
  uint32_t mUnhashable;
  uint32_t mAdditional;
  uint32_t mCollision;
  uint32_t mMismatch;
};

void
compare(const llvm::GlobalValue& gvCmp, Result* result)
{
  auto name = gvCmp.getName();

  TAPair taPairCmp;
  auto hashCmp = hash_ta_pair(&taPairCmp, gvCmp);
  if (!hashCmp) {
    llvm::outs() << "unhashable: " << name << "\n";
    result->mUnhashable++;
    return;
  }

  // 检查哈希碰撞
  if (auto it = gHashes.find(*hashCmp); it == gHashes.end()) {
    gHashes[*hashCmp] = taPairCmp;
  } else if (taPairCmp != it->second) {
    llvm::outs() << "hash collision (" << llvm::utohexstr(*hashCmp, false, 16)
                 << "): " << name << "\n";
    print_ta_pair(llvm::outs(), it->second, taPairCmp);
    result->mCollision++;
    return;
  }

  // 检查哈希匹配
  auto gv = gIir->getNamedValue(name);
  if (!gv) {
    result->mAdditional++;
    return;
  }

  TAPair taPair;
  auto hash = hash_ta_pair(&taPair, *gv);
  if (!hash) {
    llvm::outs() << "hash mismatch (" << llvm::utohexstr(*hashCmp, false, 16)
                 << " != UNHASHABLE): " << name << '\n';
    print_ta_pair(llvm::outs(), taPairCmp, taPair);
    result->mMismatch++;
    return;
  }
  if (*hash == *hashCmp) {
    llvm::outs() << "hash match (" << llvm::utohexstr(*hashCmp, false, 16)
                 << "): " << name << '\n';
    // TODO llvm::utohexstr 在 X 为 0 的时候有 bug
  } else {
    llvm::outs() << "hash mismatch (" << llvm::utohexstr(*hashCmp, false, 16)
                 << " != " << llvm::utohexstr(*hash, false, 16) << "): " << name
                 << '\n';
    print_ta_pair(llvm::outs(), taPairCmp, taPair);
    result->mMismatch++;
  }

  result->mNormal++;
}

} // namespace

static int
main_body()
{
  {
    llvm::SMDiagnostic err;
    gCmpIR = llvm::parseIRFile(gOptCmpIR.getValue(), err, gIirCTX);
    if (!gCmpIR) {
      err.print(nullptr, llvm::errs());
      return -2;
    }
  }

  using TAPair = std::pair<llvm::Type*, llvm::AttributeList>;

  Result result;
  for (auto& gvCmp : gCmpIR->globals())
    compare(gvCmp, &result);
  for (auto& gv : gIir->functions())
    compare(gv, &result);

  llvm::outs() << "==============\n"
               << "|     HASHES : " << gHashes.size() << "\n"
               << "|     NORMAL : " << result.mNormal << "\n"
               << "| UNHASHABLE : " << result.mUnhashable << "\n"
               << "| ADDITIONAL : " << result.mAdditional << "\n"
               << "|  COLLISION : " << result.mCollision << "\n"
               << "|   MISMATCH : " << result.mMismatch << "\n"
               << "--------------\n";

  if (result.mCollision > 0 || result.mMismatch > 0)
    return -3;
  return 0;
}
