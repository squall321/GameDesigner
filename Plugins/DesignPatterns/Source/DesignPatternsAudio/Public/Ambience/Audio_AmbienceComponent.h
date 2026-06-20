// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakInterfacePtr.h"
#include "Audio_AmbienceComponent.generated.h"

class UAudioComponent;
class USoundBase;
class UPrimitiveComponent;
class AActor;
class ISeam_SimClock;

/**
 * One time-of-day-keyed ambience bed variant.
 *
 * Environmental ambience often changes with the day phase — birdsong by day, crickets at night.
 * Each variant declares the normalized time-of-day window [TimeStart, TimeEnd) in which its bed is
 * the preferred one. The component picks the variant whose window contains the current time-of-day
 * (read softly from an ISeam_SimClock) and crossfades to it. With no clock present, the FIRST
 * variant is always used, so ambience works with zero dependency on the survival/day-night system.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_AmbienceVariant
{
	GENERATED_BODY()

	/** The looping ambience bed for this time window. Soft so unused beds don't load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience|Variant")
	TSoftObjectPtr<USoundBase> Bed;

	/**
	 * Normalized time-of-day [0,1) at which this variant becomes preferred (0 = midnight). Windows
	 * may wrap past 1.0 back to 0 when TimeEnd < TimeStart (e.g. a night bed 0.8 -> 0.2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience|Variant",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TimeStart = 0.0f;

	/** Normalized time-of-day [0,1) at which this variant stops being preferred. See TimeStart for wrap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience|Variant",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TimeEnd = 1.0f;

	/** Per-variant volume scalar (1.0 = unity), applied on top of the component's AmbienceVolume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience|Variant",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 1.0f;

	/** True when NormalizedTime falls within [TimeStart, TimeEnd), correctly handling wrap. */
	bool ContainsTime(float NormalizedTime) const;
};

/** How the ambience bed is gated on/off. */
UENUM(BlueprintType)
enum class EAudio_AmbienceTrigger : uint8
{
	/** Fade in when a pawn/listener overlaps the owner's trigger primitive; fade out on end-overlap. */
	OnOverlap,

	/** Play continuously from BeginPlay (e.g. a global level ambience component); no overlap gating. */
	Always
};

/**
 * Environmental ambience bed for a volume / area.
 *
 * Place on a trigger actor (or any actor with a collision primitive). When a listener pawn enters
 * the trigger the component fades its looping bed IN; on exit it fades OUT — so walking into a cave,
 * forest or room smoothly swaps the soundscape. Optional TIME-OF-DAY variation crossfades between
 * day/night beds using the shared ISeam_SimClock, resolved softly so the component works fully
 * WITHOUT any clock present (it just always uses the first variant).
 *
 * Purely LOCAL / COSMETIC: no replication. Overlap is evaluated on each client from its own listener
 * pawn, so every machine hears the correct local ambience without any networked state.
 */
UCLASS(ClassGroup = (DesignPatternsAudio), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAUDIO_API UAudio_AmbienceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAudio_AmbienceComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	// ---- Public control ----

	/** Manually fade the ambience in (overriding overlap gating until end-overlap or FadeOut). */
	UFUNCTION(BlueprintCallable, Category = "Ambience")
	void FadeIn();

	/** Manually fade the ambience out. */
	UFUNCTION(BlueprintCallable, Category = "Ambience")
	void FadeOut();

	/** True while the bed is audible (fully in or fading in). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Ambience")
	bool IsActive() const { return bDesiredActive; }

	/** Provide the simulation clock seam explicitly (otherwise it is resolved softly on BeginPlay). */
	UFUNCTION(BlueprintCallable, Category = "Ambience")
	void SetSimClock(const TScriptInterface<ISeam_SimClock>& InClock);

protected:
	// ---- Tunables ----

	/** How the bed is gated. OnOverlap requires the owner to have a collision primitive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience")
	EAudio_AmbienceTrigger TriggerMode = EAudio_AmbienceTrigger::OnOverlap;

	/**
	 * Ambience bed variants. With a clock, the variant whose time window contains the current
	 * time-of-day is chosen and crossfaded to. Without a clock (or with one variant), the first is
	 * always used. Provide exactly one for a non-time-varying bed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience")
	TArray<FAudio_AmbienceVariant> Variants;

	/** Master volume for this component's ambience (multiplies each variant's own Volume). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float AmbienceVolume = 1.0f;

	/** Seconds to fade the bed in on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience",
		meta = (ClampMin = "0.0", Units = "s"))
	float FadeInSeconds = 1.5f;

	/** Seconds to fade the bed out on deactivation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience",
		meta = (ClampMin = "0.0", Units = "s"))
	float FadeOutSeconds = 2.0f;

	/** Seconds to crossfade when the time-of-day variant changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience",
		meta = (ClampMin = "0.0", Units = "s"))
	float VariantCrossfadeSeconds = 4.0f;

	/**
	 * When TriggerMode == OnOverlap, only overlaps from actors with a pawn that is locally controlled
	 * (the listener) gate the ambience, ignoring AI / projectiles. When false, ANY pawn overlap gates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience")
	bool bOnlyLocalListener = true;

	/**
	 * Service-locator tag under which the simulation clock seam is registered, if a project exposes
	 * it there. Used as a soft fallback when no clock is set explicitly and none is found on the
	 * world. Leave empty to skip locator resolution.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambience|Clock")
	FGameplayTag SimClockServiceTag;

private:
	/** The active looping bed voice (one at a time; a second is used transiently while crossfading). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> CurrentVoice = nullptr;

	/** The outgoing voice during a variant crossfade (fading to silence, then released). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> OutgoingVoice = nullptr;

	/** Soft (non-owning) reference to the simulation clock seam; works fine when null. */
	TWeakInterfacePtr<ISeam_SimClock> SimClock;

	/** Desired audible state (set by overlap / FadeIn / FadeOut). */
	bool bDesiredActive = false;

	/** Current master fade alpha [0,1] applied to the active bed (0 = silent, 1 = full). */
	float FadeAlpha = 0.0f;

	/** Index into Variants currently playing on CurrentVoice (INDEX_NONE when nothing plays). */
	int32 ActiveVariantIndex = INDEX_NONE;

	/** Crossfade alpha [0,1] for the outgoing voice (1 = just started fading out, 0 = silent). */
	float OutgoingFadeAlpha = 0.0f;

	/** Count of qualifying overlaps currently inside the trigger (OnOverlap mode). */
	int32 OverlapRefCount = 0;

	// ---- Internals ----

	/** Bind begin/end overlap on the owner's collision primitive (OnOverlap mode). */
	void BindOverlaps();

	/** Resolve the clock seam softly: explicit -> service locator -> world game state / actors. */
	void ResolveSimClock();

	/** Choose which variant index should play right now from the clock's time-of-day. */
	int32 SelectVariantForTime() const;

	/** Start (or crossfade to) the given variant index on a fresh voice. */
	void PlayVariant(int32 VariantIndex);

	/** Create a 2D looping audio component for Sound (persistent, manually driven). */
	UAudioComponent* CreateVoice(USoundBase* Sound) const;

	/** True if the overlapping actor qualifies to gate this ambience. */
	bool DoesActorQualify(const AActor* Other) const;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
