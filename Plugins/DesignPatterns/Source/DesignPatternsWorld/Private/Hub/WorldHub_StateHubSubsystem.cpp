// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_GameStateHubSubsystem.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "Registry/WorldHub_FlagSetDataAsset.h"
#include "Replication/WorldHub_StateRepComponent.h"
#include "Blackboard/WorldHub_ScopedBlackboard.h"
#include "Save/WorldHub_Snapshot.h"
#include "WorldHub_NativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

namespace WorldHub_HubOps
{
	/** Project a flag value to its net form given the value type, mirroring the registry's logic. */
	static bool ProjectToNet(EWorldHub_FlagValueType ValueType, const FInstancedStruct& Value, FSeam_NetValue& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		switch (ValueType)
		{
		case EWorldHub_FlagValueType::Bool:    Out = FSeam_NetValue::MakeBool(Value.Get<bool>()); return true;
		case EWorldHub_FlagValueType::Int:
		case EWorldHub_FlagValueType::Counter: Out = FSeam_NetValue::MakeInt(Value.Get<int64>()); return true;
		case EWorldHub_FlagValueType::Float:   Out = FSeam_NetValue::MakeFloat(Value.Get<double>()); return true;
		case EWorldHub_FlagValueType::Vector:
			if (Value.GetScriptStruct() == TBaseStructure<FVector>::Get()) { Out = FSeam_NetValue::MakeVector(Value.Get<FVector>()); return true; }
			return false;
		case EWorldHub_FlagValueType::Tag:
			if (Value.GetScriptStruct() == TBaseStructure<FGameplayTag>::Get()) { Out = FSeam_NetValue::MakeTag(Value.Get<FGameplayTag>()); return true; }
			return false;
		case EWorldHub_FlagValueType::Name:    Out = FSeam_NetValue::MakeName(Value.Get<FName>()); return true;
		case EWorldHub_FlagValueType::Struct:
		default:                               return false;
		}
	}
}

//~ USubsystem lifecycle -----------------------------------------------------------------------

void UWorldHub_StateHubSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Own the registry as an instanced subobject so its FInstancedStruct values are GC-visible.
	Registry = NewObject<UWorldHub_FlagRegistry>(this, TEXT("WorldHub_FlagRegistry"));

	RegisterSelfAsService();

	UE_LOG(LogDP, Log, TEXT("[WorldHub] State hub initialized (authority=%s)."),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}

void UWorldHub_StateHubSubsystem::Deinitialize()
{
	// Carriers are world-lifetime; ensure no dangling reference outlives the world.
	NetCarrier.Reset();
	Providers.Reset();
	ProviderIndexByKey.Reset();
	Blackboards.Reset();
	Registry = nullptr;

	Super::Deinitialize();
}

void UWorldHub_StateHubSubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and must not keep a dead world's hub alive.
		Locator->RegisterService(WorldHubNativeTags::Service_WorldHub, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ IWorldHub_Queryable ------------------------------------------------------------------------

bool UWorldHub_StateHubSubsystem::QueryValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FInstancedStruct& Out) const
{
	if (!Key.IsValid() || !Registry)
	{
		return false;
	}

	// 1) Exact scope.
	FWorldHub_FlagValue Value;
	if (Registry->GetValue(Key, Scope, Value) && Value.Value.IsValid())
	{
		Out = Value.Value;
		return true;
	}

	// 2) Scope fallback chain (Entity/Faction -> Global).
	FWorldHub_Scope Parent;
	FWorldHub_Scope Cursor = Scope;
	while (Cursor.GetFallbackScope(Parent))
	{
		if (Registry->GetValue(Key, Parent, Value) && Value.Value.IsValid())
		{
			Out = Value.Value;
			return true;
		}
		Cursor = Parent;
	}

	// 3) Provider fallback (computed values).
	if (QueryProviders(Key, Scope, Out))
	{
		return true;
	}

	// 4) Definition default (so a defined-but-unset flag still reads its authored default).
	if (const FWorldHub_FlagDefinition* Def = Registry->FindDefinition(Key))
	{
		if (Def->DefaultValue.IsValid())
		{
			Out = Def->DefaultValue;
			return true;
		}
	}

	return false;
}

