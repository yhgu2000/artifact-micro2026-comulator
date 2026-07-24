#pragma once

#include <llvm/ADT/Hashing.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/Support/Error.h>

namespace gyhcall {

std::optional<llvm::hash_code>
type_hash(const llvm::Type* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::IntegerType* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::FunctionType* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::StructType* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::ArrayType* ty);

inline std::optional<llvm::hash_code>
type_hash(const llvm::VectorType* ty)
{
  return {};
}

std::optional<llvm::hash_code>
type_hash(const llvm::FixedVectorType* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::ScalableVectorType* ty);

std::optional<llvm::hash_code>
type_hash(const llvm::PointerType* ty);

inline std::optional<llvm::hash_code>
type_hash(const llvm::TargetExtType* ty)
{
  return {};
}

std::optional<llvm::hash_code>
attribute_hash(llvm::Attribute attr);

std::optional<llvm::hash_code>
attribute_set_hash(llvm::AttributeSet attrSet);

std::optional<llvm::hash_code>
attribute_list_hash(llvm::AttributeList attrList);

std::optional<llvm::hash_code>
function_hash(const llvm::Function* func);

std::optional<llvm::hash_code>
call_site_hash(const llvm::CallBase* callSite);

inline std::string
hash2str(llvm::hash_code hc)
{
  return llvm::utohexstr(hc, false, 16);
}

inline llvm::Expected<llvm::hash_code>
str2hash(llvm::StringRef s)
{
  llvm::hash_code ret;
  if (s.getAsInteger(16, ret))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "not a hex number");
  return ret;
}

} // namespace gyhcall
