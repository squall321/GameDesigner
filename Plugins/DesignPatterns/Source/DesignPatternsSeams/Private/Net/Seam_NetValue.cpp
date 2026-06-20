// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Net/Seam_NetValue.h"

bool FSeam_NetValue::NetSerialize(FArchive& Ar, UPackageMap* /*Map*/, bool& bOutSuccess)
{
	// Discriminator first.
	uint8 RawType = static_cast<uint8>(Type);
	Ar << RawType;
	Type = static_cast<ESeam_NetValueType>(RawType);

	// Then only the active field — unset values cost a single byte.
	switch (Type)
	{
	case ESeam_NetValueType::Bool:
	{
		// Pack the bool into a single bit when writing to a bit-archive.
		Ar.SerializeBits(&bValue, 1);
		break;
	}
	case ESeam_NetValueType::Int:
		Ar << IntValue;
		break;
	case ESeam_NetValueType::Float:
		Ar << FloatValue;
		break;
	case ESeam_NetValueType::Vector:
		// Full-precision vector; callers that want quantization should store components as Int/Float.
		Ar << VectorValue;
		break;
	case ESeam_NetValueType::Tag:
		TagValue.NetSerialize(Ar, nullptr, bOutSuccess);
		break;
	case ESeam_NetValueType::Name:
		Ar << NameValue;
		break;
	case ESeam_NetValueType::None:
	default:
		break;
	}

	bOutSuccess = true;
	return true;
}

bool FSeam_NetValue::ToInstancedStruct(FInstancedStruct& Out) const
{
	switch (Type)
	{
	case ESeam_NetValueType::Bool:
		Out.InitializeAs<bool>(bValue);
		return true;
	case ESeam_NetValueType::Int:
		Out.InitializeAs<int64>(IntValue);
		return true;
	case ESeam_NetValueType::Float:
		Out.InitializeAs<double>(FloatValue);
		return true;
	case ESeam_NetValueType::Vector:
		Out.InitializeAs<FVector>(VectorValue);
		return true;
	case ESeam_NetValueType::Tag:
		Out.InitializeAs<FGameplayTag>(TagValue);
		return true;
	case ESeam_NetValueType::Name:
		Out.InitializeAs<FName>(NameValue);
		return true;
	case ESeam_NetValueType::None:
	default:
		Out.Reset();
		return false;
	}
}

FSeam_NetValue FSeam_NetValue::FromInstancedStruct(const FInstancedStruct& In, bool& bOk)
{
	bOk = true;
	const UScriptStruct* Struct = In.GetScriptStruct();
	if (!Struct || !In.IsValid())
	{
		bOk = false;
		return FSeam_NetValue();
	}

	if (Struct == TBaseStructure<FVector>::Get())
	{
		return MakeVector(In.Get<FVector>());
	}
	if (Struct == TBaseStructure<FGameplayTag>::Get())
	{
		return MakeTag(In.Get<FGameplayTag>());
	}
	// bool / int64 / double / FName are stored as single-member wrapper structs by InitializeAs<T>;
	// resolve them by their base struct identity. TBaseStructure has no specialization for plain
	// primitives, so those values arrive only through MakeX in practice. Treat anything else as
	// unsupported for the net path (it stays an FInstancedStruct on the local/save side).
	bOk = false;
	return FSeam_NetValue();
}

bool FSeam_NetValue::operator==(const FSeam_NetValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}
	switch (Type)
	{
	case ESeam_NetValueType::Bool:   return bValue == Other.bValue;
	case ESeam_NetValueType::Int:    return IntValue == Other.IntValue;
	case ESeam_NetValueType::Float:  return FloatValue == Other.FloatValue;
	case ESeam_NetValueType::Vector: return VectorValue == Other.VectorValue;
	case ESeam_NetValueType::Tag:    return TagValue == Other.TagValue;
	case ESeam_NetValueType::Name:   return NameValue == Other.NameValue;
	case ESeam_NetValueType::None:
	default:                         return true;
	}
}