bool UWorldHub_StateHubSubsystem::HasValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope) const
{
	if (!Key.IsValid() || !Registry)
	{
		return false;
	}

	// "Has value" means a CONCRETE stored value (not a default), with scope fallback.
	if (Registry->HasValue(Key, Scope))
	{
		return true;
	}
	FWorldHub_Scope Parent;
	FWorldHub_Scope Cursor = Scope;
	while (Cursor.GetFallbackScope(Parent))
	{
		if (Registry->HasValue(Key, Parent))
		{
			return true;
		}
		Cursor = Parent;
	}
	return false;
}

bool UWorldHub_StateHubSubsystem::QueryProviders(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FInstancedStruct& Out) const
{
	const TArray<int32>* Indices = ProviderIndexByKey.Find(Key);
	if (!Indices)
	{
		return false;
	}

	for (int32 ProviderIndex : *Indices)
	{
		if (!Providers.IsValidIndex(ProviderIndex))
		{
			continue;
		}
		const TWeakInterfacePtr<IWorldHub_StateProvider>& Weak = Providers[ProviderIndex];
		IWorldHub_StateProvider* Provider = Weak.Get();
		if (!Provider)
		{
			continue;
		}

		// Gate by the provider's scope tag: a faction-scoped provider only answers matching scopes.
		const FGameplayTag ProviderScopeTag = Provider->GetProviderScopeTag();
		if (ProviderScopeTag.IsValid())
		{
			const bool bScopeMatches =
				(Scope.ScopeType == EWorldHub_ScopeType::Faction && Scope.FactionTag == ProviderScopeTag);
			if (!bScopeMatches)
			{
				continue;
			}
		}

		if (Provider->ProvideValue(Key, Out))
		{
			return true;
		}
	}
	return false;
}

//~ BP query convenience -----------------------------------------------------------------------

bool UWorldHub_StateHubSubsystem::QueryFlag(FGameplayTag Key, FWorldHub_Scope Scope, bool bDefault) const
{
	FInstancedStruct Value;
	if (QueryValue(Key, Scope, Value) && Value.IsValid())
	{
		return Value.Get<bool>();
	}
	return bDefault;
}

int64 UWorldHub_StateHubSubsystem::QueryCounter(FGameplayTag Key, FWorldHub_Scope Scope, int64 Default) const
{
	FInstancedStruct Value;
	if (QueryValue(Key, Scope, Value) && Value.IsValid())
	{
		return Value.Get<int64>();
	}
	return Default;
}

//~ Authoritative mutators ---------------------------------------------------------------------

bool UWorldHub_StateHubSubsystem::ProjectSlotToNet(const FGameplayTag& Key, const FWorldHub_FlagValue& Value, FSeam_NetValue& Out) const
{
	if (const FWorldHub_FlagDefinition* Def = Registry ? Registry->FindDefinition(Key) : nullptr)
	{
		if (WorldHub_HubOps::ProjectToNet(Def->ValueType, Value.Value, Out))
		{
			return true;
		}
	}
	bool bOk = false;
	Out = FSeam_NetValue::FromInstancedStruct(Value.Value, bOk);
	return bOk;
}

void UWorldHub_StateHubSubsystem::ApplyAuthoritativeWrite(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value)
{
	if (!Registry || !Key.IsValid())
	{
		return;
	}

	if (!Registry->SetValue(Key, Scope, Value))
	{
		return; // no change.
	}

	// Re-read the stored (clamped/policy-applied) slot so mirror + notify reflect what was stored.
	FWorldHub_FlagValue Stored;
	Registry->GetValue(Key, Scope, Stored);

	FSeam_NetValue Net;
	const bool bProjected = ProjectSlotToNet(Key, Stored, Net) && Net.IsSet();

	if (Stored.bReplicate && bProjected)
	{
		PushSlotToCarrier(Scope, Key, Stored);
	}

	NotifyValueChanged(Scope, Key, bProjected ? Net : FSeam_NetValue());
}

void UWorldHub_StateHubSubsystem::SetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}
	ApplyAuthoritativeWrite(Key, Scope, Value);
}

void UWorldHub_StateHubSubsystem::SetFlag(FGameplayTag Key, bool bValue, FWorldHub_Scope Scope)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}
	FWorldHub_FlagValue Value;
	Value.Value.InitializeAs<bool>(bValue);
	ApplyAuthoritativeWrite(Key, Scope, Value);
}

