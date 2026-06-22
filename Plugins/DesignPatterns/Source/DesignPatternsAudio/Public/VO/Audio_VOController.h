// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "VO/Audio_VOTypes.h"
#include "Audio_VOController.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UAudio_VOController : public UInterface
{
	GENERATED_BODY()
};

/**
 * VO/BARK facade — the cosmetic VO seam other systems use without depending on the concrete subsystem.
 *
 * Kept MODULE-LOCAL in DesignPatternsAudio, exactly like the shipped IAudio_AudioController: the
 * concrete implementation is the GameInstance-scoped UAudio_VOSubsystem, registered into the service
 * locator under AudioNativeTags::Service_AudioVO (DP.Service.Audio.VO, WeakObserved). Consumers
 * (AI barks, narrative one-shots, interaction acknowledgements...) resolve a
 * TScriptInterface<IAudio_VOController> from UDP_ServiceLocatorSubsystem and call these methods.
 *
 * Everything here is LOCAL/COSMETIC and never replicated. The seam plays SOUND only; it never authors
 * or shows captions — to surface a subtitle the requester attaches an opaque caption payload to the
 * FAudio_VORequest, which the subsystem forwards on DP.Bus.Loc.VoiceLine for the shipped subtitle
 * subsystem to render.
 */
class DESIGNPATTERNSAUDIO_API IAudio_VOController
{
	GENERATED_BODY()

public:
	/**
	 * Enqueue (or interrupt with) a VO/dialogue line per the request's priority and interrupt mode.
	 * @return a handle to stop this specific line, or an invalid handle if it was dropped.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	FGuid PlayVO(const FAudio_VORequest& Request);

	/** Stop a specific VO line by the handle returned from PlayVO (cancels it if still queued). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	void StopVO(FGuid Handle);

	/**
	 * Try to play a BARK by tag at a location, honouring the line's per-bark cooldown. Returns false
	 * (no-op) if the bark is on cooldown, unresolved, or not flagged as a bark.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	bool TryBark(FGameplayTag BarkTag, FVector Location);
};
