// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Text/Loc_TextFormatTypes.h"
#include "Loc_TextFormatter.generated.h"

/**
 * Stateless dynamic-text helper that wraps the engine's FText::Format / FTextFormat WITHOUT reinventing
 * FText. It is the single shared owner of the "localized text + runtime args" overlap (used by UI, HUD,
 * subtitles): any module that depends on DesignPatternsLocalization can format a localized string with
 * named arguments, gendered / plural variants, and culture-correct number formatting through one API.
 *
 * It is NOT a seam — it is a pure library. Cross-module callers reach it because they depend on this
 * module's public header; nothing here resolves a service or touches the world beyond the localization
 * subsystem (resolved null-safely for the key-based overload).
 *
 * SAFETY CONTRACT: every entry point reports whether all referenced format arguments were satisfied
 * (bAllArgsResolved), and on a missing-argument / malformed-format condition logs (verbose) and returns
 * a best-effort result rather than an empty or crashing value — so a missing arg never produces blank UI.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_TextFormatter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolve a localization key to its FText via the live ULoc_LocalizationSubsystem (FindText), then
	 * format it with Args. If no localization subsystem is reachable (module removed / early boot) the key
	 * cannot resolve and the function returns FText::FromString of the key's leaf as a visible fallback so
	 * the UI shows something diagnosable rather than blank.
	 *
	 * @param WorldContextObject  Any object with a world (to reach the GameInstance localization subsystem).
	 * @param Key                 The localization key tag (e.g. DP.Loc.UI.GreetByName).
	 * @param Args                Named arguments to substitute.
	 * @param bAllArgsResolved    Set true iff the source resolved AND every {named} token it contains had a
	 *                            matching argument supplied; false otherwise.
	 * @return The formatted FText (or a best-effort fallback as documented).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Localization|Format",
		meta = (WorldContext = "WorldContextObject"))
	static FText FormatKey(const UObject* WorldContextObject, FGameplayTag Key, const FLoc_FormatArgs& Args, bool& bAllArgsResolved);

	/**
	 * Format an already-resolved FText source (treated as an FTextFormat) with Args. Use this when the
	 * caller already holds the localized format string (e.g. from a data asset FText field).
	 *
	 * @param SourceFormat        The localized format text containing {named} argument tokens.
	 * @param Args                Named arguments to substitute.
	 * @param bAllArgsResolved    Set true iff every {named} token in SourceFormat had a matching argument.
	 * @return The formatted FText.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Localization|Format")
	static FText FormatText(const FText& SourceFormat, const FLoc_FormatArgs& Args, bool& bAllArgsResolved);

	/**
	 * Convenience wrapper around FormatText that discards the resolution flag. Always returns a value; on a
	 * missing argument the unmatched {named} token is left intact by the engine (never blank).
	 *
	 * @param Fmt  The localized format text.
	 * @param Args Named arguments to substitute.
	 * @return The formatted FText.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization|Format")
	static FText SafeFormatNamed(const FText& Fmt, const FLoc_FormatArgs& Args);

	/**
	 * Pure helper: list the argument names a format text references (its {named} argument tokens). Useful for
	 * QA / authoring tools that want to validate an FLoc_FormatArgs against a format string. Wraps the
	 * engine FTextFormat::GetFormatArgumentNames so callers never parse braces themselves.
	 *
	 * @param Fmt The localized format text to inspect.
	 * @return The distinct argument names referenced by Fmt, in engine order.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization|Format")
	static TArray<FString> GetReferencedArgumentNames(const FText& Fmt);

private:
	/**
	 * Shared core: build an FTextFormat from SourceFormat, compute whether every referenced argument token is
	 * present in Args, and produce the formatted FText. Centralizes the resolution-flag logic so both public
	 * entry points behave identically.
	 */
	static FText FormatInternal(const FText& SourceFormat, const FLoc_FormatArgs& Args, bool& bAllArgsResolved);
};
