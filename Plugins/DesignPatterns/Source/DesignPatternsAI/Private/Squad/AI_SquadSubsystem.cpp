// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Squad/AI_SquadSubsystem.h"
#include "Squad/AI_SquadCarrier.h"
#include "Settings/AI_DeveloperSettings.h"
#include "DesignPatternsAINativeTags.h"
#include "AI_BusPayloads.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

// World module is a PRIVATE dependency — resolved here in the .cpp only.
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"
#include "Blackboard/WorldHub_ScopedBlackboard.h"

#include "EngineUtils.h"
#include "Engine/World.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Defensive fallbacks used only if the settings CDO is somehow null (it never is in a running game). */
	constexpr int32 GDefaultMaxSquadSize_Fallback = 6;
	constexpr float GFallbackSpacing_Fallback = 150.f;
	constexpr int32 GFallbackColumns_Fallback = 3;
}

void UAI_SquadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSelfAsService();
}

void UAI_SquadSubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		// Only drop the binding if it still points at us (a later override may have replaced it).
		if (Locator->ResolveService(AINativeTags::Service_AI_Squad) == this)
		{
			Locator->UnregisterService(AINativeTags::Service_AI_Squad);
		}
	}
	Carriers.Reset();
	Super::Deinitialize();
}

UDP_ServiceLocatorSubsystem* UAI_SquadSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

void UAI_SquadSubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		// WeakObserved: the locator must not keep this world-lifetime subsystem alive across travel.
		Locator->RegisterService(AINativeTags::Service_AI_Squad, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ Carrier indexing ----------------------------------------------------------------------------

void UAI_SquadSubsystem::RebuildCarrierIndex() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Mutate the transient index from a const read path (lazy refresh / prune of dead carriers).
	TMap<FGuid, TWeakObjectPtr<AAI_SquadCarrier>>& Index = const_cast<UAI_SquadSubsystem*>(this)->Carriers;
	Index.Reset();
	for (TActorIterator<AAI_SquadCarrier> It(World); It; ++It)
	{
		AAI_SquadCarrier* Carrier = *It;
		if (Carrier && Carrier->GetSquadId().IsValid())
		{
			Index.Add(Carrier->GetSquadId(), Carrier);
		}
	}
}

AAI_SquadCarrier* UAI_SquadSubsystem::FindCarrier(FGuid SquadId) const
{
	if (!SquadId.IsValid())
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<AAI_SquadCarrier>* Found = Carriers.Find(SquadId))
	{
		if (AAI_SquadCarrier* Live = Found->Get())
		{
			return Live;
		}
	}
	// Miss or stale: rebuild from the world (covers client-replicated carriers and destroyed ones).
	RebuildCarrierIndex();
	if (const TWeakObjectPtr<AAI_SquadCarrier>* Found = Carriers.Find(SquadId))
	{
		return Found->Get();
	}
	return nullptr;
}

int32 UAI_SquadSubsystem::GetSquadCount() const
{
	RebuildCarrierIndex();
	return Carriers.Num();
}

//~ IAI_Squad (answers for the active squad) ----------------------------------------------------

FGuid UAI_SquadSubsystem::GetSquadId() const
{
	// Validate the active squad still exists; otherwise report invalid.
	return FindCarrier(ActiveSquadId) ? ActiveSquadId : FGuid();
}

FGameplayTag UAI_SquadSubsystem::GetRole(FSeam_EntityId Member) const
{
	if (const AAI_SquadCarrier* Carrier = FindCarrier(ActiveSquadId))
	{
		return Carrier->GetRole(Member);
	}
	return FGameplayTag();
}

FTransform UAI_SquadSubsystem::GetFormationSlot(FSeam_EntityId Member) const
{
	if (const AAI_SquadCarrier* Carrier = FindCarrier(ActiveSquadId))
	{
		return Carrier->GetAbsoluteSlot(Member);
	}
	return FTransform::Identity;
}

//~ Lifecycle -----------------------------------------------------------------------------------

FGuid UAI_SquadSubsystem::FormSquad(FGameplayTag SquadTag, FTransform Anchor, int32 MaxMembers)
{
	if (!HasWorldAuthority())
	{
		return FGuid();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return FGuid();
	}

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const int32 EffectiveMax = (MaxMembers > 0)
		? MaxMembers
		: (Settings ? Settings->DefaultMaxSquadSize : GDefaultMaxSquadSize_Fallback);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient; // runtime coordination state, not a level actor

	AAI_SquadCarrier* Carrier = World->SpawnActor<AAI_SquadCarrier>(
		AAI_SquadCarrier::StaticClass(), Anchor, SpawnParams);
	if (!Carrier)
	{
		UE_LOG(LogDP, Error, TEXT("AI SquadSubsystem: failed to spawn squad carrier."));
		return FGuid();
	}

	const FGuid NewId = FGuid::NewGuid();
	Carrier->InitSquad(NewId, SquadTag);
	Carrier->SetMaxMembers(EffectiveMax);
	Carrier->SetAnchorTransform(Anchor);

	Carriers.Add(NewId, Carrier);
	ActiveSquadId = NewId;

	// Broadcast formation on the bus with a flat payload.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FAI_SquadEventPayload Payload;
		Payload.SquadId = NewId;
		Payload.SquadTag = SquadTag;
		Payload.MemberCount = 0;
		Bus->BroadcastPayload(AINativeTags::Bus_AI_Squad_Formed,
			FInstancedStruct::Make(Payload), this);
	}

	UE_LOG(LogDP, Log, TEXT("AI SquadSubsystem: formed squad %s (tag %s, max %d)."),
		*NewId.ToString(EGuidFormats::DigitsWithHyphens), *SquadTag.ToString(), EffectiveMax);
	return NewId;
}

