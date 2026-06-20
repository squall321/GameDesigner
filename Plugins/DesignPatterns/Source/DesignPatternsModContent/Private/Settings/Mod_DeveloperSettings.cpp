// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Mod_DeveloperSettings.h"
#include "Core/DPLog.h"

UMod_DeveloperSettings::UMod_DeveloperSettings()
{
	// Conservative shipped defaults: strict allowlist, no auto-mount. A project must opt in to
	// modding by widening AllowlistPolicy / populating AllowedPackIds and/or setting AutoMountPolicy.
	AllowlistPolicy = EMod_AllowlistPolicy::AllowlistOnly;
	AutoMountPolicy = EMod_AutoMountPolicy::Off;
	MountOrderPolicy = EMod_MountOrderPolicy::DiscoveryOrder;
}

const UMod_DeveloperSettings* UMod_DeveloperSettings::Get()
{
	// GetDefault never returns null for a registered UDeveloperSettings; the CDO is populated from
	// the project's DefaultGame.ini at load. Callers still null-check and fall back defensively.
	return GetDefault<UMod_DeveloperSettings>();
}

bool UMod_DeveloperSettings::IsPackIdEligible(const FGameplayTag& PackId) const
{
	if (!PackId.IsValid())
	{
		// An invalid id can never be addressed safely; refuse regardless of policy.
		return false;
	}

	// Deny always wins, under every policy: if the id matches a denied tag or any of its ancestors
	// is denied, the pack is refused. MatchesAny treats listed ancestors as covering their subtree.
	if (DeniedPackIds.Num() > 0 && PackId.MatchesAny(DeniedPackIds))
	{
		return false;
	}

	switch (AllowlistPolicy)
	{
	case EMod_AllowlistPolicy::AllowlistOnly:
		// Eligible only if explicitly allowed (exact tag or a covering ancestor in the container).
		return AllowedPackIds.Num() > 0 && PackId.MatchesAny(AllowedPackIds);

	case EMod_AllowlistPolicy::DenylistOnly:
		// Already passed the deny check above; everything else is eligible.
		return true;

	case EMod_AllowlistPolicy::AllowAll:
		return true;

	default:
		// Unknown policy value: fail closed (treat as not eligible).
		return false;
	}
}
