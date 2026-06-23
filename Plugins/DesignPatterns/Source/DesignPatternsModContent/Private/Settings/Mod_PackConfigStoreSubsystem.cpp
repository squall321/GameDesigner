// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Mod_PackConfigStoreSubsystem.h"
#include "Registry/Mod_ContentRegistrySubsystem.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Engine/GameInstance.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_PackConfigStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Make sure the locator/bus exist before we register so the save flow can discover us.
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	Collection.InitializeDependency(UDP_MessageBusSubsystem::StaticClass());

	// Register as a save participant under the persistence-kind key so a save object can enumerate us
	// (the core save object does not auto-discover GI subsystems). WeakObserved: the locator never
	// extends our lifetime (the GI owns it). This is purely the discovery hook; the store works standalone.
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(ModTags::Persist_PackConfig, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}

	UE_LOG(LogDP, Verbose, TEXT("ModContent: pack config store initialised."));
}

void UMod_PackConfigStoreSubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->UnregisterService(ModTags::Persist_PackConfig);
	}
	StoredByPack.Reset();
	Super::Deinitialize();
}

// =====================================================================================================
// Schema resolution / validation
// =====================================================================================================

const UMod_PackSettingsSchema* UMod_PackConfigStoreSubsystem::ResolveSchema(FGameplayTag PackId) const
{
	if (!PackId.IsValid())
	{
		return nullptr;
	}
	// Resolve through the mod-aware registry so a pack that overrides another's schema is honoured.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UMod_ContentRegistrySubsystem* Registry = GI->GetSubsystem<UMod_ContentRegistrySubsystem>())
		{
			return Registry->Resolve<UMod_PackSettingsSchema>(PackId);
		}
	}
	return nullptr;
}

FMod_PackSettingValue UMod_PackConfigStoreSubsystem::SanitiseAgainstField(
	const FMod_PackSettingField& Field, const FMod_PackSettingValue& In)
{
	FMod_PackSettingValue Out = In;
	Out.FieldId = Field.FieldId;

	// Numeric clamp is applied on read at the consumer side (the value struct shape is project-defined and
	// the store does not interpret it). What the store CAN do generically is guarantee the field id matches
	// the schema, and carry the value through unchanged otherwise. The HasClampRange() metadata travels with
	// the schema field so the consumer/UI clamps consistently. We keep the stored value faithful so a later
	// schema-clamp change re-applies cleanly rather than baking a stale clamp into saved data.
	return Out;
}

// =====================================================================================================
// Queries / mutation
// =====================================================================================================

bool UMod_PackConfigStoreSubsystem::GetSetting(FGameplayTag PackId, FGameplayTag FieldId, FMod_PackSettingValue& Out) const
{
	// 1) Explicit stored value wins.
	if (const FMod_PackSettingValueMap* PackMap = StoredByPack.Find(PackId))
	{
		if (const FMod_PackSettingValue* Stored = PackMap->Values.Find(FieldId))
		{
			Out = *Stored;
			return true;
		}
	}

	// 2) Otherwise synthesise the schema default.
	if (const UMod_PackSettingsSchema* Schema = ResolveSchema(PackId))
	{
		if (const FMod_PackSettingField* Field = Schema->FindField(FieldId))
		{
			Out.FieldId = FieldId;
			Out.Value = Field->DefaultValue;
			return true;
		}
	}

	return false;
}

bool UMod_PackConfigStoreSubsystem::SetSetting(FGameplayTag PackId, const FMod_PackSettingValue& Value)
{
	if (!PackId.IsValid() || !Value.FieldId.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: SetSetting ignored an invalid pack/field id."));
		return false;
	}

	FMod_PackSettingValue ToStore = Value;

	// If a schema is resolvable, validate the field exists and sanitise; if no schema is available (pack not
	// mounted yet) we still accept the value defensively so config persists across a not-yet-mounted pack.
	if (const UMod_PackSettingsSchema* Schema = ResolveSchema(PackId))
	{
		const FMod_PackSettingField* Field = Schema->FindField(Value.FieldId);
		if (!Field)
		{
			UE_LOG(LogDP, Warning, TEXT("ModContent: SetSetting rejected unknown field '%s' for pack '%s'."),
				*Value.FieldId.ToString(), *PackId.ToString());
			return false;
		}
		ToStore = SanitiseAgainstField(*Field, Value);
	}

	FMod_PackSettingValueMap& PackMap = StoredByPack.FindOrAdd(PackId);
	PackMap.Values.Add(Value.FieldId, ToStore);

	BroadcastSettingsChanged(PackId, Value.FieldId, /*bWholePackReset*/ false);
	return true;
}

