// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Text/Loc_TextFormatter.h"

#include "Localization/Loc_LocalizationSubsystem.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Internationalization/TextFormatter.h"

FText ULoc_TextFormatter::FormatInternal(const FText& SourceFormat, const FLoc_FormatArgs& Args, bool& bAllArgsResolved)
{
	// Build an FTextFormat from the source; this parses the {named} argument tokens once.
	const FTextFormat Format(SourceFormat);

	// Determine whether every argument token the format references has a supplied argument. We never mutate
	// the format on a miss — the engine leaves an unmatched {token} verbatim, which is a visible, debuggable
	// fallback rather than blank output.
	TArray<FString> ReferencedNames;
	Format.GetFormatArgumentNames(ReferencedNames);

	bAllArgsResolved = true;
	const FFormatNamedArguments EngineArgs = Args.BuildEngineArgs();
	for (const FString& Name : ReferencedNames)
	{
		if (!EngineArgs.Contains(Name))
		{
			bAllArgsResolved = false;
			UE_LOG(LogDP, Verbose,
				TEXT("ULoc_TextFormatter: format argument '{%s}' was referenced but not supplied; leaving it intact."),
				*Name);
		}
	}

	return FText::Format(Format, EngineArgs);
}

FText ULoc_TextFormatter::FormatText(const FText& SourceFormat, const FLoc_FormatArgs& Args, bool& bAllArgsResolved)
{
	return FormatInternal(SourceFormat, Args, bAllArgsResolved);
}

FText ULoc_TextFormatter::SafeFormatNamed(const FText& Fmt, const FLoc_FormatArgs& Args)
{
	bool bUnused = false;
	return FormatInternal(Fmt, Args, bUnused);
}

FText ULoc_TextFormatter::FormatKey(const UObject* WorldContextObject, FGameplayTag Key, const FLoc_FormatArgs& Args, bool& bAllArgsResolved)
{
	bAllArgsResolved = false;

	if (!Key.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_TextFormatter::FormatKey called with an invalid key."));
		return FText::GetEmpty();
	}

	// Resolve the key through the live localization subsystem (which wraps FText::FromStringTable).
	ULoc_LocalizationSubsystem* Loc = FDP_SubsystemStatics::GetGameInstanceSubsystem<ULoc_LocalizationSubsystem>(WorldContextObject);
	if (!Loc)
	{
		// Defensive fallback: no localization subsystem reachable. Show the key's leaf so the UI is
		// diagnosable rather than blank, and report unresolved.
		FString KeyString = Key.ToString();
		int32 LastDot = INDEX_NONE;
		KeyString.FindLastChar(TEXT('.'), LastDot);
		const FString Leaf = (LastDot != INDEX_NONE) ? KeyString.RightChop(LastDot + 1) : KeyString;
		UE_LOG(LogDP, Verbose,
			TEXT("ULoc_TextFormatter::FormatKey: no localization subsystem reachable for key '%s'; returning leaf fallback."),
			*KeyString);
		return FText::FromString(Leaf);
	}

	bool bKeyFound = false;
	const FText Source = Loc->FindText(Key, bKeyFound);
	if (!bKeyFound)
	{
		// FindText already returns the engine's visible "missing entry" marker for an unknown row; format it
		// anyway (it has no placeholders) but report unresolved so QA tooling can flag the key.
		UE_LOG(LogDP, Verbose, TEXT("ULoc_TextFormatter::FormatKey: key '%s' did not resolve to a string-table row."),
			*Key.ToString());
		bAllArgsResolved = false;
		return Source;
	}

	return FormatInternal(Source, Args, bAllArgsResolved);
}

TArray<FString> ULoc_TextFormatter::GetReferencedArgumentNames(const FText& Fmt)
{
	const FTextFormat Format(Fmt);
	TArray<FString> Names;
	Format.GetFormatArgumentNames(Names);
	return Names;
}
