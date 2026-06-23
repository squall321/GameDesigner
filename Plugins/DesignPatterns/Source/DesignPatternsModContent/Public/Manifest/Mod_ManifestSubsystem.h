// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Seam/Mod_ContentSource.h"            // FMod_PackInfo
#include "Mod/Seam_ModSignature.h"            // FMod_PackManifest, FMod_TrustVerdict, ISeam_ModSignatureVerifier
#include "Mod_ManifestSubsystem.generated.h"

class UDP_ServiceLocatorSubsystem;

/** Delegate fired (game thread) when an async manifest computation completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMod_OnManifestComputed, FGameplayTag, PackId, const FMod_PackManifest&, Manifest);

/**
 * GameInstance subsystem that builds and verifies a pack's integrity/trust MANIFEST before mount.
 *
 * It hashes the files a pack contributes (wrapping the engine's FSHA256/FArchive — never reinventing
 * crypto), rolls those up into an FMod_PackManifest, and consults the optional ISeam_ModSignatureVerifier
 * (DP.Service.Mod.Signature, held weakly + pruned on use). The trust verdict is exposed as a QUERY:
 * VerifyTrust returns the verdict so the validator-seam path (which the manager already calls before
 * DoEngineMount) can BLOCK an Untrusted pack — this subsystem never reaches into the manager's private
 * internals.
 *
 * Large packs are hashed on a background ThreadPool task that captures a game-thread COPY of the file
 * list by value (never 'this'); the resulting manifest is marshalled back to the game thread before the
 * completion delegate fires. With no verifier registered the path is hash-integrity only (the inert
 * default verdict is HashOk).
 *
 * Non-replicated, non-saved (manifests are environment state, recomputed from disk).
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_ManifestSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Synchronously compute the manifest for Pack by hashing the files under its content roots / disk
	 * path. Game-thread; suitable for small packs and the validate-before-mount window. Returns true and
	 * fills Out on success; false (with an empty Out) when the pack path is unusable.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Manifest")
	bool ComputeManifest(const FMod_PackInfo& Pack, FMod_PackManifest& Out) const;

	/**
	 * Asynchronously compute the manifest on a background task, then fire OnManifestComputed on the game
	 * thread. The task captures a plain-value copy of the file paths (never this/the subsystem), so it is
	 * safe even if the subsystem is torn down mid-hash (the completion re-resolves the subsystem via the
	 * game instance and no-ops if gone). Use for large packs to avoid a game-thread hitch.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Manifest")
	void ComputeManifestAsync(const FMod_PackInfo& Pack);

	/**
	 * Re-hash the files in Manifest and confirm each entry's stored hash still matches the bytes on disk
	 * (and the rolled-up ManifestHash is consistent). True when integrity holds. Game-thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Manifest")
	bool VerifyIntegrity(const FMod_PackManifest& Manifest) const;

	/**
	 * Produce the trust verdict for Manifest: first integrity (hash) then the optional signature-verifier
	 * seam. When integrity fails the verdict is Untrusted (HashMismatch reason). When no verifier is
	 * registered the inert default returns HashOk. The manager's validator path uses this to gate mount.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Manifest")
	FMod_TrustVerdict VerifyTrust(const FMod_PackManifest& Manifest) const;

	/** Fired on the game thread when an async computation finishes. */
	UPROPERTY(BlueprintAssignable, Category = "ModContent|Manifest")
	FMod_OnManifestComputed OnManifestComputed;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the optional signature verifier seam (weak, locator-backed). Null when none registered. */
	TScriptInterface<ISeam_ModSignatureVerifier> ResolveVerifier() const;

	/** Resolve the service locator (may be null in early/teardown contexts). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/**
	 * Gather the absolute file paths a pack contributes, for hashing. Wraps IFileManager directory
	 * iteration under the pack's content roots / disk path; sorted for a reproducible manifest hash.
	 */
	static void GatherPackFilePaths(const FMod_PackInfo& Pack, TArray<FString>& OutAbsolutePaths);

	/** Hash one file's bytes to a lowercase hex SHA-256 string. Empty on read failure. */
	static FString HashFileHex(const FString& AbsolutePath, int64& OutSize);

	/** Roll the ordered file entries up into the manifest hash (hex SHA-256 over path+hash+size lines). */
	static FString ComputeManifestHash(const TArray<FMod_FileEntry>& Files);

	/** Build a manifest struct from a pre-gathered path list (the work both sync + async paths share). */
	static FMod_PackManifest BuildManifestFromPaths(FGameplayTag PackId, const FString& RootForRelative, const TArray<FString>& AbsolutePaths);

	/** Count of async computations dispatched this session (for debug). */
	int32 AsyncDispatchCount = 0;
};
