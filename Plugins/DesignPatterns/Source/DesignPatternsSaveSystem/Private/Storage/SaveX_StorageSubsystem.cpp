// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/SaveX_StorageSubsystem.h"
#include "Storage/SaveX_ThumbnailCapturer.h"

#include "Settings/SaveX_DeveloperSettings.h"
#include "Settings/SaveX_StorageDeveloperSettings.h"
#include "SaveX_StorageServiceKeys.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"           // FDP_SubsystemStatics
#include "Services/DPServiceLocatorSubsystem.h"

#include "Save/DPSaveGame.h"
#include "Save/DPSaveGameSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Misc/CompressionFlags.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Async/Async.h"

namespace
{
	/** The core writes blobs to <ProjectSavedDir>/SaveGames/<Slot>.dpsav; reproduce that path to read the bytes. */
	FString CoreBlobFilePath(const FString& Slot)
	{
		const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
		return FPaths::Combine(SaveDir, Slot + TEXT(".dpsav"));
	}

	/** Map an FCompression failure to the right wrapper result. */
	const TCHAR* CompressionNameFor(ESaveX_Compression Method)
	{
		switch (Method)
		{
		case ESaveX_Compression::Zlib:  return TEXT("Zlib");
		case ESaveX_Compression::Oodle: return TEXT("Oodle");
		default:                        return TEXT("None");
		}
	}

	/** Resolve the FName codec for FCompression, downgrading Oodle->Zlib when Oodle is not registered. */
	FName ResolveCompressionFName(ESaveX_Compression& InOutMethod)
	{
		if (InOutMethod == ESaveX_Compression::Oodle)
		{
			// FCompression has no public "is registered" query for Oodle by name in all engine versions, so
			// probe by attempting a trivial bound-check. NAME_Oodle is only usable if the format is present;
			// fall back to Zlib defensively when unsure so the container always decodes everywhere.
			static const FName OodleName(TEXT("Oodle"));
			if (FCompression::IsFormatValid(OodleName))
			{
				return OodleName;
			}
			InOutMethod = ESaveX_Compression::Zlib;
		}
		if (InOutMethod == ESaveX_Compression::Zlib)
		{
			return NAME_Zlib;
		}
		return NAME_None;
	}
}

void USaveX_StorageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Owned helper for async thumbnails (instanced subobject via NewObject(this) so the engine GCs it with us).
	ThumbnailCapturer = NewObject<USaveX_ThumbnailCapturer>(this, USaveX_ThumbnailCapturer::StaticClass());

	// Resolve the cipher key (settings -> conventional fallback) and try an initial resolve.
	if (const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get())
	{
		CipherServiceKey = Settings->CipherServiceTag.IsValid() ? Settings->CipherServiceTag : SaveX_StorageServiceKeys::Cipher();
	}
	else
	{
		CipherServiceKey = SaveX_StorageServiceKeys::Cipher();
	}
	ResolveCipher();

	UE_LOG(LogDPSave, Log, TEXT("[Storage] Initialized (cipherKey=%s)."),
		CipherServiceKey.IsValid() ? *CipherServiceKey.ToString() : TEXT("<none>"));
}

void USaveX_StorageSubsystem::Deinitialize()
{
	if (ThumbnailCapturer)
	{
		ThumbnailCapturer->Shutdown();
		ThumbnailCapturer = nullptr;
	}
	Cipher = nullptr;
	Super::Deinitialize();
}

// ---- Resolution helpers --------------------------------------------------------------------------

UDP_SaveGameSubsystem* USaveX_StorageSubsystem::GetCoreSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(
		const_cast<USaveX_StorageSubsystem*>(this));
}

UDP_ServiceLocatorSubsystem* USaveX_StorageSubsystem::GetServiceLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(
		const_cast<USaveX_StorageSubsystem*>(this));
}

