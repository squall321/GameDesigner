// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/SaveX_ContainerHeader.h"

// Stable GUID for the container envelope custom-version stream. Distinct from FDP_SaveVersion::GUID so
// the wrapper envelope and the inner core blob version independently. NEVER change this value.
const FGuid FSaveX_ContainerVersion::GUID(0x5A56C0DE, 0x53415658, 0xD9000001, 0xC0000001);

namespace
{
	/** One-time engine registration of the container custom version. */
	struct FSaveX_ContainerVersionRegistration
	{
		FSaveX_ContainerVersionRegistration()
		{
			// Registered lazily via FSaveX_ContainerVersion::Register(); the constructor body is in Register()
			// so registration order is explicit (called from module startup), not static-init dependent.
		}
	};
}

void FSaveX_ContainerVersion::Register()
{
	// FCustomVersionRegistration is normally a static; here we register imperatively from module startup so
	// the registration is deterministic and we do not rely on translation-unit static-init ordering.
	static FCustomVersionRegistration GRegister(
		GUID,
		static_cast<int32>(FSaveX_ContainerVersion::LatestVersion),
		TEXT("DesignPatternsSaveXContainer"));
}

void FSaveX_ContainerHeader::Serialize(FArchive& Ar)
{
	// Tie this archive to the container version stream so loaders can branch on ContainerFormatVersion.
	Ar.UsingCustomVersion(FSaveX_ContainerVersion::GUID);

	// --- Scalar metadata (stable order; matches the on-disk layout intent) ---
	Ar << ContainerMagic;
	Ar << ContainerFormatVersion;
	Ar << Flags;

	// ESaveX_Compression is a uint8 enum; serialize through a uint8 so the byte width is fixed regardless of
	// the underlying enum type the compiler picks.
	uint8 CompressionByte = static_cast<uint8>(CompressionMethod);
	Ar << CompressionByte;
	if (Ar.IsLoading())
	{
		CompressionMethod = static_cast<ESaveX_Compression>(CompressionByte);
	}

	Ar << InnerCrc;
	Ar << ThumbnailWidth;
	Ar << ThumbnailHeight;
	Ar << EncryptionKeyId;
	Ar << WrittenUtc;
	Ar << CloudConflictETag;
	Ar << CloudSyncedUtc;

	// --- 64-bit sizes/offsets ---
	Ar << UncompressedSize;
	Ar << PayloadSize;
	Ar << ThumbnailByteOffset;
	Ar << ThumbnailLen;
}
