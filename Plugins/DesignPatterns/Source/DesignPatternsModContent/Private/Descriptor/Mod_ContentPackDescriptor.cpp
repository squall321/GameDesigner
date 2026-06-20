// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Descriptor/Mod_ContentPackDescriptor.h"
#include "DesignPatternsModContentModule.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Mod_ContentPackDescriptor"

UMod_ContentPackDescriptor::UMod_ContentPackDescriptor()
{
	// A fresh descriptor starts at 1.0.0 so a never-edited pack still compares as a real release
	// rather than the "unset / any" 0.0.0 used for dependency floors.
	PackVersion = FMod_SemVer(1, 0, 0);
}

FName UMod_ContentPackDescriptor::GetDataAssetType_Implementation() const
{
	// All pack descriptors share one asset-manager type so the manager / tooling can enumerate every
	// pack with a single PrimaryAssetType query instead of per-subclass buckets.
	static const FName PackType(TEXT("Mod_ContentPack"));
	return PackType;
}

#if WITH_EDITOR
EDataValidationResult UMod_ContentPackDescriptor::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// PackId must live under the DP.Mod.Pack root so tag-hierarchy lookups and the allowlist work.
	if (DataTag.IsValid() && !DataTag.MatchesTag(ModTags::Pack))
	{
		Context.AddError(FText::Format(
			LOCTEXT("PackIdNotUnderRoot", "Pack id '{0}' must be a child of DP.Mod.Pack."),
			FText::FromName(DataTag.GetTagName())));
		Result = EDataValidationResult::Invalid;
	}

	// Cross-check dependencies: no self-dependency, no duplicate ids.
	TSet<FGameplayTag> Seen;
	for (const FMod_PackDependency& Dep : Dependencies)
	{
		if (!Dep.DependencyId.IsValid())
		{
			Context.AddError(LOCTEXT("EmptyDependency", "A dependency entry has an empty pack id."));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Dep.DependencyId == DataTag)
		{
			Context.AddError(FText::Format(
				LOCTEXT("SelfDependency", "Pack '{0}' declares a dependency on itself."),
				FText::FromName(DataTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		if (Seen.Contains(Dep.DependencyId))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateDependency", "Pack declares dependency '{0}' more than once."),
				FText::FromName(Dep.DependencyId.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Dep.DependencyId);
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
