// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AI_FormationDataAsset.generated.h"

/**
 * The shape a formation lays its members out in. The procedural shapes derive slot offsets from Spacing
 * and member index; Custom uses the explicit Slots array verbatim.
 */
UENUM(BlueprintType)
enum class EAI_FormationKind : uint8
{
	/** A single straight rank (members spread left-right, perpendicular to facing). */
	Line,

	/** A V/wedge opening backward from a lead slot. */
	Wedge,

	/** A single file (members trail front-to-back along facing). */
	Column,

	/** An evenly-spaced ring around the anchor. */
	Circle,

	/** Use the explicit per-slot offsets authored in Slots (ignore procedural shaping). */
	Custom
};

/**
 * One explicitly-authored formation slot (used by EAI_FormationKind::Custom, and as the role source for
 * procedural shapes). Relative to the squad anchor.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_FormationSlot
{
	GENERATED_BODY()

	/** Slot offset relative to the squad anchor (local space; X forward, Y right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation")
	FVector RelativeOffset = FVector::ZeroVector;

	/** Yaw (degrees) the occupant should face, relative to the anchor facing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation")
	float RelativeYaw = 0.f;

	/** Tactical role tag preferred for the occupant of this slot (e.g. AI.Role.Leader). May be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation")
	FGameplayTag PreferredRole;

	FAI_FormationSlot() = default;
};

/**
 * Designer FORMATION definition: a shape + spacing + (for Custom) explicit slot offsets and per-slot
 * roles. Pure data, consumed SUBSYSTEM-side. The replicated AInfo carrier stays formation-asset-agnostic
 * (no UObject soft ref is ever replicated on it) — the subsystem composes the resulting relative slot
 * transforms onto the carrier, exactly as the fallback grid does today.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAI_API UAI_FormationDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The formation shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation")
	EAI_FormationKind Kind = EAI_FormationKind::Wedge;

	/** Spacing (world units) between adjacent members for the procedural shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation", meta = (ClampMin = "1.0"))
	float Spacing = 150.f;

	/** Explicit slots for EAI_FormationKind::Custom, and the role source for any shape (by index). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Formation")
	TArray<FAI_FormationSlot> Slots;

	/**
	 * Compute the relative slot transform for member Index of a formation of Count members. Procedural for
	 * Line/Wedge/Column/Circle; for Custom returns Slots[Index] (Identity if out of range). The subsystem
	 * composes this with the squad anchor to get the absolute slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Formation")
	FTransform GetSlotTransform(int32 Index, int32 Count) const;

	/** @return the preferred role for slot Index, or empty (clamps to the Slots array). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Formation")
	FGameplayTag GetSlotRole(int32 Index) const;
};
