// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Voice/Loc_VoiceSubsystem.h"

#include "Voice/Loc_VoiceBankDataAsset.h"
#include "Localization/Loc_LocalizationSubsystem.h"
#include "Subtitle/Loc_SubtitleTypes.h"
#include "DesignPatternsLocalizationModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Loc/Seam_LipSync.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

// FInstancedStruct: StructUtils on 5.3/5.4, CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void ULoc_VoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BindCultureChanged();

	// Select the bank for whatever culture is active at boot.
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		SelectBankForCulture(Loc->GetCurrentCulture());
	}

	PublishToServiceLocator();

	UE_LOG(LogDP, Log, TEXT("ULoc_VoiceSubsystem initialized (bank culture='%s')."), *GetActiveBankCulture());
}

void ULoc_VoiceSubsystem::Deinitialize()
{
	UnpublishFromServiceLocator();
	UnbindCultureChanged();

	// Cancel any pending loads and end lip-sync passes.
	for (FActiveLine& Line : ActiveLines)
	{
		if (Line.LoadHandle.IsValid())
		{
			Line.LoadHandle->CancelHandle();
			Line.LoadHandle.Reset();
		}
		if (Line.bStarted && Line.Speaker.IsValid())
		{
			if (ISeam_LipSync* LipSync = ResolveLipSync())
			{
				if (UObject* LipObj = CachedLipSync.GetObject())
				{
					ISeam_LipSync::Execute_EndLipSync(LipObj, Line.Speaker);
				}
			}
		}
	}
	ActiveLines.Reset();

	CachedLipSync.Reset();
	ActiveBank = nullptr;

	Super::Deinitialize();
}

void ULoc_VoiceSubsystem::BindCultureChanged()
{
	if (bCultureBound)
	{
		return;
	}
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		Loc->OnCultureChanged.AddDynamic(this, &ULoc_VoiceSubsystem::HandleCultureChanged);
		bCultureBound = true;
	}
}

void ULoc_VoiceSubsystem::UnbindCultureChanged()
{
	if (!bCultureBound)
	{
		return;
	}
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		Loc->OnCultureChanged.RemoveDynamic(this, &ULoc_VoiceSubsystem::HandleCultureChanged);
	}
	bCultureBound = false;
}

void ULoc_VoiceSubsystem::HandleCultureChanged(const FString& NewCulture)
{
	SelectBankForCulture(NewCulture);
}

void ULoc_VoiceSubsystem::SelectBankForCulture(const FString& CultureCode)
{
	ActiveBank = nullptr;

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem: no asset manager; voice banks unavailable."));
		return;
	}

	// Voice banks form their own asset-manager type bucket (see GetDataAssetType_Implementation).
	const FPrimaryAssetType BankType(FName(TEXT("Loc_VoiceBank")));

	TArray<FPrimaryAssetId> Ids;
	AssetManager->GetPrimaryAssetIdList(BankType, Ids);
	if (Ids.Num() == 0)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem: no voice banks registered for culture '%s'."), *CultureCode);
		return;
	}

	// Resolve the language prefix (e.g. "en" from "en-US") for fallback matching.
	FString LanguagePrefix = CultureCode;
	int32 DashIdx = INDEX_NONE;
	if (CultureCode.FindChar(TEXT('-'), DashIdx))
	{
		LanguagePrefix = CultureCode.Left(DashIdx);
	}

	ULoc_VoiceBankDataAsset* ExactMatch = nullptr;
	ULoc_VoiceBankDataAsset* PrefixMatch = nullptr;

	for (const FPrimaryAssetId& Id : Ids)
	{
		// Synchronously resolve the bank object path then load it; banks are small (soft clip refs only).
		const FSoftObjectPath Path = AssetManager->GetPrimaryAssetPath(Id);
		if (!Path.IsValid())
		{
			continue;
		}
		ULoc_VoiceBankDataAsset* Bank = Cast<ULoc_VoiceBankDataAsset>(Path.TryLoad());
		if (!Bank)
		{
			continue;
		}

		if (Bank->Culture.Equals(CultureCode, ESearchCase::IgnoreCase))
		{
			ExactMatch = Bank;
			break; // exact is best; stop.
		}
		if (!PrefixMatch && Bank->Culture.Equals(LanguagePrefix, ESearchCase::IgnoreCase))
		{
			PrefixMatch = Bank;
		}
	}

	ActiveBank = ExactMatch ? ExactMatch : PrefixMatch;

	if (ActiveBank)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem: selected voice bank culture='%s' for active culture '%s'."),
			*ActiveBank->Culture, *CultureCode);
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem: no voice bank matched culture '%s' (or prefix '%s')."),
			*CultureCode, *LanguagePrefix);
	}
}