void USaveX_StorageSubsystem::ResolveCipher()
{
	Cipher = nullptr;
	if (!CipherServiceKey.IsValid())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = GetServiceLocator())
	{
		if (UObject* Provider = Locator->ResolveService(CipherServiceKey))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_SaveCipher::StaticClass()))
			{
				Cipher.SetObject(Provider);
				Cipher.SetInterface(Cast<ISeam_SaveCipher>(Provider));
			}
		}
	}
}

// ---- Path helpers --------------------------------------------------------------------------------

FString USaveX_StorageSubsystem::WrappedFilePath(const FString& Slot)
{
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	return FPaths::Combine(SaveDir, Slot + TEXT(".") + WrappedExtension());
}

FString USaveX_StorageSubsystem::TempFilePath(const FString& Slot)
{
	return WrappedFilePath(Slot) + TEXT(".tmp");
}

FString USaveX_StorageSubsystem::BackupFilePath(const FString& Slot, int32 Index)
{
	return WrappedFilePath(Slot) + FString::Printf(TEXT(".bak%d"), FMath::Max(0, Index));
}

FString USaveX_StorageSubsystem::ScratchSlotName(const FString& Slot)
{
	// Reserved prefix the slot manager already excludes from named-slot enumeration; unique per slot so two
	// concurrent wraps never share a scratch file.
	return FString::Printf(TEXT("_dpscratch_%s"), *Slot);
}

// ---- Public API: save ----------------------------------------------------------------------------

void USaveX_StorageSubsystem::SaveWrapped(const FString& Slot, UDP_SaveGame* SaveObject, bool bIsAutosave,
	uint8 ExtraFlags, FSaveX_StorageSaveDone OnDone)
{
	check(IsInGameThread());

	if (Slot.IsEmpty() || !SaveObject)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] SaveWrapped rejected: invalid slot/object."));
		OnDone.ExecuteIfBound(Slot, ESaveX_StorageResult::InvalidArgument);
		return;
	}

	// GAME THREAD: get the opaque core blob now (UObject work must stay on the game thread).
	TArray<uint8> InnerBlob;
	if (!AcquireCoreBlob_GameThread(Slot, SaveObject, InnerBlob))
	{
		OnDone.ExecuteIfBound(Slot, ESaveX_StorageResult::CoreFailure);
		return;
	}

	// Request a thumbnail (async). The rest of the pipeline runs from the thumbnail callback so the captured
	// frame can be appended to the container. A skipped/failed capture returns empty bytes immediately.
	++PendingOps;
	TWeakObjectPtr<USaveX_StorageSubsystem> WeakThis(this);
	const FString CapturedSlot = Slot;
	const uint8 CapturedFlags = ExtraFlags;

	USaveX_ThumbnailCapturer::FSaveX_ThumbnailReady OnThumb;
	OnThumb.BindLambda([WeakThis, CapturedSlot, CapturedFlags, InnerBlob = MoveTemp(InnerBlob), OnDone]
		(TArray<uint8>&& Png, int32 ThumbW, int32 ThumbH) mutable
	{
		USaveX_StorageSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			OnDone.ExecuteIfBound(CapturedSlot, ESaveX_StorageResult::SubsystemUnavailable);
			return;
		}

		// OFF-THREAD: build the container bytes and write atomically (plain copies only).
		Async(EAsyncExecution::ThreadPool,
			[WeakThis, CapturedSlot, CapturedFlags, InnerBlob = MoveTemp(InnerBlob), Png = MoveTemp(Png), ThumbW, ThumbH, OnDone]() mutable
		{
			ESaveX_StorageResult Result = ESaveX_StorageResult::CoreFailure;
			FSaveX_ContainerHeader Header;
			TArray<uint8> FileBytes;
			FString ETag;

			if (USaveX_StorageSubsystem* Self2 = WeakThis.Get())
			{
				Result = Self2->BuildContainerBytes(InnerBlob, CapturedFlags, Png, ThumbW, ThumbH, Header, FileBytes);
				if (Result == ESaveX_StorageResult::Success)
				{
					Result = Self2->WriteContainerAtomic_OffThread(CapturedSlot, FileBytes);
					ETag = Header.CloudConflictETag;
				}
			}

			// Back to the game thread to fire delegates (UObject-touching).
			AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSlot, Result, ETag, OnDone]()
			{
				if (USaveX_StorageSubsystem* Self3 = WeakThis.Get())
				{
					Self3->PendingOps = FMath::Max(0, Self3->PendingOps - 1);
					if (Result == ESaveX_StorageResult::Success)
					{
						Self3->OnStorageWritten.Broadcast(CapturedSlot, WrappedFilePath(CapturedSlot), ETag);
					}
					else
					{
						UE_LOG(LogDPSave, Warning, TEXT("[Storage] SaveWrapped('%s') failed (result=%d)."),
							*CapturedSlot, static_cast<int32>(Result));
					}
				}
				OnDone.ExecuteIfBound(CapturedSlot, Result);
			});
		});
	});

	ThumbnailCapturer->RequestThumbnailForSave(this, bIsAutosave, MoveTemp(OnThumb));
}

