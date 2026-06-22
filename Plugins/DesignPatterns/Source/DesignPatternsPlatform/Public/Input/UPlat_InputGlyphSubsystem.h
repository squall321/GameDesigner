// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Input/Seam_InputGlyphProvider.h"
#include "Input/UPlat_InputGlyphTypes.h"
#include "Input/UPlat_InputRouterSubsystem.h"
#include "UPlat_InputGlyphSubsystem.generated.h"

/** Broadcast when the active glyph family changes (e.g. player switches from gamepad to keyboard). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnGlyphFamilyChanged, EPlat_InputFamily, NewFamily);

/**
 * Resolves action -> button glyph for the player's CURRENT input family. Subscribes to the real
 * UPlat_InputRouterSubsystem::OnInputDeviceChanged and refines the coarse Gamepad device into a vendor
 * family (Xbox/PlayStation/Nintendo/SteamDeck) behind platform #ifdefs with a Generic fallback;
 * Touch and KeyboardMouse map directly.
 *
 * Implements ISeam_InputGlyphProvider (UI resolves glyphs through the seam without depending on this
 * module) and self-registers under DP.Service.Platform.Glyphs (WeakObserved). Banks are authored as
 * UPlat_GlyphSet data assets, one per family. Skipped on dedicated servers.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_InputGlyphSubsystem : public UDP_GameInstanceSubsystem, public ISeam_InputGlyphProvider
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast when the active family changes. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Input")
	FPlat_OnGlyphFamilyChanged OnGlyphFamilyChanged;

	/** Resolve the glyph for an action on the active family (empty glyph if unmapped). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Input")
	FPlat_InputGlyph ResolveGlyph(FGameplayTag ActionTag) const;

	/** The currently-active input family. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Input")
	EPlat_InputFamily GetActiveFamily() const { return ActiveFamily; }

	/** Register (or replace) the glyph bank for its declared family. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Input")
	void RegisterGlyphSet(UPlat_GlyphSet* Set);

	//~ Begin ISeam_InputGlyphProvider
	virtual bool ResolveActionGlyph_Implementation(FGameplayTag ActionTag, TSoftObjectPtr<UTexture2D>& OutTexture, FText& OutLabel) const override;
	//~ End ISeam_InputGlyphProvider

private:
	/** Router callback: map a coarse device class to a refined family and broadcast on change. */
	UFUNCTION()
	void HandleInputDeviceChanged(EPlat_InputDevice NewDevice);

	/** Map a coarse device class to a refined input family (Gamepad refinement is platform-confined). */
	EPlat_InputFamily ResolveFamilyForDevice(EPlat_InputDevice Device) const;

	/** Set the active family and broadcast if it changed. */
	void SetActiveFamily(EPlat_InputFamily NewFamily);

	/** Register/unregister the glyph-provider service. */
	void RegisterGlyphService();
	void UnregisterGlyphService();

	/** Weak: the input router is engine/GI-owned; we never keep it alive. */
	TWeakObjectPtr<UPlat_InputRouterSubsystem> RouterWeak;

	/** Family -> bank. Strong: the subsystem keeps registered banks loaded while live. */
	UPROPERTY()
	TMap<EPlat_InputFamily, TObjectPtr<UPlat_GlyphSet>> Sets;

	/** The currently-active family. */
	EPlat_InputFamily ActiveFamily = EPlat_InputFamily::Generic;

	/** True once the service was registered. */
	bool bRegisteredService = false;
};
