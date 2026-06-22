// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/CustomVersion.h"
#include "SaveX_ContainerHeader.generated.h"

/**
 * Compression method recorded in the container header.
 *
 * Only methods the engine's FCompression layer can decode are listed. Oodle is preferred when the engine
 * has it registered (most shipping builds), but the storage subsystem falls back to Zlib (always present)
 * when Oodle is unavailable so a wrapped save is portable across editor/standalone/dedicated builds.
 */
UENUM(BlueprintType)
enum class ESaveX_Compression : uint8
{
	/** Payload is stored verbatim (no compression). */
	None,
	/** Payload is zlib-compressed via FCompression (NAME_Zlib). Always decodable. */
	Zlib,
	/** Payload is Oodle-compressed via FCompression (NAME_Oodle). Only chosen when Oodle is registered. */
	Oodle
};

/**
 * Bit flags packed into FSaveX_ContainerHeader::Flags. Serialized as a single uint8 so the on-disk layout
 * is stable and tool-readable. Mirrors the explicit, version-stable layout style of FDP_SaveHeader.
 */
enum class ESaveX_ContainerFlag : uint8
{
	None        = 0,
	/** Payload bytes are compressed (CompressionMethod tells which codec). */
	Compressed  = 1 << 0,
	/** Payload bytes were run through ISeam_SaveCipher (EncryptionKeyId selects the key). */
	Encrypted   = 1 << 1,
	/** A PNG thumbnail blob follows the payload (ThumbnailByteOffset/ThumbnailLen are valid). */
	HasThumbnail = 1 << 2,
	/** This container is the persistent profile partition (not a per-world save). */
	IsProfile   = 1 << 3,
	/** This container was written by the checkpoint feature. */
	IsCheckpoint = 1 << 4
};

ENUM_CLASS_FLAGS(ESaveX_ContainerFlag);

/**
 * Custom-version stream for the SaveX wrapper container ONLY.
 *
 * This is INTENTIONALLY a separate GUID from the core FDP_SaveVersion: the container envelope and the
 * inner core blob version independently. The inner bytes remain the untouched core
 * [int64 hdrLen][FDP_SaveHeader][int64 bodyLen][body] blob and are versioned by the core's own stream.
 */
struct DESIGNPATTERNSSAVESYSTEM_API FSaveX_ContainerVersion
{
	enum Type
	{
		/** Pre-versioning sentinel (never written). */
		BeforeCustomVersionWasAdded = 0,

		/** Initial container: magic, flags, compression, CRC, sizes, thumbnail offsets, cloud metadata. */
		InitialVersion = 1,

		// --- new container versions go ABOVE this line ---

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	/** Unique GUID identifying the container custom-version stream (distinct from FDP_SaveVersion::GUID). */
	static const FGuid GUID;

