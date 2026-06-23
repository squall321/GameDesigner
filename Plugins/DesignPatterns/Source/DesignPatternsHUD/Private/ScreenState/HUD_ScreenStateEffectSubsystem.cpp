// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ScreenState/HUD_ScreenStateEffectSubsystem.h"

#include "ScreenState/HUD_ScreenStateViewModel.h"
#include "ScreenState/HUD_ScreenStateConfigDataAsset.h"
#include "HUD_DeepNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Vfx/Seam_VfxController.h"

#include "Curves/CurveFloat.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void UHUD_ScreenStateEffectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewModel = NewObject<UHUD_ScreenStateViewModel>(this);

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// Health fraction (HUD-owned channel; a health system publishes to it).
		Bus->ListenNative(HUDTags::Bus_HUD_HealthFraction,
			[this](const FDP_Message& Message) { HandleHealthFraction(Message); },
			this, EDP_MessageMatch::ExactOrChild);

		// Combat hit-feedback (resolved by string -> no Combat module dependency).
		const FGameplayTag HitFeedback = FGameplayTag::RequestGameplayTag(TEXT("DP.Bus.Combat.HitFeedback"), /*ErrorIfNotFound*/ false);
		if (HitFeedback.IsValid())
		{
			Bus->ListenNative(HitFeedback,
				[this](const FDP_Message& Message) { HandleHitFeedback(Message); },
				this, EDP_MessageMatch::ExactOrChild);
		}
	}

	// Register ourselves with the accessibility provider (push-only seam) via reflection on its
	// "RegisterConsumer" UFUNCTION, exactly as the UI responsive-layout subsystem does — no Localization dep.
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		const FGameplayTag Key = FGameplayTag::RequestGameplayTag(TEXT("DP.Service.AccessibilityProvider"), /*ErrorIfNotFound*/ false);
		if (Key.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(Key))
			{
				static const FName RegisterConsumerFuncName(TEXT("RegisterConsumer"));
				if (UFunction* RegisterFunc = Provider->FindFunction(RegisterConsumerFuncName))
				{
					FProperty* ParamProperty = nullptr;
					for (TFieldIterator<FProperty> It(RegisterFunc); It && (It->PropertyFlags & CPF_Parm); ++It)
					{
						if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
						{
							ParamProperty = *It;
							break;
						}
					}
					if (ParamProperty && (ParamProperty->IsA<FObjectPropertyBase>() || ParamProperty->IsA<FInterfaceProperty>()))
					{
						void* ParamBuffer = FMemory_Alloca(RegisterFunc->ParmsSize);
						FMemory::Memzero(ParamBuffer, RegisterFunc->ParmsSize);
						RegisterFunc->InitializeStruct(ParamBuffer);
						if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(ParamProperty))
						{
							ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(ParamBuffer), this);
						}
						else if (FInterfaceProperty* IfaceProp = CastField<FInterfaceProperty>(ParamProperty))
						{
							FScriptInterface ScriptIface;
							ScriptIface.SetObject(this);
							ScriptIface.SetInterface(static_cast<ISeam_AccessibilityConsumer*>(this));
							IfaceProp->SetPropertyValue(IfaceProp->ContainerPtrToValuePtr<void>(ParamBuffer), ScriptIface);
						}
						Provider->ProcessEvent(RegisterFunc, ParamBuffer);
						RegisterFunc->DestroyStruct(ParamBuffer);
					}
				}
			}
		}
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_ScreenStateEffectSubsystem::TickEffects));
}

void UHUD_ScreenStateEffectSubsystem::Deinitialize()
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
	VfxController.Reset();
	HitDirections.Reset();
	Super::Deinitialize();
}

void UHUD_ScreenStateEffectSubsystem::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	// Reuse ScreenShakeScale as the reduce-flashing control: a single accessibility slider damps both shake
	// and the full-screen damage flash. 0 => flash disabled.
	FlashScale = FMath::Clamp(Options.ScreenShakeScale, 0.f, 1.f);
}

void UHUD_ScreenStateEffectSubsystem::SetConfig(UHUD_ScreenStateConfigDataAsset* InConfig)
{
	Config = InConfig;
}

void UHUD_ScreenStateEffectSubsystem::SetHealthFraction(float Frac)
{
	HealthFraction = FMath::Clamp(Frac, 0.f, 1.f);
}

void UHUD_ScreenStateEffectSubsystem::ResolveVfxController()
{
	if (VfxController.IsValid())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		const FGameplayTag Key = FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Vfx"), /*ErrorIfNotFound*/ false);
		if (Key.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(Key))
			{
				if (Provider->GetClass()->ImplementsInterface(USeam_VfxController::StaticClass()))
				{
					VfxController = TWeakInterfacePtr<ISeam_VfxController>(*Provider);
				}
			}
		}
	}
}

APlayerController* UHUD_ScreenStateEffectSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
}

void UHUD_ScreenStateEffectSubsystem::HandleHealthFraction(const FDP_Message& Message)
{
	if (!Config)
	{
		return;
	}
	float Frac = HealthFraction;
	// The health channel payload exposes a single float fraction; the field name is tunable on the config
	// asset (default "Fraction") so a project's health payload can be retargeted without code changes.
	if (ReadFloatField(Message.Payload, Config->HealthFractionFieldName, Frac))
	{
		SetHealthFraction(Frac);
	}
}

