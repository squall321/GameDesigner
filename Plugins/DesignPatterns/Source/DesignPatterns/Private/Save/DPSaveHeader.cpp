// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/DPSaveHeader.h"

void FDP_SaveHeader::Serialize(FArchive& Ar)
{
	// Explicit field-by-field layout so the chunk format is stable and externally parseable.
	Ar << Magic;
	Ar << SaveVersion;
	Ar << SaveGameClassPath;

	int64 Ticks = TimestampUtc.GetTicks();
	Ar << Ticks;
	if (Ar.IsLoading())
	{
		TimestampUtc = FDateTime(Ticks);
	}

	Ar << DisplayName;
	Ar << PlaytimeSeconds;
}
