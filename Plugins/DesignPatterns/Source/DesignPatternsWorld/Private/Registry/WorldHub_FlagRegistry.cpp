// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/WorldHub_FlagRegistry.h"
#include "Registry/WorldHub_FlagSetDataAsset.h"
#include "Core/DPLog.h"

namespace WorldHub_FlagValueOps
{
	/**
	 * Read a net-friendly FSeam_NetValue projection out of an FInstancedStruct, given the authored
	 * value type. The Seam's own FromInstancedStruct only reverses FVector/FGameplayTag reliably;
	 * primitive wrappers (bool/int64/double/FName produced by ToInstancedStruct's InitializeAs<T>)
	 * are read back here by their concrete inner type, which we know from the definition.
	 *
	 * @return true and fills Out for replicable types with a populated payload; false otherwise.
	 */
	static bool ProjectToNet(EWorldHub_FlagValueType ValueType, const FInstancedStruct& Value, FSeam_NetValue& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const UScriptStruct* Struct = Value.GetScriptStruct();
		switch (ValueType)
		{
		case EWorldHub_FlagValueType::Bool:
			Out = FSeam_NetValue::MakeBool(Value.Get<bool>());
			return true;
		case EWorldHub_FlagValueType::Int:
		case EWorldHub_FlagValueType::Counter:
			Out = FSeam_NetValue::MakeInt(Value.Get<int64>());
			return true;
		case EWorldHub_FlagValueType::Float:
			Out = FSeam_NetValue::MakeFloat(Value.Get<double>());
			return true;
		case EWorldHub_FlagValueType::Vector:
			if (Struct == TBaseStructure<FVector>::Get())
			{
				Out = FSeam_NetValue::MakeVector(Value.Get<FVector>());
				return true;
			}
			return false;
		case EWorldHub_FlagValueType::Tag:
			if (Struct == TBaseStructure<FGameplayTag>::Get())
			{
				Out = FSeam_NetValue::MakeTag(Value.Get<FGameplayTag>());
				return true;
			}
			return false;
		case EWorldHub_FlagValueType::Name:
			Out = FSeam_NetValue::MakeName(Value.Get<FName>());
			return true;
		case EWorldHub_FlagValueType::Struct:
		default:
			return false;
		}
	}

	/**
	 * Best-effort projection when no definition is known: infer the net type from the FInstancedStruct
	 * itself. Only FVector/FGameplayTag are unambiguously recoverable from the struct identity; other
	 * inner primitive wrappers are not reliably distinguishable, so they stay local-only here.
	 */
	static bool ProjectToNetInferred(const FInstancedStruct& Value, FSeam_NetValue& Out)
	{
		bool bOk = false;
		Out = FSeam_NetValue::FromInstancedStruct(Value, bOk);
		return bOk;
	}

	/** Read a slot's int64 payload (for counters), or Default when not an int wrapper. */
	static int64 ReadInt(const FInstancedStruct& Value, int64 Default)
	{
		return Value.IsValid() ? Value.Get<int64>() : Default;
	}

	/** Read a slot's bool payload, or Default when not a bool wrapper. */
	static bool ReadBool(const FInstancedStruct& Value, bool Default)
	{
		return Value.IsValid() ? Value.Get<bool>() : Default;
	}
}

void UWorldHub_FlagRegistry::MakeZeroValue(EWorldHub_FlagValueType ValueType, FInstancedStruct& Out)
{
	switch (ValueType)
	{
	case EWorldHub_FlagValueType::Bool:    Out.InitializeAs<bool>(false); break;
	case EWorldHub_FlagValueType::Int:     Out.InitializeAs<int64>(0); break;
	case EWorldHub_FlagValueType::Counter: Out.InitializeAs<int64>(0); break;
	case EWorldHub_FlagValueType::Float:   Out.InitializeAs<double>(0.0); break;
	case EWorldHub_FlagValueType::Vector:  Out.InitializeAs<FVector>(FVector::ZeroVector); break;
	case EWorldHub_FlagValueType::Tag:     Out.InitializeAs<FGameplayTag>(FGameplayTag()); break;
	case EWorldHub_FlagValueType::Name:    Out.InitializeAs<FName>(NAME_None); break;
	case EWorldHub_FlagValueType::Struct:
	default:                               Out.Reset(); break;
	}
}

