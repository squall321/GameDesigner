// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Versioning/SaveX_VersionInspector.h"
#include "Storage/SaveX_StorageSubsystem.h"
#include "Storage/SaveX_ContainerHeader.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"          // FDP_SubsystemStatics
#include "Save/DPSaveGameSubsystem.h"
#include "Save/DPSaveHeader.h"
#include "Save/DPSaveVersion.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

UDP_SaveGameSubsystem* USaveX_VersionInspector::GetCoreSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(
		const_cast<USaveX_VersionInspector*>(this));
}

USaveX_StorageSubsystem* USaveX_VersionInspector::GetStorage() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<USaveX_StorageSubsystem>(
		const_cast<USaveX_VersionInspector*>(this));
}

int32 USaveX_VersionInspector::GetOldestSupportedVersion() const
{
	// Heuristic floor: the first versioned format. A save older than this predates the chain entirely.
	return static_cast<int32>(FDP_SaveVersion::InitialVersion);
}

void USaveX_VersionInspector::EnumerateSlots(TArray<FString>& OutSlots) const
{
	OutSlots.Reset();

	// Plain core slots.
	if (UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem())
	{
		for (const FString& Name : Core->GetAllSlots())
		{
			OutSlots.AddUnique(Name);
		}
	}

	// Wrapped container slots: enumerate ".dpcsav" files in the SaveGames directory.
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	TArray<FString> WrappedFiles;
	IFileManager::Get().FindFiles(WrappedFiles, *FPaths::Combine(SaveDir, FString::Printf(TEXT("*.%s"), USaveX_StorageSubsystem::WrappedExtension())), /*Files=*/true, /*Directories=*/false);
	for (const FString& File : WrappedFiles)
	{
		OutSlots.AddUnique(FPaths::GetBaseFilename(File));
	}
}

ESaveX_VersionStatus USaveX_VersionInspector::ClassifySlot(const FString& Slot) const
{
	FSaveX_SlotVersionInfo Info;
	if (GetSlotVersionInfo(Slot, Info))
	{
		return Info.Status;
	}
	return ESaveX_VersionStatus::Missing;
}

bool USaveX_VersionInspector::GetSlotVersionInfo(const FString& Slot, FSaveX_SlotVersionInfo& Out) const
{
	Out = FSaveX_SlotVersionInfo();
	Out.SlotName = Slot;
	Out.DisplayName = FText::FromString(Slot);
	Out.LatestVersion = static_cast<int32>(FDP_SaveVersion::LatestVersion);

	USaveX_StorageSubsystem* Storage = GetStorage();
	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();

	const bool bWrapped = Storage && Storage->DoesWrappedSlotExist(Slot);
	Out.bWrapped = bWrapped;

	if (bWrapped)
	{
		// Cheap pass: container header validity (magic) and the read itself confirm structural integrity. A
		// full CRC/inner-version check would require decompressing the payload, so the inner SaveVersion is
		// not read here; a wrapped container that parses is treated as loadable. The CRC is verified for real
		// at load time (LoadWrapped), where corruption triggers backup recovery.
		FSaveX_ContainerHeader Header;
		if (!Storage->ReadContainerHeader(Slot, Header) || !Header.IsMagicValid())
		{
			Out.Status = ESaveX_VersionStatus::Corrupt;
			return true;
		}
		// The container does not record the inner FDP_SaveVersion; report Latest so a UI does not falsely
		// flag a wrapped save for migration. Migration still runs inside the core loader on actual load.
		Out.SaveVersion = static_cast<int32>(FDP_SaveVersion::LatestVersion);
		Out.Status = ESaveX_VersionStatus::Current;
		return true;
	}

	// Plain core save: read the FDP_SaveHeader for the authoritative version.
	if (!Core)
	{
		Out.Status = ESaveX_VersionStatus::Missing;
		return false;
	}
	if (!Core->DoesSlotExist(Slot))
	{
		Out.Status = ESaveX_VersionStatus::Missing;
		return false;
	}

	FDP_SaveHeader Header;
	if (!Core->ReadSlotHeader(Slot, Header) || !Header.IsMagicValid())
	{
		Out.Status = ESaveX_VersionStatus::Corrupt;
		return true;
	}

	Out.SaveVersion = Header.SaveVersion;
	Out.DisplayName = Header.DisplayName.IsEmpty() ? FText::FromString(Slot) : FText::FromString(Header.DisplayName);

	const int32 Latest = static_cast<int32>(FDP_SaveVersion::LatestVersion);
	const int32 Oldest = GetOldestSupportedVersion();

	if (Header.SaveVersion > Latest)
	{
		Out.Status = ESaveX_VersionStatus::IncompatibleNewer;
	}
	else if (Header.SaveVersion == Latest)
	{
		Out.Status = ESaveX_VersionStatus::Current;
	}
	else if (Header.SaveVersion >= Oldest)
	{
		Out.Status = ESaveX_VersionStatus::NeedsMigration;
		Out.bNeedsMigration = true;
	}
	else
	{
		Out.Status = ESaveX_VersionStatus::IncompatibleOlder;
	}

	return true;
}

void USaveX_VersionInspector::ClassifyAllSlots(TArray<FSaveX_SlotVersionInfo>& Out) const
{
	Out.Reset();
	TArray<FString> Slots;
	EnumerateSlots(Slots);
	Out.Reserve(Slots.Num());
	for (const FString& Slot : Slots)
	{
		FSaveX_SlotVersionInfo Info;
		if (GetSlotVersionInfo(Slot, Info))
		{
			Out.Add(MoveTemp(Info));
		}
	}
}

bool USaveX_VersionInspector::DryRunMigrationCheck(const FString& Slot, FString& OutReason) const
{
	FSaveX_SlotVersionInfo Info;
	if (!GetSlotVersionInfo(Slot, Info))
	{
		OutReason = TEXT("Slot not found.");
		return false;
	}

	switch (Info.Status)
	{
	case ESaveX_VersionStatus::Current:
		OutReason = TEXT("Save is current; no migration needed.");
		return true; // loadable as-is
	case ESaveX_VersionStatus::NeedsMigration:
		OutReason = FString::Printf(TEXT("Save v%d will be migrated to v%d on load (heuristic)."),
			Info.SaveVersion, Info.LatestVersion);
		return true;
	case ESaveX_VersionStatus::IncompatibleNewer:
		OutReason = FString::Printf(TEXT("Save v%d is newer than this build (v%d); cannot load."),
			Info.SaveVersion, Info.LatestVersion);
		return false;
	case ESaveX_VersionStatus::IncompatibleOlder:
		OutReason = FString::Printf(TEXT("Save v%d predates the oldest supported version (v%d)."),
			Info.SaveVersion, GetOldestSupportedVersion());
		return false;
	case ESaveX_VersionStatus::Corrupt:
		OutReason = TEXT("Save header is unreadable (corrupt).");
		return false;
	default:
		OutReason = TEXT("Slot missing.");
		return false;
	}
}
