// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Footstep/Audio_SurfaceBankDataAsset.h"

namespace
{
	/** Resolve a sound tag from a per-surface mapping for a gait, with the mapping's own fallbacks. */
	FGameplayTag ResolveFromMapping(const FAudio_SurfaceFootstep& Mapping, const FGameplayTag& Gait)
	{
		if (Gait.IsValid())
		{
			if (const FGameplayTag* Found = Mapping.ByGait.Find(Gait))
			{
				if (Found->IsValid())
				{
					return *Found;
				}
			}
		}
		// Empty/invalid gait slot acts as the mapping's per-gait default.
		if (const FGameplayTag* Default = Mapping.ByGait.Find(FGameplayTag()))
		{
			if (Default->IsValid())
			{
				return *Default;
			}
		}
		return Mapping.DefaultSound;
	}
}

FGameplayTag UAudio_SurfaceBankDataAsset::ResolveSoundTag(EPhysicalSurface Surface, FGameplayTag Gait) const
{
	// 1) Exact surface entry.
	if (const FAudio_SurfaceFootstep* Entry = BySurface.Find(Surface))
	{
		const FGameplayTag Resolved = ResolveFromMapping(*Entry, Gait);
		if (Resolved.IsValid())
		{
			return Resolved;
		}
	}

	// 2) Explicit SurfaceType_Default entry (if the caller's surface had none / resolved nothing).
	if (Surface != SurfaceType_Default)
	{
		if (const FAudio_SurfaceFootstep* DefaultEntry = BySurface.Find(SurfaceType_Default))
		{
			const FGameplayTag Resolved = ResolveFromMapping(*DefaultEntry, Gait);
			if (Resolved.IsValid())
			{
				return Resolved;
			}
		}
	}

	// 3) Bank-wide fallback mapping.
	return ResolveFromMapping(FallbackSurface, Gait);
}

FName UAudio_SurfaceBankDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Audio_SurfaceBank"));
}
