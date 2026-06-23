// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Font/Loc_FontSubsystem.h"

#include "Font/Loc_FontProfileDataAsset.h"
#include "Localization/Loc_LocalizationSubsystem.h"
#include "DesignPatternsLocalizationModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"

void ULoc_FontSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BindCultureChanged();

	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		SelectProfileForCulture(Loc->GetCurrentCulture());
	}

	PublishToServiceLocator();

	UE_LOG(LogDP, Log, TEXT("ULoc_FontSubsystem initialized (profile culture='%s')."), *GetActiveProfileCulture());
}

void ULoc_FontSubsystem::Deinitialize()
{
	UnpublishFromServiceLocator();
	UnbindCultureChanged();

	ActiveProfile = nullptr;

	Super::Deinitialize();
}

void ULoc_FontSubsystem::BindCultureChanged()
{
	if (bCultureBound)
	{
		return;
	}
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		Loc->OnCultureChanged.AddDynamic(this, &ULoc_FontSubsystem::HandleCultureChanged);
		bCultureBound = true;
	}
}

void ULoc_FontSubsystem::UnbindCultureChanged()
{
	if (!bCultureBound)
	{
		return;
	}
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		Loc->OnCultureChanged.RemoveDynamic(this, &ULoc_FontSubsystem::HandleCultureChanged);
	}
	bCultureBound = false;
}

void ULoc_FontSubsystem::HandleCultureChanged(const FString& NewCulture)
{
	SelectProfileForCulture(NewCulture);
}

void ULoc_FontSubsystem::SelectProfileForCulture(const FString& CultureCode)
{
	ActiveProfile = nullptr;

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		return;
	}

	const FPrimaryAssetType ProfileType(FName(TEXT("Loc_FontProfile")));
	TArray<FPrimaryAssetId> Ids;
	AssetManager->GetPrimaryAssetIdList(ProfileType, Ids);
	if (Ids.Num() == 0)
	{
		return;
	}

	FString LanguagePrefix = CultureCode;
	int32 DashIdx = INDEX_NONE;
	if (CultureCode.FindChar(TEXT('-'), DashIdx))
	{
		LanguagePrefix = CultureCode.Left(DashIdx);
	}

	ULoc_FontProfileDataAsset* ExactMatch = nullptr;
	ULoc_FontProfileDataAsset* PrefixMatch = nullptr;

	for (const FPrimaryAssetId& Id : Ids)
	{
		const FSoftObjectPath Path = AssetManager->GetPrimaryAssetPath(Id);
		if (!Path.IsValid())
		{
			continue;
		}
		ULoc_FontProfileDataAsset* Profile = Cast<ULoc_FontProfileDataAsset>(Path.TryLoad());
		if (!Profile)
		{
			continue;
		}

		if (Profile->Culture.Equals(CultureCode, ESearchCase::IgnoreCase))
		{
			ExactMatch = Profile;
			break;
		}
		if (!PrefixMatch && Profile->Culture.Equals(LanguagePrefix, ESearchCase::IgnoreCase))
		{
			PrefixMatch = Profile;
		}
	}

	ActiveProfile = ExactMatch ? ExactMatch : PrefixMatch;
}

TSoftObjectPtr<UObject> ULoc_FontSubsystem::ResolveFaceForRole(FGameplayTag Role, FName& OutTypeface, int32& OutSize) const
{
	OutTypeface = NAME_None;
	OutSize = 18;

	if (!ActiveProfile)
	{
		return TSoftObjectPtr<UObject>();
	}

	// Default first.
	OutTypeface = ActiveProfile->DefaultTypefaceName;
	OutSize = FMath::Max(1, ActiveProfile->DefaultSizePoints);
	TSoftObjectPtr<UObject> Face = ActiveProfile->DefaultFontFace;

	// Role override wins if present.
	if (const FLoc_FontRoleOverride* Override = ActiveProfile->FindRoleOverride(Role))
	{
		if (!Override->FontFace.IsNull())
		{
			Face = Override->FontFace;
		}
		if (Override->TypefaceName != NAME_None)
		{
			OutTypeface = Override->TypefaceName;
		}
		OutSize = FMath::Max(1, Override->SizePoints);
	}

	return Face;
}

FSlateFontInfo ULoc_FontSubsystem::GetCultureFont(FGameplayTag FontRole) const
{
	FName Typeface = NAME_None;
	int32 Size = 18;
	TSoftObjectPtr<UObject> FaceSoft = ResolveFaceForRole(FontRole, Typeface, Size);

	// Load the face synchronously (cosmetic; called from UI paint/construct paths). Null face => engine
	// default FSlateFontInfo so the UI never renders empty.
	UObject* FaceObj = FaceSoft.IsNull() ? nullptr : FaceSoft.LoadSynchronous();

	FSlateFontInfo Info;
	if (const UFont* AsFont = Cast<UFont>(FaceObj))
	{
		Info = FSlateFontInfo(AsFont, Size, Typeface);
	}
	else if (FaceObj) // UFontFace or any other font-face object the engine accepts by object + typeface
	{
		Info = FSlateFontInfo(FaceObj, Size, Typeface);
	}
	else
	{
		// Documented inert fallback: a default-constructed FSlateFontInfo resolves to the engine font.
		Info = FSlateFontInfo();
		Info.Size = static_cast<float>(Size);
	}

	return Info;
}

bool ULoc_FontSubsystem::IsCurrentCultureRTL() const
{
	return ActiveProfile ? ActiveProfile->bRightToLeft : false;
}

TArray<TSoftObjectPtr<UObject>> ULoc_FontSubsystem::GetFallbackFontFaces_Native() const
{
	return ActiveProfile ? ActiveProfile->FallbackFontFaces : TArray<TSoftObjectPtr<UObject>>();
}

FString ULoc_FontSubsystem::GetActiveProfileCulture() const
{
	return ActiveProfile ? ActiveProfile->Culture : FString();
}

// --- ISeam_FontProfileProvider (SLATE-FREE) ---

TSoftObjectPtr<UObject> ULoc_FontSubsystem::GetCultureFontFace_Implementation(FGameplayTag Role) const
{
	FName Typeface = NAME_None;
	int32 Size = 18;
	return ResolveFaceForRole(Role, Typeface, Size);
}

TArray<TSoftObjectPtr<UObject>> ULoc_FontSubsystem::GetFallbackFontFaces_Implementation() const
{
	return GetFallbackFontFaces_Native();
}

bool ULoc_FontSubsystem::IsRightToLeft_Implementation() const
{
	return IsCurrentCultureRTL();
}

void ULoc_FontSubsystem::PublishToServiceLocator()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_ServiceLocatorSubsystem>() : nullptr)
	{
		Locator->RegisterService(DPLocTags::Service_FontProfile, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void ULoc_FontSubsystem::UnpublishFromServiceLocator()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
		{
			Locator->UnregisterService(DPLocTags::Service_FontProfile);
		}
	}
}

FString ULoc_FontSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Font: profile='%s' rtl=%s fallbacks=%d"),
		*GetActiveProfileCulture(),
		IsCurrentCultureRTL() ? TEXT("true") : TEXT("false"),
		ActiveProfile ? ActiveProfile->FallbackFontFaces.Num() : 0);
}
