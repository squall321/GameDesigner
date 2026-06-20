// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Threat/AI_ThreatComponent.h"

#include "DesignPatternsAINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"
#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UnrealType.h"

UAI_ThreatComponent::UAI_ThreatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f; // decay runs every frame on authority; cheap (small table)
	// Carries a replicated authoritative top-threat id.
	SetIsReplicatedByDefault(true);
}

void UAI_ThreatComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the authority maintains the aggro table and converts combat damage into threat.
	if (HasAuthoritySafe() && bConvertCombatDamage)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			// DP.Bus.Combat.Damaged is the agreed channel; we read its payload generically (no Combat dep).
			const FGameplayTag DamagedChannel = FGameplayTag::RequestGameplayTag(TEXT("DP.Bus.Combat.Damaged"), /*ErrorIfNotFound*/ false);
			if (DamagedChannel.IsValid())
			{
				Bus->ListenNative(DamagedChannel,
					[this](const FDP_Message& Message) { HandleCombatDamaged(Message); },
					this, EDP_MessageMatch::ExactOrChild);
			}
			else
			{
				UE_LOG(LogDP, Warning, TEXT("[AI.Threat] DP.Bus.Combat.Damaged tag not registered; combat-to-threat bridge inactive on %s."),
					*GetNameSafe(GetOwner()));
			}
		}
	}

	// On clients with no authority, disable ticking (nothing to decay locally).
	if (!HasAuthoritySafe())
	{
		SetComponentTickEnabled(false);
	}
}

void UAI_ThreatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	Entries.Reset();
	Super::EndPlay(EndPlayReason);
}

void UAI_ThreatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasAuthoritySafe() || Entries.Num() == 0)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	DecayAndRecompute(Now);
}

void UAI_ThreatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAI_ThreatComponent, TopThreat);
}

void UAI_ThreatComponent::AddThreat(FSeam_EntityId Source, float Amount, FGameplayTag ThreatTag)
{
	// AUTHORITY ONLY — the aggro table is authoritative gameplay state.
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (!Source.IsValid())
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	FAI_ThreatEntry* Entry = Entries.FindByPredicate([&Source](const FAI_ThreatEntry& E) { return E.Source == Source; });
	if (!Entry)
	{
		FAI_ThreatEntry NewEntry;
		NewEntry.Source = Source;
		Entry = &Entries.Add_GetRef(NewEntry);
	}

	Entry->Value = FMath::Max(0.f, Entry->Value + Amount);
	Entry->LastAddedTime = Now;

	UE_LOG(LogDP, Verbose, TEXT("[AI.Threat] %s +%.1f threat from %s (tag %s) -> %.1f"),
		*GetNameSafe(GetOwner()), Amount, *Source.ToString(), *ThreatTag.ToString(), Entry->Value);

	RecomputeTopThreat();
}

float UAI_ThreatComponent::GetThreatFor(FSeam_EntityId Source) const
{
	const FAI_ThreatEntry* Entry = Entries.FindByPredicate([&Source](const FAI_ThreatEntry& E) { return E.Source == Source; });
	return Entry ? Entry->Value : 0.f;
}

void UAI_ThreatComponent::ClearThreat()
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	Entries.Reset();
	RecomputeTopThreat();
}

void UAI_ThreatComponent::RemoveSource(FSeam_EntityId Source)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	const int32 Removed = Entries.RemoveAll([&Source](const FAI_ThreatEntry& E) { return E.Source == Source; });
	if (Removed > 0)
	{
		RecomputeTopThreat();
	}
}

void UAI_ThreatComponent::DecayAndRecompute(double NowSeconds)
{
	bool bChanged = false;
	for (FAI_ThreatEntry& Entry : Entries)
	{
		const double SinceAdded = NowSeconds - Entry.LastAddedTime;
		if (SinceAdded > DecayDelaySeconds && DecayPerSecond > 0.f)
		{
			// Decay is integrated per tick: shave DecayPerSecond * dt off, where dt is the time past delay.
			// We approximate per-frame by decaying toward zero relative to the previous frame's time.
			const float DecayAmount = DecayPerSecond * GetWorld()->GetDeltaSeconds();
			const float NewValue = FMath::Max(0.f, Entry.Value - DecayAmount);
			if (!FMath::IsNearlyEqual(NewValue, Entry.Value))
			{
				Entry.Value = NewValue;
				bChanged = true;
			}
		}
	}

	// Prune small/empty entries.
	const int32 PrunedCount = Entries.RemoveAll([this](const FAI_ThreatEntry& E) { return E.Value <= PruneThreshold; });
	if (PrunedCount > 0)
	{
		bChanged = true;
	}

	if (bChanged)
	{
		RecomputeTopThreat();
	}
}

void UAI_ThreatComponent::RecomputeTopThreat()
{
	const FAI_ThreatEntry* Best = nullptr;
	for (const FAI_ThreatEntry& Entry : Entries)
	{
		if (!Best || Entry.Value > Best->Value)
		{
			Best = &Entry;
		}
	}

	const FSeam_EntityId NewTop = Best ? Best->Source : FSeam_EntityId::Invalid();
	if (NewTop != TopThreat)
	{
		const FSeam_EntityId Previous = TopThreat;
		TopThreat = NewTop;
		HandleTopThreatChanged(Previous, NewTop);
	}
}

