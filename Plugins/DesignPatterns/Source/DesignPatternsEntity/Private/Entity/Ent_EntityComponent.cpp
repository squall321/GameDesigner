// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Entity/Ent_EntityComponent.h"
#include "Trait/Ent_Trait.h"
#include "Archetype/Ent_ArchetypeAsset.h"
#include "Capability/Ent_CapabilityProvider.h"
#include "Registry/Ent_EntityRegistrySubsystem.h"
#include "Save/Ent_EntitySaveData.h"
#include "DesignPatternsEntityTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UEnt_EntityComponent::UEnt_EntityComponent()
{
	// Off by default; enabled only if a live trait wants ticking (see UpdateTickEnabled).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// This component is the authoritative replicated carrier for entity identity + traits.
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so per-entry callbacks can notify us (server and clients).
	ReplicatedTraits.OwnerComponent = this;
}

void UEnt_EntityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UEnt_EntityComponent, EntityId);
	DOREPLIFETIME(UEnt_EntityComponent, ArchetypeTag);
	DOREPLIFETIME(UEnt_EntityComponent, ReplicatedTraits);
}

bool UEnt_EntityComponent::HasEntityAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

FGameplayTag UEnt_EntityComponent::GetTraitIdentityTag(const UEnt_Trait* Trait)
{
	return Trait ? Trait->CapabilityTag : FGameplayTag();
}

//~ Lifecycle ---------------------------------------------------------------------------------

void UEnt_EntityComponent::BeginPlay()
{
	Super::BeginPlay();
	bHasBegunPlay = true;

	if (HasEntityAuthority())
	{
		// Assign a stable id exactly once, if one was not pre-seeded (e.g. by a loader/spawner).
		if (!EntityId.IsValid())
		{
			EntityId = FSeam_EntityId::NewId();
		}

		// Seed the archetype tag + trait set from the assigned archetype asset (walks parents).
		if (Archetype)
		{
			ApplyArchetype(Archetype);
		}
		else
		{
			// No archetype: still publish whatever traits were authored directly (e.g. via AddTrait).
			RebuildAllReplicatedEntries();
		}

		// Authority knows its id now, so register immediately.
		RegisterWithRegistry();
	}
	else
	{
		// Client: reconcile any traits already replicated, then register once the id is known.
		ReconcileLiveTraitsFromReplication();
		if (EntityId.IsValid())
		{
			RegisterWithRegistry();
		}
	}

	UpdateTickEnabled();
}

void UEnt_EntityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UEnt_EntityRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(this))
		{
			Registry->UnregisterEntity(this);
		}
		bRegistered = false;
	}

	// Tear down live traits so they release any owned resources.
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait)
		{
			UninitializeTrait(Trait);
		}
	}
	LiveTraits.Reset();

	Super::EndPlay(EndPlayReason);
}

void UEnt_EntityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Drive only the traits that asked to tick. Iterate a copy-safe index loop in case a trait
	// removes itself; null entries are tolerated.
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait && Trait->bWantsTick)
		{
			UEnt_Trait::Execute_OnTraitTick(Trait, DeltaTime);
		}
	}
}

void UEnt_EntityComponent::UpdateTickEnabled()
{
	bool bAnyWantsTick = false;
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait && Trait->bWantsTick)
		{
			bAnyWantsTick = true;
			break;
		}
	}
	SetComponentTickEnabled(bAnyWantsTick);
}

void UEnt_EntityComponent::RegisterWithRegistry()
{
	if (bRegistered || !EntityId.IsValid())
	{
		return;
	}
	if (UEnt_EntityRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(this))
	{
		Registry->RegisterEntity(this);
		bRegistered = true;
	}
}

//~ Identity ----------------------------------------------------------------------------------

void UEnt_EntityComponent::SetEntityId(FSeam_EntityId InId)
{
	// AUTHORITY GUARD: identity is server-authoritative.
	if (!HasEntityAuthority())
	{
		return;
	}
	// An entity's id never changes once assigned.
	if (EntityId.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Entity] SetEntityId ignored: id already assigned (%s) on '%s'."),
			*EntityId.ToString(), *GetNameSafe(GetOwner()));
		return;
	}
	EntityId = InId.IsValid() ? InId : FSeam_EntityId::NewId();

	if (bHasBegunPlay)
	{
		RegisterWithRegistry();
	}
}

