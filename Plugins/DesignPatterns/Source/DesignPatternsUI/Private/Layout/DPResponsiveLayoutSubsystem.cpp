// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Layout/DPResponsiveLayoutSubsystem.h"
#include "Layout/DPResponsiveLayoutDataAsset.h"
#include "DPUINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Display/Seam_SafeZoneProvider.h"

#include "UObject/UnrealType.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

void UDP_ResponsiveLayoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A defensive default so thresholds always exist even before a project assigns a data asset.
	DefaultDataAsset = NewObject<UDP_ResponsiveLayoutDataAsset>(this, NAME_None, RF_Transient);
	// Default the service keys on the fallback asset to the verified platform keys.
	DefaultDataAsset->SafeZoneServiceKey = DPUITags::Service_SafeZone;
	DefaultDataAsset->AccessibilityServiceKey = DPUITags::Service_AccessibilityProvider;

	// Accessibility defaults to UIScale 1.0 (the struct default) until a provider pushes.
	RegisterAsAccessibilityConsumer();

	Recompute();

	// Light poll for resolution/inset/DPI changes (platforms rarely push these).
	const UDP_ResponsiveLayoutDataAsset* Asset = GetEffectiveDataAsset();
	if (Asset && Asset->PollIntervalSeconds > 0.0f)
	{
		PollHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UDP_ResponsiveLayoutSubsystem::TickPoll),
			Asset->PollIntervalSeconds);
	}

	UE_LOG(LogDP, Verbose, TEXT("[ResponsiveLayout] Initialized (%s)."), *GetDebugString());
}

void UDP_ResponsiveLayoutSubsystem::Deinitialize()
{
	if (PollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
		PollHandle.Reset();
	}
	Super::Deinitialize();
}

TScriptInterface<ISeam_SafeZoneProvider> UDP_ResponsiveLayoutSubsystem::ResolveSafeZoneProvider() const
{
	const UDP_ResponsiveLayoutDataAsset* Asset = GetEffectiveDataAsset();
	const FGameplayTag Key = (Asset && Asset->SafeZoneServiceKey.IsValid())
		? Asset->SafeZoneServiceKey : DPUITags::Service_SafeZone;

	const ULocalPlayer* LP = GetLocalPlayer();
	UDP_ServiceLocatorSubsystem* Locator = LP
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(LP->GetGameInstance())
		: nullptr;
	if (!Locator)
	{
		return TScriptInterface<ISeam_SafeZoneProvider>();
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->Implements<USeam_SafeZoneProvider>())
	{
		return TScriptInterface<ISeam_SafeZoneProvider>(Provider);
	}
	return TScriptInterface<ISeam_SafeZoneProvider>();
}