// ---- Public API: load ----------------------------------------------------------------------------

void USaveX_StorageSubsystem::LoadWrapped(const FString& Slot, FSaveX_StorageLoadDone OnDone)
{
	check(IsInGameThread());

	if (Slot.IsEmpty())
	{
		OnDone.ExecuteIfBound(Slot, ESaveX_StorageResult::InvalidArgument, nullptr);
		return;
	}

	const FString WrappedPath = WrappedFilePath(Slot);
	const bool bHasWrapped = IFileManager::Get().FileExists(*WrappedPath);

	// A plain (un-wrapped) ".dpsav" with no SAVX container is routed straight to the core loader so shipped
	// saves keep working. We detect this by the absence of a ".dpcsav".
	if (!bHasWrapped)
	{
		UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
		if (!Core)
		{
			OnDone.ExecuteIfBound(Slot, ESaveX_StorageResult::SubsystemUnavailable, nullptr);
			return;
		}
		if (!Core->DoesSlotExist(Slot))
		{
			OnDone.ExecuteIfBound(Slot, ESaveX_StorageResult::SlotNotFound, nullptr);
			return;
		}
		EDP_SaveResult CoreResult = EDP_SaveResult::Success;
		UDP_SaveGame* Loaded = Core->LoadNow(Slot, CoreResult);
		const ESaveX_StorageResult R = (CoreResult == EDP_SaveResult::Success && Loaded)
			? ESaveX_StorageResult::Success : ESaveX_StorageResult::CoreFailure;
		UE_LOG(LogDPSave, Log, TEXT("[Storage] LoadWrapped('%s'): plain core save routed to core loader (result=%d)."),
			*Slot, static_cast<int32>(CoreResult));
		OnDone.ExecuteIfBound(Slot, R, R == ESaveX_StorageResult::Success ? Loaded : nullptr);
		return;
	}

	++PendingOps;
	TWeakObjectPtr<USaveX_StorageSubsystem> WeakThis(this);
	const FString CapturedSlot = Slot;

	// OFF-THREAD: read + verify + decrypt + decompress the container to the inner core blob.
	Async(EAsyncExecution::ThreadPool, [WeakThis, CapturedSlot, OnDone]() mutable
	{
		ESaveX_StorageResult ExtractResult = ESaveX_StorageResult::IOFailed;
		TArray<uint8> InnerBlob;
		FSaveX_ContainerHeader Header;
		int32 BackupUsed = INDEX_NONE;

		USaveX_StorageSubsystem* Self = WeakThis.Get();
		if (Self)
		{
			// Try the primary; on corruption, walk the backup ring (newest first).
			TArray<uint8> FileBytes;
			if (FFileHelper::LoadFileToArray(FileBytes, *WrappedFilePath(CapturedSlot)))
			{
				ExtractResult = Self->ExtractInnerBlob(FileBytes, Header, InnerBlob);
			}

			if (ExtractResult != ESaveX_StorageResult::Success)
			{
				const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get();
				const bool bRecover = !Settings || Settings->bRecoverFromBackupOnCorruption;
				const int32 BackupCount = Settings ? Settings->GetEffectiveBackupCount() : 0;
				if (bRecover)
				{
					for (int32 i = 0; i < BackupCount; ++i)
					{
						TArray<uint8> BakBytes;
						if (FFileHelper::LoadFileToArray(BakBytes, *BackupFilePath(CapturedSlot, i)))
						{
							FSaveX_ContainerHeader BakHeader;
							TArray<uint8> BakInner;
							if (Self->ExtractInnerBlob(BakBytes, BakHeader, BakInner) == ESaveX_StorageResult::Success)
							{
								InnerBlob = MoveTemp(BakInner);
								Header = BakHeader;
								ExtractResult = ESaveX_StorageResult::Success;
								BackupUsed = i;
								break;
							}
						}
					}
				}
			}
		}

		// Back to the game thread for the core (UObject) deserialize + delegate.
		AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSlot, ExtractResult, InnerBlob = MoveTemp(InnerBlob), BackupUsed, OnDone]() mutable
		{
			USaveX_StorageSubsystem* Self2 = WeakThis.Get();
			if (Self2)
			{
				Self2->PendingOps = FMath::Max(0, Self2->PendingOps - 1);
			}

			if (ExtractResult != ESaveX_StorageResult::Success || !Self2)
			{
				OnDone.ExecuteIfBound(CapturedSlot, ExtractResult, nullptr);
				return;
			}

			if (BackupUsed != INDEX_NONE)
			{
				UE_LOG(LogDPSave, Warning, TEXT("[Storage] LoadWrapped('%s'): primary corrupt, recovered from backup %d."),
					*CapturedSlot, BackupUsed);
				Self2->OnStorageRecovered.Broadcast(CapturedSlot, BackupUsed);
			}

			ESaveX_StorageResult DeserResult = ESaveX_StorageResult::CoreFailure;
			UDP_SaveGame* Loaded = Self2->DeserializeCoreBlob_GameThread(CapturedSlot, InnerBlob, DeserResult);
			OnDone.ExecuteIfBound(CapturedSlot, DeserResult, DeserResult == ESaveX_StorageResult::Success ? Loaded : nullptr);
		});
	});
}

