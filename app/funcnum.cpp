#define NOPT_I_C
#define OPT_I_IR
#define OPT_OVERVIEW "count number of functions in LLVM IR"
#include "_main.hpp"

static int
main_body()
{
  std::uint64_t total = 0;
  std::uint64_t original = 0;
  std::uint64_t guestCallbacks = 0;
  std::uint64_t hostCalls = 0;
  std::vector<std::string> misc;
  for (auto&& func : gIir->functions()) {
    ++total;

    auto name = func.getName();
    if (!name.starts_with("__gyh")) {
      ++original;
      continue;
    }

    if (name.starts_with("__gyh_callback_")) {
      ++guestCallbacks;
      continue;
    }

    if (name.starts_with("__gyh_call_")) {
      ++hostCalls;
      continue;
    }

    misc.push_back(name.str());
  }
  llvm::outs() << "total: " << total << '\n'
               << "original: " << original << '\n'
               << "guest __gyh_callback_*: " << guestCallbacks << '\n'
               << "host __gyh_call_*: " << hostCalls << '\n'
               << "miscellaneous:\n";
  for (auto&& name : misc)
    llvm::outs() << " " << name << '\n';
  return 0;
}
