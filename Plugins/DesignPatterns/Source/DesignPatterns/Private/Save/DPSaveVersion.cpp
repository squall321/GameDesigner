// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/DPSaveVersion.h"
#include "Core/DPLog.h"

// Stable, randomly-generated GUID identifying the DP save custom-version stream.
const FGuid FDP_SaveVersion::GUID(0x6D2A1F4C, 0x8B7E4A91, 0xA3C50D62, 0x9E1F7B30);

namespace
{
	/** Friendly name shown in version-mismatch diagnostics. */
	struct FDP_SaveVersionRegistration
	{
		FCustomVersionRegistration Registration;

		FDP_SaveVersionRegistration()
			: Registration(FDP_SaveVersion::GUID, FDP_SaveVersion::LatestVersion, TEXT("DPSaveVersion"))
		{
		}
	};

	/** Lazily-constructed singleton so registration happens exactly once. */
	TUniquePtr<FDP_SaveVersionRegistration> GDP_SaveVersionRegistration;
}

void FDP_SaveVersion::Register()
{
	if (!GDP_SaveVersionRegistration.IsValid())
	{
		GDP_SaveVersionRegistration = MakeUnique<FDP_SaveVersionRegistration>();
		UE_LOG(LogDPSave, Verbose, TEXT("Registered DP save custom version (latest=%d)."), (int32)LatestVersion);
	}
}