// ---- Core blob acquisition (game thread, via scratch slot) ---------------------------------------

bool USaveX_StorageSubsystem::AcquireCoreBlob_GameThread(const FString& Slot, UDP_SaveGame* SaveObject, TArray<uint8>& OutBlob) const
{
	check(IsInGameThread());
	OutBlob.Reset();

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] AcquireCoreBlob('%s'): no core save subsystem."), *Slot);
		return false;
	}

	// Route the object through the core to a reserved scratch slot (synchronous so the file is on disk when
	// we read it), grab the produced bytes, then delete the scratch slot. The core owns ALL serialization.
	const FString Scratch = ScratchSlotName(Slot);
	const EDP_SaveResult CoreResult = Core->SaveNow(Scratch, SaveObject);
	if (CoreResult != EDP_SaveResult::Success)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] AcquireCoreBlob('%s'): core SaveNow failed (result=%d)."),
			*Slot, static_cast<int32>(CoreResult));
		Core->DeleteSlot(Scratch);
		return false;
	}

	const bool bRead = FFileHelper::LoadFileToArray(OutBlob, *CoreBlobFilePath(Scratch));
	Core->DeleteSlot(Scratch); // always clean up the scratch slot

	if (!bRead || OutBlob.Num() == 0)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] AcquireCoreBlob('%s'): could not read scratch blob."), *Slot);
		OutBlob.Reset();
		return false;
	}
	return true;
}

