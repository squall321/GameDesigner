// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "WorldHub_Scope.generated.h"

/**
 * The granularity at which a world-hub key is addressed.
 *
 * A flag/variable/counter is stored under (Key, Scope). The scope picks WHO the value belongs
 * to, allowing the same key to carry distinct values for the world, for a faction, or for a
 * single entity, with a well-defined fallback order applied by the hub subsystem (Entity ->
 * Faction -> Global) when a query asks for an effective value.
 */
UENUM(BlueprintType)
enum class EWorldHub_ScopeType : uint8
{
	/** A single value shared by the whole session/world. FactionTag and EntityId are ignored. */
	Global,

	/** A value owned by a faction, addressed by FactionTag. EntityId is ignored. */
	Faction,

	/** A value owned by a single entity, addressed by its net-/save-stable EntityId. */
	Entity
};

/**
 * The addressing key that, together with a flag FGameplayTag, identifies one slot of world-hub
 * state.
 *
 * Deliberately holds NO weak object pointer: a scope must be net- and save-stable and usable as
 * a TMap key, so an entity scope is identified by its FSeam_EntityId (an FGuid wrapper) rather
 * than by a live UObject reference. The hub resolves a scope back to a runtime object (if any)
 * via the identity seam, never the other way around.
 *
 * Equality and hashing only consider the fields relevant to the active ScopeType so that, e.g.,
 * two Global scopes compare equal regardless of any stale FactionTag/EntityId left in the struct.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_Scope
{
	GENERATED_BODY()

	/** Selects which of the fields below participate in identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|WorldHub|Scope")
	EWorldHub_ScopeType ScopeType = EWorldHub_ScopeType::Global;

	/** Owning faction; only meaningful (and only compared/hashed) when ScopeType == Faction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|WorldHub|Scope")
	FGameplayTag FactionTag;

	/** Owning entity; only meaningful (and only compared/hashed) when ScopeType == Entity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|WorldHub|Scope")
	FSeam_EntityId EntityId;

	FWorldHub_Scope() = default;

	explicit FWorldHub_Scope(EWorldHub_ScopeType InType)
		: ScopeType(InType)
	{
	}

	// --- Named constructors ---

	/** The single world-wide scope. */
	static FWorldHub_Scope Global()
	{
		return FWorldHub_Scope(EWorldHub_ScopeType::Global);
	}

	/** A faction scope addressed by FactionTag. */
	static FWorldHub_Scope Faction(const FGameplayTag& InFactionTag)
	{
		FWorldHub_Scope S(EWorldHub_ScopeType::Faction);
		S.FactionTag = InFactionTag;
		return S;
	}

	/** An entity scope addressed by a net-/save-stable EntityId. */
	static FWorldHub_Scope Entity(const FSeam_EntityId& InEntityId)
	{
		FWorldHub_Scope S(EWorldHub_ScopeType::Entity);
		S.EntityId = InEntityId;
		return S;
	}

	/** @return true if this scope is fully specified for its ScopeType (a faction/entity is named). */
	bool IsValid() const
	{
		switch (ScopeType)
		{
		case EWorldHub_ScopeType::Global:  return true;
		case EWorldHub_ScopeType::Faction: return FactionTag.IsValid();
		case EWorldHub_ScopeType::Entity:  return EntityId.IsValid();
		default:                           return false;
		}
	}

	/**
	 * @return the parent scope used for effective-value fallback, and true if one exists.
	 * Entity falls back to Global (entity values are not faction-owned by construction here),
	 * Faction falls back to Global, and Global has no parent.
	 */
	bool GetFallbackScope(FWorldHub_Scope& OutParent) const
	{
		switch (ScopeType)
		{
		case EWorldHub_ScopeType::Entity:
		case EWorldHub_ScopeType::Faction:
			OutParent = FWorldHub_Scope::Global();
			return true;
		case EWorldHub_ScopeType::Global:
		default:
			return false;
		}
	}

	bool operator==(const FWorldHub_Scope& Other) const
	{
		if (ScopeType != Other.ScopeType)
		{
			return false;
		}

		switch (ScopeType)
		{
		case EWorldHub_ScopeType::Faction: return FactionTag == Other.FactionTag;
		case EWorldHub_ScopeType::Entity:  return EntityId == Other.EntityId;
		case EWorldHub_ScopeType::Global:
		default:                           return true;
		}
	}

	bool operator!=(const FWorldHub_Scope& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FWorldHub_Scope& Scope)
	{
		uint32 Hash = GetTypeHash(static_cast<uint8>(Scope.ScopeType));
		switch (Scope.ScopeType)
		{
		case EWorldHub_ScopeType::Faction:
			Hash = HashCombine(Hash, GetTypeHash(Scope.FactionTag));
			break;
		case EWorldHub_ScopeType::Entity:
			Hash = HashCombine(Hash, GetTypeHash(Scope.EntityId));
			break;
		case EWorldHub_ScopeType::Global:
		default:
			break;
		}
		return Hash;
	}

	/** Human-readable form for debug strings/logs. */
	FString ToString() const
	{
		switch (ScopeType)
		{
		case EWorldHub_ScopeType::Faction:
			return FString::Printf(TEXT("Faction(%s)"), *FactionTag.ToString());
		case EWorldHub_ScopeType::Entity:
			return FString::Printf(TEXT("Entity(%s)"), *EntityId.ToString());
		case EWorldHub_ScopeType::Global:
		default:
			return TEXT("Global");
		}
	}
};
