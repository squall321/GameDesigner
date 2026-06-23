// Copyright DesignPatterns plugin. All Rights Reserved.

#include "CombatText/HUD_DamageNumberSubsystem.h"

#include "CombatText/HUD_DamageNumberViewModel.h"
#include "CombatText/HUD_DamageNumberStyleDataAsset.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Algo/Reverse.h"

void UHUD_DamageNumberSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Pure-projection ViewModel, instanced under this subsystem so it is GC-kept for the subsystem lifetime.
	ViewModel = NewObject<UHUD_DamageNumberViewModel>(this);

	// Subscribe to the combat hit/damage channels by string so we never include the Combat module. Both
	// channels carry a payload whose fields we read generically by reflection; either may be absent in a
	// project without the Combat module, in which case nothing fires.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		auto Subscribe = [this, Bus](const TCHAR* ChannelString)
		{
			const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(FName(ChannelString), /*ErrorIfNotFound*/ false);
			if (Channel.IsValid())
			{
				Bus->ListenNative(Channel,
					[this](const FDP_Message& Message) { HandleCombatEvent(Message); },
					this, EDP_MessageMatch::ExactOrChild);
			}
			else
			{
				UE_LOG(LogDP, Verbose,
					TEXT("[HUD.DamageNumber] Channel %s not registered; that source is inactive for floating text."),
					ChannelString);
			}
		};

		Subscribe(TEXT("DP.Bus.Combat.HitFeedback"));
		Subscribe(TEXT("DP.Bus.Combat.Damaged"));
	}

	// Advance + reproject on an FTSTicker (frame-delta, editor-safe) rather than a fixed-interval world timer.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_DamageNumberSubsystem::TickNumbers));
}

void UHUD_DamageNumberSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Drop every bus listener owned by this subsystem.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	ActiveNumbers.Reset();
	ViewModel = nullptr;
	Style = nullptr;

	Super::Deinitialize();
}

void UHUD_DamageNumberSubsystem::SetStyleAsset(UHUD_DamageNumberStyleDataAsset* InStyle)
{
	Style = InStyle;

	// Re-clamp the pool to the new cap (defensive: oldest items drop first).
	if (Style && ActiveNumbers.Num() > Style->MaxConcurrent)
	{
		const int32 Excess = ActiveNumbers.Num() - Style->MaxConcurrent;
		// Legacy bool overload (count + bAllowShrinking) is available across the 5.3-5.5 band; the 3-arg
		// EAllowShrinking enum overload is 5.5+ only.
		ActiveNumbers.RemoveAt(0, Excess, /*bAllowShrinking*/ false);
	}
}

APlayerController* UHUD_DamageNumberSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
}

void UHUD_DamageNumberSubsystem::SpawnNumber(const FVector& WorldLocation, float Amount, FGameplayTag Classification, AActor* /*Victim*/)
{
	if (!Style)
	{
		// No style asset configured -> nothing to present. Documented inert fallback (not an error).
		return;
	}

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	FActiveNumber Item;
	Item.InstanceId = NextInstanceId++;
	Item.WorldLocation = WorldLocation;
	Item.Amount = Amount;
	Item.Classification = Classification;
	Item.SpawnTimeSeconds = Now;

	// Deterministic-but-varied horizontal jitter so stacked hits fan out (seeded by instance id so the same
	// item is stable frame to frame).
	if (Style->HorizontalJitterPixels > 0.f)
	{
		FRandomStream Stream(static_cast<int32>(Item.InstanceId));
		Item.JitterPixels.X = Stream.FRandRange(-Style->HorizontalJitterPixels, Style->HorizontalJitterPixels);
	}

	// Recycle the oldest if at capacity.
	if (ActiveNumbers.Num() >= Style->MaxConcurrent)
	{
		ActiveNumbers.RemoveAt(0, 1, /*bAllowShrinking*/ false);
	}
	ActiveNumbers.Add(MoveTemp(Item));
}

void UHUD_DamageNumberSubsystem::HandleCombatEvent(const FDP_Message& Message)
{
	if (!Style)
	{
		return;
	}

	const FInstancedStruct& Payload = Message.Payload;
	if (!Payload.IsValid())
	{
		return;
	}

	// Read the amount; if there is no amount field we still spawn a "0"-less marker only when an impact point
	// resolves, so require at least an impact point OR a victim to anchor the number.
	float Amount = 0.f;
	ReadFloatField(Payload, Style->AmountFieldName, Amount);

	FGameplayTag Classification;
	ReadTagField(Payload, Style->ClassificationFieldName, Classification);

	FVector ImpactPoint = FVector::ZeroVector;
	bool bHaveImpact = ReadVectorField(Payload, Style->ImpactPointFieldName, ImpactPoint);

	AActor* Victim = ReadActorField(Payload, Style->VictimFieldName);

	// Fall back to the victim's location when the payload carries no impact point.
	if (!bHaveImpact && Victim)
	{
		ImpactPoint = Victim->GetActorLocation();
		bHaveImpact = true;
	}

	if (!bHaveImpact)
	{
		// Nothing to anchor a screen position to; skip rather than draw at the origin.
		return;
	}

	SpawnNumber(ImpactPoint, Amount, Classification, Victim);
}

