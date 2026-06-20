// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Validation/Mod_ContentValidatorSubsystem.h"

#include "DesignPatternsModContentModule.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataAsset.h"
#include "Descriptor/Mod_ContentPackDescriptor.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "NativeGameplayTags.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ModContentValidation"

namespace ModValidatorReasonTags
{
	// Owned by the validator subsystem area (distinct from the rule area's reason namespace).
	UE_DEFINE_GAMEPLAY_TAG(Reason_LoadFailed,    "DP.Mod.Reason.AssetLoadFailed");
	UE_DEFINE_GAMEPLAY_TAG(Reason_EngineInvalid, "DP.Mod.Reason.EngineDataInvalid");
	UE_DEFINE_GAMEPLAY_TAG(Reason_NoAssets,      "DP.Mod.Reason.NoAssets");
}

void UMod_ContentValidatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BuildActiveRules();

	// Publish ourselves as the pack validator seam so the content manager resolves us by stable tag.
	// WeakObserved: the engine already owns this GI subsystem's lifetime; the locator must only observe.
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(ModTags::Service_Validator, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}

	UE_LOG(LogDPData, Log, TEXT("[ModContent] Pack validator initialized with %d active rule(s)."), ActiveRules.Num());
}

void UMod_ContentValidatorSubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only retract our own binding (someone may have legitimately overridden the validator since).
		if (Locator->Resolve<UMod_ContentValidatorSubsystem>(ModTags::Service_Validator) == this)
		{
			Locator->UnregisterService(ModTags::Service_Validator);
		}
	}

	ActiveRules.Reset();
	ConfiguredRules.Reset();
	bRulesBuilt = false;

	Super::Deinitialize();
}

void UMod_ContentValidatorSubsystem::SetConfiguredRules(const TArray<UMod_ValidationRule*>& InRules)
{
	ConfiguredRules.Reset();
	for (UMod_ValidationRule* Rule : InRules)
	{
		if (IsValid(Rule))
		{
			ConfiguredRules.Add(Rule);
		}
	}
	bRulesBuilt = false;
	BuildActiveRules();
}

void UMod_ContentValidatorSubsystem::BuildActiveRules() const
{
	ActiveRules.Reset();

	if (ConfiguredRules.Num() > 0)
	{
		// Use the project-authored rule set verbatim (already-validated, GC-rooted instances).
		for (const TObjectPtr<UMod_ValidationRule>& Rule : ConfiguredRules)
		{
			if (IsValid(Rule))
			{
				ActiveRules.Add(Rule);
			}
		}
	}

	if (ActiveRules.Num() == 0)
	{
		// DOCUMENTED conservative default (no project policy configured): structurally unsafe content is
		// rejected (Fail) while a dangling soft reference is only warned (it may be an optional ref).
		// These instances are outered to THIS subsystem and held in the ActiveRules UPROPERTY (GC-rooted).
		UMod_ContentValidatorSubsystem* MutableThis = const_cast<UMod_ContentValidatorSubsystem*>(this);

		UMod_ValidationRule_RequiredTagPresent* TagRule =
			NewObject<UMod_ValidationRule_RequiredTagPresent>(MutableThis);
		TagRule->FailureSeverity = EMod_ValidationResult::Fail;
		ActiveRules.Add(TagRule);

		UMod_ValidationRule_NoDanglingSoftRef* RefRule =
			NewObject<UMod_ValidationRule_NoDanglingSoftRef>(MutableThis);
		RefRule->FailureSeverity = EMod_ValidationResult::Warn;
		ActiveRules.Add(RefRule);

		UMod_ValidationRule_VersionCompatible* VersionRule =
			NewObject<UMod_ValidationRule_VersionCompatible>(MutableThis);
		VersionRule->FailureSeverity = EMod_ValidationResult::Fail;
		ActiveRules.Add(VersionRule);

		UMod_ValidationRule_WithinContentRoot* RootRule =
			NewObject<UMod_ValidationRule_WithinContentRoot>(MutableThis);
		RootRule->FailureSeverity = EMod_ValidationResult::Fail;
		ActiveRules.Add(RootRule);
	}

	bRulesBuilt = true;
}

FMod_ValidationReport UMod_ContentValidatorSubsystem::ValidatePack_Implementation(
	const FMod_PackInfo& Pack, const TArray<FMod_PackInfo>& /*KnownPacks*/) const
{
	// Per-asset rules do not currently cross-reference KnownPacks (dependency/version cross-checks are
	// the manager's own hard guards). The seam keeps the parameter so a project rule set can use it.
	return ValidatePackAssets(Pack);
}

TArray<FString> UMod_ContentValidatorSubsystem::GatherPackRoots(const FMod_PackInfo& Pack)
{
	TArray<FString> Roots;

	// Authoritative virtual content roots from the pack's descriptor (e.g. "/MyPack/Maps").
	if (const UMod_ContentPackDescriptor* Descriptor = Pack.Descriptor.Get())
	{
		Roots.Append(Descriptor->ContentRoots);
	}

	// For pak packs the engine mount point is the sandbox boundary; include it defensively.
	if (!Pack.MountPoint.IsEmpty())
	{
		Roots.AddUnique(Pack.MountPoint);
	}

	return Roots;
}

