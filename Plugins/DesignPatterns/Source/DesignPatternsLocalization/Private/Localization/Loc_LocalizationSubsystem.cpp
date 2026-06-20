// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Localization/Loc_LocalizationSubsystem.h"

#include "DesignPatternsLocalizationModule.h"
#include "Settings/Loc_DeveloperSettings.h"
#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Engine/GameInstance.h"

#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/StringTableCore.h"
#include "Engine/StringTable.h"
#include "Misc/ConfigCacheIni.h"

void ULoc_LocalizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache the engine's current culture up front so the first GetCurrentCulture / debug string is cheap.
	CachedCulture = FInternationalization::Get().GetCurrentCulture()->GetName();

	ApplyDefaultCulture();
	PublishToServiceLocator();

	UE_LOG(LogDP, Log, TEXT("ULoc_LocalizationSubsystem initialized (culture='%s')."), *CachedCulture);
}

void ULoc_LocalizationSubsystem::Deinitialize()
{
	UnpublishFromServiceLocator();
	OnCultureChanged.Clear();

	Super::Deinitialize();
}

void ULoc_LocalizationSubsystem::ApplyDefaultCulture()
{
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	if (!Settings)
	{
		// Defensive: with no settings CDO we leave the engine-resolved culture untouched.
		return;
	}

	if (Settings->DefaultCulture.IsEmpty())
	{
		// Empty means "respect the engine/platform default" — do nothing.
		return;
	}

	if (Settings->DefaultCulture.Equals(CachedCulture, ESearchCase::IgnoreCase))
	{
		return;
	}

	if (!SetCurrentCulture(Settings->DefaultCulture))
	{
		UE_LOG(LogDP, Warning,
			TEXT("ULoc_LocalizationSubsystem: configured DefaultCulture '%s' was rejected by the engine; staying on '%s'."),
			*Settings->DefaultCulture, *CachedCulture);
	}
}

FString ULoc_LocalizationSubsystem::GetCurrentCulture() const
{
	const FCulturePtr Current = FInternationalization::Get().GetCurrentCulture();
	return Current.IsValid() ? Current->GetName() : FString();
}

bool ULoc_LocalizationSubsystem::SetCurrentCulture(const FString& CultureCode)
{
	if (CultureCode.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("ULoc_LocalizationSubsystem::SetCurrentCulture rejected an empty culture code."));
		return false;
	}

	FInternationalization& I18N = FInternationalization::Get();

	const FString PreviousCulture = I18N.GetCurrentCulture()->GetName();
	if (CultureCode.Equals(PreviousCulture, ESearchCase::IgnoreCase))
	{
		// Already on this culture — treat as success, no broadcast.
		CachedCulture = PreviousCulture;
		return true;
	}

	// Wrap the engine call. SetCurrentCulture returns false if the code is not an available culture.
	if (!I18N.SetCurrentCulture(CultureCode))
	{
		UE_LOG(LogDP, Warning, TEXT("ULoc_LocalizationSubsystem::SetCurrentCulture('%s') was rejected by the engine."), *CultureCode);
		return false;
	}

	CachedCulture = I18N.GetCurrentCulture()->GetName();

	// Optionally persist the choice through the engine localization config so it survives a restart.
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const bool bPersist = Settings ? Settings->bPersistCultureChange : false;
	if (bPersist)
	{
		// Persist by writing the culture into the [Internationalization] section of the user-settings ini
		// — exactly the key the engine reads on startup to restore the player's language choice. Using
		// GConfig directly (rather than an engine helper whose availability varies by minor version) keeps
		// this stable across UE 5.3-5.5.
		if (GConfig)
		{
			GConfig->SetString(TEXT("Internationalization"), TEXT("Culture"), *CachedCulture, GGameUserSettingsIni);
			GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
		}
	}

	UE_LOG(LogDP, Log, TEXT("ULoc_LocalizationSubsystem: culture changed '%s' -> '%s' (persist=%s)."),
		*PreviousCulture, *CachedCulture, bPersist ? TEXT("true") : TEXT("false"));

	OnCultureChanged.Broadcast(CachedCulture);
	return true;
}

TArray<FString> ULoc_LocalizationSubsystem::GetAvailableCultures() const
{
	TArray<FString> Result;

	// Gather the culture names the engine knows about (localized + game cultures).
	FInternationalization::Get().GetCultureNames(/*out*/ Result);

	// GetCultureNames already returns culture name strings; ensure uniqueness defensively.
	TSet<FString> Seen;
	Result.RemoveAll([&Seen](const FString& Name)
	{
		bool bAlready = false;
		Seen.Add(Name, &bAlready);
		return bAlready;
	});

	return Result;
}