const FWorldHub_FlagDefinition* UWorldHub_FlagRegistry::FindDefinition(const FGameplayTag& Key) const
{
	return Definitions.Find(Key);
}

void UWorldHub_FlagRegistry::ClampIfCounter(const FGameplayTag& Key, FWorldHub_FlagValue& Value) const
{
	const FWorldHub_FlagDefinition* Def = FindDefinition(Key);
	if (Def && Def->ValueType == EWorldHub_FlagValueType::Counter && Value.Value.IsValid())
	{
		const int64 Raw = Value.Value.Get<int64>();
		const int64 Clamped = Def->ClampCounter(Raw);
		if (Clamped != Raw)
		{
			Value.Value.InitializeAs<int64>(Clamped);
		}
	}
}

bool UWorldHub_FlagRegistry::SlotReplicates(const FGameplayTag& Key, const FWorldHub_FlagValue& Value) const
{
	if (const FWorldHub_FlagDefinition* Def = FindDefinition(Key))
	{
		return Def->ShouldReplicate();
	}
	return Value.bReplicate;
}

bool UWorldHub_FlagRegistry::SlotSaves(const FGameplayTag& Key, const FWorldHub_FlagValue& Value) const
{
	if (const FWorldHub_FlagDefinition* Def = FindDefinition(Key))
	{
		return Def->bSave;
	}
	return Value.bSave;
}

int32 UWorldHub_FlagRegistry::LoadDefaultsFrom(const UWorldHub_FlagSetDataAsset* FlagSet, bool bOverwriteExisting)
{
	if (!FlagSet)
	{
		return 0;
	}

	int32 Applied = 0;
	for (const FWorldHub_FlagDefinition& Def : FlagSet->Definitions)
	{
		if (!Def.Key.IsValid())
		{
			UE_LOG(LogDP, Warning, TEXT("[WorldHub] FlagSet '%s' has a definition with an invalid key; skipping."),
				*FlagSet->GetName());
			continue;
		}

		// Index the definition (last duplicate wins, matching the asset's own rule).
		Definitions.Add(Def.Key, Def);

		// Seed the Global-scope slot unless one already exists (and we are not forcing).
		const FSlotKey SlotKey{ FWorldHub_Scope::Global(), Def.Key };
		const bool bExists = Slots.Contains(SlotKey);
		if (bExists && !bOverwriteExisting)
		{
			++Applied;
			continue;
		}

		FWorldHub_FlagValue Seed;
		Seed.bReplicate = Def.ShouldReplicate();
		Seed.bSave = Def.bSave;
		if (Def.DefaultValue.IsValid())
		{
			Seed.Value = Def.DefaultValue;
		}
		else
		{
			MakeZeroValue(Def.ValueType, Seed.Value);
		}
		ClampIfCounter(Def.Key, Seed);

		Slots.Add(SlotKey, Seed);
		++Applied;
	}

	UE_LOG(LogDP, Verbose, TEXT("[WorldHub] Loaded %d flag definitions from '%s'."), Applied, *FlagSet->GetName());
	return Applied;
}

bool UWorldHub_FlagRegistry::GetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FWorldHub_FlagValue& Out) const
{
	if (const FWorldHub_FlagValue* Found = Slots.Find(FSlotKey{ Scope, Key }))
	{
		Out = *Found;
		return true;
	}
	return false;
}

