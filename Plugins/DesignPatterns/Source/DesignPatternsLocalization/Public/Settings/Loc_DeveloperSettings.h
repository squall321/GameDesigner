// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Loc_DeveloperSettings.generated.h"

/**
 * One configured string-table binding: a designer-facing FGameplayTag namespace mapped to a registered
 * string-table id, plus the asset that backs it. The localization subsystem uses this to resolve
 * FindText(KeyTag) into FText::FromStringTable(TableId, KeyString) without callers knowing the table id.
 *
 * The KeyTag's leaf (the part below NamespaceRoot) is used as the string-table KEY, so a designer can
 * author tags like DP.Loc.UI.Confirm and have them resolve against the "DP_UI" table's "Confirm" row.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_StringTableBinding
{
	GENERATED_BODY()

	/**
	 * Tag namespace root. Any FindText key that is this tag OR a child of it routes to this binding.
	 * The portion of the key below this root (dot-joined) is used as the string-table key string.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Localization")
	FGameplayTag NamespaceRoot;

	/**
	 * The registered FName id of the string table (FStringTableRegistry::FindStringTable). Must match the
	 * id the table asset registers itself under. Designers set this to the table's StringTableId.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Localization")
	FName StringTableId;

	/**
	 * Soft reference to the string-table asset so it can be force-loaded (registering the table) before a
	 * lookup. Optional: if the table is already loaded/registered by other means this may be left unset.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Localization", meta = (AllowedClasses = "/Script/Engine.StringTable"))
	FSoftObjectPath StringTableAsset;
};

/**
 * Project-wide configuration for the DesignPatternsLocalization module. Appears under
 * Project Settings -> Plugins -> Design Patterns Localization. Editing here requires no code.
 *
 * Every tunable the localization + subtitle subsystems consult lives here (no magic numbers in code):
 * the requested default culture, subtitle pacing (duration-per-character + clamps), the on-screen cap,
 * and the string-table bindings FindText resolves against.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Localization"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULoc_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. May be null in extremely early load; callers fall back defensively. */
	static const ULoc_DeveloperSettings* Get();

	// --- Culture ---

	/**
	 * Culture code (e.g. "en", "fr-FR") the localization subsystem requests on Initialize. Empty means
	 * "leave the engine's resolved current culture untouched" (the engine default from platform/config).
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Culture")
	FString DefaultCulture;

	/**
	 * When true, SetCurrentCulture also persists the choice through the engine's localization config
	 * (FInternationalization save) so it survives a restart. When false the change is session-only.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Culture")
	bool bPersistCultureChange = true;

	// --- String tables (FindText backing) ---

	/** Designer-authored tag-namespace -> string-table bindings the localization subsystem resolves keys against. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "String Tables")
	TArray<FLoc_StringTableBinding> StringTableBindings;

	// --- Subtitle pacing ---

	/**
	 * Seconds of on-screen time added per character when a subtitle line has no explicit Duration. The
	 * subsystem computes Duration = Clamp(Length * this, Min, Max). Defensive fallback default below is
	 * used only if this CDO somehow cannot be read.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Subtitles", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SubtitleSecondsPerCharacter = 0.06f;

	/** Lower clamp for a computed subtitle duration (so very short lines still linger). */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Subtitles", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SubtitleMinDuration = 1.5f;

	/** Upper clamp for a computed subtitle duration (so very long lines do not stick forever). */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Subtitles", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float SubtitleMaxDuration = 12.0f;

	/** Maximum subtitle lines shown on screen simultaneously; lower-priority lines wait in the queue. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Subtitles", meta = (ClampMin = "1"))
	int32 MaxSubtitlesOnScreen = 2;
};
