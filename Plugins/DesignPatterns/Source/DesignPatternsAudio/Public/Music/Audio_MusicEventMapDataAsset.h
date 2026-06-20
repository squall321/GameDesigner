// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Audio_MusicEventMapDataAsset.generated.h"

class UAudio_MusicStateDataAsset;

/** What the director should do when a mapped bus event arrives. */
UENUM(BlueprintType)
enum class EAudio_MusicEventAction : uint8
{
	/** Crossfade to the rule's target music state (SetMusicState). */
	SetState,

	/** Fire the rule's stinger tag against the currently-active state (TriggerStinger). */
	TriggerStinger,

	/** Set the adaptive intensity to the rule's IntensityValue (SetIntensity). */
	SetIntensity
};

/**
 * One data-driven rule mapping a message-bus channel tag onto a music action.
 *
 * This is the decoupling seam between gameplay and music: combat (or any system) only broadcasts
 * neutral bus anchors like DP.Bus.Combat.Begin / DP.Bus.Combat.End; this rule says "when that
 * channel fires, crossfade to DP.Music.State.Combat". The audio module therefore NEVER includes a
 * combat (or any sibling) header — it reacts purely to tags.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_MusicEventRule
{
	GENERATED_BODY()

	/**
	 * Message-bus channel this rule listens on (e.g. DP.Bus.Combat.Begin). The director subscribes
	 * with ExactOrChild matching, so a rule on DP.Bus.Combat catches DP.Bus.Combat.* unless a more
	 * specific rule also matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventRule")
	FGameplayTag BusChannel;

	/** Which director action to perform when BusChannel fires. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventRule")
	EAudio_MusicEventAction Action = EAudio_MusicEventAction::SetState;

	/**
	 * Target music-state tag for Action == SetState (a DP.Music.State.* tag resolved through the
	 * data registry, or matched against the director's assigned playlist). Ignored for other actions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventRule",
		meta = (EditCondition = "Action == EAudio_MusicEventAction::SetState", EditConditionHides))
	FGameplayTag TargetStateTag;

	/**
	 * Stinger tag for Action == TriggerStinger (a DP.Music.Stinger.* tag resolved against the
	 * active state's stinger map). Ignored for other actions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventRule",
		meta = (EditCondition = "Action == EAudio_MusicEventAction::TriggerStinger", EditConditionHides))
	FGameplayTag StingerTag;

	/** Target normalized intensity for Action == SetIntensity. Ignored for other actions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventRule",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "Action == EAudio_MusicEventAction::SetIntensity", EditConditionHides))
	float IntensityValue = 0.0f;
};

/**
 * Data-driven table that maps message-bus events onto adaptive-music actions.
 *
 * The music director loads ONE of these (from project settings or assigned directly) and subscribes
 * to every distinct BusChannel in Rules. When a subscribed channel fires, the director applies the
 * matching rule(s). This is the ONLY place gameplay->music coupling is expressed, and it is pure
 * tag data — so games re-skin the reactive behaviour entirely in content with zero code, and the
 * audio module stays free of any sibling-module include.
 *
 * Optionally carries an inline playlist of music states so a project can ship a self-contained
 * "music set" (map + the states it references) without relying on a global registry scan.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_MusicEventMapDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UAudio_MusicEventMapDataAsset();

	/** The mapping rules. Evaluated in array order when multiple rules match one channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventMap")
	TArray<FAudio_MusicEventRule> Rules;

	/**
	 * Optional explicit playlist of states this map references. When non-empty the director resolves
	 * TargetStateTag against this list first (so a music set is fully self-contained); when empty it
	 * falls back to the core data registry. Soft so unused sets stay out of memory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventMap")
	TArray<TSoftObjectPtr<UAudio_MusicStateDataAsset>> Playlist;

	/**
	 * The state the director should start in when this map is installed (e.g. DP.Music.State.Explore).
	 * Empty = start silent until a rule fires.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|EventMap")
	FGameplayTag DefaultStateTag;

	/** Gather the set of distinct bus channels referenced by Rules (for subscription). */
	UFUNCTION(BlueprintCallable, Category = "Music|EventMap")
	void GetDistinctChannels(TArray<FGameplayTag>& OutChannels) const;

	/** Append every rule whose BusChannel matches Channel (exact or, when bAllowChildMatch, parent). */
	void GatherMatchingRules(FGameplayTag Channel, bool bAllowChildMatch, TArray<FAudio_MusicEventRule>& OutRules) const;

	//~ Begin UDP_DataAsset
	/** Own asset-manager bucket "Audio_MusicEventMap". */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
