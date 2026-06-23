// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "Loc_TextFormatTypes.generated.h"

/**
 * Blueprint-safe payload of named arguments for runtime FText formatting.
 *
 * The engine's native FFormatNamedArguments (a TMap<FString, FFormatArgumentValue>) is NOT a reflected /
 * Blueprint-exposable type, so this struct holds PARALLEL, BP-visible UPROPERTY maps keyed by argument
 * name, plus fluent setters. It carries NO UObject references, so it is safe to copy across the message
 * bus and pass through Blueprint. The non-reflected FFormatNamedArguments is assembled only inside the
 * native const helper BuildEngineArgs().
 *
 * Argument-type coverage mirrors what FText::Format supports for gendered / plural / numeric substitution:
 *  - Text   : an already-localized FText value (the common case; preserves nested localization).
 *  - Number : an integer (drives plural forms and number formatting per the active culture).
 *  - Float  : a real number (number formatting per the active culture).
 *  - Gender : an ETextGender (drives gendered FText branches authored with the engine's gender syntax).
 *
 * If the same name appears in more than one map, resolution priority is Text > Number > Float > Gender
 * (documented + deterministic), so a caller never gets undefined behavior from an accidental collision.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_FormatArgs
{
	GENERATED_BODY()

	/** Named FText arguments. Preferred for already-localized values (keeps nested localization intact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Format")
	TMap<FString, FText> TextArgs;

	/** Named integer arguments. Drive plural selection and culture-correct number formatting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Format")
	TMap<FString, int64> NumberArgs;

	/** Named real-number arguments. Culture-correct number formatting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Format")
	TMap<FString, float> FloatArgs;

	/** Named grammatical-gender arguments. Drive gendered FText branches. Stored as the engine ETextGender. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Format")
	TMap<FString, ETextGender> GenderArgs;

	FLoc_FormatArgs() = default;

	/** Fluent: set/overwrite an FText argument. */
	FLoc_FormatArgs& Set(const FString& Name, const FText& Value)
	{
		TextArgs.Add(Name, Value);
		return *this;
	}

	/** Fluent: set/overwrite an integer argument. */
	FLoc_FormatArgs& SetNumber(const FString& Name, int64 Value)
	{
		NumberArgs.Add(Name, Value);
		return *this;
	}

	/** Fluent: set/overwrite a real-number argument. */
	FLoc_FormatArgs& SetFloat(const FString& Name, float Value)
	{
		FloatArgs.Add(Name, Value);
		return *this;
	}

	/** Fluent: set/overwrite a grammatical-gender argument. */
	FLoc_FormatArgs& SetGender(const FString& Name, ETextGender Gender)
	{
		GenderArgs.Add(Name, Gender);
		return *this;
	}

	/** Total number of distinct argument names across all maps (collisions counted once). */
	int32 NumArgs() const
	{
		TSet<FString> Names;
		Names.Reserve(TextArgs.Num() + NumberArgs.Num() + FloatArgs.Num() + GenderArgs.Num());
		for (const TPair<FString, FText>& P : TextArgs)        { Names.Add(P.Key); }
		for (const TPair<FString, int64>& P : NumberArgs)      { Names.Add(P.Key); }
		for (const TPair<FString, float>& P : FloatArgs)       { Names.Add(P.Key); }
		for (const TPair<FString, ETextGender>& P : GenderArgs){ Names.Add(P.Key); }
		return Names.Num();
	}

	/**
	 * Assemble the engine-native FFormatNamedArguments from the parallel maps. Resolution priority on a
	 * name collision is Text > Number > Float > Gender. This is the only place the non-reflected engine
	 * type is constructed. Const + allocation-light; safe to call per format.
	 */
	FFormatNamedArguments BuildEngineArgs() const
	{
		FFormatNamedArguments Out;

		// Gender first (lowest priority) then override upward, so the documented priority holds.
		for (const TPair<FString, ETextGender>& P : GenderArgs)
		{
			Out.Add(P.Key, FFormatArgumentValue(P.Value));
		}
		for (const TPair<FString, float>& P : FloatArgs)
		{
			Out.Add(P.Key, FFormatArgumentValue(static_cast<double>(P.Value)));
		}
		for (const TPair<FString, int64>& P : NumberArgs)
		{
			Out.Add(P.Key, FFormatArgumentValue(P.Value));
		}
		for (const TPair<FString, FText>& P : TextArgs)
		{
			Out.Add(P.Key, FFormatArgumentValue(P.Value));
		}

		return Out;
	}
};
