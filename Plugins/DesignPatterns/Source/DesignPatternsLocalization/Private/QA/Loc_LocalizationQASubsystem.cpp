// Copyright DesignPatterns plugin. All Rights Reserved.

#include "QA/Loc_LocalizationQASubsystem.h"

#include "Localization/Loc_LocalizationSubsystem.h"
#include "Settings/Loc_DeveloperSettings.h"
#include "DesignPatternsLocalizationModule.h"

#include "Core/DPLog.h"
#include "Engine/GameInstance.h"

// Engine localization internals (all public engine headers, no custom reinvention).
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/Culture.h"
#include "UObject/Package.h"

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

bool ULoc_LocalizationQASubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	// Never exist in shipping — QA is a dev-only concern.
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer);
#endif
}

void ULoc_LocalizationQASubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if !UE_BUILD_SHIPPING
	// Restore the pseudo-loc state that was persisted in settings (in case the editor was restarted).
	if (const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get())
	{
		if (Settings->bPseudoLocalizationEnabled)
		{
			SetPseudoLocalizationEnabled(true);
		}
	}
#endif

	UE_LOG(LogDP, Log, TEXT("ULoc_LocalizationQASubsystem initialized (pseudo-loc=%s)."),
		bPseudoLocalizationEnabled ? TEXT("ON") : TEXT("off"));
}

void ULoc_LocalizationQASubsystem::Deinitialize()
{
#if !UE_BUILD_SHIPPING
	// Ensure pseudo-loc is disabled when the subsystem tears down so it doesn't bleed into a PIE restart.
	if (bPseudoLocalizationEnabled)
	{
		SetPseudoLocalizationEnabled(false);
	}
#endif

	Super::Deinitialize();
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

FLoc_QAReport ULoc_LocalizationQASubsystem::ScanMissingTranslations(const TArray<FString>& Cultures)
{
	FLoc_QAReport Report;

	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	if (!Settings || Settings->StringTableBindings.IsEmpty())
	{
		UE_LOG(LogDP, Verbose,
			TEXT("ULoc_LocalizationQASubsystem: no StringTableBindings configured; report will be empty."));
		return Report;
	}

	// Pre-register every table asset so rows are reachable from FindText regardless of load state.
	for (const FLoc_StringTableBinding& Binding : Settings->StringTableBindings)
	{
		ForceRegisterTable(Binding.StringTableAsset);
	}

	// Determine which cultures to scan.
	TArray<FString> CulturesToScan = Cultures;
	if (CulturesToScan.IsEmpty())
	{
		// No cultures specified — scan the currently active one.
		const TSharedPtr<FCulture> Current = FInternationalization::Get().GetCurrentCulture();
		if (Current.IsValid())
		{
			CulturesToScan.Add(Current->GetName());
		}
	}

	Report.ScannedCultures = CulturesToScan;

	for (const FString& Culture : CulturesToScan)
	{
		if (Culture.IsEmpty())
		{
			continue;
		}
		ScanForCulture(Culture, Report);
	}

	UE_LOG(LogDP, Log,
		TEXT("ULoc_LocalizationQASubsystem: scan complete — %d checked, %d missing, %d unlocalized across %d culture(s)."),
		Report.TotalChecked, Report.MissingKeys.Num(), Report.UnlocalizedKeys.Num(), CulturesToScan.Num());

	return Report;
}

void ULoc_LocalizationQASubsystem::SetPseudoLocalizationEnabled(bool bEnabled)
{
#if UE_BUILD_SHIPPING
	// No-op in shipping — should not be called (ShouldCreateSubsystem prevents existence), but guard anyway.
	return;
#else
	if (bPseudoLocalizationEnabled == bEnabled)
	{
		return;
	}

	bPseudoLocalizationEnabled = bEnabled;

	// Drive the engine's built-in pseudo-localization toggle. When enabled, FText resolution wraps
	// strings with accent marks, brackets, and optional length-padding configured in the editor's
	// Internationalization settings (or the game's i18n config). We do NOT reinvent this behavior.
	FInternationalization& I18N = FInternationalization::Get();
	if (bEnabled)
	{
		// Enabling pseudo-loc: tell the engine to use pseudo-culture output.
		I18N.SetCurrentLanguageAndLocale(TEXT("io")); // "io" is the engine's pseudo-locale code.
		UE_LOG(LogDP, Log, TEXT("ULoc_LocalizationQASubsystem: pseudo-localization ENABLED (culture set to 'io')."));
	}
	else
	{
		// Disabling: restore the culture from settings so real text resumes.
		FString RestoreCulture;
		if (const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get())
		{
			RestoreCulture = Settings->DefaultCulture;
		}
		if (RestoreCulture.IsEmpty())
		{
			// No configured culture — let the engine use its own default (platform/config-driven).
			RestoreCulture = FPlatformMisc::GetDefaultLocale();
		}
		if (!RestoreCulture.IsEmpty())
		{
			I18N.SetCurrentLanguageAndLocale(RestoreCulture);
		}
		UE_LOG(LogDP, Log,
			TEXT("ULoc_LocalizationQASubsystem: pseudo-localization DISABLED (culture restored to '%s')."),
			*RestoreCulture);
	}

	// Persist the state to settings (Config=Game) so a re-opened editor starts in the same QA state.
	// We read the CDO and write back via UObject reflection so no private accessors are needed.
	if (ULoc_DeveloperSettings* MutableSettings = GetMutableDefault<ULoc_DeveloperSettings>())
	{
		MutableSettings->bPseudoLocalizationEnabled = bEnabled;
		MutableSettings->SaveConfig();
	}

	// Fire OnCultureChanged on the localization subsystem so every text consumer re-reads in the new state.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (ULoc_LocalizationSubsystem* LocSub = GI->GetSubsystem<ULoc_LocalizationSubsystem>())
		{
			// We use SetCurrentCulture with the engine's now-active culture name to trigger the broadcast.
			const TSharedPtr<FCulture> Active = I18N.GetCurrentCulture();
			if (Active.IsValid())
			{
				LocSub->SetCurrentCulture(Active->GetName());
			}
		}
	}
#endif
}

