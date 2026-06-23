// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mod/Seam_ModResolution.h"

// Inert (unoverridden) resolution-policy default. With no real policy registered the consumer keeps the
// manager's existing ResolveMountOrder and the registry's existing precedence: ResolveConflict simply
// echoes the already-computed default winner, and ScoreLoadOrder is neutral (0). A project overrides
// these to impose a curated load order / conflict resolution.

FGameplayTag ISeam_ModResolutionPolicy::ResolveConflict_Implementation(const FMod_TagConflict& Conflict) const
{
	// Default: defer to the precedence policy's already-chosen winner.
	return Conflict.DefaultWinner;
}

int32 ISeam_ModResolutionPolicy::ScoreLoadOrder_Implementation(FGameplayTag /*PackId*/) const
{
	// Default: neutral score, so the manager's existing tie-break ordering is preserved.
	return 0;
}
