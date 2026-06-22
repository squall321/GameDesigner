// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SaveX_StorageDeveloperSettings.h"
#include "SaveX_StorageServiceKeys.h"

USaveX_StorageDeveloperSettings::USaveX_StorageDeveloperSettings()
{
	// Seed the service keys from the conventional SaveSystem keys so an unedited project still resolves the
	// cipher/cloud seams without any manual configuration. A project may relocate them by editing the fields.
	CipherServiceTag = SaveX_StorageServiceKeys::Cipher();
	CloudServiceTag = SaveX_StorageServiceKeys::Cloud();
}

const USaveX_StorageDeveloperSettings* USaveX_StorageDeveloperSettings::Get()
{
	return GetDefault<USaveX_StorageDeveloperSettings>();
}
