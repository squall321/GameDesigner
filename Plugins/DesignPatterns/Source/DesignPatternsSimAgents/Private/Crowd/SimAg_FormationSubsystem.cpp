// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_FormationSubsystem.h"
#include "Crowd/SimAg_FormationAsset.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

void USimAg_FormationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		DefaultSpacing = FMath::Max(1.f, Settings->FormationSpacing);
	}
}

void USimAg_FormationSubsystem::Deinitialize()
{
	GroupMembers.Reset();
	Super::Deinitialize();
}

FVector USimAg_FormationSubsystem::ResolveSlotWorld(FGameplayTag FormationTag, int32 SlotIndex, const FVector& Anchor, const FRotator& AnchorRotation) const
{
	const USimAg_FormationAsset* Formation = ResolveFormation(FormationTag);

	// Local-space offset (X forward, Y right) from the asset or the procedural grid fallback.
	const FVector LocalOffset = Formation
		? Formation->GetSlotOffset(SlotIndex, DefaultSpacing)
		: FVector(-static_cast<float>(SlotIndex) * DefaultSpacing, 0.f, 0.f);

	// Rotate the local offset into world space by the anchor's facing, then translate.
	return Anchor + AnchorRotation.RotateVector(LocalOffset);
}

int32 USimAg_FormationSubsystem::AssignSlot(FSeam_EntityId Group, FSeam_EntityId Agent)
{
	if (!Group.IsValid() || !Agent.IsValid())
	{
		return 0;
	}
	TArray<FSeam_EntityId>& Members = GroupMembers.FindOrAdd(Group);
	const int32 Existing = Members.IndexOfByKey(Agent);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}
	Members.Add(Agent);
	return Members.Num() - 1;
}

void USimAg_FormationSubsystem::ReleaseSlot(FSeam_EntityId Group, FSeam_EntityId Agent)
{
	if (TArray<FSeam_EntityId>* Members = GroupMembers.Find(Group))
	{
		// Remove and keep order so remaining members keep their relative slots (those after shift down by
		// one, which is acceptable — the formation simply tightens up).
		Members->Remove(Agent);
		if (Members->Num() == 0)
		{
			GroupMembers.Remove(Group);
		}
	}
}

USimAg_FormationAsset* USimAg_FormationSubsystem::ResolveFormation(const FGameplayTag& FormationTag) const
{
	if (!FormationTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<USimAg_FormationAsset>(FormationTag);
	}
	return nullptr;
}

FString USimAg_FormationSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimAg Formations: groups=%d"), GroupMembers.Num());
}