UDP_SaveGame* USaveX_StorageSubsystem::DeserializeCoreBlob_GameThread(const FString& Slot, const TArray<uint8>& InnerBlob, ESaveX_StorageResult& OutResult) const
{
	check(IsInGameThread());
	OutResult = ESaveX_StorageResult::CoreFailure;

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		OutResult = ESaveX_StorageResult::SubsystemUnavailable;
		return nullptr;
	}
	if (InnerBlob.Num() == 0)
	{
		OutResult = ESaveX_StorageResult::CorruptData;
		return nullptr;
	}

	// Write the recovered inner blob to a scratch ".dpsav" and let the core LoadNow it (UObject construction
	// + migrate + OnPostLoad all happen inside the core), then delete the scratch slot.
	const FString Scratch = ScratchSlotName(Slot);
	if (!FFileHelper::SaveArrayToFile(InnerBlob, *CoreBlobFilePath(Scratch)))
	{
		OutResult = ESaveX_StorageResult::IOFailed;
		return nullptr;
	}

	EDP_SaveResult CoreResult = EDP_SaveResult::Success;
	UDP_SaveGame* Loaded = Core->LoadNow(Scratch, CoreResult);
	Core->DeleteSlot(Scratch);

	if (CoreResult == EDP_SaveResult::Success && Loaded)
	{
		OutResult = ESaveX_StorageResult::Success;
		return Loaded;
	}

	UE_LOG(LogDPSave, Warning, TEXT("[Storage] DeserializeCoreBlob('%s'): core LoadNow failed (result=%d)."),
		*Slot, static_cast<int32>(CoreResult));
	OutResult = (CoreResult == EDP_SaveResult::CorruptData) ? ESaveX_StorageResult::CorruptData : ESaveX_StorageResult::CoreFailure;
	return nullptr;
}

// ---- Container build / extract (off-thread-safe; plain copies) -----------------------------------