void UAI_ThreatComponent::OnRep_TopThreat(FSeam_EntityId PreviousTopThreat)
{
	HandleTopThreatChanged(PreviousTopThreat, TopThreat);
}

void UAI_ThreatComponent::HandleTopThreatChanged(FSeam_EntityId Previous, FSeam_EntityId NewTop)
{
	UE_LOG(LogDP, Verbose, TEXT("[AI.Threat] %s top threat %s -> %s"),
		*GetNameSafe(GetOwner()), *Previous.ToString(), *NewTop.ToString());

	OnTopThreatChanged.Broadcast(NewTop);

	if (bBroadcastOnBus)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FAI_ThreatChangedPayload Payload;
			Payload.AgentId = GetOwnerEntityId();
			Payload.TopThreat = NewTop;
			Payload.TopThreatValue = GetThreatFor(NewTop);

			const FInstancedStruct PayloadStruct = FInstancedStruct::Make(Payload);
			Bus->BroadcastPayload(AINativeTags::Bus_AI_ThreatChanged, PayloadStruct, this);
		}
	}
}

void UAI_ThreatComponent::HandleCombatDamaged(const FDP_Message& Message)
{
	// AUTHORITY ONLY — we credit authoritative threat from the (locally broadcast) damage event.
	if (!HasAuthoritySafe())
	{
		return;
	}

	// The bus is local: this fires on every machine. Only the server maintains threat, hence the guard.
	const FInstancedStruct& Payload = Message.Payload;
	if (!Payload.IsValid())
	{
		return;
	}

	// Victim must resolve to OUR owner for this event to count against us.
	AActor* Victim = ReadActorField(Payload, CombatPayload_VictimField);
	if (Victim && Victim != GetOwner())
	{
		return;
	}
	// If the payload carries no victim field, fall back to instigator targeting only when the message
	// instigator is our owner's attacker context; otherwise require the victim to be us.
	if (!Victim)
	{
		// Without a resolvable victim we cannot safely attribute this damage to our owner; skip.
		return;
	}

	AActor* Instigator = ReadActorField(Payload, CombatPayload_InstigatorField);
	const FSeam_EntityId SourceId = GetEntityIdFor(Instigator);
	if (!SourceId.IsValid())
	{
		return;
	}

	float Amount = 0.f;
	if (!ReadFloatField(Payload, CombatPayload_AmountField, Amount))
	{
		// No amount field: nothing to convert.
		return;
	}

	const float ThreatAmount = FMath::Abs(Amount) * DamageToThreatScale;
	if (ThreatAmount <= 0.f)
	{
		return;
	}

	AddThreat(SourceId, ThreatAmount, CombatThreatTag);
}

AActor* UAI_ThreatComponent::ReadActorField(const FInstancedStruct& Payload, FName FieldName)
{
	if (FieldName.IsNone() || !Payload.IsValid())
	{
		return nullptr;
	}

	const UScriptStruct* StructType = Payload.GetScriptStruct();
	const uint8* Memory = Payload.GetMemory();
	if (!StructType || !Memory)
	{
		return nullptr;
	}

	FProperty* Prop = StructType->FindPropertyByName(FieldName);
	if (!Prop)
	{
		return nullptr;
	}

	// TWeakObjectPtr<AActor> case.
	if (const FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Prop))
	{
		const FWeakObjectPtr* WeakPtr = WeakProp->ContainerPtrToValuePtr<FWeakObjectPtr>(Memory);
		return WeakPtr ? Cast<AActor>(WeakPtr->Get()) : nullptr;
	}

	// Raw AActor* / TObjectPtr<AActor> case.
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue_InContainer(Memory);
		return Cast<AActor>(Obj);
	}

	return nullptr;
}

bool UAI_ThreatComponent::ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue)
{
	if (FieldName.IsNone() || !Payload.IsValid())
	{
		return false;
	}

	const UScriptStruct* StructType = Payload.GetScriptStruct();
	const uint8* Memory = Payload.GetMemory();
	if (!StructType || !Memory)
	{
		return false;
	}

	FProperty* Prop = StructType->FindPropertyByName(FieldName);
	if (!Prop)
	{
		return false;
	}

	if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		OutValue = FloatProp->GetPropertyValue_InContainer(Memory);
		return true;
	}
	if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		OutValue = static_cast<float>(DoubleProp->GetPropertyValue_InContainer(Memory));
		return true;
	}

	return false;
}

FSeam_EntityId UAI_ThreatComponent::GetEntityIdFor(const AActor* Actor)
{
	if (!Actor)
	{
		return FSeam_EntityId::Invalid();
	}
	if (Actor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(const_cast<AActor*>(Actor));
	}
	if (UActorComponent* Comp = const_cast<AActor*>(Actor)->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Comp);
	}
	return FSeam_EntityId::Invalid();
}

FSeam_EntityId UAI_ThreatComponent::GetOwnerEntityId() const
{
	return GetEntityIdFor(GetOwner());
}

bool UAI_ThreatComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