void UEnt_EntityComponent::SetArchetypeTag(FGameplayTag InArchetypeTag)
{
	// AUTHORITY GUARD.
	if (!HasEntityAuthority())
	{
		return;
	}
	ArchetypeTag = InArchetypeTag;
}

void UEnt_EntityComponent::OnRep_EntityId()
{
	// Client received the stable id: register with the registry exactly once.
	if (EntityId.IsValid())
	{
		RegisterWithRegistry();
	}
	OnEntityChanged.Broadcast(this);
}

void UEnt_EntityComponent::OnRep_ArchetypeTag()
{
	// Archetype tag changed on a client; live traits may need reconciling against the entries.
	ReconcileLiveTraitsFromReplication();
	OnEntityChanged.Broadcast(this);
}

//~ Archetype application ---------------------------------------------------------------------

void UEnt_EntityComponent::ApplyArchetype(UEnt_ArchetypeAsset* Asset)
{
	// AUTHORITY GUARD: trait set is server-authoritative.
	if (!HasEntityAuthority())
	{
		return;
	}
	if (!Asset)
	{
		return;
	}

	Archetype = Asset;
	ArchetypeTag = Asset->DataTag;

	// Clear any existing live traits before rebuilding from the archetype.
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait)
		{
			UninitializeTrait(Trait);
		}
	}
	LiveTraits.Reset();

	// The archetype resolves its full parent-chain trait list (parents first, child last). These are
	// SOURCE templates — duplicate each under this component before use. Later (child) traits of the
	// same identity tag override earlier ones.
	TArray<UEnt_Trait*> ResolvedTemplates;
	Asset->GetResolvedDefaultTraits(ResolvedTemplates);

	for (UEnt_Trait* Template : ResolvedTemplates)
	{
		if (!Template)
		{
			continue;
		}
		UEnt_Trait* NewTrait = DuplicateObject<UEnt_Trait>(Template, this);
		if (!NewTrait)
		{
			continue;
		}

		// Replace an existing live trait of the same identity (override semantics).
		const FGameplayTag KindTag = GetTraitIdentityTag(NewTrait);
		if (KindTag.IsValid())
		{
			for (int32 Index = LiveTraits.Num() - 1; Index >= 0; --Index)
			{
				if (GetTraitIdentityTag(LiveTraits[Index]) == KindTag)
				{
					UninitializeTrait(LiveTraits[Index]);
					LiveTraits.RemoveAt(Index);
				}
			}
		}

		InitializeTrait(NewTrait);
		LiveTraits.Add(NewTrait);
	}

	RebuildAllReplicatedEntries();
	UpdateTickEnabled();
	OnEntityChanged.Broadcast(this);
}

//~ Trait mutators (authority only) -----------------------------------------------------------

UEnt_Trait* UEnt_EntityComponent::AddTrait(UEnt_Trait* Template)
{
	// AUTHORITY GUARD.
	if (!HasEntityAuthority() || !Template)
	{
		return nullptr;
	}

	// Always duplicate under this component so the live trait is owned here (the input may be an
	// archetype template or a trait owned elsewhere).
	UEnt_Trait* NewTrait = DuplicateObject<UEnt_Trait>(Template, this);
	if (!NewTrait)
	{
		return nullptr;
	}

	const FGameplayTag KindTag = GetTraitIdentityTag(NewTrait);
	if (KindTag.IsValid())
	{
		for (int32 Index = LiveTraits.Num() - 1; Index >= 0; --Index)
		{
			if (GetTraitIdentityTag(LiveTraits[Index]) == KindTag)
			{
				UninitializeTrait(LiveTraits[Index]);
				LiveTraits.RemoveAt(Index);
			}
		}
	}

	InitializeTrait(NewTrait);
	LiveTraits.Add(NewTrait);

	SyncReplicatedEntryForTrait(NewTrait);
	UpdateTickEnabled();
	OnEntityChanged.Broadcast(this);
	return NewTrait;
}