ESaveX_StorageResult USaveX_StorageSubsystem::BuildContainerBytes(const TArray<uint8>& InnerBlob, uint8 ExtraFlags,
	const TArray<uint8>& ThumbnailPng, int32 ThumbW, int32 ThumbH,
	FSaveX_ContainerHeader& OutHeader, TArray<uint8>& OutFileBytes) const
{
	const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get();

	OutHeader = FSaveX_ContainerHeader();
	OutHeader.Flags |= ExtraFlags;
	OutHeader.WrittenUtc = FDateTime::UtcNow();
	OutHeader.UncompressedSize = InnerBlob.Num();

	TArray<uint8> Payload = InnerBlob; // start from a plain copy

	// --- Compression ---
	bool bCompressed = false;
	ESaveX_Compression Method = Settings ? Settings->CompressionMethod : ESaveX_Compression::Zlib;
	const int32 MinToCompress = Settings ? FMath::Max(0, Settings->MinBytesToCompress) : 256;
	const bool bWantCompress = (!Settings || Settings->bCompressSaves) && Payload.Num() >= MinToCompress;
	if (bWantCompress)
	{
		const FName CodecName = ResolveCompressionFName(Method);
		if (CodecName != NAME_None)
		{
			int32 CompressedBound = FCompression::CompressMemoryBound(CodecName, Payload.Num());
			TArray<uint8> Compressed;
			Compressed.SetNumUninitialized(CompressedBound);
			if (FCompression::CompressMemory(CodecName, Compressed.GetData(), CompressedBound, Payload.GetData(), Payload.Num()))
			{
				Compressed.SetNum(CompressedBound, /*bAllowShrinking=*/false);
				// Only keep the compressed form if it actually shrank; otherwise store verbatim.
				if (Compressed.Num() < Payload.Num())
				{
					Payload = MoveTemp(Compressed);
					bCompressed = true;
					OutHeader.CompressionMethod = Method;
				}
			}
			else
			{
				UE_LOG(LogDPSave, Warning, TEXT("[Storage] %s compression failed; storing verbatim."), CompressionNameFor(Method));
			}
		}
	}
	OutHeader.SetFlag(ESaveX_ContainerFlag::Compressed, bCompressed);
	if (!bCompressed)
	{
		OutHeader.CompressionMethod = ESaveX_Compression::None;
	}

	// --- Encryption (via the cipher seam) ---
	bool bEncrypted = false;
	const bool bCipherEnabled = Cipher && Cipher.GetObject() && ISeam_SaveCipher::Execute_IsEnabled(Cipher.GetObject());
	if (bCipherEnabled)
	{
		FGuid KeyId;
		TArray<uint8> Encrypted;
		if (ISeam_SaveCipher::Execute_EncryptBuffer(Cipher.GetObject(), Payload, KeyId, Encrypted))
		{
			Payload = MoveTemp(Encrypted);
			bEncrypted = true;
			OutHeader.EncryptionKeyId = KeyId;
		}
		else
		{
			UE_LOG(LogDPSave, Warning, TEXT("[Storage] Cipher EncryptBuffer failed."));
			if (Settings && Settings->bRequireEncryption)
			{
				return ESaveX_StorageResult::EncryptionFailed;
			}
		}
	}
	else if (Settings && Settings->bRequireEncryption)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] Encryption required but no enabled cipher is registered."));
		return ESaveX_StorageResult::EncryptionFailed;
	}
	OutHeader.SetFlag(ESaveX_ContainerFlag::Encrypted, bEncrypted);

	// --- CRC over the FINAL transformed payload ---
	OutHeader.PayloadSize = Payload.Num();
	OutHeader.InnerCrc = static_cast<int32>(FCrc::MemCrc32(Payload.GetData(), Payload.Num()));

	// --- Thumbnail metadata ---
	const bool bHasThumb = ThumbnailPng.Num() > 0 && ThumbW > 0 && ThumbH > 0;
	OutHeader.SetFlag(ESaveX_ContainerFlag::HasThumbnail, bHasThumb);
	OutHeader.ThumbnailWidth = bHasThumb ? ThumbW : 0;
	OutHeader.ThumbnailHeight = bHasThumb ? ThumbH : 0;
	OutHeader.ThumbnailLen = bHasThumb ? ThumbnailPng.Num() : 0;

	// --- Assemble the file: [header][payload][thumbnail?] ---
	// The header's 64-bit fields are FIXED-WIDTH, so the serialized header length does not depend on any
	// field VALUE. We therefore measure the header length once with a provisional offset, set the real
	// thumbnail offset, then serialize the final header (same length) followed by the payload + thumbnail.
	OutHeader.ThumbnailByteOffset = 0; // provisional
	{
		TArray<uint8> HeaderProbe;
		FMemoryWriter Probe(HeaderProbe, /*bIsPersistent=*/true);
		OutHeader.Serialize(Probe);
		const int64 HeaderLen = HeaderProbe.Num();
		if (bHasThumb)
		{
			OutHeader.ThumbnailByteOffset = HeaderLen + Payload.Num();
		}
	}

	OutFileBytes.Reset();
	FMemoryWriter Writer(OutFileBytes, /*bIsPersistent=*/true);
	OutHeader.Serialize(Writer);
	OutFileBytes.Append(Payload);
	if (bHasThumb)
	{
		OutFileBytes.Append(ThumbnailPng);
	}

	return ESaveX_StorageResult::Success;
}

