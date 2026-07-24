#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/JSON.h>

namespace gyhcall {

struct Symtbl
{
  struct StubInfo
  {
    /// 全局标号
    std::uint32_t mUid{ UINT32_MAX };

    /// 原函数的类型, 即 llvm::Function::get(mRet, mArg->elements())
    llvm::FunctionType* mFunc{ nullptr };

    /// 函数的属性, 筛选出那些 ABI 相关的属性
    llvm::AttributeList mAttrs;

    /// 返回值的具体类型, 为空表示 void
    llvm::Type* mRet{ nullptr };

    /// 参数结构体的具体类型, 为空表示 void
    llvm::StructType* mArg{ nullptr };

    StubInfo() = default;
    StubInfo(llvm::FunctionType* funcTy)
      : StubInfo(funcTy, {})
    {
    }
    StubInfo(llvm::FunctionType* funcTy, llvm::AttributeList attrs);

    llvm::json::Object dump_json(llvm::Module& tmp) const;
    llvm::Error load_json(const llvm::json::Value& jval,
                          llvm::json::Path path,
                          llvm::Module& mod);
  };

  struct FuncInfo
  {
    /// 全局标号
    std::uint32_t mUid{ UINT32_MAX };

    /// 该函数对应的桩类型
    StubInfo* mStubType{ nullptr };

    /// 函数内涉及的所有外部引用, 通过第三个参数传入
    llvm::SmallVector<llvm::GlobalValue*, 16> mExtRefArgs;

    /// 函数内涉及的所有回调类型, 通过第四个参数传入
    llvm::SmallVector<llvm::hash_code, 16> mCbStubArgs;

    llvm::json::Object dump_json(llvm::Module& tmp) const;
    llvm::Error load_json(const llvm::json::Value& jval,
                          llvm::json::Path path,
                          llvm::Module& mod,
                          Symtbl& st);
  };

  struct GrefInfo
  {
    /// 全局标号
    std::uint32_t mUid{ UINT32_MAX };

    llvm::json::Object dump_json(llvm::Module& tmp) const;
    llvm::Error load_json(const llvm::json::Value& jval,
                          llvm::json::Path path,
                          llvm::Module& mod,
                          Symtbl& st);
  };

  /// 随机生成的库 (llvm::Module) 唯一 ID
  std::uint32_t mUid{ UINT32_MAX };

  /// 客方被分离函数的所有类型, 键为它们的哈希值
  std::map<llvm::hash_code, StubInfo> mCallingStubs;

  /// 主侧可能回调的所有客方函数类型, 键为它们的哈希值
  std::map<llvm::hash_code, StubInfo> mCallbackStubs;

  /// 所有可以从客方卸载到主侧的函数索引表
  std::map<llvm::Function*, FuncInfo> mFunctbl;

  /// 所有可卸载函数对客方全局符号的引用表
  std::map<llvm::GlobalValue*, GrefInfo> mGreftbl;

  /// FCP-CM 所使用的 gfunc 前缀魔数
  static constexpr std::uint64_t kFCPcmMagic = 0x6666200009068888;

  llvm::json::Object dump_json(llvm::LLVMContext& ctx) const;
  llvm::Error load_json(const llvm::json::Value& jval,
                        llvm::json::Path path,
                        llvm::Module& mod);

  llvm::Error to_file(llvm::StringRef path, llvm::LLVMContext& ctx) const;
  static llvm::Expected<Symtbl> from_file(llvm::StringRef path,
                                          llvm::Module& mod);
};

llvm::AttributeList
filter_abi_attributes(llvm::AttributeList attrList, llvm::LLVMContext& ctx);

} // namespace gyhcall