FText ULoc_LocalizationSubsystem::FindText(FGameplayTag Key, bool& bFound) const
{
	bFound = false;

	if (!Key.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_LocalizationSubsystem::FindText called with an invalid key."));
		return FText::GetEmpty();
	}

	FString LeafKey;
	const FLoc_StringTableBinding* Binding = FindBindingForKey(Key, /*out*/ LeafKey);
	if (!Binding)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_LocalizationSubsystem::FindText: no string-table binding matches key '%s'."), *Key.ToString());
		return FText::GetEmpty();
	}

	if (Binding->StringTableId.IsNone())
	{
		UE_LOG(LogDP, Warning, TEXT("ULoc_LocalizationSubsystem::FindText: binding for '%s' has no StringTableId."), *Key.ToString());
		return FText::GetEmpty();
	}

	// Make sure the table asset is loaded/registered before resolving against it.
	EnsureTableRegistered(*Binding);

	// If NamespaceRoot == Key (empty leaf), fall back to the key's own leaf segment as the row key
	// (the text after the final '.', or the whole tag if it has no dot).
	FString RowKey = LeafKey;
	if (RowKey.IsEmpty())
	{
		const FString FullKey = Key.ToString();
		int32 LastDot = INDEX_NONE;
		if (FullKey.FindLastChar(TEXT('.'), LastDot))
		{
			RowKey = FullKey.RightChop(LastDot + 1);
		}
		else
		{
			RowKey = FullKey;
		}
	}

	// FText::FromStringTable resolves through FStringTableRegistry; for an unknown row the engine returns
	// a visible "missing entry" marker rather than crashing, so we still report bFound=true (binding matched).
	FText Result = FText::FromStringTable(Binding->StringTableId, RowKey);
	bFound = true;
	return Result;
}

FText ULoc_LocalizationSubsystem::FindTextSimple(FGameplayTag Key) const
{
	bool bUnused = false;
	return FindText(Key, bUnused);
}

const FLoc_StringTableBinding* ULoc_LocalizationSubsystem::FindBindingForKey(const FGameplayTag& Key, FString& OutLeafKey) const
{
	OutLeafKey.Reset();

	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	if (!Settings)
	{
		return nullptr;
	}

	const FLoc_StringTableBinding* Best = nullptr;
	int32 BestDepth = -1;

	const FString KeyString = Key.ToString();

	for (const FLoc_StringTableBinding& Binding : Settings->StringTableBindings)
	{
		if (!Binding.NamespaceRoot.IsValid())
		{
			continue;
		}

		// Match when Key equals the root or is a child of it (hierarchy-aware).
		const bool bMatches = (Key == Binding.NamespaceRoot) ||
			Key.MatchesTag(Binding.NamespaceRoot);
		if (!bMatches)
		{
			continue;
		}

		// Prefer the deepest (most specific) NamespaceRoot when several ancestors match.
		const FString RootString = Binding.NamespaceRoot.ToString();
		int32 Depth = 0;
		for (TCHAR Ch : RootString)
		{
			if (Ch == TEXT('.'))
			{
				++Depth;
			}
		}

		if (Depth > BestDepth)
		{
			BestDepth = Depth;
			Best = &Binding;

			// Compute the leaf: the remainder of the key string below the root, dropping the leading dot.
			if (KeyString.Len() > RootString.Len() && KeyString.StartsWith(RootString))
			{
				OutLeafKey = KeyString.RightChop(RootString.Len() + 1 /*the '.' */);
			}
			else
			{
				OutLeafKey.Reset();
			}
		}
	}

	return Best;
}

void ULoc_LocalizationSubsystem::EnsureTableRegistered(const FLoc_StringTableBinding& Binding) const
{
	if (Binding.StringTableId.IsNone())
	{
		return;
	}

	// Already registered? Nothing to do.
	if (FStringTableRegistry::Get().FindStringTable(Binding.StringTableId).IsValid())
	{
		return;
	}

	// Force-load the asset; a UStringTable registers itself with the registry on load.
	if (Binding.StringTableAsset.IsValid())
	{
		UObject* Loaded = Binding.StringTableAsset.TryLoad();
		if (Loaded == nullptr)
		{
			UE_LOG(LogDP, Warning, TEXT("ULoc_LocalizationSubsystem: failed to load string-table asset '%s' for id '%s'."),
				*Binding.StringTableAsset.ToString(), *Binding.StringTableId.ToString());
		}
	}
}

void ULoc_LocalizationSubsystem::PublishToServiceLocator()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_ServiceLocatorSubsystem>() : nullptr)
	{
		// Weak-observed: the locator must not keep a (game-instance-lifetime) subsystem alive itself.
		Locator->RegisterService(DPLocTags::Service_Localization, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void ULoc_LocalizationSubsystem::UnpublishFromServiceLocator()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
		{
			Locator->UnregisterService(DPLocTags::Service_Localization);
		}
	}
}

FString ULoc_LocalizationSubsystem::GetDPDebugString_Implementation() const
{
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const int32 BindingCount = Settings ? Settings->StringTableBindings.Num() : 0;
	return FString::Printf(TEXT("Localization: culture='%s' bindings=%d"), *GetCurrentCulture(), BindingCount);
}