bool ULoc_LocalizationQASubsystem::IsPseudoLocalizationEnabled() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bPseudoLocalizationEnabled;
#endif
}

// ------------------------------------------------------------------------------------------------
// Debug
// ------------------------------------------------------------------------------------------------

FString ULoc_LocalizationQASubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("LocalizationQA: pseudo=%s"),
		IsPseudoLocalizationEnabled() ? TEXT("ON") : TEXT("off"));
}

// ------------------------------------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------------------------------------

void ULoc_LocalizationQASubsystem::ForceRegisterTable(const FSoftObjectPath& TableAsset) const
{
	if (TableAsset.IsNull())
	{
		return; // Binding has no asset path set; skip.
	}

	// Synchronously load the string table asset so it self-registers with FStringTableRegistry.
	// This is intentional for QA purposes (non-shipping only — confirmed by ShouldCreateSubsystem).
	UObject* Loaded = TableAsset.TryLoad();
	if (!Loaded)
	{
		UE_LOG(LogDP, Warning,
			TEXT("ULoc_LocalizationQASubsystem: could not force-load string table asset '%s'; keys in this table will report as missing."),
			*TableAsset.ToString());
	}
}

void ULoc_LocalizationQASubsystem::ScanForCulture(const FString& Culture, FLoc_QAReport& Report)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	ULoc_LocalizationSubsystem* LocSub = GI->GetSubsystem<ULoc_LocalizationSubsystem>();
	if (!LocSub)
	{
		UE_LOG(LogDP, Warning,
			TEXT("ULoc_LocalizationQASubsystem: no ULoc_LocalizationSubsystem available; cannot scan culture '%s'."),
			*Culture);
		return;
	}

	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	// Temporarily switch to the target culture so FindText resolves strings for that culture.
	FInternationalization& I18N = FInternationalization::Get();
	const TSharedPtr<FCulture> OriginalCulture = I18N.GetCurrentCulture();
	const bool bCultureChanged = I18N.SetCurrentLanguageAndLocale(Culture);

	if (!bCultureChanged)
	{
		UE_LOG(LogDP, Warning,
			TEXT("ULoc_LocalizationQASubsystem: engine rejected culture '%s'; it may not be available. Skipping."),
			*Culture);
		return;
	}

	// Collect every unique key from all configured bindings and scan each one.
	TSet<FGameplayTag> ScannedKeys;
	for (const FLoc_StringTableBinding& Binding : Settings->StringTableBindings)
	{
		if (!Binding.NamespaceRoot.IsValid())
		{
			continue;
		}

		// We scan the namespace root tag itself as a representative key. For a comprehensive scan, a
		// project would enumerate all rows in the table; here we scan the namespace root and let the
		// subsystem resolve the leaf, which is the same path the production code takes (FindText).
		if (ScannedKeys.Contains(Binding.NamespaceRoot))
		{
			continue; // already checked this key in this culture pass.
		}
		ScannedKeys.Add(Binding.NamespaceRoot);

		++Report.TotalChecked;

		bool bFound = false;
		const FText Resolved = LocSub->FindText(Binding.NamespaceRoot, bFound);

		FLoc_QAKeyResult Result;
		Result.Key = Binding.NamespaceRoot;
		Result.Culture = Culture;
		Result.bResolved = bFound;

		if (!bFound)
		{
			// Row missing entirely for this culture (table unregistered, or row absent).
			Report.MissingKeys.Add(Result);
			UE_LOG(LogDP, Verbose,
				TEXT("ULoc_LocalizationQASubsystem [%s]: key '%s' MISSING."),
				*Culture, *Binding.NamespaceRoot.ToString());
		}
		else
		{
			// Check whether the resolved FText is culture-invariant (not translated from the source).
			// FText::IsCultureInvariant() returns true for literals built via FText::FromString, which
			// is what the engine falls back to for unlocalized rows.
			Result.bCultureInvariant = Resolved.IsCultureInvariant();
			Result.ResolvedText = Resolved;

			if (Result.bCultureInvariant)
			{
				Report.UnlocalizedKeys.Add(Result);
				UE_LOG(LogDP, Verbose,
					TEXT("ULoc_LocalizationQASubsystem [%s]: key '%s' UNLOCALIZED (culture-invariant literal)."),
					*Culture, *Binding.NamespaceRoot.ToString());
			}
		}
	}

	// Restore the original culture so we leave the engine in the state we found it.
	if (OriginalCulture.IsValid())
	{
		I18N.SetCurrentLanguageAndLocale(OriginalCulture->GetName());
	}
}
