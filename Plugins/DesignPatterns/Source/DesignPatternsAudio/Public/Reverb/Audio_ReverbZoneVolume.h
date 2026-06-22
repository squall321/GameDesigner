// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "Misc/Guid.h"
#include "Audio_ReverbZoneVolume.generated.h"

class UAudio_ReverbMixProfileDataAsset;
class AActor;

/**
 * REVERB ZONES (2). A trigger volume that applies a reverb mix profile to the LOCAL listener while a
 * qualifying pawn is inside it.
 *
 * On a qualifying begin-overlap it pushes the reverb profile (asset or registry-resolved tag) onto
 * the sound manager's mix controller at ZonePriority via the EXISTING priority-stack/fade machinery,
 * stores the returned FGuid, and pops exactly that handle on end-overlap / EndPlay. Because the mix
 * stack is priority-ordered, nested or overlapping zones compose correctly (the highest-priority zone
 * is the active reverb) and unrelated pushes (combat ducking, pause) are never disturbed.
 *
 * Purely LOCAL / COSMETIC and NEVER replicated: each client evaluates the overlap against its own
 * listener pawn, so every machine hears its own correct reverb. There is no networked state and no
 * authority guard is needed (nothing here mutates replicated/authoritative state).
 *
 * The actual reverb effect chain is added/removed by UAudio_MixController::ApplyExtraSubmixEffects
 * when the pushed reverb profile becomes / stops being the active snapshot, wrapping the engine
 * AudioMixer. This actor never touches submixes directly.
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API AAudio_ReverbZoneVolume : public AVolume
{
	GENERATED_BODY()

public:
	AAudio_ReverbZoneVolume();

	//~ Begin AActor
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
	//~ End AActor

protected:
	/**
	 * The reverb profile pushed while a listener is inside the zone. Prefer assigning this directly;
	 * if null, ReverbProfileTag is resolved through the data registry on first overlap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reverb")
	TObjectPtr<UAudio_ReverbMixProfileDataAsset> ReverbProfile;

	/**
	 * Fallback reverb profile identity (child of DP.Audio.Mix) resolved from the data registry when
	 * ReverbProfile is not set. Lets a designer drop a zone and pick a profile by tag with no asset ref.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reverb", meta = (Categories = "DP.Audio.Mix"))
	FGameplayTag ReverbProfileTag;

	/**
	 * Priority override for the push onto the mix stack (>= 0 replaces the profile's own Priority; < 0
	 * keeps it). Higher wins, so a small interior zone can be authored to override a larger surrounding
	 * zone. A pure designer ordering value — no magic number in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reverb", meta = (ClampMin = "-1", UIMin = "-1", UIMax = "100"))
	int32 ZonePriority = -1;

	/**
	 * Optional blend-time override (seconds) used for this zone's push (drives the reverb effect fade).
	 * < 0 uses each effect entry's authored FadeTime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reverb", meta = (ClampMin = "-1.0", UIMax = "5.0"))
	float BlendTimeOverride = -1.f;

	/**
	 * When true, only an overlap from a LOCALLY-CONTROLLED pawn (the listener) applies the reverb,
	 * ignoring AI / remote pawns. When false, any pawn overlap applies it. True is correct for
	 * split-screen-aware, per-client reverb.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reverb")
	bool bOnlyLocalListener = true;

private:
	/** The active push handle while a qualifying pawn is inside (invalid when none / popped). */
	FGuid ActiveHandle;

	/** Count of qualifying overlaps currently inside, so the push survives until the LAST one leaves. */
	int32 OverlapRefCount = 0;

	/** Resolve the reverb profile (direct asset first, then registry by tag). Null if unresolved. */
	UAudio_ReverbMixProfileDataAsset* ResolveProfile() const;

	/** True if Other is a pawn that qualifies to drive this zone's reverb (honours bOnlyLocalListener). */
	bool DoesActorQualify(const AActor* Other) const;

	/** Push the profile if not already active. */
	void PushReverb();

	/** Pop the active push if any. */
	void PopReverb();
};
