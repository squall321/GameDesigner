// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Seam_NetValue.generated.h"

/** Discriminator for the value currently held by an FSeam_NetValue. */
UENUM(BlueprintType)
enum class ESeam_NetValueType : uint8
{
	None,
	Bool,
	Int,      // stored as int64
	Float,    // stored as double
	Vector,
	Tag,
	Name
};

/**
 * A closed, net-friendly value variant — the ONLY arbitrary-typed value allowed to cross the wire
 * in the high-level layer. A raw FInstancedStruct must never be a plain Replicated UPROPERTY (its
 * ScriptStruct/memory are not UPROPERTYs and do not serialize); this struct provides a hand-written
 * NetSerialize that writes only the active field, plus lossless conversion to/from FInstancedStruct
 * for the local/save side (which may use the full FInstancedStruct).
 *
 * Supported types: bool, int64, double, FVector, FGameplayTag, FName. Anything outside this set is
 * a save-/server-local value and stays an FInstancedStruct (never replicated directly).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_NetValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	ESeam_NetValueType Type = ESeam_NetValueType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	bool bValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	int64 IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	double FloatValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	FVector VectorValue = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	FGameplayTag TagValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	FName NameValue;

	FSeam_NetValue() = default;

	// --- Typed constructors ---
	static FSeam_NetValue MakeBool(bool In)              { FSeam_NetValue V; V.Type = ESeam_NetValueType::Bool;   V.bValue = In;     return V; }
	static FSeam_NetValue MakeInt(int64 In)              { FSeam_NetValue V; V.Type = ESeam_NetValueType::Int;    V.IntValue = In;   return V; }
	static FSeam_NetValue MakeFloat(double In)           { FSeam_NetValue V; V.Type = ESeam_NetValueType::Float;  V.FloatValue = In; return V; }
	static FSeam_NetValue MakeVector(const FVector& In)  { FSeam_NetValue V; V.Type = ESeam_NetValueType::Vector; V.VectorValue = In;return V; }
	static FSeam_NetValue MakeTag(const FGameplayTag& In){ FSeam_NetValue V; V.Type = ESeam_NetValueType::Tag;    V.TagValue = In;   return V; }
	static FSeam_NetValue MakeName(const FName& In)      { FSeam_NetValue V; V.Type = ESeam_NetValueType::Name;   V.NameValue = In;  return V; }

	bool IsSet() const { return Type != ESeam_NetValueType::None; }

	/**
	 * Hand-written network serialization. Writes the discriminator, then ONLY the active field, so
	 * an unset value costs one byte and a bool costs two. Symmetric on read.
	 */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Convert to an FInstancedStruct for local/save use. Returns false if Type is None. */
	bool ToInstancedStruct(FInstancedStruct& Out) const;

	/**
	 * Build from an FInstancedStruct if its inner type is one of the supported net types.
	 * bOk is false (and the result is an empty value) for unsupported inner types.
	 */
	static FSeam_NetValue FromInstancedStruct(const FInstancedStruct& In, bool& bOk);

	bool operator==(const FSeam_NetValue& Other) const;
	bool operator!=(const FSeam_NetValue& Other) const { return !(*this == Other); }
};

// Enable the custom NetSerialize so this struct can live inside a fast-array item or a replicated
// wrapper and serialize compactly.
template<>
struct TStructOpsTypeTraits<FSeam_NetValue> : public TStructOpsTypeTraitsBase2<FSeam_NetValue>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true,
	};
};
