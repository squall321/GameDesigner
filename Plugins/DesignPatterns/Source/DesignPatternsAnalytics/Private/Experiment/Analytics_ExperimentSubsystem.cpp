// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Experiment/Analytics_ExperimentSubsystem.h"
#include "Experiment/Analytics_ExperimentDataAsset.h"
#include "Experiment/Analytics_ExperimentTags.h"
#include "Settings/Analytics_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Net/Seam_NetValue.h"
#include "Analytics/Seam_AnalyticsSink.h"

#include "Hash/CityHash.h"

namespace
{
	/** Attribute keys for the assignment event. Stable names so dashboards can pivot on them. */
	const FName GAttr_Experiment(TEXT("experiment"));
	const FName GAttr_Variant(TEXT("variant"));
	const FName GAttr_VariantSetVersion(TEXT("variant_set_version"));
	const FName GAttr_HasStableId(TEXT("has_stable_id"));
	const FName GAttr_PlayerBucket(TEXT("player_bucket"));

	/**
	 * Number of coarse, non-reversible player buckets recorded alongside an assignment. This is a
	 * privacy guard: we NEVER record the raw/hashed player id, only which of a small number of
	 * cohorts the player falls in, so cross-experiment correlation by id is impossible from the data
	 * while still allowing rough cohort balance checks. 256 cohorts is plenty for balance QA.
	 */
	constexpr uint32 GPlayerCohortCount = 256;
}

void UAnalytics_ExperimentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("Analytics ExperimentSubsystem initialized."));
}

void UAnalytics_ExperimentSubsystem::Deinitialize()
{
	Assignments.Reset();
	PlayerIdProviderObject.Reset();
	AnalyticsSubsystem.Reset();
	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_ExperimentSubsystem::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = AnalyticsSubsystem.Get())
	{
		return Cached;
	}

	// Same GameInstance as us; sibling GI subsystem, so resolving via the engine is correct and cheap.
	if (UGameInstance* GI = GetGameInstance())
	{
		UAnalytics_Subsystem* Sub = GI->GetSubsystem<UAnalytics_Subsystem>();
		AnalyticsSubsystem = Sub;
		return Sub;
	}
	return nullptr;
}

FString UAnalytics_ExperimentSubsystem::GetStablePlayerId()
{
	// 1) Fast path: the previously-resolved provider is still alive.
	UObject* ProviderObj = PlayerIdProviderObject.Get();

	// 2) Re-resolve from the service locator if the cached weak ref is gone.
	if (!ProviderObj)
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			FGameplayTag ProviderTag;
			if (const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get())
			{
				ProviderTag = Settings->PlayerIdProviderServiceTag;
			}

			if (ProviderTag.IsValid())
			{
				UObject* Resolved = Locator->ResolveService(ProviderTag);
				if (Resolved && Resolved->GetClass()->ImplementsInterface(UAnalytics_PlayerIdProvider::StaticClass()))
				{
					ProviderObj = Resolved;
					PlayerIdProviderObject = Resolved; // cache weak; never a hard ref in a GI subsystem
				}
			}
		}
	}

	if (!ProviderObj)
	{
		// Inert default: no provider -> no stable id -> deterministic cross-session bucketing is absent.
		return FString();
	}

	// The seam is a BlueprintNativeEvent; invoke via the generated Execute_ thunk.
	const FString Id = IAnalytics_PlayerIdProvider::Execute_GetStablePlayerId(ProviderObj);
	return Id;
}

float UAnalytics_ExperimentSubsystem::ComputeSelector(const FString& PlayerId, FGameplayTag Experiment, int32 VariantSetVersion)
{
	// Build a stable, platform-independent key string and hash it with CityHash64 (stable across
	// runs and machines, unlike GetTypeHash). Folding to [0,1) gives the rollout selector.
	const FString ExperimentName = Experiment.IsValid() ? Experiment.GetTagName().ToString() : FString(TEXT("<none>"));
	const FString Key = FString::Printf(TEXT("%s|%s|v%d"), *PlayerId, *ExperimentName, VariantSetVersion);

	const FTCHARToUTF8 Utf8(*Key);
	const uint64 Hash = CityHash64(reinterpret_cast<const char*>(Utf8.Get()), Utf8.Length());

	// Use the high 53 bits so the double conversion is exact, then normalise to [0,1).
	const uint64 Mantissa = Hash >> 11; // 53 bits
	constexpr double Denominator = static_cast<double>(1ULL << 53);
	return static_cast<float>(static_cast<double>(Mantissa) / Denominator);
}

UAnalytics_ExperimentDataAsset* UAnalytics_ExperimentSubsystem::ResolveExperimentAsset(FGameplayTag Experiment) const
{
	if (!Experiment.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<UAnalytics_ExperimentDataAsset>(Experiment);
	}
	return nullptr;
}