bool UAI_SquadSubsystem::DissolveSquad(FGuid SquadId)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier)
	{
		return false;
	}

	const FGameplayTag SquadTag = Carrier->GetSquadTag();
	const int32 MemberCount = Carrier->GetMemberCount();

	ClearSquadBlackboard(SquadId);
	Carrier->Destroy();
	Carriers.Remove(SquadId);
	if (ActiveSquadId == SquadId)
	{
		ActiveSquadId = FGuid();
	}

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FAI_SquadEventPayload Payload;
		Payload.SquadId = SquadId;
		Payload.SquadTag = SquadTag;
		Payload.MemberCount = MemberCount;
		Bus->BroadcastPayload(AINativeTags::Bus_AI_Squad_Dissolved,
			FInstancedStruct::Make(Payload), this);
	}

	UE_LOG(LogDP, Log, TEXT("AI SquadSubsystem: dissolved squad %s."),
		*SquadId.ToString(EGuidFormats::DigitsWithHyphens));
	return true;
}

bool UAI_SquadSubsystem::SetActiveSquad(FGuid SquadId)
{
	if (FindCarrier(SquadId))
	{
		ActiveSquadId = SquadId;
		return true;
	}
	return false;
}

//~ Membership / roles --------------------------------------------------------------------------

bool UAI_SquadSubsystem::AddMember(FGuid SquadId, FSeam_EntityId Member)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier || !Carrier->AddMember(Member))
	{
		return false;
	}
	RebuildFormation(SquadId);
	SyncSquadBlackboard(*Carrier);
	return true;
}

bool UAI_SquadSubsystem::RemoveMember(FGuid SquadId, FSeam_EntityId Member)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier || !Carrier->RemoveMember(Member))
	{
		return false;
	}
	RebuildFormation(SquadId);
	SyncSquadBlackboard(*Carrier);
	return true;
}

bool UAI_SquadSubsystem::ClaimRole(FGuid SquadId, FSeam_EntityId Member, FGameplayTag Role, bool bExclusive)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier || !Carrier->ClaimRole(Member, Role, bExclusive))
	{
		return false;
	}
	SyncSquadBlackboard(*Carrier);
	return true;
}

bool UAI_SquadSubsystem::RebuildFormation(FGuid SquadId)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier)
	{
		return false;
	}

	const TArray<FAI_SquadMember>& Members = Carrier->GetMembers();
	const int32 Count = Members.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Carrier->AssignSlot(Members[Index].MemberId, ComputeGridSlot(Index, Count));
	}
	return true;
}

bool UAI_SquadSubsystem::SetSquadAnchor(FGuid SquadId, FTransform Anchor)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	AAI_SquadCarrier* Carrier = FindCarrier(SquadId);
	if (!Carrier)
	{
		return false;
	}
	Carrier->SetAnchorTransform(Anchor);
	return true;
}

//~ Formation math ------------------------------------------------------------------------------

FTransform UAI_SquadSubsystem::ComputeGridSlot(int32 Index, int32 Count) const
{
	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const float Spacing = Settings ? Settings->FallbackFormationSpacing : GFallbackSpacing_Fallback;
	const int32 Columns = FMath::Max(1, Settings ? Settings->FallbackFormationColumns : GFallbackColumns_Fallback);

	const int32 Col = Index % Columns;
	const int32 Row = Index / Columns;
	const int32 RowsForCount = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(1, Count), Columns));

	// Center the grid on the anchor: X is depth (rows), Y is width (columns).
	const float HalfWidth = 0.5f * static_cast<float>(Columns - 1) * Spacing;
	const float HalfDepth = 0.5f * static_cast<float>(RowsForCount - 1) * Spacing;

	const FVector Offset(
		static_cast<float>(Row) * Spacing - HalfDepth,
		static_cast<float>(Col) * Spacing - HalfWidth,
		0.f);
	return FTransform(Offset);
}

//~ Shared blackboard via the World hub ---------------------------------------------------------

void UAI_SquadSubsystem::SyncSquadBlackboard(const AAI_SquadCarrier& Carrier)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (!Hub)
	{
		return; // hub optional; squads still work without it
	}

	// One scratch blackboard per squad, addressed by an Entity scope keyed on the squad id.
	const FWorldHub_Scope Scope = FWorldHub_Scope::Entity(FSeam_EntityId(Carrier.GetSquadId()));
	UWorldHub_ScopedBlackboard* Board = Hub->GetBlackboard(Scope, /*bCreate=*/true);
	if (!Board)
	{
		return;
	}

	// Mirror simple coordination facts: member count and whether a leader exists. Behaviour reads these
	// off the hub blackboard without touching the carrier directly.
	Board->SetInt(FName(TEXT("MemberCount")), Carrier.GetMemberCount());
}

void UAI_SquadSubsystem::ClearSquadBlackboard(const FGuid& SquadId)
{
	if (!HasWorldAuthority() || !SquadId.IsValid())
	{
		return;
	}
	if (UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		Hub->ClearBlackboard(FWorldHub_Scope::Entity(FSeam_EntityId(SquadId)));
	}
}

//~ Debug ---------------------------------------------------------------------------------------

FString UAI_SquadSubsystem::GetDPDebugString_Implementation() const
{
	RebuildCarrierIndex();
	return FString::Printf(TEXT("AI Squads: %d (active %s)"),
		Carriers.Num(),
		ActiveSquadId.IsValid() ? *ActiveSquadId.ToString(EGuidFormats::Digits) : TEXT("none"));
}