void UHUD_ScreenStateEffectSubsystem::HandleHitFeedback(const FDP_Message& Message)
{
	if (!Config || !ViewModel)
	{
		return;
	}

	const FInstancedStruct& Payload = Message.Payload;

	// Only react to hits where the LOCAL pawn is the victim.
	APlayerController* PC = GetOwningPlayerController();
	APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	AActor* Victim = ReadActorField(Payload, Config->VictimFieldName);
	if (!LocalPawn || (Victim && Victim != LocalPawn))
	{
		return;
	}

	// Compute the hit-direction bearing from the source (instigator location or impact point) vs camera fwd.
	FVector SourceLoc = FVector::ZeroVector;
	bool bHaveSource = ReadVectorField(Payload, Config->ImpactPointFieldName, SourceLoc);
	if (!bHaveSource)
	{
		if (AActor* Instigator = ReadActorField(Payload, Config->InstigatorFieldName))
		{
			SourceLoc = Instigator->GetActorLocation();
			bHaveSource = true;
		}
	}

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (bHaveSource && PC)
	{
		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);

		const FVector ToSource = (SourceLoc - LocalPawn->GetActorLocation()).GetSafeNormal2D();
		const FVector Forward = ViewRot.Vector().GetSafeNormal2D();
		const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y).GetSafeNormal2D();

		// Bearing: 0 = directly ahead, +90 = right, -90 = left (screen clockwise).
		const float Fwd = static_cast<float>(FVector::DotProduct(ToSource, Forward));
		const float Rgt = static_cast<float>(FVector::DotProduct(ToSource, Right));
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rgt, Fwd));

		FActiveHitDir Dir;
		Dir.AngleDegrees = Angle;
		Dir.SpawnTimeSeconds = Now;

		if (HitDirections.Num() >= Config->MaxHitDirections)
		{
			HitDirections.RemoveAt(0, 1, /*bAllowShrinking*/ false);
		}
		HitDirections.Add(Dir);
	}

	// Trigger the damage flash (scaled by accessibility reduce-flashing).
	LastFlashTime = Now;
	FlashPeak = Config->DamageFlashPeak * FlashScale;

	// Optionally route a one-shot flash VFX through the world VFX seam.
	if (Config->DamageVfxTag.IsValid())
	{
		ResolveVfxController();
		if (UObject* VfxObj = VfxController.GetObject())
		{
			ISeam_VfxController::Execute_SpawnVfxAtLocation(VfxObj, Config->DamageVfxTag,
				LocalPawn->GetActorLocation(), FRotator::ZeroRotator);
		}
	}
}

bool UHUD_ScreenStateEffectSubsystem::TickEffects(float /*DeltaTime*/)
{
	if (!ViewModel || !Config)
	{
		return true;
	}

	// --- Low-health vignette ---
	const float Start = Config->VignetteStartFraction;
	const float Full = Config->GetEffectiveVignetteFull();
	float Vignette = 0.f;
	if (HealthFraction <= Start)
	{
		// Band position: 0 at start fraction, 1 at full fraction (lower health = higher intensity).
		const float Band = (Start > Full)
			? FMath::Clamp((Start - HealthFraction) / (Start - Full), 0.f, 1.f)
			: 1.f;
		Vignette = Config->VignetteCurve ? Config->VignetteCurve->GetFloatValue(Band) : Band;
	}
	ViewModel->SetVignetteIntensity(Vignette);

	// --- Damage flash decay ---
	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float SinceFlash = static_cast<float>(Now - LastFlashTime);
	const float FlashT = SinceFlash / FMath::Max(0.01f, Config->DamageFlashFadeSeconds);
	const float FlashAlpha = FlashPeak * (1.f - FMath::Clamp(FlashT, 0.f, 1.f));
	ViewModel->SetDamageFlashAlpha(FlashAlpha);

	// --- Hit-direction indicators (hold then fade, recycle expired) ---
	TArray<FHUD_HitDirectionView> DirViews;
	DirViews.Reserve(HitDirections.Num());
	for (int32 Index = HitDirections.Num() - 1; Index >= 0; --Index)
	{
		const FActiveHitDir& Dir = HitDirections[Index];
		const float Age = static_cast<float>(Now - Dir.SpawnTimeSeconds);
		const float Total = Config->HitDirectionHoldSeconds + Config->HitDirectionFadeSeconds;
		if (Age >= Total)
		{
			HitDirections.RemoveAt(Index, 1, /*bAllowShrinking*/ false);
			continue;
		}
		float Alpha = 1.f;
		if (Age > Config->HitDirectionHoldSeconds)
		{
			const float FadeT = (Age - Config->HitDirectionHoldSeconds) / FMath::Max(0.01f, Config->HitDirectionFadeSeconds);
			Alpha = 1.f - FMath::Clamp(FadeT, 0.f, 1.f);
		}
		FHUD_HitDirectionView View;
		View.AngleDegrees = Dir.AngleDegrees;
		View.Alpha = Alpha;
		DirViews.Add(View);
	}
	ViewModel->SetHitDirections(DirViews);

	return true;
}

// --- Reflection payload readers (no Combat header) ---

AActor* UHUD_ScreenStateEffectSubsystem::ReadActorField(const FInstancedStruct& Payload, FName FieldName)
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

bool UHUD_ScreenStateEffectSubsystem::ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue)
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

bool UHUD_ScreenStateEffectSubsystem::ReadVectorField(const FInstancedStruct& Payload, FName FieldName, FVector& OutValue)
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
	if (!StructProp || StructProp->Struct != TBaseStructure<FVector>::Get())
	{
		return false;
	}
	OutValue = *StructProp->ContainerPtrToValuePtr<FVector>(Memory);
	return true;
}