bool UWorldHub_FlagRegistry::SetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value)
{
	if (!Key.IsValid())
	{
		return false;
	}

	FWorldHub_FlagValue NewValue = Value;
	// Apply definition-driven policy so slots set directly still carry the right flags.
	if (const FWorldHub_FlagDefinition* Def = FindDefinition(Key))
	{
		NewValue.bReplicate = Def->ShouldReplicate();
		NewValue.bSave = Def->bSave;
	}
	ClampIfCounter(Key, NewValue);

	const FSlotKey SlotKey{ Scope, Key };
	if (FWorldHub_FlagValue* Existing = Slots.Find(SlotKey))
	{
		if (Existing->Value.Identical(&NewValue.Value, PPF_None)
			&& Existing->bReplicate == NewValue.bReplicate
			&& Existing->bSave == NewValue.bSave)
		{
			return false;
		}
		*Existing = NewValue;
		return true;
	}

	Slots.Add(SlotKey, NewValue);
	return true;
}

bool UWorldHub_FlagRegistry::HasValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope) const
{
	const FWorldHub_FlagValue* Found = Slots.Find(FSlotKey{ Scope, Key });
	return Found && Found->IsSet();
}

bool UWorldHub_FlagRegistry::ClearValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope)
{
	return Slots.Remove(FSlotKey{ Scope, Key }) > 0;
}

void UWorldHub_FlagRegistry::ResetValues()
{
	Slots.Reset();
}

void UWorldHub_FlagRegistry::ResetAll()
{
	Slots.Reset();
	Definitions.Reset();
}

bool UWorldHub_FlagRegistry::SetFlag(const FGameplayTag& Key, bool bValue, const FWorldHub_Scope& Scope)
{
	FWorldHub_FlagValue NewValue;
	NewValue.Value.InitializeAs<bool>(bValue);
	return SetValue(Key, Scope, NewValue);
}

bool UWorldHub_FlagRegistry::GetFlag(const FGameplayTag& Key, bool bDefault, const FWorldHub_Scope& Scope) const
{
	FWorldHub_FlagValue Value;
	if (GetValue(Key, Scope, Value) && Value.Value.IsValid())
	{
		return WorldHub_FlagValueOps::ReadBool(Value.Value, bDefault);
	}
	return bDefault;
}

int64 UWorldHub_FlagRegistry::IncrementCounter(const FGameplayTag& Key, int64 Delta, const FWorldHub_Scope& Scope)
{
	const int64 Current = GetCounter(Key, 0, Scope);
	int64 Next = Current + Delta;
	if (const FWorldHub_FlagDefinition* Def = FindDefinition(Key))
	{
		Next = Def->ClampCounter(Next);
	}

	FWorldHub_FlagValue NewValue;
	NewValue.Value.InitializeAs<int64>(Next);
	SetValue(Key, Scope, NewValue);
	return Next;
}

int64 UWorldHub_FlagRegistry::GetCounter(const FGameplayTag& Key, int64 Default, const FWorldHub_Scope& Scope) const
{
	FWorldHub_FlagValue Value;
	if (GetValue(Key, Scope, Value) && Value.Value.IsValid())
	{
		return WorldHub_FlagValueOps::ReadInt(Value.Value, Default);
	}
	return Default;
}

bool UWorldHub_FlagRegistry::SetVariable(const FGameplayTag& Key, const FInstancedStruct& Value, const FWorldHub_Scope& Scope)
{
	FWorldHub_FlagValue NewValue;
	NewValue.Value = Value;
	return SetValue(Key, Scope, NewValue);
}

bool UWorldHub_FlagRegistry::GetVariable(const FGameplayTag& Key, FInstancedStruct& Out, const FWorldHub_Scope& Scope) const
{
	FWorldHub_FlagValue Value;
	if (GetValue(Key, Scope, Value) && Value.Value.IsValid())
	{
		Out = Value.Value;
		return true;
	}
	return false;
}

void UWorldHub_FlagRegistry::CaptureSaveSlots(TArray<FSlotRecord>& Out) const
{
	Out.Reserve(Out.Num() + Slots.Num());
	for (const TPair<FSlotKey, FWorldHub_FlagValue>& Pair : Slots)
	{
		if (!SlotSaves(Pair.Key.Key, Pair.Value))
		{
			continue;
		}
		FSlotRecord& Record = Out.AddDefaulted_GetRef();
		Record.Scope = Pair.Key.Scope;
		Record.Key = Pair.Key.Key;
		Record.Value = Pair.Value;
	}
}

