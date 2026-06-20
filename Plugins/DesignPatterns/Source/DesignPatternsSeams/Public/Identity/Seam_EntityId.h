// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Seam_EntityId.generated.h"

/**
 * A net- and save-stable entity identifier, wrapping an FGuid.
 *
 * Used to key world/grid/agent state by a stable id rather than a raw object pointer (which is
 * neither replicable nor save-stable). Hashable and comparable so it can be a TMap key.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_EntityId
{
	GENERATED_BODY()

	/** The underlying stable id. Invalid (all-zero) until assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|Seam")
	FGuid Value;

	FSeam_EntityId() = default;
	explicit FSeam_EntityId(const FGuid& InValue) : Value(InValue) {}

	/** Returns true when this id has been assigned a non-zero value. */
	bool IsValid() const { return Value.IsValid(); }

	/** Create a fresh, unique id. */
	static FSeam_EntityId NewId() { return FSeam_EntityId(FGuid::NewGuid()); }

	/** The invalid/empty id. */
	static FSeam_EntityId Invalid() { return FSeam_EntityId(); }

	bool operator==(const FSeam_EntityId& Other) const { return Value == Other.Value; }
	bool operator!=(const FSeam_EntityId& Other) const { return Value != Other.Value; }

	friend uint32 GetTypeHash(const FSeam_EntityId& Id) { return GetTypeHash(Id.Value); }

	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphens); }
};
