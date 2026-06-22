// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/UPlat_InputGlyphSubsystem.h"
#include "Plat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/GameInstance.h"

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UPlat_InputGlyphSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Glyphs are UI-only; a dedicated server has no UI.
	return !IsRunningDedicatedServer();
}

void UPlat_InputGlyphSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to live device changes from the input router (resolved from the same GI).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UPlat_InputRouterSubsystem* Router = GI->GetSubsystem<UPlat_InputRouterSubsystem>())
		{
			RouterWeak = Router;
			Router->OnInputDeviceChanged.AddDynamic(this, &UPlat_InputGlyphSubsystem::HandleInputDeviceChanged);
			// Seed the active family from the router's current device.
			ActiveFamily = ResolveFamilyForDevice(Router->GetCurrentInputDevice());
		}
	}

	RegisterGlyphService();

	UE_LOG(LogDP, Log, TEXT("[Platform] InputGlyphSubsystem initialized (family=%d)."), (int32)ActiveFamily);
}

void UPlat_InputGlyphSubsystem::Deinitialize()
{
	// Unsubscribe the router delegate so it never fires into a dead subsystem.
	if (UPlat_InputRouterSubsystem* Router = RouterWeak.Get())
	{
		Router->OnInputDeviceChanged.RemoveDynamic(this, &UPlat_InputGlyphSubsystem::HandleInputDeviceChanged);
	}
	RouterWeak.Reset();

	UnregisterGlyphService();
	Sets.Empty();

	Super::Deinitialize();
}

FString UPlat_InputGlyphSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Glyphs family=%d banks=%d"), (int32)ActiveFamily, Sets.Num());
}

// ---------------------------------------------------------------------------------------------
//  Service registration
// ---------------------------------------------------------------------------------------------

void UPlat_InputGlyphSubsystem::RegisterGlyphService()
{
	if (bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		bRegisteredService = Locator->RegisterService(
			Plat_NativeTags::Service_Glyphs, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UPlat_InputGlyphSubsystem::UnregisterGlyphService()
{
	if (!bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Locator->ResolveService(Plat_NativeTags::Service_Glyphs) == this)
		{
			Locator->UnregisterService(Plat_NativeTags::Service_Glyphs);
		}
	}
	bRegisteredService = false;
}

// ---------------------------------------------------------------------------------------------
//  Family resolution
// ---------------------------------------------------------------------------------------------

EPlat_InputFamily UPlat_InputGlyphSubsystem::ResolveFamilyForDevice(EPlat_InputDevice Device) const
{
	switch (Device)
	{
	case EPlat_InputDevice::Touch:
		return EPlat_InputFamily::Touch;

	case EPlat_InputDevice::KeyboardMouse:
		return EPlat_InputFamily::KeyboardMouse;

	case EPlat_InputDevice::Gamepad:
	default:
		// Refine the coarse gamepad into a vendor family. All platform branching is confined here; the
		// generic fallback is the safe choice when the vendor cannot be determined.
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE || PLATFORM_XSX
		return EPlat_InputFamily::Xbox;
#elif PLATFORM_PS4 || PLATFORM_PS5
		return EPlat_InputFamily::PlayStation;
#elif PLATFORM_SWITCH
		return EPlat_InputFamily::Nintendo;
#else
		return EPlat_InputFamily::Generic;
#endif
	}
}

void UPlat_InputGlyphSubsystem::HandleInputDeviceChanged(EPlat_InputDevice NewDevice)
{
	SetActiveFamily(ResolveFamilyForDevice(NewDevice));
}

void UPlat_InputGlyphSubsystem::SetActiveFamily(EPlat_InputFamily NewFamily)
{
	if (ActiveFamily == NewFamily)
	{
		return;
	}
	ActiveFamily = NewFamily;
	OnGlyphFamilyChanged.Broadcast(ActiveFamily);
	UE_LOG(LogDP, Verbose, TEXT("[Platform] Glyph family changed to %d."), (int32)ActiveFamily);
}

// ---------------------------------------------------------------------------------------------
//  Glyph resolution
// ---------------------------------------------------------------------------------------------

void UPlat_InputGlyphSubsystem::RegisterGlyphSet(UPlat_GlyphSet* Set)
{
	if (!Set)
	{
		return;
	}
	Sets.Add(Set->Family, Set);
}

FPlat_InputGlyph UPlat_InputGlyphSubsystem::ResolveGlyph(FGameplayTag ActionTag) const
{
	FPlat_InputGlyph Result;
	Result.ActionTag = ActionTag;

	// Active family bank first.
	if (const TObjectPtr<UPlat_GlyphSet>* Bank = Sets.Find(ActiveFamily))
	{
		if (*Bank && (*Bank)->ResolveGlyph(ActionTag, Result))
		{
			return Result;
		}
	}

	// Fall back to the Generic bank so an unmapped vendor still shows something.
	if (ActiveFamily != EPlat_InputFamily::Generic)
	{
		if (const TObjectPtr<UPlat_GlyphSet>* Generic = Sets.Find(EPlat_InputFamily::Generic))
		{
			if (*Generic && (*Generic)->ResolveGlyph(ActionTag, Result))
			{
				return Result;
			}
		}
	}

	return Result; // empty glyph (texture null, label empty), ActionTag preserved
}

bool UPlat_InputGlyphSubsystem::ResolveActionGlyph_Implementation(FGameplayTag ActionTag, TSoftObjectPtr<UTexture2D>& OutTexture, FText& OutLabel) const
{
	const FPlat_InputGlyph Glyph = ResolveGlyph(ActionTag);
	OutTexture = Glyph.GlyphTexture;
	OutLabel = Glyph.Label;
	return !Glyph.GlyphTexture.IsNull() || !Glyph.Label.IsEmpty();
}