FMod_ValidationReport UMod_ContentValidatorSubsystem::ValidatePackAssets(const FMod_PackInfo& Pack) const
{
	if (!bRulesBuilt)
	{
		BuildActiveRules();
	}

	FMod_ValidationReport Report;

	if (!Pack.PackId.IsValid())
	{
		FMod_ValidationMessage Msg;
		Msg.Severity = EMod_ValidationResult::Fail;
		Msg.Reason = ModValidatorReasonTags::Reason_NoAssets;
		Msg.Detail = LOCTEXT("Pack_InvalidId", "Pack has no valid PackId; cannot validate.");
		Report.Messages.Add(Msg);
		Report.RecomputeResult();
		return Report;
	}

	const TArray<FString> PackRoots = GatherPackRoots(Pack);

	// The validator's input contract is the manager-built FMod_PackInfo. The per-asset list lives on the
	// pack's descriptor / the manager's contained-asset scan; when none is provided there is nothing for
	// the asset rules to inspect (the manager's structural guards still ran). We surface that as a Warn so
	// the operator notices an empty pack rather than silently passing it.
	TArray<FSoftObjectPath> AssetPaths;
	if (const UMod_ContentPackDescriptor* Descriptor = Pack.Descriptor.Get())
	{
		// The descriptor itself is a validatable asset of the pack.
		AssetPaths.AddUnique(FSoftObjectPath(Descriptor));
	}

	if (AssetPaths.Num() == 0)
	{
		FMod_ValidationMessage Msg;
		Msg.Severity = EMod_ValidationResult::Warn;
		Msg.Reason = ModValidatorReasonTags::Reason_NoAssets;
		Msg.Detail = LOCTEXT("Pack_NoAssets", "Pack exposes no inspectable assets to the validator (descriptor only / unresolved).");
		Report.Messages.Add(Msg);
	}

	for (const FSoftObjectPath& AssetPath : AssetPaths)
	{
		if (AssetPath.IsNull())
		{
			continue;
		}

		UObject* Loaded = AssetPath.TryLoad();
		if (Loaded == nullptr)
		{
			FMod_ValidationMessage Msg;
			Msg.Severity = EMod_ValidationResult::Fail;
			Msg.Reason = ModValidatorReasonTags::Reason_LoadFailed;
			Msg.Detail = FText::Format(
				LOCTEXT("Asset_LoadFailed", "Pack asset '{0}' failed to load."),
				FText::FromString(AssetPath.ToString()));
			Report.Messages.Add(Msg);
			continue;
		}

		ValidateAsset(Loaded, AssetPath, PackRoots, Report);
	}

	Report.RecomputeResult();

	UE_LOG(LogDPData, Verbose, TEXT("[ModContent] Validated pack '%s': result=%d, %d message(s)."),
		*Pack.PackId.ToString(), static_cast<int32>(Report.Result), Report.Messages.Num());

	return Report;
}

void UMod_ContentValidatorSubsystem::ValidateAsset(
	UObject* Asset, const FSoftObjectPath& AssetPath, const TArray<FString>& PackRoots, FMod_ValidationReport& InOutReport) const
{
#if WITH_EDITOR
	// Engine-native editor validation first (only meaningful in editor/commandlet builds).
	if (const UDP_DataAsset* AsData = Cast<UDP_DataAsset>(Asset))
	{
		FDataValidationContext Context;
		const EDataValidationResult EngineResult = AsData->IsDataValid(Context);
		if (EngineResult == EDataValidationResult::Invalid)
		{
			FMod_ValidationMessage Msg;
			Msg.Severity = EMod_ValidationResult::Fail;
			Msg.Reason = ModValidatorReasonTags::Reason_EngineInvalid;
			Msg.Detail = FText::Format(
				LOCTEXT("Asset_EngineInvalid", "Asset '{0}' failed engine data validation (IsDataValid)."),
				FText::FromString(AssetPath.ToString()));
			InOutReport.Messages.Add(Msg);
		}
	}
#endif

	// Then the configured rule set. Inject the pack's roots into any WithinContentRoot rule so the
	// sandbox boundary is the pack's own content roots, not a global one.
	for (const TObjectPtr<UMod_ValidationRule>& Rule : ActiveRules)
	{
		if (!IsValid(Rule))
		{
			continue;
		}

		if (UMod_ValidationRule_WithinContentRoot* RootRule = Cast<UMod_ValidationRule_WithinContentRoot>(Rule))
		{
			RootRule->InjectedPackRoots = PackRoots;
		}

		const FMod_ValidationMessage Result = Rule->Validate(Asset);
		if (Result.Severity != EMod_ValidationResult::Pass)
		{
			InOutReport.Messages.Add(Result);
		}
	}
}

FString UMod_ContentValidatorSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("ModContentValidator: rules=%d (configured=%d)"),
		ActiveRules.Num(), ConfiguredRules.Num());
}

#undef LOCTEXT_NAMESPACE
