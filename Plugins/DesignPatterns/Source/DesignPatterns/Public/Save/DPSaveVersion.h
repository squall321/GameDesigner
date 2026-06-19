// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/CustomVersion.h"

/**
 * Append-only version enum for the DesignPatterns save format.
 *
 * NEVER reorder or remove entries — old save files reference these values by number. Add new
 * versions immediately above VersionPlusOne and bump LatestVersion implicitly via the helper.
 * Registered as an FCustomVersion so the value is embedded in every FArchive that serializes
 * a DP save, letting older archives drive migration.
 */
struct DESIGNPATTERNS_API FDP_SaveVersion
{
	enum Type
	{
		/** Before any versioning existed. */
		BeforeCustomVersionWasAdded = 0,

		/** Initial versioned format: header chunk + body chunk. */
		InitialVersion = 1,

		/** Added per-save user metadata (display name / playtime). */
		AddedSlotMetadata = 2,

		// --- new versions go ABOVE this line ---

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	/** Unique GUID identifying this custom version stream. */
	static const FGuid GUID;

	/** Register the custom version with the engine. Call once at module startup. */
	static void Register();

private:
	FDP_SaveVersion() = delete;
};
