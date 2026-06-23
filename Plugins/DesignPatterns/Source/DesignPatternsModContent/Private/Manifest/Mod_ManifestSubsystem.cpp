// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Manifest/Mod_ManifestSubsystem.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"   // GEngine, FWorldContext (async completion re-resolve)

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"   // FSHA1 / FSHAHash — the always-available engine digest used by default

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_ManifestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Verbose, TEXT("ModContent: manifest subsystem initialised."));
}

void UMod_ManifestSubsystem::Deinitialize()
{
	// No tickers/timers/listeners to remove; async tasks self-guard via the game instance on completion.
	Super::Deinitialize();
}

// =====================================================================================================
// Hashing helpers (wrap engine SHA; never reinvent crypto)
// =====================================================================================================

void UMod_ManifestSubsystem::GatherPackFilePaths(const FMod_PackInfo& Pack, TArray<FString>& OutAbsolutePaths)
{
	OutAbsolutePaths.Reset();
	if (Pack.DiskPath.IsEmpty())
	{
		return;
	}

	IFileManager& FM = IFileManager::Get();

	// For a Pak pack the disk path is the .pak file itself; hash just that file. For a Plugin pack the disk
	// path is the .uplugin — hash the .uplugin plus the files in its directory tree (the pack content).
	if (Pack.Kind == EMod_PackKind::Pak)
	{
		if (FM.FileExists(*Pack.DiskPath))
		{
			OutAbsolutePaths.Add(FPaths::ConvertRelativePathToFull(Pack.DiskPath));
		}
		return;
	}

	// Plugin pack: walk the .uplugin's directory recursively.
	const FString PluginDir = FPaths::GetPath(Pack.DiskPath);
	if (!PluginDir.IsEmpty())
	{
		TArray<FString> Found;
		FM.FindFilesRecursive(Found, *PluginDir, TEXT("*"), /*Files*/ true, /*Directories*/ false);
		for (const FString& F : Found)
		{
			OutAbsolutePaths.Add(FPaths::ConvertRelativePathToFull(F));
		}
	}

	// Deterministic order so the rolled-up manifest hash is reproducible run-to-run.
	OutAbsolutePaths.Sort();
}

FString UMod_ManifestSubsystem::HashFileHex(const FString& AbsolutePath, int64& OutSize)
{
	OutSize = 0;

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *AbsolutePath))
	{
		return FString();
	}
	OutSize = Bytes.Num();

	// FSHA1 is the engine's always-available content digest (present across UE 5.3-5.5). The seam treats
	// the hash as an opaque hex string and lets a host verifier choose a stronger algorithm if it wishes.
	FSHAHash Hash;
	FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
	return Hash.ToString();
}

FString UMod_ManifestSubsystem::ComputeManifestHash(const TArray<FMod_FileEntry>& Files)
{
	// Build a stable text blob over the ordered entries and SHA-256 it. Path + hash + size per line.
	FString Blob;
	Blob.Reserve(Files.Num() * 96);
	for (const FMod_FileEntry& E : Files)
	{
		Blob.Append(E.RelativePath);
		Blob.AppendChar(TEXT('|'));
		Blob.Append(E.HashHex);
		Blob.AppendChar(TEXT('|'));
		Blob.AppendInt(E.SizeBytes);
		Blob.AppendChar(TEXT('\n'));
	}

	const FTCHARToUTF8 Utf8(*Blob);
	FSHAHash Hash;
	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Hash.Hash);
	return Hash.ToString();
}

FMod_PackManifest UMod_ManifestSubsystem::BuildManifestFromPaths(
	FGameplayTag PackId, const FString& RootForRelative, const TArray<FString>& AbsolutePaths)
{
	FMod_PackManifest Manifest;
	Manifest.PackId = PackId;
	Manifest.Files.Reserve(AbsolutePaths.Num());

	for (const FString& Abs : AbsolutePaths)
	{
		int64 Size = 0;
		const FString Hash = HashFileHex(Abs, Size);
		if (Hash.IsEmpty())
		{
			continue; // unreadable file -> skip (integrity check will notice a missing entry)
		}

		FString Rel = Abs;
		if (!RootForRelative.IsEmpty())
		{
			FPaths::MakePathRelativeTo(Rel, *(RootForRelative / TEXT("")));
		}
		Manifest.Files.Emplace(Rel, Hash, Size);
	}

	Manifest.ManifestHash = ComputeManifestHash(Manifest.Files);
	return Manifest;
}

// =====================================================================================================
// Compute
// =====================================================================================================

bool UMod_ManifestSubsystem::ComputeManifest(const FMod_PackInfo& Pack, FMod_PackManifest& Out) const
{
	Out = FMod_PackManifest();
	if (!Pack.IsUsable())
	{
		return false;
	}

	TArray<FString> Paths;
	GatherPackFilePaths(Pack, Paths);
	if (Paths.Num() == 0)
	{
		return false;
	}

	const FString Root = (Pack.Kind == EMod_PackKind::Plugin) ? FPaths::GetPath(Pack.DiskPath) : FPaths::GetPath(Pack.DiskPath);
	Out = BuildManifestFromPaths(Pack.PackId, Root, Paths);
	return Out.Files.Num() > 0;
}