bool UHUD_DamageNumberSubsystem::ProjectToScreen(const FVector& World, FVector2D& OutScreen) const
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return false;
	}
	// bPlayerViewportRelative = false (absolute viewport pixels); returns false when behind the camera.
	return PC->ProjectWorldLocationToScreen(World, OutScreen, /*bPlayerViewportRelative*/ false);
}

bool UHUD_DamageNumberSubsystem::TickNumbers(float /*DeltaTime*/)
{
	if (!ViewModel || !Style || ActiveNumbers.Num() == 0)
	{
		// Still ensure the view empties out once when the last item expires.
		if (ViewModel && ActiveNumbers.Num() == 0)
		{
			static const TArray<FHUD_FloatingTextView> Empty;
			// Only push an empty set if the VM currently shows something.
			if (ViewModel->GetNumbers().Num() > 0)
			{
				ViewModel->SetNumbers(Empty);
			}
		}
		return true; // keep ticking
	}

	const UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float Lifetime = FMath::Max(0.01f, Style->Lifetime);

	TArray<FHUD_FloatingTextView> Views;
	Views.Reserve(ActiveNumbers.Num());

	// Walk back-to-front so RemoveAtSwap of expired items is safe.
	for (int32 Index = ActiveNumbers.Num() - 1; Index >= 0; --Index)
	{
		FActiveNumber& Item = ActiveNumbers[Index];
		const float Age = static_cast<float>(Now - Item.SpawnTimeSeconds);
		if (Age >= Lifetime)
		{
			ActiveNumbers.RemoveAtSwap(Index, 1, /*bAllowShrinking*/ false);
			continue;
		}

		FVector2D Screen;
		if (!ProjectToScreen(Item.WorldLocation, Screen))
		{
			// Behind camera this frame; keep the item alive (it may come back on screen) but do not draw it.
			continue;
		}

		const FHUD_DamageNumberStyleRow& StyleRow = Style->ResolveStyle(Item.Classification);

		FHUD_FloatingTextView View;
		View.InstanceId = Item.InstanceId;
		View.ScreenPosition = Screen + Item.JitterPixels;
		View.Color = StyleRow.Color;
		View.Scale = Style->BaseScale * StyleRow.ScaleMultiplier;
		View.RisePixels = Style->RisePixels;
		View.LifetimeAlpha = FMath::Clamp(Age / Lifetime, 0.f, 1.f);
		View.ClassificationTag = Item.Classification;

		// Compose the text: prefix + rounded amount. Heals show positive; everything else shows magnitude.
		const int32 Rounded = FMath::RoundToInt(FMath::Abs(Item.Amount));
		View.Text = FText::FromString(StyleRow.Prefix + FString::FromInt(Rounded));

		Views.Add(MoveTemp(View));
	}

	// Restore spawn order (we iterated back-to-front).
	Algo::Reverse(Views);

	ViewModel->SetNumbers(Views);
	return true; // keep ticking
}

void UHUD_DamageNumberSubsystem::DumpTo(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("[HUD.DamageNumber] Active=%d Style=%s"),
		ActiveNumbers.Num(), *GetNameSafe(Style)));
	for (const FActiveNumber& Item : ActiveNumbers)
	{
		OutLines.Add(FString::Printf(TEXT("  #%lld amount=%.0f class=%s"),
			Item.InstanceId, Item.Amount, *Item.Classification.ToString()));
	}
}

// --- Reflection payload readers (generic; mirror UAI_ThreatComponent's pattern, no Combat header) ---

AActor* UHUD_DamageNumberSubsystem::ReadActorField(const FInstancedStruct& Payload, FName FieldName)
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

bool UHUD_DamageNumberSubsystem::ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue)
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

bool UHUD_DamageNumberSubsystem::ReadVectorField(const FInstancedStruct& Payload, FName FieldName, FVector& OutValue)
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

bool UHUD_DamageNumberSubsystem::ReadTagField(const FInstancedStruct& Payload, FName FieldName, FGameplayTag& OutValue)
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
