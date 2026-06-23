// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reticle/HUD_ReticleSubsystem.h"

#include "Reticle/HUD_ReticleViewModel.h"
#include "Reticle/HUD_ReticleConfigDataAsset.h"
#include "HUD_DeepNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"

void UHUD_ReticleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewModel = NewObject<UHUD_ReticleViewModel>(this);

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// Hit-feedback (for hit confirm) — resolved by string so we never include the Combat module.
		const FGameplayTag HitFeedback = FGameplayTag::RequestGameplayTag(TEXT("DP.Bus.Combat.HitFeedback"), /*ErrorIfNotFound*/ false);
		if (HitFeedback.IsValid())
		{
			Bus->ListenNative(HitFeedback,
				[this](const FDP_Message& Message) { HandleHitFeedback(Message); },
				this, EDP_MessageMatch::ExactOrChild);
		}

		// Spread channel is HUD-owned (a weapon system publishes to it).
		Bus->ListenNative(HUDTags::Bus_HUD_ReticleSpread,
			[this](const FDP_Message& Message) { HandleSpread(Message); },
			this, EDP_MessageMatch::ExactOrChild);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_ReticleSubsystem::TickReticle));
}

void UHUD_ReticleSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	ViewModel = nullptr;
	Config = nullptr;
	TeamAffinity.Reset();
	Super::Deinitialize();
}

void UHUD_ReticleSubsystem::SetConfig(UHUD_ReticleConfigDataAsset* InConfig)
{
	Config = InConfig;
	if (ViewModel && Config)
	{
		ViewModel->SetSpread(Config->DefaultSpreadDegrees);
	}
}

APlayerController* UHUD_ReticleSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
}

void UHUD_ReticleSubsystem::ResolveTeamAffinity()
{
	if (TeamAffinity.IsValid())
	{
		return; // already resolved + live
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// The GameMode team subsystem publishes ISeam_TeamAffinity under this stable key. Resolved by string
		// so the HUD never includes the GameMode module.
		const FGameplayTag Key = FGameplayTag::RequestGameplayTag(TEXT("DP.Service.GM.TeamAffinity"), /*ErrorIfNotFound*/ false);
		if (Key.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(Key))
			{
				if (Provider->GetClass()->ImplementsInterface(USeam_TeamAffinity::StaticClass()))
				{
					TeamAffinity = TWeakInterfacePtr<ISeam_TeamAffinity>(*Provider);
				}
			}
		}
	}
}

void UHUD_ReticleSubsystem::HandleHitFeedback(const FDP_Message& Message)
{
	if (!Config || !ViewModel)
	{
		return;
	}
	const FInstancedStruct& Payload = Message.Payload;

	// Only confirm hits the LOCAL pawn dealt.
	APlayerController* PC = GetOwningPlayerController();
	APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	AActor* Instigator = ReadActorField(Payload, Config->InstigatorFieldName);
	if (!Instigator || Instigator != LocalPawn)
	{
		return;
	}

	FGameplayTag Classification;
	ReadTagField(Payload, Config->ClassificationFieldName, Classification);
	const bool bCrit = Config->CritTags.HasTag(Classification);

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	LastHitConfirmTime = World ? World->GetTimeSeconds() : 0.0;
	HitConfirmPeak = bCrit ? 1.f : 0.7f;
}

void UHUD_ReticleSubsystem::HandleSpread(const FDP_Message& Message)
{
	if (!Config || !ViewModel)
	{
		return;
	}
	float Spread = Config->DefaultSpreadDegrees;
	if (ReadFloatField(Message.Payload, Config->SpreadFieldName, Spread))
	{
		ViewModel->SetSpread(FMath::Max(0.f, Spread));
	}
}

bool UHUD_ReticleSubsystem::TickReticle(float /*DeltaTime*/)
{
	if (!ViewModel || !Config)
	{
		return true;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		ViewModel->SetVisible(false);
		return true;
	}
	ViewModel->SetVisible(true);

	// Hit-confirm decay (hold then fade), driven by the config timings.
	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float Since = static_cast<float>(Now - LastHitConfirmTime);
	float Alpha = 0.f;
	if (Since <= Config->HitConfirmHoldSeconds)
	{
		Alpha = HitConfirmPeak;
	}
	else
	{
		const float FadeT = (Since - Config->HitConfirmHoldSeconds) / FMath::Max(0.01f, Config->HitConfirmFadeSeconds);
		Alpha = HitConfirmPeak * (1.f - FMath::Clamp(FadeT, 0.f, 1.f));
	}
	ViewModel->SetHitConfirmAlpha(Alpha);

	// Forward target trace -> team color.
	if (Config->bTraceForTargetType)
	{
		APawn* LocalPawn = PC->GetPawn();
		ViewModel->SetTargetTypeTag(ResolveTargetType(LocalPawn, PC));
	}

	return true;
}

FGameplayTag UHUD_ReticleSubsystem::ResolveTargetType(AActor* LocalPawn, APlayerController* PC) const
{
	if (!PC || !Config)
	{
		return HUDTags::Reticle_Target_Neutral;
	}
	const UWorld* World = PC->GetWorld();
	if (!World)
	{
		return HUDTags::Reticle_Target_Neutral;
	}

	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector End = ViewLoc + ViewRot.Vector() * Config->TargetTraceLength;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HUDReticleTarget), /*bTraceComplex*/ false);
	if (LocalPawn)
	{
		Params.AddIgnoredActor(LocalPawn);
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLoc, End, Config->TargetTraceChannel.GetValue(), Params))
	{
		return HUDTags::Reticle_Target_Neutral;
	}

	AActor* Target = Hit.GetActor();
	if (!Target || !LocalPawn)
	{
		return HUDTags::Reticle_Target_Neutral;
	}

	// Re-resolve the team-affinity seam (held weakly, pruned on use).
	const_cast<UHUD_ReticleSubsystem*>(this)->ResolveTeamAffinity();
	if (UObject* AffinityObj = TeamAffinity.GetObject())
	{
		const bool bFriendly = ISeam_TeamAffinity::Execute_AreFriendly(AffinityObj, LocalPawn, Target);
		return bFriendly ? HUDTags::Reticle_Target_Friendly : HUDTags::Reticle_Target_Hostile;
	}

	return HUDTags::Reticle_Target_Neutral;
}

// --- Reflection payload readers (no Combat header) ---

AActor* UHUD_ReticleSubsystem::ReadActorField(const FInstancedStruct& Payload, FName FieldName)
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
	if (const FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Prop))
	{
		const FWeakObjectPtr* WeakPtr = WeakProp->ContainerPtrToValuePtr<FWeakObjectPtr>(Memory);
		return WeakPtr ? Cast<AActor>(WeakPtr->Get()) : nullptr;
	}
	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		return Cast<AActor>(ObjProp->GetObjectPropertyValue_InContainer(Memory));
	}
	return nullptr;
}

bool UHUD_ReticleSubsystem::ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue)
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

bool UHUD_ReticleSubsystem::ReadTagField(const FInstancedStruct& Payload, FName FieldName, FGameplayTag& OutValue)
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
	const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp || StructProp->Struct != FGameplayTag::StaticStruct())
	{
		return false;
	}
	OutValue = *StructProp->ContainerPtrToValuePtr<FGameplayTag>(Memory);
	return true;
}