UEnt_Trait* UEnt_EntityComponent::AddTraitByClass(TSubclassOf<UEnt_Trait> TraitClass)
{
	// AUTHORITY GUARD.
	if (!HasEntityAuthority() || !*TraitClass)
	{
		return nullptr;
	}
	UEnt_Trait* NewTrait = NewObject<UEnt_Trait>(this, TraitClass);
	if (!NewTrait)
	{
		return nullptr;
	}

	const FGameplayTag KindTag = GetTraitIdentityTag(NewTrait);
	if (KindTag.IsValid())
	{
		for (int32 Index = LiveTraits.Num() - 1; Index >= 0; --Index)
		{
			if (GetTraitIdentityTag(LiveTraits[Index]) == KindTag)
			{
				UninitializeTrait(LiveTraits[Index]);
				LiveTraits.RemoveAt(Index);
			}
		}
	}

	InitializeTrait(NewTrait);
	LiveTraits.Add(NewTrait);

	SyncReplicatedEntryForTrait(NewTrait);
	UpdateTickEnabled();
	OnEntityChanged.Broadcast(this);
	return NewTrait;
}

bool UEnt_EntityComponent::RemoveTrait(FGameplayTag TraitTag)
{
	// AUTHORITY GUARD.
	if (!HasEntityAuthority() || !TraitTag.IsValid())
	{
		return false;
	}

	bool bRemoved = false;
	for (int32 Index = LiveTraits.Num() - 1; Index >= 0; --Index)
	{
		if (GetTraitIdentityTag(LiveTraits[Index]) == TraitTag)
		{
			UninitializeTrait(LiveTraits[Index]);
			LiveTraits.RemoveAt(Index);
			bRemoved = true;
		}
	}

	if (bRemoved)
	{
		ReplicatedTraits.Entries.RemoveAll([&TraitTag](const FEnt_TraitEntry& Entry)
		{
			return Entry.TraitClassTag == TraitTag;
		});
		ReplicatedTraits.MarkArrayDirty();
		UpdateTickEnabled();
		OnEntityChanged.Broadcast(this);
	}
	return bRemoved;
}

//~ Trait/capability queries ------------------------------------------------------------------

UEnt_Trait* UEnt_EntityComponent::FindTraitByTag(FGameplayTag TraitTag) const
{
	if (!TraitTag.IsValid())
	{
		return nullptr;
	}
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (GetTraitIdentityTag(Trait) == TraitTag)
		{
			return Trait;
		}
	}
	return nullptr;
}

UEnt_Trait* UEnt_EntityComponent::GetTraitByClass(TSubclassOf<UEnt_Trait> TraitClass) const
{
	if (!*TraitClass)
	{
		return nullptr;
	}
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait && Trait->IsA(TraitClass))
		{
			return Trait;
		}
	}
	return nullptr;
}

TArray<UEnt_Trait*> UEnt_EntityComponent::GetAllTraits() const
{
	TArray<UEnt_Trait*> Result;
	Result.Reserve(LiveTraits.Num());
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait)
		{
			Result.Add(Trait);
		}
	}
	return Result;
}

void UEnt_EntityComponent::GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const
{
	// Aggregate each live trait's advertised capabilities (the trait interface appends).
	if (LiveTraits.Num() > 0)
	{
		for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
		{
			if (Trait)
			{
				IEnt_CapabilityProvider::Execute_GetProvidedCapabilities(Trait, OutCapabilities);
			}
		}
		return;
	}

	// Client fallback before reconciliation: derive from replicated entries.
	for (const FEnt_TraitEntry& Entry : ReplicatedTraits.Entries)
	{
		if (Entry.CapabilityTag.IsValid())
		{
			OutCapabilities.AddTag(Entry.CapabilityTag);
		}
		OutCapabilities.AppendTags(Entry.ProvidedCapabilities);
	}
}

bool UEnt_EntityComponent::HasCapability_Implementation(FGameplayTag CapabilityTag) const
{
	if (!CapabilityTag.IsValid())
	{
		return false;
	}
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait && Trait->ProvidesCapability(CapabilityTag))
		{
			return true;
		}
	}

	// Client fallback: consult the replicated entries (covers a not-yet-reconciled client).
	for (const FEnt_TraitEntry& Entry : ReplicatedTraits.Entries)
	{
		if (Entry.CapabilityTag == CapabilityTag || Entry.ProvidedCapabilities.HasTagExact(CapabilityTag))
		{
			return true;
		}
	}
	return false;
}

UObject* UEnt_EntityComponent::GetCapabilityObject_Implementation(FGameplayTag CapabilityTag) const
{
	if (!CapabilityTag.IsValid())
	{
		return nullptr;
	}
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (Trait && Trait->ProvidesCapability(CapabilityTag))
		{
			// The trait itself is the backing object a consumer casts to a domain seam.
			return Trait;
		}
	}
	return nullptr;
}

