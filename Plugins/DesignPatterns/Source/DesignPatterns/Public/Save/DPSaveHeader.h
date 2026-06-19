// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/StructuredArchive.h"
#include "DPSaveHeader.generated.h"

/**
 * Self-describing metadata chunk written at the front of every DP save blob.
 *
 * The header is serialized as a SEPARATE length-prefixed chunk ahead of the SaveGame body so
 * tools (and DP.Save.DumpHeader) can read slot metadata — version, timestamp, display name,
 * playtime — WITHOUT deserializing (or even knowing the class of) the full save object.
 *
 * The header carries its own magic + format version so a corrupt or foreign file is rejected
 * before any body bytes are touched.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_SaveHeader
{
	GENERATED_BODY()

	/** Four-byte magic identifying a DP save blob ('D''P''S''V'). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 Magic = DefaultMagic();

	/** FDP_SaveVersion::Type the body was written with (drives migration on load). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 SaveVersion = 0;

	/** Class path of the concrete UDP_SaveGame so the loader can instantiate the right type. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FString SaveGameClassPath;

	/** UTC timestamp the save was created (ISO-8601 ticks via FDateTime). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FDateTime TimestampUtc = FDateTime(0);

	/** Designer/player-facing slot label. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FString DisplayName;

	/** Accumulated in-game playtime in seconds at save time. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	float PlaytimeSeconds = 0.f;

	FDP_SaveHeader() = default;

	/** The expected magic value. */
	static constexpr int32 DefaultMagic() { return 0x44505356; } // 'DPSV'

	/** True if Magic matches the DP magic. */
	bool IsMagicValid() const { return Magic == DefaultMagic(); }

	/** Serialize this header's fields. Kept explicit so chunk layout is stable and tool-readable. */
	void Serialize(FArchive& Ar);

	/** UStruct Serialize hook so FArchive << works for the header. */
	bool Serialize(FStructuredArchive::FSlot Slot)
	{
		Serialize(Slot.GetUnderlyingArchive());
		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FDP_SaveHeader& H)
	{
		H.Serialize(Ar);
		return Ar;
	}
};

template<>
struct TStructOpsTypeTraits<FDP_SaveHeader> : public TStructOpsTypeTraitsBase2<FDP_SaveHeader>
{
	enum { WithSerializer = true };
};