int64 ULoc_VoiceSubsystem::PlayLine(const UObject* WorldContextObject, FGameplayTag LineId, FGameplayTag Speaker)
{
	if (!LineId.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem::PlayLine called with an invalid line id."));
		return 0;
	}
	if (!ActiveBank)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem::PlayLine: no active voice bank (culture has none)."));
		return 0;
	}

	const FLoc_VoiceLineRow* Row = ActiveBank->FindRow(LineId);
	if (!Row)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_VoiceSubsystem::PlayLine: line '%s' not in bank culture '%s'."),
			*LineId.ToString(), *ActiveBank->Culture);
		return 0;
	}

	FActiveLine Line;
	Line.Handle = NextHandle++;
	Line.LineId = LineId;
	Line.Speaker = Speaker;
	Line.WorldContext = WorldContextObject;

	const int64 NewHandle = Line.Handle;

	// Gather the soft paths to load (clip + optional curve), then async-load and continue on completion.
	TArray<FSoftObjectPath> ToLoad;
	if (!Row->SoundAsset.IsNull())
	{
		ToLoad.Add(Row->SoundAsset.ToSoftObjectPath());
	}
	if (!Row->LipSyncCurve.IsNull())
	{
		ToLoad.Add(Row->LipSyncCurve.ToSoftObjectPath());
	}

	if (ToLoad.Num() == 0)
	{
		// Caption-only line (no audio): surface immediately with a computed duration (subtitle subsystem
		// resolves duration from text length when Duration<=0), no lip-sync.
		ActiveLines.Add(MoveTemp(Line));
		SurfaceCaption(WorldContextObject, *Row, /*ResolvedDuration=*/0.f);
		FindActive(NewHandle)->bStarted = true;
		return NewHandle;
	}

	Line.LoadHandle = Streamable.RequestAsyncLoad(
		ToLoad,
		FStreamableDelegate::CreateUObject(this, &ULoc_VoiceSubsystem::OnLineLoaded, NewHandle),
		FStreamableManager::AsyncLoadHighPriority);

	ActiveLines.Add(MoveTemp(Line));
	return NewHandle;
}

void ULoc_VoiceSubsystem::OnLineLoaded(int64 Handle)
{
	FActiveLine* Line = FindActive(Handle);
	if (!Line)
	{
		// Stopped before the load completed.
		return;
	}
	if (!ActiveBank)
	{
		return;
	}

	const FLoc_VoiceLineRow* Row = ActiveBank->FindRow(Line->LineId);
	if (!Row)
	{
		return;
	}

	const UObject* WorldContext = Line->WorldContext.Get();

	USoundBase* LoadedSound = Row->SoundAsset.Get();
	UObject* LoadedCurve = Row->LipSyncCurve.Get();

	// Now that the clip is loaded we can read its REAL duration — never guessed before the load.
	float Duration = 0.f;
	if (LoadedSound)
	{
		Duration = LoadedSound->GetDuration();
		// A looping/streaming clip can report an indefinite duration; fall back to 0 so the subtitle
		// subsystem computes a text-length duration instead of an absurd value.
		if (!FMath::IsFinite(Duration) || Duration <= 0.f)
		{
			Duration = 0.f;
		}

		// Play the localized VO as 2D non-positional audio (localized narration). Positional VO is the
		// producer's responsibility; this orchestrator handles localized lines.
		if (WorldContext)
		{
			UGameplayStatics::PlaySound2D(WorldContext, LoadedSound);
		}
	}

	SurfaceCaption(WorldContext, *Row, Duration);
	BeginLipSyncFor(*Row, Line->Speaker, LoadedSound, LoadedCurve);

	Line->bStarted = true;
}