int64 UWorldHub_StateHubSubsystem::IncrementCounter(FGameplayTag Key, int64 Delta, FWorldHub_Scope Scope)
{
	// AUTHORITY GUARD at the TOP. On clients return the locally-mirrored current value.
	if (!HasWorldAuthority())
	{
		return Registry ? Registry->GetCounter(Key, 0, Scope) : 0;
	}
	if (!Registry)
	{
		return 0;
	}

	const int64 NewValue = Registry->IncrementCounter(Key, Delta, Scope);

	FWorldHub_FlagValue Stored;
	Registry->GetValue(Key, Scope, Stored);
	if (Stored.bReplicate)
	{
		PushSlotToCarrier(Scope, Key, Stored);
	}
	NotifyValueChanged(Scope, Key, FSeam_NetValue::MakeInt(NewValue));
	return NewValue;
}

void UWorldHub_StateHubSubsystem::SetNetValue(FGameplayTag Key, FSeam_NetValue Value, FWorldHub_Scope Scope)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}
	FWorldHub_FlagValue Slot;
	if (!Value.ToInstancedStruct(Slot.Value))
	{
		return;
	}
	ApplyAuthoritativeWrite(Key, Scope, Slot);
}

void UWorldHub_StateHubSubsystem::SetVariable(FGameplayTag Key, FInstancedStruct Value, FWorldHub_Scope Scope)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}
	FWorldHub_FlagValue Slot;
	Slot.Value = Value;
	ApplyAuthoritativeWrite(Key, Scope, Slot);
}

void UWorldHub_StateHubSubsystem::ClearValue(FGameplayTag Key, FWorldHub_Scope Scope)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority() || !Registry)
	{
		return;
	}
	if (Registry->ClearValue(Key, Scope))
	{
		RemoveSlotFromCarrier(Scope, Key);
		NotifyValueChanged(Scope, Key, FSeam_NetValue());
	}
}

bool UWorldHub_StateHubSubsystem::GetVariable(FGameplayTag Key, FWorldHub_Scope Scope, FInstancedStruct& Out) const
{
	return Registry && Registry->GetVariable(Key, Out, Scope);
}

//~ Carrier mirroring --------------------------------------------------------------------------

void UWorldHub_StateHubSubsystem::PushSlotToCarrier(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FWorldHub_FlagValue& Value)
{
	UWorldHub_StateRepComponent* Carrier = NetCarrier.Get();
	if (!Carrier)
	{
		return;
	}
	FSeam_NetValue Net;
	if (ProjectSlotToNet(Key, Value, Net) && Net.IsSet())
	{
		Carrier->Authority_SetEntry(Scope, Key, Net);
	}
}

void UWorldHub_StateHubSubsystem::RemoveSlotFromCarrier(const FWorldHub_Scope& Scope, const FGameplayTag& Key)
{
	if (UWorldHub_StateRepComponent* Carrier = NetCarrier.Get())
	{
		Carrier->Authority_RemoveEntry(Scope, Key);
	}
}

void UWorldHub_StateHubSubsystem::NotifyValueChanged(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& NewValue)
{
	OnValueChanged.Broadcast(Scope, Key, NewValue);

	if (bRepublishOnBus)
	{
		if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			// FLAT payload, no weak refs — safe to queue for deferred dispatch.
			FWorldHub_ValueChangedPayload Payload(Scope, Key, NewValue);
			FInstancedStruct Wrapped;
			Wrapped.InitializeAs<FWorldHub_ValueChangedPayload>(Payload);
			Bus->BroadcastPayload(WorldHubNativeTags::Bus_WorldHub_FlagChanged, Wrapped, this);
		}
	}
}

//~ Scoped blackboards -------------------------------------------------------------------------

/**
 * Cross-area dispatch hook DECLARED by the blackboard area and DEFINED here (the subsystem area).
 * Routes a scoped-blackboard change into the hub's change pipeline without the blackboard area
 * hard-depending on this subsystem's concrete type.
 */
void WorldHub_DispatchScopedBlackboardChanged(UWorldHub_StateHubSubsystem* Sink, const FWorldHub_Scope& Scope, FName Key)
{
	if (Sink)
	{
		Sink->OnScopedBlackboardChanged(Scope, Key);
	}
}

UWorldHub_ScopedBlackboard* UWorldHub_StateHubSubsystem::GetBlackboard(FWorldHub_Scope Scope, bool bCreate)
{
	if (TObjectPtr<UWorldHub_ScopedBlackboard>* Found = Blackboards.Find(Scope))
	{
		return Found->Get();
	}
	if (!bCreate)
	{
		return nullptr;
	}

	UWorldHub_ScopedBlackboard* Board = NewObject<UWorldHub_ScopedBlackboard>(this);
	Board->InitializeScopedBlackboard(Scope);
	Board->SetChangeSink(this);
	Blackboards.Add(Scope, Board);
	return Board;
}

