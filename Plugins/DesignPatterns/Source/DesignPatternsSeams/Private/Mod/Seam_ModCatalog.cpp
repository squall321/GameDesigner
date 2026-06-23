// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mod/Seam_ModCatalog.h"

// Inert (unoverridden) catalog-source default. With no real source registered the catalog is empty and
// downloads are refused — the module still works off local disk discovery. The default implementation is
// UMod_LocalFolderCatalogSource (a network-free local-folder store) which overrides these.

int32 ISeam_ModCatalogSource::EnumerateCatalog_Implementation(TArray<FMod_CatalogEntry>& /*Out*/) const
{
	// Default: no listings.
	return 0;
}

bool ISeam_ModCatalogSource::RequestDownload_Implementation(FGameplayTag /*CatalogItemId*/)
{
	// Default: no backend to fetch from -> request not accepted.
	return false;
}

EMod_CatalogItemState ISeam_ModCatalogSource::GetCatalogState_Implementation(FGameplayTag /*CatalogItemId*/) const
{
	// Default: unknown item -> treat as not installed.
	return EMod_CatalogItemState::NotInstalled;
}

FGameplayTag ISeam_ModCatalogSource::GetSourceId_Implementation() const
{
	// Default: no named source.
	return FGameplayTag();
}
