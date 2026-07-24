#include "type_hash.hpp"
#include <llvm/IR/Function.h>

namespace gyhcall {

std::optional<llvm::hash_code>
type_hash(const llvm::Type* ty)
{
  switch (ty->getTypeID()) {
    case llvm::Type::HalfTyID:
    case llvm::Type::BFloatTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID:
    // case llvm::Type::X86_FP80TyID:
    case llvm::Type::FP128TyID:
    // case llvm::Type::PPC_FP128TyID:
    case llvm::Type::VoidTyID:
    case llvm::Type::LabelTyID:
    case llvm::Type::MetadataTyID:
    // case llvm::Type::X86_MMXTyID:
    // case llvm::Type::X86_AMXTyID:
    case llvm::Type::TokenTyID:
      return ty->getTypeID();

    case llvm::Type::IntegerTyID:
      return type_hash(llvm::cast<llvm::IntegerType>(ty));

    case llvm::Type::FunctionTyID:
      return type_hash(llvm::cast<llvm::FunctionType>(ty));

    case llvm::Type::PointerTyID:
      return type_hash(llvm::cast<llvm::PointerType>(ty));

    case llvm::Type::StructTyID:
      return type_hash(llvm::cast<llvm::StructType>(ty));

    case llvm::Type::ArrayTyID:
      return type_hash(llvm::cast<llvm::ArrayType>(ty));

    case llvm::Type::FixedVectorTyID:
      return type_hash(llvm::cast<llvm::FixedVectorType>(ty));

    case llvm::Type::ScalableVectorTyID:
      return type_hash(llvm::cast<llvm::ScalableVectorType>(ty));

    // case llvm::Type::TypedPointerTyID:
    // case llvm::Type::TargetExtTyID:
  }

  return {};
}

std::optional<llvm::hash_code>
type_hash(const llvm::IntegerType* ty)
{
  return llvm::hash_combine(llvm::Type::IntegerTyID, ty->getBitWidth());
}

std::optional<llvm::hash_code>
type_hash(const llvm::FunctionType* ty)
{
  llvm::hash_code ret = llvm::Type::FunctionTyID;

  auto retHash = type_hash(ty->getReturnType());
  if (!retHash)
    return {};
  ret = llvm::hash_combine(ret, *retHash);

  for (auto param : ty->params()) {
    auto paramHash = type_hash(param);
    if (!paramHash)
      return {};
    ret = llvm::hash_combine(ret, *paramHash);
  }

  ret = llvm::hash_combine(ret, ty->isVarArg());
  return ret;
}

std::optional<llvm::hash_code>
type_hash(const llvm::StructType* ty)
{
  if (ty->isOpaque())
    return {};

  llvm::hash_code ret = llvm::Type::StructTyID;

  for (auto elem : ty->elements()) {
    auto elemHash = type_hash(elem);
    if (!elemHash)
      return {};
    ret = llvm::hash_combine(ret, elemHash);
  }

  ret = llvm::hash_combine(ret, ty->isPacked(), ty->isSized());
  return ret;
}

std::optional<llvm::hash_code>
type_hash(const llvm::ArrayType* ty)
{
  llvm::hash_code ret = llvm::Type::ArrayTyID;

  auto elemHash = type_hash(ty->getElementType());
  if (!elemHash)
    return {};
  ret = llvm::hash_combine(ret, *elemHash, ty->getNumElements());

  return ret;
}

std::optional<llvm::hash_code>
type_hash(const llvm::FixedVectorType* ty)
{
  llvm::hash_code ret = llvm::Type::FixedVectorTyID;

  auto elemHash = type_hash(ty->getElementType());
  if (!elemHash)
    return {};
  ret = llvm::hash_combine(ret, *elemHash, ty->getNumElements());

  return ret;
}

std::optional<llvm::hash_code>
type_hash(const llvm::ScalableVectorType* ty)
{
  llvm::hash_code ret = llvm::Type::ScalableVectorTyID;

  auto elemHash = type_hash(ty->getElementType());
  if (!elemHash)
    return {};
  ret = llvm::hash_combine(ret, *elemHash, ty->getMinNumElements());

  return ret;
}

std::optional<llvm::hash_code>
type_hash(const llvm::PointerType* ty)
{
  return llvm::hash_combine(llvm::Type::PointerTyID, ty->getAddressSpace());
}

std::optional<llvm::hash_code>
attribute_hash(llvm::Attribute attr)
{
  if (!attr.isValid())
    return {};
  auto kind = attr.getKindAsEnum();
  llvm::hash_code ret = kind;

  if (attr.isEnumAttribute())
    ;
  else if (attr.isIntAttribute())
    ret = llvm::hash_combine(ret, attr.getValueAsInt());
  else if (attr.isStringAttribute())
    ret = llvm::hash_combine(ret, attr.getValueAsString());
  else if (attr.isTypeAttribute())
    ret = llvm::hash_combine(ret, type_hash(attr.getValueAsType()));

  return ret;
}

std::optional<llvm::hash_code>
attribute_set_hash(llvm::AttributeSet attrSet)
{
  llvm::hash_code ret = 200000406;
  for (auto attr : attrSet) {
    auto hash = attribute_hash(attr);
    if (!hash)
      return {};
    ret = llvm::hash_combine(ret, *hash);
  }
  return ret;
}

std::optional<llvm::hash_code>
attribute_list_hash(llvm::AttributeList attrList)
{
  llvm::hash_code ret = 200000906;
  for (auto attrSet : attrList) {
    auto hash = attribute_set_hash(attrSet);
    if (!hash)
      return {};
    ret = llvm::hash_combine(ret, *hash);
  }
  return ret;
}

std::optional<llvm::hash_code>
function_hash(const llvm::Function* func)
{
  llvm::hash_code ret = 20010313;
  auto typeHash = type_hash(func->getFunctionType());
  if (!typeHash)
    return {};
  ret = llvm::hash_combine(ret, *typeHash);
  auto attrHash = attribute_list_hash(func->getAttributes());
  if (!attrHash)
    return {};
  ret = llvm::hash_combine(ret, *attrHash);
  return ret;
}

std::optional<llvm::hash_code>
call_site_hash(const llvm::CallBase* callSite)
{
  llvm::hash_code ret = 20010313;
  auto typeHash = type_hash(callSite->getFunctionType());
  if (!typeHash)
    return {};
  ret = llvm::hash_combine(ret, *typeHash);
  auto attrHash = attribute_list_hash(callSite->getAttributes());
  if (!attrHash)
    return {};
  ret = llvm::hash_combine(ret, *attrHash);
  return ret;
}

} // namespace gyhcall