void UMod_PackConfigStoreSubsystem::ResetPackToDefaults(FGameplayTag PackId)
{
	if (StoredByPack.Remove(PackId) > 0)
	{
		BroadcastSettingsChanged(PackId, FGameplayTag(), /*bWholePackReset*/ true);
	}
}

bool UMod_PackConfigStoreSubsystem::HasStoredValue(FGameplayTag PackId, FGameplayTag FieldId) const
{
	if (const FMod_PackSettingValueMap* PackMap = StoredByPack.Find(PackId))
	{
		return PackMap->Values.Contains(FieldId);
	}
	return false;
}

TArray<FGameplayTag> UMod_PackConfigStoreSubsystem::GetConfiguredPacks() const
{
	TArray<FGameplayTag> Out;
	StoredByPack.GetKeys(Out);
	return Out;
}

// =====================================================================================================
// ISeam_Persistable
// =====================================================================================================

void UMod_PackConfigStoreSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FMod_PackConfigSaveRecord Record;
	for (const TPair<FGameplayTag, FMod_PackSettingValueMap>& PackPair : StoredByPack)
	{
		for (const TPair<FGameplayTag, FMod_PackSettingValue>& FieldPair : PackPair.Value.Values)
		{
			Record.StoredPackIds.Add(PackPair.Key);
			Record.StoredValues.Add(FieldPair.Value);
		}
	}
	Out.InitializeAs<FMod_PackConfigSaveRecord>(Record);
}

void UMod_PackConfigStoreSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// UNCONDITIONAL: this is local per-machine config — an authority gate would wrongly drop a client's
	// chosen options. There is no authoritative/replicated state here to guard.
	const FMod_PackConfigSaveRecord* Record = In.GetPtr<FMod_PackConfigSaveRecord>();
	if (!Record)
	{
		return;
	}

	StoredByPack.Reset();

	const int32 Count = FMath::Min(Record->StoredPackIds.Num(), Record->StoredValues.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const FGameplayTag PackId = Record->StoredPackIds[i];
		const FMod_PackSettingValue& Value = Record->StoredValues[i];
		if (PackId.IsValid() && Value.FieldId.IsValid())
		{
			StoredByPack.FindOrAdd(PackId).Values.Add(Value.FieldId, Value);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("ModContent: pack config store restored %d value(s) across %d pack(s)."),
		Count, StoredByPack.Num());
}

FGameplayTag UMod_PackConfigStoreSubsystem::GetPersistenceKind_Implementation() const
{
	return ModTags::Persist_PackConfig;
}

// =====================================================================================================
// Bus / locator helpers / debug
// =====================================================================================================

void UMod_PackConfigStoreSubsystem::BroadcastSettingsChanged(FGameplayTag PackId, FGameplayTag FieldId, bool bWholePackReset) const
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}

	FMod_SettingsChangedEvent Event;
	Event.PackId = PackId;
	Event.FieldId = FieldId;
	Event.bWholePackReset = bWholePackReset;

	const FInstancedStruct Payload = FInstancedStruct::Make(Event);
	Bus->BroadcastPayload(ModTags::Bus_SettingsChanged, Payload, const_cast<UMod_PackConfigStoreSubsystem*>(this));
}

UDP_MessageBusSubsystem* UMod_PackConfigStoreSubsystem::GetBus() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_MessageBusSubsystem>();
	}
	return nullptr;
}

UDP_ServiceLocatorSubsystem* UMod_PackConfigStoreSubsystem::GetLocator() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	}
	return nullptr;
}

FString UMod_PackConfigStoreSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalValues = 0;
	for (const TPair<FGameplayTag, FMod_PackSettingValueMap>& Pair : StoredByPack)
	{
		TotalValues += Pair.Value.Values.Num();
	}
	return FString::Printf(TEXT("PackConfigStore: %d pack(s), %d stored value(s)"), StoredByPack.Num(), TotalValues);
}