	/** Register the custom version with the engine. Called once at module startup. */
	static void Register();

private:
	FSaveX_ContainerVersion() = delete;
};

/**
 * Outer corruption-safety + feature envelope written in FRONT of the (optionally compressed/encrypted)
 * opaque core save blob.
 *
 * LAYOUT ON DISK (a wrapped ".dpcsav" file):
 *   [ FSaveX_ContainerHeader (this struct, via Serialize) ]
 *   [ PayloadSize bytes : final transformed payload (compressed?->encrypted?) ]
 *   [ ThumbnailLen bytes : optional PNG thumbnail, present iff HasThumbnail flag ]
 *
 * The INNER payload (after decrypt+decompress) is the byte-identical core blob the core subsystem
 * produces, so the core round-trips verbatim. A DISTINCT extension (".dpcsav") and DISTINCT magic
 * ('SAVX') keep the core's "*.dpsav" enumeration/ReadSlotHeader from ever touching a wrapped file.
 *
 * Sizes and offsets are 64-bit and are NOT UPROPERTYs — they are written through explicit Serialize only
 * (matching FDP_SaveHeader's hand-rolled layout), so reflected serialization never reorders them. The
 * BP-readable scalar metadata fields (magic, version, flags, CRC, thumbnail dimensions, cloud info) ARE
 * UPROPERTYs so a save/load UI and the version inspector can read them without a custom archive.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSAVESYSTEM_API FSaveX_ContainerHeader
{
	GENERATED_BODY()

	/** Four-byte magic identifying a SaveX wrapped container ('S''A''V''X'). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 ContainerMagic = DefaultMagic();

	/** FSaveX_ContainerVersion::Type this container was written with (drives any future envelope migration). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 ContainerFormatVersion = static_cast<int32>(FSaveX_ContainerVersion::LatestVersion);

	/** Packed ESaveX_ContainerFlag bits (Compressed/Encrypted/HasThumbnail/IsProfile/IsCheckpoint). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	uint8 Flags = 0;

	/** Codec used for the payload (only meaningful when the Compressed flag is set). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	ESaveX_Compression CompressionMethod = ESaveX_Compression::None;

	/**
	 * uint32 FCrc::MemCrc32 over the FINAL post-transform payload bytes (after compress+encrypt), stored as
	 * int32 so it is BP-safe. Verified before any decrypt/decompress so corruption is caught early.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 InnerCrc = 0;

	/** PNG thumbnail pixel width (0 when no thumbnail). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 ThumbnailWidth = 0;

	/** PNG thumbnail pixel height (0 when no thumbnail). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 ThumbnailHeight = 0;

	/** Identifier (never the key) of the ISeam_SaveCipher key used; zero GUID when not encrypted. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FGuid EncryptionKeyId;

	/** UTC time the container was written, used for cloud conflict detection. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FDateTime WrittenUtc = FDateTime(0);

	/** Opaque cloud ETag/version token last synced for this slot (empty if never uploaded). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FString CloudConflictETag;

	/** UTC time this container was last confirmed synced to cloud (zero if never). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FDateTime CloudSyncedUtc = FDateTime(0);

	// ---- 64-bit sizes/offsets: explicit Serialize ONLY (not UPROPERTY) ----

	/** Size in bytes of the INNER core blob BEFORE compression (needed to size the decompress buffer). */
	int64 UncompressedSize = 0;

	/** Size in bytes of the final transformed payload as stored on disk. */
	int64 PayloadSize = 0;

	/** Byte offset of the PNG thumbnail from the START of the file (0 when no thumbnail). */
	int64 ThumbnailByteOffset = 0;

	/** Length in bytes of the PNG thumbnail blob (0 when no thumbnail). */
	int64 ThumbnailLen = 0;

	FSaveX_ContainerHeader() = default;

	/** The expected container magic value. */
	static constexpr int32 DefaultMagic() { return 0x53415658; } // 'SAVX'

	/** True if ContainerMagic matches the SaveX magic. */
	bool IsMagicValid() const { return ContainerMagic == DefaultMagic(); }

	/** Convenience flag testers/setters operating on the packed Flags byte. */
	bool HasFlag(ESaveX_ContainerFlag Flag) const
	{
		return (Flags & static_cast<uint8>(Flag)) != 0;
	}
	void SetFlag(ESaveX_ContainerFlag Flag, bool bSet)
	{
		if (bSet) { Flags |= static_cast<uint8>(Flag); }
		else      { Flags &= static_cast<uint8>(~static_cast<uint8>(Flag)); }
	}

	/**
	 * Serialize this header's fields. Kept explicit so the chunk layout is stable and tool-readable,
	 * exactly like FDP_SaveHeader::Serialize. Writes scalar metadata then the 64-bit sizes/offsets.
	 */
	void Serialize(FArchive& Ar);

	/** UStruct Serialize hook so FArchive << works for the header. */
	bool Serialize(FStructuredArchive::FSlot Slot)
	{
		Serialize(Slot.GetUnderlyingArchive());
		return true;
	}

	friend FArchive& operator<<(FArchive& Ar, FSaveX_ContainerHeader& H)
	{
		H.Serialize(Ar);
		return Ar;
	}
};

template<>
struct TStructOpsTypeTraits<FSaveX_ContainerHeader> : public TStructOpsTypeTraitsBase2<FSaveX_ContainerHeader>
{
	enum { WithSerializer = true };
};
