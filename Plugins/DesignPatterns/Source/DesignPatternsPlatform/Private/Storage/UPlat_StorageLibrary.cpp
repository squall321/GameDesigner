// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/UPlat_StorageLibrary.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/PlatformProcess.h"

FString UPlat_StorageLibrary::GetPlatformSaveDirectory()
{
	FString BaseDir;

	// Platform branching for the base save location is confined here.
#if PLATFORM_DESKTOP
	// Per-user settings dir (roams on Windows, ~/Library on Mac, XDG on Linux).
	BaseDir = FString(FPlatformProcess::UserSettingsDir());
	if (BaseDir.IsEmpty())
	{
		// Defensive fallback if the platform returns nothing.
		BaseDir = FPaths::ProjectSavedDir();
	}
	else
	{
		BaseDir = FPaths::Combine(BaseDir, FApp::GetProjectName(), TEXT("SaveGames"));
	}
#else
	// Mobile / console / other: use the sandboxed, platform-managed Saved directory.
	BaseDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
#endif

	FPaths::NormalizeDirectoryName(BaseDir);
	// Guarantee a trailing separator for safe concatenation by callers.
	if (!BaseDir.EndsWith(TEXT("/")))
	{
		BaseDir += TEXT("/");
	}
	return BaseDir;
}

bool UPlat_StorageLibrary::IsCloudSaveAware()
{
	// Desktop user-settings dirs roam/sync on supported OSes; mobile/console route saves
	// through platform cloud services. The generic sandbox case is treated as not cloud-aware.
#if PLATFORM_DESKTOP
	return true;
#elif PLATFORM_ANDROID || PLATFORM_IOS || PLATFORM_CONSOLE
	return true;
#else
	return false;
#endif
}

FString UPlat_StorageLibrary::SanitizeSlotName(const FString& RawSlotName)
{
	FString Result;
	Result.Reserve(RawSlotName.Len());

	for (const TCHAR Ch : RawSlotName)
	{
		// Allow only a safe, portable subset; map everything else (separators, illegal
		// filename chars, whitespace) to an underscore.
		const bool bIsAllowed =
			(Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
			(Ch >= TEXT('a') && Ch <= TEXT('z')) ||
			(Ch >= TEXT('0') && Ch <= TEXT('9')) ||
			Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT('.');

		Result.AppendChar(bIsAllowed ? Ch : TEXT('_'));
	}

	// Collapse leading/trailing dots and underscores that could create hidden/odd files.
	// Use RemoveFromStart/End in a loop (portable across UE 5.3-5.5; avoids the
	// EAllowShrinking overload that only exists on 5.5).
	Result.TrimStartAndEndInline();
	while (Result.RemoveFromStart(TEXT(".")) || Result.RemoveFromStart(TEXT("_")))
	{
	}
	while (Result.RemoveFromEnd(TEXT(".")) || Result.RemoveFromEnd(TEXT("_")))
	{
	}

	// Clamp length so the final absolute path stays well under platform path limits.
	if (Result.Len() > MaxSlotNameLength)
	{
		Result = Result.Left(MaxSlotNameLength);
	}

	if (Result.IsEmpty())
	{
		Result = TEXT("save");
	}
	return Result;
}

FPlat_SavePath UPlat_StorageLibrary::ResolveSavePath(const FString& RawSlotName, const FString& Extension)
{
	FPlat_SavePath Out;
	Out.Directory = GetPlatformSaveDirectory();
	Out.SanitizedSlotName = SanitizeSlotName(RawSlotName);
	Out.bCloudSaveAware = IsCloudSaveAware();

	FString Ext = Extension;
	Ext.TrimStartAndEndInline();
	while (Ext.RemoveFromStart(TEXT(".")))
	{
	}

	const FString FileName = Ext.IsEmpty()
		? Out.SanitizedSlotName
		: FString::Printf(TEXT("%s.%s"), *Out.SanitizedSlotName, *Ext);

	Out.FullPath = Out.Directory + FileName;
	return Out;
}