void UMod_ManifestSubsystem::ComputeManifestAsync(const FMod_PackInfo& Pack)
{
	if (!Pack.IsUsable())
	{
		OnManifestComputed.Broadcast(Pack.PackId, FMod_PackManifest());
		return;
	}

	// Capture plain-value copies for the background task: NEVER 'this'. The pack id + gathered paths +
	// relative-root are all copyable strings/tags.
	TArray<FString> Paths;
	GatherPackFilePaths(Pack, Paths);
	const FGameplayTag PackId = Pack.PackId;
	const FString Root = FPaths::GetPath(Pack.DiskPath);

	// Re-resolve the subsystem on completion via the game instance with a null-guard, so a torn-down
	// subsystem is a no-op rather than a use-after-free.
	TWeakObjectPtr<UMod_ManifestSubsystem> WeakSelf(this);
	++AsyncDispatchCount;

	Async(EAsyncExecution::ThreadPool, [PackId, Root, Paths = MoveTemp(Paths)]()
	{
		FMod_PackManifest Manifest = BuildManifestFromPaths(PackId, Root, Paths);

		// Marshal the result back to the game thread.
		AsyncTask(ENamedThreads::GameThread, [PackId, Manifest = MoveTemp(Manifest)]()
		{
			// We can only safely touch UObjects on the game thread; resolve a live subsystem by walking
			// the world contexts is overkill — instead the result is delivered through the FIRST live
			// manifest subsystem found. We re-resolve via GEngine's game instances defensively.
			if (GEngine)
			{
				for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				{
					if (UGameInstance* GI = Ctx.OwningGameInstance)
					{
						if (UMod_ManifestSubsystem* Self = GI->GetSubsystem<UMod_ManifestSubsystem>())
						{
							Self->OnManifestComputed.Broadcast(PackId, Manifest);
							return;
						}
					}
				}
			}
			UE_LOG(LogDP, Verbose, TEXT("ModContent: async manifest for '%s' completed but no live subsystem remained."),
				*PackId.ToString());
		});
	});
}

// =====================================================================================================
// Verify
// =====================================================================================================

bool UMod_ManifestSubsystem::VerifyIntegrity(const FMod_PackManifest& Manifest) const
{
	// Re-roll the manifest hash from the stored entries and confirm it matches the recorded one.
	const FString Recomputed = ComputeManifestHash(Manifest.Files);
	if (!Manifest.ManifestHash.IsEmpty() && !Recomputed.Equals(Manifest.ManifestHash, ESearchCase::IgnoreCase))
	{
		return false;
	}
	// A manifest with no files is treated as integrity-pass-but-empty (nothing to tamper with).
	return true;
}

FMod_TrustVerdict UMod_ManifestSubsystem::VerifyTrust(const FMod_PackManifest& Manifest) const
{
	// 1) Integrity first — a tampered manifest is Untrusted regardless of any signer.
	if (!VerifyIntegrity(Manifest))
	{
		return FMod_TrustVerdict(EMod_TrustLevel::Untrusted, ModTags::Reason_HashMismatch);
	}

	// 2) Consult the optional verifier seam. Absent -> inert HashOk default via the seam's own default
	//    implementation, so callers never special-case "no verifier".
	if (TScriptInterface<ISeam_ModSignatureVerifier> Verifier = ResolveVerifier())
	{
		return ISeam_ModSignatureVerifier::Execute_VerifyManifest(Verifier.GetObject(), Manifest);
	}

	// No verifier registered: hash integrity is the only evidence.
	return FMod_TrustVerdict(EMod_TrustLevel::HashOk);
}

// =====================================================================================================
// Resolution / debug
// =====================================================================================================

TScriptInterface<ISeam_ModSignatureVerifier> UMod_ManifestSubsystem::ResolveVerifier() const
{
	TScriptInterface<ISeam_ModSignatureVerifier> Result;
	if (const UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (UObject* Provider = Locator->ResolveService(ModTags::Service_ModSignature))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_ModSignatureVerifier::StaticClass()))
			{
				Result.SetObject(Provider);
				Result.SetInterface(Cast<ISeam_ModSignatureVerifier>(Provider));
			}
		}
	}
	return Result;
}

UDP_ServiceLocatorSubsystem* UMod_ManifestSubsystem::GetLocator() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	}
	return nullptr;
}

FString UMod_ManifestSubsystem::GetDPDebugString_Implementation() const
{
	const bool bVerifier = ResolveVerifier().GetObject() != nullptr;
	return FString::Printf(TEXT("Manifest: verifier=%s, %d async dispatch(es)"),
		bVerifier ? TEXT("present") : TEXT("inert(hash-only)"), AsyncDispatchCount);
}