const FAnalytics_VariantAssignment& UAnalytics_ExperimentSubsystem::EnsureAssignment(
	FGameplayTag Experiment, const UAnalytics_ExperimentDataAsset& Asset)
{
	const FString PlayerId = GetStablePlayerId();
	const bool bHasStableId = !PlayerId.IsEmpty();

	if (FAnalytics_VariantAssignment* Existing = Assignments.Find(Experiment))
	{
		// Sticky unless the player id changed (e.g. guest -> signed in) or the variant set was re-versioned.
		if (Existing->bValid
			&& Existing->PlayerIdUsed == PlayerId
			&& Existing->VariantSetVersion == Asset.VariantSetVersion)
		{
			return *Existing;
		}
	}

	FAnalytics_VariantAssignment Assignment;
	Assignment.PlayerIdUsed = PlayerId;
	Assignment.VariantSetVersion = Asset.VariantSetVersion;
	Assignment.bValid = true;
	Assignment.bRecorded = false;

	if (!Asset.bExperimentEnabled || !Asset.HasAssignableVariants())
	{
		// Inert: disabled experiment or no weighted variants -> default bucket.
		Assignment.VariantTag = Asset.DefaultVariantTag;
	}
	else
	{
		const float Selector = ComputeSelector(PlayerId, Experiment, Asset.VariantSetVersion);
		Assignment.VariantTag = Asset.SelectVariantFromNormalized(Selector);
	}

	FAnalytics_VariantAssignment& Stored = Assignments.Add(Experiment, Assignment);

	UE_LOG(LogDP, Verbose, TEXT("Experiment '%s' -> variant '%s' (stableId=%s, ver=%d)."),
		*Experiment.GetTagName().ToString(),
		*Stored.VariantTag.GetTagName().ToString(),
		bHasStableId ? TEXT("yes") : TEXT("no"),
		Asset.VariantSetVersion);

	return Stored;
}

void UAnalytics_ExperimentSubsystem::RecordAssignment(FGameplayTag Experiment, const FAnalytics_VariantAssignment& Assignment)
{
	// Record once per (sticky) assignment. The recording itself is consent-gated inside the core
	// subsystem, so even calling this with consent OFF produces nothing.
	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		return;
	}

	const bool bHasStableId = !Assignment.PlayerIdUsed.IsEmpty();

	// Non-reversible coarse cohort: hash the id into a small bucket count. NEVER record the raw id.
	uint32 Cohort = 0;
	if (bHasStableId)
	{
		const FTCHARToUTF8 Utf8(*Assignment.PlayerIdUsed);
		const uint64 Hash = CityHash64(reinterpret_cast<const char*>(Utf8.Get()), Utf8.Length());
		Cohort = static_cast<uint32>(Hash % GPlayerCohortCount);
	}

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Reserve(5);
	Attrs.Emplace(GAttr_Experiment, FSeam_NetValue::MakeTag(Experiment));
	Attrs.Emplace(GAttr_Variant, FSeam_NetValue::MakeTag(Assignment.VariantTag));
	Attrs.Emplace(GAttr_VariantSetVersion, FSeam_NetValue::MakeInt(Assignment.VariantSetVersion));
	Attrs.Emplace(GAttr_HasStableId, FSeam_NetValue::MakeBool(bHasStableId));
	if (bHasStableId)
	{
		Attrs.Emplace(GAttr_PlayerBucket, FSeam_NetValue::MakeInt(static_cast<int64>(Cohort)));
	}

	Analytics->RecordEvent(AnalyticsProgressionTags::Event_Experiment_Assigned, Attrs);
}

FGameplayTag UAnalytics_ExperimentSubsystem::GetVariant(FGameplayTag Experiment)
{
	const UAnalytics_ExperimentDataAsset* Asset = ResolveExperimentAsset(Experiment);
	if (!Asset)
	{
		// Unknown experiment: nothing to assign, nothing to record. Empty tag == "no treatment".
		UE_LOG(LogDP, Verbose, TEXT("GetVariant: experiment '%s' not found in the data registry."),
			*Experiment.GetTagName().ToString());
		return FGameplayTag();
	}

	const FAnalytics_VariantAssignment& Assignment = EnsureAssignment(Experiment, *Asset);

	// Record the assignment exactly once (the first time this experiment is resolved this session).
	if (!Assignment.bRecorded)
	{
		RecordAssignment(Experiment, Assignment);
		if (FAnalytics_VariantAssignment* Mutable = Assignments.Find(Experiment))
		{
			Mutable->bRecorded = true;
		}
	}

	return Assignment.VariantTag;
}

bool UAnalytics_ExperimentSubsystem::IsFeatureEnabled(FGameplayTag Experiment)
{
	const UAnalytics_ExperimentDataAsset* Asset = ResolveExperimentAsset(Experiment);
	if (!Asset)
	{
		// Unresolved experiment -> fall through to "off". A missing experiment must never gate a
		// feature on by accident.
		return false;
	}

	if (!Asset->bExperimentEnabled)
	{
		return Asset->bDefaultFeatureEnabled;
	}

	const FGameplayTag Variant = GetVariant(Experiment);
	if (!Variant.IsValid())
	{
		return Asset->bDefaultFeatureEnabled;
	}

	return Asset->FeatureEnabledForVariants.HasTagExact(Variant);
}

bool UAnalytics_ExperimentSubsystem::IsExperimentKnown(FGameplayTag Experiment) const
{
	return ResolveExperimentAsset(Experiment) != nullptr;
}

void UAnalytics_ExperimentSubsystem::ResetAssignments()
{
	Assignments.Reset();
	UE_LOG(LogDP, Verbose, TEXT("Experiment assignments reset; next GetVariant re-buckets."));
}

FString UAnalytics_ExperimentSubsystem::GetDPDebugString_Implementation() const
{
	int32 RecordedCount = 0;
	for (const TPair<FGameplayTag, FAnalytics_VariantAssignment>& Pair : Assignments)
	{
		if (Pair.Value.bRecorded)
		{
			++RecordedCount;
		}
	}
	const bool bHasProvider = PlayerIdProviderObject.IsValid();
	return FString::Printf(TEXT("Experiments: %d assigned (%d recorded), idProvider=%s"),
		Assignments.Num(), RecordedCount, bHasProvider ? TEXT("yes") : TEXT("no"));
}