ESaveX_StorageResult USaveX_StorageSubsystem::ExtractInnerBlob(const TArray<uint8>& FileBytes,
	FSaveX_ContainerHeader& OutHeader, TArray<uint8>& OutInnerBlob) const
{
	OutInnerBlob.Reset();
	if (!LooksLikeWrappedContainer(FileBytes))
	{
		return ESaveX_StorageResult::CorruptData;
	}

	FMemoryReader Reader(FileBytes, /*bIsPersistent=*/true);
	OutHeader = FSaveX_ContainerHeader();
	OutHeader.Serialize(Reader);
	if (!OutHeader.IsMagicValid())
	{
		return ESaveX_StorageResult::CorruptData;
	}

	const int64 HeaderEnd = Reader.Tell();
	if (OutHeader.PayloadSize <= 0 || HeaderEnd + OutHeader.PayloadSize > FileBytes.Num())
	{
		return ESaveX_StorageResult::CorruptData;
	}

	// Slice out the payload bytes.
	TArray<uint8> Payload;
	Payload.Append(FileBytes.GetData() + HeaderEnd, OutHeader.PayloadSize);

	// Verify CRC over the stored payload before any transform.
	const int32 ActualCrc = static_cast<int32>(FCrc::MemCrc32(Payload.GetData(), Payload.Num()));
	if (ActualCrc != OutHeader.InnerCrc)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] Container CRC mismatch (stored=%d actual=%d)."), OutHeader.InnerCrc, ActualCrc);
		return ESaveX_StorageResult::CorruptData;
	}

	// --- Decrypt ---
	if (OutHeader.HasFlag(ESaveX_ContainerFlag::Encrypted))
	{
		if (!Cipher || !Cipher.GetObject())
		{
			UE_LOG(LogDPSave, Warning, TEXT("[Storage] Encrypted container but no cipher registered."));
			return ESaveX_StorageResult::EncryptionFailed;
		}
		TArray<uint8> Decrypted;
		if (!ISeam_SaveCipher::Execute_DecryptBuffer(Cipher.GetObject(), Payload, OutHeader.EncryptionKeyId, Decrypted))
		{
			UE_LOG(LogDPSave, Warning, TEXT("[Storage] Cipher DecryptBuffer failed."));
			return ESaveX_StorageResult::EncryptionFailed;
		}
		Payload = MoveTemp(Decrypted);
	}

	// --- Decompress ---
	if (OutHeader.HasFlag(ESaveX_ContainerFlag::Compressed))
	{
		ESaveX_Compression Method = OutHeader.CompressionMethod;
		const FName CodecName = ResolveCompressionFName(Method);
		if (CodecName == NAME_None || OutHeader.UncompressedSize <= 0)
		{
			return ESaveX_StorageResult::CompressionFailed;
		}
		TArray<uint8> Decompressed;
		Decompressed.SetNumUninitialized(OutHeader.UncompressedSize);
		if (!FCompression::UncompressMemory(CodecName, Decompressed.GetData(), OutHeader.UncompressedSize, Payload.GetData(), Payload.Num()))
		{
			UE_LOG(LogDPSave, Warning, TEXT("[Storage] Decompression failed."));
			return ESaveX_StorageResult::CompressionFailed;
		}
		OutInnerBlob = MoveTemp(Decompressed);
	}
	else
	{
		OutInnerBlob = MoveTemp(Payload);
	}

	return ESaveX_StorageResult::Success;
}