void UDP_ResponsiveLayoutSubsystem::RegisterAsAccessibilityConsumer()
{
	const UDP_ResponsiveLayoutDataAsset* Asset = GetEffectiveDataAsset();
	const FGameplayTag Key = (Asset && Asset->AccessibilityServiceKey.IsValid())
		? Asset->AccessibilityServiceKey : DPUITags::Service_AccessibilityProvider;

	const ULocalPlayer* LP = GetLocalPlayer();
	UDP_ServiceLocatorSubsystem* Locator = LP
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(LP->GetGameInstance())
		: nullptr;
	if (!Locator)
	{
		return;
	}

	// The accessibility seam is PUSH-only: the provider calls OnAccessibilityOptionsChanged on its
	// registered consumers. To register WITHOUT hard-depending on the Localization concrete type, we
	// resolve the provider as a UObject and invoke its consumer-registration entry point by NAME via
	// reflection if it exposes one (the shipped provider advertises a "RegisterConsumer" UFUNCTION
	// taking a UObject consumer). This keeps the module fully decoupled and additive; when the
	// provider is absent or exposes no such entry point, we simply keep the default options
	// (UIScale 1.0), so layout never breaks.
	UObject* Provider = Locator->ResolveService(Key);
	if (!Provider)
	{
		return;
	}

	static const FName RegisterConsumerFuncName(TEXT("RegisterConsumer"));
	if (UFunction* RegisterFunc = Provider->FindFunction(RegisterConsumerFuncName))
	{
		// The entry point takes a single object/interface param: this subsystem (an ISeam_AccessibilityConsumer).
		FProperty* ParamProperty = nullptr;
		for (TFieldIterator<FProperty> It(RegisterFunc); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ParamProperty = *It;
				break;
			}
		}

		// Only invoke when the single param is an object/interface slot we can fill with ourselves.
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

const UDP_ResponsiveLayoutDataAsset* UDP_ResponsiveLayoutSubsystem::GetEffectiveDataAsset() const
{
	return DataAsset ? DataAsset.Get() : DefaultDataAsset.Get();
}

void UDP_ResponsiveLayoutSubsystem::SetLayoutDataAsset(UDP_ResponsiveLayoutDataAsset* InDataAsset)
{
	DataAsset = InDataAsset;
	Recompute();
}

void UDP_ResponsiveLayoutSubsystem::RefreshNow()
{
	Recompute();
}

void UDP_ResponsiveLayoutSubsystem::Recompute()
{
	const UDP_ResponsiveLayoutDataAsset* Asset = GetEffectiveDataAsset();

	FDP_LayoutState NewState;

	// Defensive defaults when no provider is present.
	FVector4 Insets(0, 0, 0, 0);
	float ProviderDPI = 1.0f;
	FIntPoint Resolution = FIntPoint::ZeroValue;

	TScriptInterface<ISeam_SafeZoneProvider> Provider = ResolveSafeZoneProvider();
	if (Provider.GetObject())
	{
		Insets = ISeam_SafeZoneProvider::Execute_GetSafeInsets(Provider.GetObject());
		ProviderDPI = ISeam_SafeZoneProvider::Execute_GetDPIScale(Provider.GetObject());
		Resolution = ISeam_SafeZoneProvider::Execute_GetResolution(Provider.GetObject());
	}
	else if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		// Fallback to the engine viewport size so the breakpoint is still meaningful with no seam.
		if (UGameViewportClient* VPC = LP->ViewportClient)
		{
			FVector2D ViewportSize;
			VPC->GetViewportSize(ViewportSize);
			Resolution = FIntPoint(FMath::RoundToInt(ViewportSize.X), FMath::RoundToInt(ViewportSize.Y));
		}
	}

	NewState.SafeZoneMargin = FMargin(Insets.X, Insets.Y, Insets.Z, Insets.W);
	NewState.Resolution = Resolution;

	// Effective DPI folds the player's accessibility UI-scale into the platform DPI.
	const float AccessibilityScale = (CachedAccessibility.UIScale > 0.0f) ? CachedAccessibility.UIScale : 1.0f;
	NewState.EffectiveDPIScale = (ProviderDPI > 0.0f ? ProviderDPI : 1.0f) * AccessibilityScale;

	// Classify by width. Use resolution width when known, else keep the previous breakpoint.
	NewState.Breakpoint = (Resolution.X > 0 && Asset)
		? Asset->ClassifyWidth(Resolution.X)
		: State.Breakpoint;

	// Only broadcast when something a layout actually cares about changed.
	const bool bBreakpointChanged = NewState.Breakpoint != State.Breakpoint;
	const bool bMarginChanged =
		!FMath::IsNearlyEqual(NewState.SafeZoneMargin.Left, State.SafeZoneMargin.Left) ||
		!FMath::IsNearlyEqual(NewState.SafeZoneMargin.Top, State.SafeZoneMargin.Top) ||
		!FMath::IsNearlyEqual(NewState.SafeZoneMargin.Right, State.SafeZoneMargin.Right) ||
		!FMath::IsNearlyEqual(NewState.SafeZoneMargin.Bottom, State.SafeZoneMargin.Bottom);
	const bool bDPIChanged = !FMath::IsNearlyEqual(NewState.EffectiveDPIScale, State.EffectiveDPIScale);

	State = NewState;

	if (bBreakpointChanged || bMarginChanged || bDPIChanged)
	{
		OnLayoutChanged.Broadcast(State.Breakpoint);
	}
}

bool UDP_ResponsiveLayoutSubsystem::TickPoll(float /*DeltaTime*/)
{
	Recompute();
	return true; // keep polling
}

void UDP_ResponsiveLayoutSubsystem::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	CachedAccessibility = Options;
	Recompute();
}

FString UDP_ResponsiveLayoutSubsystem::GetDebugString() const
{
	const TCHAR* BpName = TEXT("?");
	switch (State.Breakpoint)
	{
	case EDP_UIBreakpoint::Compact:  BpName = TEXT("Compact");  break;
	case EDP_UIBreakpoint::Medium:   BpName = TEXT("Medium");   break;
	case EDP_UIBreakpoint::Expanded: BpName = TEXT("Expanded"); break;
	case EDP_UIBreakpoint::Wide:     BpName = TEXT("Wide");     break;
	}
	return FString::Printf(TEXT("Layout: %s @ %dx%d DPI %.2f safe(%.0f,%.0f,%.0f,%.0f)"),
		BpName, State.Resolution.X, State.Resolution.Y, State.EffectiveDPIScale,
		State.SafeZoneMargin.Left, State.SafeZoneMargin.Top, State.SafeZoneMargin.Right, State.SafeZoneMargin.Bottom);
}