void ULoc_VoiceSubsystem::SurfaceCaption(const UObject* WorldContextObject, const FLoc_VoiceLineRow& Row, float ResolvedDuration)
{
	if (!Row.SubtitleKey.IsValid())
	{
		return; // VO-only line, no caption.
	}

	// Resolve the caption FText via the localization subsystem (wraps FText::FromStringTable).
	FText CaptionText = FText::GetEmpty();
	if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
	{
		bool bFound = false;
		CaptionText = Loc->FindText(Row.SubtitleKey, bFound);
	}

	FLoc_SubtitleLine SubtitleLine(Row.Speaker, CaptionText, ResolvedDuration, Row.SubtitlePriority);

	// Broadcast on the voice-line channel the base subtitle subsystem already listens on — zero coupling.
	if (UDP_MessageBusSubsystem* Bus = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr)
	{
		FInstancedStruct Payload = FInstancedStruct::Make(SubtitleLine);
		Bus->BroadcastPayload(DPLocTags::Bus_VoiceLine, Payload, this);
	}
}

void ULoc_VoiceSubsystem::BeginLipSyncFor(const FLoc_VoiceLineRow& Row, FGameplayTag Speaker, USoundBase* LoadedSound, UObject* LoadedCurve)
{
	if (!Speaker.IsValid())
	{
		return; // lip-sync is routed by speaker identity.
	}

	ISeam_LipSync* LipSync = ResolveLipSync();
	if (!LipSync)
	{
		return; // inert: no facial-anim backend.
	}

	UObject* LipObj = CachedLipSync.GetObject();
	if (!LipObj)
	{
		return;
	}

	ISeam_LipSync::Execute_BeginLipSync(LipObj, Speaker, LoadedSound, LoadedCurve);
}

void ULoc_VoiceSubsystem::StopLine(int64 Handle)
{
	const int32 Index = ActiveLines.IndexOfByPredicate([Handle](const FActiveLine& L) { return L.Handle == Handle; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	FActiveLine& Line = ActiveLines[Index];

	if (Line.LoadHandle.IsValid())
	{
		Line.LoadHandle->CancelHandle();
		Line.LoadHandle.Reset();
	}

	// End lip-sync for this speaker.
	if (Line.Speaker.IsValid())
	{
		if (ISeam_LipSync* LipSync = ResolveLipSync())
		{
			if (UObject* LipObj = CachedLipSync.GetObject())
			{
				ISeam_LipSync::Execute_EndLipSync(LipObj, Line.Speaker);
			}
		}

		// Clear this speaker's caption on the bus (the subtitle subsystem clears by speaker on a tag payload).
		if (UDP_MessageBusSubsystem* Bus = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr)
		{
			FInstancedStruct Payload = FInstancedStruct::Make(Line.Speaker);
			Bus->BroadcastPayload(DPLocTags::Bus_SubtitleClear, Payload, this);
		}
	}

	ActiveLines.RemoveAt(Index);
}

ULoc_VoiceSubsystem::FActiveLine* ULoc_VoiceSubsystem::FindActive(int64 Handle)
{
	return ActiveLines.FindByPredicate([Handle](const FActiveLine& L) { return L.Handle == Handle; });
}

ISeam_LipSync* ULoc_VoiceSubsystem::ResolveLipSync() const
{
	if (CachedLipSync.IsValid())
	{
		return CachedLipSync.Get();
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(DPLocTags::Service_LipSync);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(USeam_LipSync::StaticClass()))
	{
		return nullptr;
	}

	const_cast<ULoc_VoiceSubsystem*>(this)->CachedLipSync = TWeakInterfacePtr<ISeam_LipSync>(*Provider);
	return CachedLipSync.Get();
}

void ULoc_VoiceSubsystem::PublishToServiceLocator()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_ServiceLocatorSubsystem>() : nullptr)
	{
		Locator->RegisterService(DPLocTags::Service_Voice, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void ULoc_VoiceSubsystem::UnpublishFromServiceLocator()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
		{
			Locator->UnregisterService(DPLocTags::Service_Voice);
		}
	}
}

FString ULoc_VoiceSubsystem::GetActiveBankCulture() const
{
	return ActiveBank ? ActiveBank->Culture : FString();
}

FString ULoc_VoiceSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Voice: bank='%s' active-lines=%d lipsync=%s"),
		*GetActiveBankCulture(),
		ActiveLines.Num(),
		CachedLipSync.IsValid() ? TEXT("on") : TEXT("off"));
}