void UWorldHub_FlagRegistry::RestoreSaveSlots(const TArray<FSlotRecord>& Records)
{
	// Remove every save-bearing slot first so restore is authoritative for the saved set, then apply.
	for (auto It = Slots.CreateIterator(); It; ++It)
	{
		if (SlotSaves(It.Key().Key, It.Value()))
		{
			It.RemoveCurrent();
		}
	}

	for (const FSlotRecord& Record : Records)
	{
		if (!Record.Key.IsValid())
		{
			continue;
		}
		FWorldHub_FlagValue Value = Record.Value;
		ClampIfCounter(Record.Key, Value);
		Slots.Add(FSlotKey{ Record.Scope, Record.Key }, Value);
	}
}

void UWorldHub_FlagRegistry::GetReplicatedEntries(TArray<FWorldHub_ScopedRepEntry>& Out) const
{
	Out.Reserve(Out.Num() + Slots.Num());
	for (const TPair<FSlotKey, FWorldHub_FlagValue>& Pair : Slots)
	{
		if (!SlotReplicates(Pair.Key.Key, Pair.Value) || !Pair.Value.Value.IsValid())
		{
			continue;
		}

		FSeam_NetValue Net;
		bool bProjected = false;
		if (const FWorldHub_FlagDefinition* Def = FindDefinition(Pair.Key.Key))
		{
			bProjected = WorldHub_FlagValueOps::ProjectToNet(Def->ValueType, Pair.Value.Value, Net);
		}
		if (!bProjected)
		{
			bProjected = WorldHub_FlagValueOps::ProjectToNetInferred(Pair.Value.Value, Net);
		}
		if (bProjected && Net.IsSet())
		{
			Out.Emplace(Pair.Key.Scope, Pair.Key.Key, Net);
		}
	}
}

bool UWorldHub_FlagRegistry::ApplyReplicatedEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& NetValue)
{
	if (!Key.IsValid() || !NetValue.IsSet())
	{
		return false;
	}

	FWorldHub_FlagValue NewValue;
	NewValue.bReplicate = true;
	if (const FWorldHub_FlagDefinition* Def = FindDefinition(Key))
	{
		NewValue.bSave = Def->bSave;
	}

	// Convert the net value back into the lossless FInstancedStruct slot.
	if (!NetValue.ToInstancedStruct(NewValue.Value))
	{
		return false;
	}
	ClampIfCounter(Key, NewValue);

	const FSlotKey SlotKey{ Scope, Key };
	if (FWorldHub_FlagValue* Existing = Slots.Find(SlotKey))
	{
		if (Existing->Value.Identical(&NewValue.Value, PPF_None))
		{
			return false;
		}
		Existing->Value = NewValue.Value;
		Existing->bReplicate = true;
		return true;
	}
	Slots.Add(SlotKey, NewValue);
	return true;
}

void UWorldHub_FlagRegistry::PruneReplicatedSlotsNotIn(const TSet<TPair<FWorldHub_Scope, FGameplayTag>>& KeepKeys)
{
	for (auto It = Slots.CreateIterator(); It; ++It)
	{
		if (!SlotReplicates(It.Key().Key, It.Value()))
		{
			continue; // never prune local/save-only slots on a client sync.
		}
		const TPair<FWorldHub_Scope, FGameplayTag> Composite(It.Key().Scope, It.Key().Key);
		if (!KeepKeys.Contains(Composite))
		{
			It.RemoveCurrent();
		}
	}
}

FString UWorldHub_FlagRegistry::ToDebugString() const
{
	int32 RepCount = 0;
	for (const TPair<FSlotKey, FWorldHub_FlagValue>& Pair : Slots)
	{
		if (SlotReplicates(Pair.Key.Key, Pair.Value))
		{
			++RepCount;
		}
	}
	return FString::Printf(TEXT("Slots=%d (Replicated=%d) Defs=%d"), Slots.Num(), RepCount, Definitions.Num());
}