//~ Persistence (ISeam_Persistable) -----------------------------------------------------------

FGameplayTag UEnt_EntityComponent::GetPersistenceKind_Implementation() const
{
	return EntNativeTags::Persist_Entity;
}

void UEnt_EntityComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FEnt_EntitySaveData Record;
	Record.EntityId = EntityId;
	Record.ArchetypeTag = ArchetypeTag;
	Record.TraitFragments.Reserve(LiveTraits.Num());

	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (!Trait)
		{
			continue;
		}
		FEnt_TraitSaveFragment Fragment;
		Fragment.TraitClassTag = GetTraitIdentityTag(Trait);
		UEnt_Trait::Execute_SaveState(Trait, Fragment.TraitState);
		Record.TraitFragments.Add(MoveTemp(Fragment));
	}

	Out.InitializeAs<FEnt_EntitySaveData>(Record);
}

void UEnt_EntityComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client-side load must be a no-op (state arrives via replication).
	if (!HasEntityAuthority())
	{
		return;
	}
	const FEnt_EntitySaveData* Record = In.GetPtr<FEnt_EntitySaveData>();
	if (!Record)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Entity] RestoreState got a payload that is not FEnt_EntitySaveData on '%s'."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// Restore identity verbatim so cross-system keys stay valid across the save/load boundary.
	if (Record->EntityId.IsValid())
	{
		EntityId = Record->EntityId;
	}
	ArchetypeTag = Record->ArchetypeTag;

	// Scatter each fragment back into the matching live trait (rebuilt from the archetype on BeginPlay).
	for (const FEnt_TraitSaveFragment& Fragment : Record->TraitFragments)
	{
		if (UEnt_Trait* Trait = FindTraitByTag(Fragment.TraitClassTag))
		{
			UEnt_Trait::Execute_RestoreState(Trait, Fragment.TraitState);
			SyncReplicatedEntryForTrait(Trait);
		}
		else
		{
			UE_LOG(LogDPSave, Verbose,
				TEXT("[Entity] RestoreState had a fragment for missing trait '%s' on '%s'."),
				*Fragment.TraitClassTag.ToString(), *GetNameSafe(GetOwner()));
		}
	}

	OnEntityChanged.Broadcast(this);
}

//~ Replication helpers -----------------------------------------------------------------------

void UEnt_EntityComponent::SyncReplicatedEntryForTrait(UEnt_Trait* Trait)
{
	if (!HasEntityAuthority() || !Trait)
	{
		return;
	}
	const FGameplayTag KindTag = GetTraitIdentityTag(Trait);

	FEnt_TraitEntry* Entry = KindTag.IsValid() ? ReplicatedTraits.FindByTraitClassTag(KindTag) : nullptr;
	if (!Entry)
	{
		Entry = &ReplicatedTraits.Entries.AddDefaulted_GetRef();
		Entry->TraitClassTag = KindTag;
	}

	Entry->CapabilityTag = Trait->CapabilityTag;

	// The trait's full advertised set, gathered via the capability seam (it appends, so reset first).
	Entry->ProvidedCapabilities.Reset();
	IEnt_CapabilityProvider::Execute_GetProvidedCapabilities(Trait, Entry->ProvidedCapabilities);

	// Traits carry no net-relevant scalar by contract, so StatePayload stays unset (Type == None).
	// The channel remains available for trait subclasses that mirror a value onto their entry.
	Entry->StatePayload = FSeam_NetValue();

	ReplicatedTraits.MarkItemDirty(*Entry);
}

void UEnt_EntityComponent::RebuildAllReplicatedEntries()
{
	if (!HasEntityAuthority())
	{
		return;
	}
	ReplicatedTraits.Entries.Reset();
	for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
	{
		if (!Trait)
		{
			continue;
		}
		FEnt_TraitEntry& Entry = ReplicatedTraits.Entries.AddDefaulted_GetRef();
		Entry.TraitClassTag = GetTraitIdentityTag(Trait);
		Entry.CapabilityTag = Trait->CapabilityTag;
		IEnt_CapabilityProvider::Execute_GetProvidedCapabilities(Trait, Entry.ProvidedCapabilities);
	}
	ReplicatedTraits.MarkArrayDirty();
}