bool UWorldHub_StateHubSubsystem::ClearBlackboard(FWorldHub_Scope Scope)
{
	if (TObjectPtr<UWorldHub_ScopedBlackboard>* Found = Blackboards.Find(Scope))
	{
		if (UWorldHub_ScopedBlackboard* Board = Found->Get())
		{
			Board->SetChangeSink(nullptr);
		}
		Blackboards.Remove(Scope);
		return true;
	}
	return false;
}

void UWorldHub_StateHubSubsystem::OnScopedBlackboardChanged(const FWorldHub_Scope& Scope, FName Key)
{
	if (!bRepublishOnBus)
	{
		return;
	}
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// Carry the scope and the (FName) key as a tag-less name value in a flat payload.
		FWorldHub_ValueChangedPayload Payload(Scope, FGameplayTag(), FSeam_NetValue::MakeName(Key));
		FInstancedStruct Wrapped;
		Wrapped.InitializeAs<FWorldHub_ValueChangedPayload>(Payload);
		Bus->BroadcastPayload(WorldHubNativeTags::Bus_WorldHub_BlackboardChanged, Wrapped, this);
	}
}

//~ State providers ----------------------------------------------------------------------------

void UWorldHub_StateHubSubsystem::RegisterStateProvider(const TScriptInterface<IWorldHub_StateProvider>& Provider)
{
	IWorldHub_StateProvider* Raw = Provider.GetInterface();
	if (!Raw)
	{
		return;
	}
	const TWeakInterfacePtr<IWorldHub_StateProvider> Weak(Raw);
	if (Providers.Contains(Weak))
	{
		return;
	}
	Providers.Add(Weak);
	RebuildProviderIndex();
}

void UWorldHub_StateHubSubsystem::UnregisterStateProvider(const TScriptInterface<IWorldHub_StateProvider>& Provider)
{
	const TWeakInterfacePtr<IWorldHub_StateProvider> Weak(Provider.GetInterface());
	if (Providers.Remove(Weak) > 0)
	{
		RebuildProviderIndex();
	}
}

void UWorldHub_StateHubSubsystem::RebuildProviderIndex()
{
	ProviderIndexByKey.Reset();

	// Drop stale weak entries first.
	for (int32 i = Providers.Num() - 1; i >= 0; --i)
	{
		if (!Providers[i].IsValid())
		{
			Providers.RemoveAt(i);
		}
	}

	for (int32 i = 0; i < Providers.Num(); ++i)
	{
		IWorldHub_StateProvider* Provider = Providers[i].Get();
		if (!Provider)
		{
			continue;
		}
		TArray<FGameplayTag> Keys;
		Provider->CollectProvidedKeys(Keys);
		for (const FGameplayTag& Key : Keys)
		{
			if (Key.IsValid())
			{
				ProviderIndexByKey.FindOrAdd(Key).AddUnique(i);
			}
		}
	}
}

//~ Replication carrier wiring -----------------------------------------------------------------

void UWorldHub_StateHubSubsystem::AttachNetCarrier(UWorldHub_StateRepComponent* Carrier)
{
	NetCarrier = Carrier;
}

void UWorldHub_StateHubSubsystem::DetachNetCarrier(UWorldHub_StateRepComponent* Carrier)
{
	if (NetCarrier.Get() == Carrier)
	{
		NetCarrier.Reset();
	}
}

void UWorldHub_StateHubSubsystem::BuildRepState(FWorldHub_RepState& Out) const
{
	Out.Entries.Reset();
	if (Registry)
	{
		Registry->GetReplicatedEntries(Out.Entries);
	}
}

void UWorldHub_StateHubSubsystem::SyncReplicatedState(const FWorldHub_RepState& State)
{
	// CLIENT path only: the authority owns the source of truth and never overwrites itself.
	if (HasWorldAuthority() || !Registry)
	{
		return;
	}

	// Apply each entry, tracking which (Scope, Key) survive so we can prune removed replicated slots.
	TSet<TPair<FWorldHub_Scope, FGameplayTag>> Seen;
	Seen.Reserve(State.Entries.Num());

	for (const FWorldHub_ScopedRepEntry& Entry : State.Entries)
	{
		Seen.Add(TPair<FWorldHub_Scope, FGameplayTag>(Entry.Scope, Entry.Key));
		if (Registry->ApplyReplicatedEntry(Entry.Scope, Entry.Key, Entry.Value))
		{
			NotifyValueChanged(Entry.Scope, Entry.Key, Entry.Value);
		}
	}

	// Anything replicated previously but absent now was cleared on the server.
	Registry->PruneReplicatedSlotsNotIn(Seen);
}

