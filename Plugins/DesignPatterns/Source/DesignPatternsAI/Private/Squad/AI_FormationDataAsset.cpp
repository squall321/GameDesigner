// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Squad/AI_FormationDataAsset.h"

FTransform UAI_FormationDataAsset::GetSlotTransform(int32 Index, int32 Count) const
{
	Count = FMath::Max(1, Count);
	Index = FMath::Clamp(Index, 0, Count - 1);

	// Custom uses the authored slots verbatim.
	if (Kind == EAI_FormationKind::Custom)
	{
		if (Slots.IsValidIndex(Index))
		{
			const FAI_FormationSlot& Slot = Slots[Index];
			return FTransform(FRotator(0.f, Slot.RelativeYaw, 0.f), Slot.RelativeOffset);
		}
		return FTransform::Identity;
	}

	const float S = FMath::Max(1.f, Spacing);
	FVector Offset = FVector::ZeroVector;
	float Yaw = 0.f;

	switch (Kind)
	{
	case EAI_FormationKind::Line:
	{
		// Spread left-right (Y), centered on the anchor.
		const float Half = 0.5f * static_cast<float>(Count - 1) * S;
		Offset = FVector(0.f, static_cast<float>(Index) * S - Half, 0.f);
		break;
	}
	case EAI_FormationKind::Wedge:
	{
		// A V opening backward: alternate members to the right/left, stepping back each pair.
		const int32 Pair = (Index + 1) / 2;           // 0,1,1,2,2,...
		const int32 Side = (Index % 2 == 0) ? -1 : 1;  // index 0 = lead (Pair 0)
		Offset = FVector(-static_cast<float>(Pair) * S, static_cast<float>(Side) * static_cast<float>(Pair) * S, 0.f);
		break;
	}
	case EAI_FormationKind::Column:
	{
		// Single file trailing backward (X is forward).
		Offset = FVector(-static_cast<float>(Index) * S, 0.f, 0.f);
		break;
	}
	case EAI_FormationKind::Circle:
	{
		// Evenly distribute on a ring; radius scales so the chord between members ~= Spacing.
		const float Radius = (Count > 1) ? (S / (2.f * FMath::Sin(PI / static_cast<float>(Count)))) : 0.f;
		const float Angle = (2.f * PI) * (static_cast<float>(Index) / static_cast<float>(Count));
		Offset = FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		Yaw = FMath::RadiansToDegrees(Angle); // face outward
		break;
	}
	default:
		break;
	}

	// Apply any authored per-slot role-yaw override if a matching slot exists.
	if (Slots.IsValidIndex(Index))
	{
		Yaw = Slots[Index].RelativeYaw != 0.f ? Slots[Index].RelativeYaw : Yaw;
	}

	return FTransform(FRotator(0.f, Yaw, 0.f), Offset);
}

FGameplayTag UAI_FormationDataAsset::GetSlotRole(int32 Index) const
{
	return Slots.IsValidIndex(Index) ? Slots[Index].PreferredRole : FGameplayTag();
}