void UEnt_EntityComponent::InitializeTrait(UEnt_Trait* Trait)
{
	if (Trait)
	{
		UEnt_Trait::Execute_OnTraitAdded(Trait, this);
	}
}

void UEnt_EntityComponent::UninitializeTrait(UEnt_Trait* Trait)
{
	if (Trait)
	{
		UEnt_Trait::Execute_OnTraitRemoved(Trait);
	}
}

void UEnt_EntityComponent::SetTraitStatePayload(FGameplayTag TraitTag, FSeam_NetValue Payload)
{
	// AUTHORITY GUARD: replicated trait state is server-authoritative.
	if (!HasEntityAuthority() || !TraitTag.IsValid())
	{
		return;
	}
	FEnt_TraitEntry* Entry = ReplicatedTraits.FindByTraitClassTag(TraitTag);
	if (!Entry)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Entity] SetTraitStatePayload: no replicated entry for trait '%s' on '%s'."),
			*TraitTag.ToString(), *GetNameSafe(GetOwner()));
		return;
	}
	if (Entry->StatePayload != Payload)
	{
		Entry->StatePayload = Payload;
		ReplicatedTraits.MarkItemDirty(*Entry);
	}
}

FSeam_NetValue UEnt_EntityComponent::GetTraitStatePayload(FGameplayTag TraitTag) const
{
	if (const FEnt_TraitEntry* Entry = ReplicatedTraits.FindByTraitClassTag(TraitTag))
	{
		return Entry->StatePayload;
	}
	return FSeam_NetValue();
}

void UEnt_EntityComponent::HandleReplicatedTraitChange()
{
	// Client: a trait entry was added/changed/removed. Reconcile the live mirror and notify.
	if (!HasEntityAuthority())
	{
		ReconcileLiveTraitsFromReplication();
	}
	OnEntityChanged.Broadcast(this);
}

void UEnt_EntityComponent::ReconcileLiveTraitsFromReplication()
{
	// Only clients rebuild from replication; authority is the source of truth.
	if (HasEntityAuthority())
	{
		return;
	}

	// Remove live traits no longer present in the replicated entries.
	for (int32 Index = LiveTraits.Num() - 1; Index >= 0; --Index)
	{
		UEnt_Trait* Trait = LiveTraits[Index];
		const FGameplayTag KindTag = GetTraitIdentityTag(Trait);
		if (!Trait || !ReplicatedTraits.FindByTraitClassTag(KindTag))
		{
			if (Trait)
			{
				UninitializeTrait(Trait);
			}
			LiveTraits.RemoveAt(Index);
		}
	}

	// Resolve the archetype's template list once so we can map a replicated entry's identity tag
	// back to a concrete trait class on the client.
	TArray<UEnt_Trait*> Templates;
	if (Archetype)
	{
		Archetype->GetResolvedDefaultTraits(Templates);
	}

	// Add a live trait for each replicated entry not already mirrored.
	for (const FEnt_TraitEntry& Entry : ReplicatedTraits.Entries)
	{
		if (!Entry.IsValidEntry())
		{
			continue;
		}
		if (FindTraitByTag(Entry.TraitClassTag))
		{
			continue;
		}

		// Find the matching archetype template (by identity tag) and duplicate it onto this client.
		UEnt_Trait* MatchTemplate = nullptr;
		for (UEnt_Trait* Template : Templates)
		{
			if (GetTraitIdentityTag(Template) == Entry.TraitClassTag)
			{
				MatchTemplate = Template;
				break;
			}
		}
		if (!MatchTemplate)
		{
			// No class mapping on the client (archetype unknown). The replicated entry still answers
			// capability queries via the fallback path, so this is acceptable.
			continue;
		}

		if (UEnt_Trait* NewTrait = DuplicateObject<UEnt_Trait>(MatchTemplate, this))
		{
			InitializeTrait(NewTrait);
			LiveTraits.Add(NewTrait);
		}
	}

	UpdateTickEnabled();
}

//~ Debug -------------------------------------------------------------------------------------

FString UEnt_EntityComponent::GetEntityDebugString() const
{
	return FString::Printf(TEXT("Entity[id=%s archetype=%s traits=%d auth=%s]"),
		*EntityId.ToString(),
		ArchetypeTag.IsValid() ? *ArchetypeTag.ToString() : TEXT("<none>"),
		LiveTraits.Num(),
		HasEntityAuthority() ? TEXT("server") : TEXT("client"));
}