ESaveX_StorageResult USaveX_StorageSubsystem::WriteContainerAtomic_OffThread(const FString& Slot, const TArray<uint8>& FileBytes) const
{
	const FString FinalPath = WrappedFilePath(Slot);
	const FString TempPath = TempFilePath(Slot);

	IFileManager& FM = IFileManager::Get();
	// Ensure the SaveGames directory exists.
	FM.MakeDirectory(*FPaths::GetPath(FinalPath), /*Tree=*/true);

	// 1) Write to a temp file first so a crash mid-write never corrupts the live save.
	if (!FFileHelper::SaveArrayToFile(FileBytes, *TempPath))
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] Temp write failed for '%s'."), *Slot);
		return ESaveX_StorageResult::IOFailed;
	}

	// 2) Rotate backups: shift bak(N-2)->bak(N-1) ... bak0->bak1, then current final->bak0.
	const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get();
	const int32 BackupCount = Settings ? Settings->GetEffectiveBackupCount() : 0;
	if (BackupCount > 0 && FM.FileExists(*FinalPath))
	{
		for (int32 i = BackupCount - 1; i >= 1; --i)
		{
			const FString Src = BackupFilePath(Slot, i - 1);
			const FString Dst = BackupFilePath(Slot, i);
			if (FM.FileExists(*Src))
			{
				FM.Move(*Dst, *Src, /*bReplace=*/true, /*bEvenIfReadOnly=*/true);
			}
		}
		// Promote the current good file to bak0.
		FM.Move(*BackupFilePath(Slot, 0), *FinalPath, /*bReplace=*/true, /*bEvenIfReadOnly=*/true);
	}

	// 3) Atomically promote the temp file to the final name.
	if (!FM.Move(*FinalPath, *TempPath, /*bReplace=*/true, /*bEvenIfReadOnly=*/true))
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Storage] Atomic rename failed for '%s'."), *Slot);
		FM.Delete(*TempPath, /*bRequireExists=*/false, /*bEvenIfReadOnly=*/true);
		return ESaveX_StorageResult::IOFailed;
	}

	return ESaveX_StorageResult::Success;
}

bool USaveX_StorageSubsystem::LooksLikeWrappedContainer(const TArray<uint8>& FileBytes)
{
	if (FileBytes.Num() < static_cast<int32>(sizeof(int32)))
	{
		return false;
	}
	// The first serialized field is ContainerMagic (int32). Read it without consuming a full header.
	int32 Magic = 0;
	FMemoryReader Reader(FileBytes, true);
	Reader << Magic;
	return Magic == FSaveX_ContainerHeader::DefaultMagic();
}

// ---- Read-only header / existence ----------------------------------------------------------------

bool USaveX_StorageSubsystem::ReadContainerHeader(const FString& Slot, FSaveX_ContainerHeader& Out) const
{
	Out = FSaveX_ContainerHeader();
	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *WrappedFilePath(Slot)))
	{
		return false;
	}
	if (!LooksLikeWrappedContainer(FileBytes))
	{
		return false;
	}
	FMemoryReader Reader(FileBytes, true);
	Out.Serialize(Reader);
	return Out.IsMagicValid();
}

bool USaveX_StorageSubsystem::DoesWrappedSlotExist(const FString& Slot) const
{
	return IFileManager::Get().FileExists(*WrappedFilePath(Slot));
}

ESaveX_StorageResult USaveX_StorageSubsystem::RecoverFromBackup(const FString& Slot)
{
	const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get();
	const int32 BackupCount = Settings ? Settings->GetEffectiveBackupCount() : 0;

	for (int32 i = 0; i < BackupCount; ++i)
	{
		TArray<uint8> BakBytes;
		if (FFileHelper::LoadFileToArray(BakBytes, *BackupFilePath(Slot, i)))
		{
			FSaveX_ContainerHeader Header;
			TArray<uint8> Inner;
			if (ExtractInnerBlob(BakBytes, Header, Inner) == ESaveX_StorageResult::Success)
			{
				// Promote this valid backup to the primary file.
				if (FFileHelper::SaveArrayToFile(BakBytes, *WrappedFilePath(Slot)))
				{
					UE_LOG(LogDPSave, Log, TEXT("[Storage] Recovered slot '%s' from backup %d."), *Slot, i);
					OnStorageRecovered.Broadcast(Slot, i);
					return ESaveX_StorageResult::Success;
				}
				return ESaveX_StorageResult::IOFailed;
			}
		}
	}
	return ESaveX_StorageResult::SlotNotFound;
}

// ---- Debug ---------------------------------------------------------------------------------------

FString USaveX_StorageSubsystem::GetDPDebugString_Implementation() const
{
	const bool bCipher = Cipher && Cipher.GetObject();
	return FString::Printf(TEXT("Storage: pendingOps=%d cipher=%s ext=.%s"),
		PendingOps, bCipher ? TEXT("yes") : TEXT("no"), WrappedExtension());
}