//~ Defaults & flush ---------------------------------------------------------------------------

void UWorldHub_StateHubSubsystem::LoadDefaults(UWorldHub_FlagSetDataAsset* FlagSet, bool bOverwriteExisting)
{
	if (!Registry || !FlagSet)
	{
		return;
	}

	// Definitions are local typing metadata and load regardless of authority so clients project and
	// clamp mirrored state correctly. Seeding VALUES into slots is an authoritative write, so on a
	// client we deliberately never overwrite (the server's seeded values arrive via replication).
	if (!HasWorldAuthority())
	{
		Registry->LoadDefaultsFrom(FlagSet, /*bOverwriteExisting=*/false);
		return;
	}

	Registry->LoadDefaultsFrom(FlagSet, bOverwriteExisting);

	// Mirror all newly-seeded replicable slots to the carrier so clients receive the defaults.
	if (UWorldHub_StateRepComponent* Carrier = NetCarrier.Get())
	{
		FWorldHub_RepState State;
		BuildRepState(State);
		Carrier->Authority_ReplaceAll(State.Entries);
	}
}

void UWorldHub_StateHubSubsystem::FlushSaveStateTo(UWorldHub_GameStateHubSubsystem* GameStateHub)
{
	// AUTHORITY GUARD at the TOP: only the server's world state is canonical for persistence.
	if (!HasWorldAuthority() || !GameStateHub)
	{
		return;
	}
	GameStateHub->ReceiveFlush(this);
}

//~ ISeam_Persistable --------------------------------------------------------------------------

void UWorldHub_StateHubSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FWorldHub_Snapshot Snapshot;
	if (Registry)
	{
		TArray<UWorldHub_FlagRegistry::FSlotRecord> Records;
		Registry->CaptureSaveSlots(Records);
		Snapshot.Entries.Reserve(Records.Num());
		for (const UWorldHub_FlagRegistry::FSlotRecord& Record : Records)
		{
			Snapshot.Entries.Emplace(Record.Scope, Record.Key, Record.Value);
		}
	}
	Out.InitializeAs<FWorldHub_Snapshot>(Snapshot);
}

void UWorldHub_StateHubSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client-side load must be a no-op (state arrives via replication).
	if (!HasWorldAuthority() || !Registry)
	{
		return;
	}
	if (!In.IsValid() || In.GetScriptStruct() != FWorldHub_Snapshot::StaticStruct())
	{
		return;
	}

	const FWorldHub_Snapshot& Snapshot = In.Get<FWorldHub_Snapshot>();

	TArray<UWorldHub_FlagRegistry::FSlotRecord> Records;
	Records.Reserve(Snapshot.Entries.Num());
	for (const FWorldHub_SnapshotEntry& Entry : Snapshot.Entries)
	{
		UWorldHub_FlagRegistry::FSlotRecord& Record = Records.AddDefaulted_GetRef();
		Record.Scope = Entry.Scope;
		Record.Key = Entry.Key;
		Record.Value = Entry.Value;
	}
	Registry->RestoreSaveSlots(Records);

	// Re-seed the carrier so clients receive the restored replicable state.
	if (UWorldHub_StateRepComponent* Carrier = NetCarrier.Get())
	{
		FWorldHub_RepState State;
		BuildRepState(State);
		Carrier->Authority_ReplaceAll(State.Entries);
	}
}

FGameplayTag UWorldHub_StateHubSubsystem::GetPersistenceKind_Implementation() const
{
	// Route the hub's save record under its service tag; the save object keys records by this.
	return WorldHubNativeTags::Service_WorldHub;
}

//~ Debug --------------------------------------------------------------------------------------

FString UWorldHub_StateHubSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("WorldHub [%s] %s | Blackboards=%d Providers=%d Carrier=%s"),
		HasWorldAuthority() ? TEXT("AUTH") : TEXT("client"),
		Registry ? *Registry->ToDebugString() : TEXT("<no registry>"),
		Blackboards.Num(),
		Providers.Num(),
		NetCarrier.IsValid() ? TEXT("attached") : TEXT("none"));
}
