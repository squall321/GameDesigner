// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ModSignature.generated.h"

/**
 * Trust level a verifier assigns to a content pack's manifest. Strictly ordered from least to most
 * trusted EXCEPT for Untrusted, which is a terminal "actively rejected" state used to BLOCK a mount
 * (it is given the highest numeric value so a worst-severity max() across verdicts surfaces it).
 *
 * The values are PII-free, plain data — they never carry a path, an FText, or a raw object.
 */
UENUM(BlueprintType)
enum class EMod_TrustLevel : uint8
{
	/** No integrity/trust evidence available (e.g. no manifest was supplied). Treated as not-yet-verified. */
	Unverified = 0,

	/**
	 * The manifest's file hashes match the on-disk bytes (content integrity confirmed), but no signer
	 * identity was validated. This is the inert default verifier's best outcome — hash-only integrity.
	 */
	HashOk = 1,

	/** Hash integrity AND a recognised signer signature verified. The strongest "allow" outcome. */
	Signed = 2,

	/**
	 * The pack is actively distrusted (tampered hash, bad signature, or a denied signer). The mount MUST
	 * be blocked. Highest value so worst-severity aggregation across multiple verdicts wins.
	 */
	Untrusted = 3
};

/**
 * One file recorded in a pack manifest: its pack-relative path, its content hash, and its byte size.
 * Pure POD — load-free, never replicated, hand-off-thread-copyable (no FText / UObject / soft refs).
 *
 * Hash is hex text (e.g. lowercase SHA-256) so the manifest serializes trivially and compares without
 * a binary blob. The concrete hash algorithm is the implementer's choice; the seam only carries the
 * resulting digest string and never performs crypto itself.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FMod_FileEntry
{
	GENERATED_BODY()

	/** Pack-relative path of the file (e.g. "Content/Maps/Foo.umap"). Never absolute (no sandbox leak). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FString RelativePath;

	/** Hex content digest of the file's bytes (e.g. SHA-256). Empty means "not hashed". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FString HashHex;

	/** File size in bytes (defensive cross-check against the hash; 0 means unknown). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	int64 SizeBytes = 0;

	FMod_FileEntry() = default;
	FMod_FileEntry(const FString& InPath, const FString& InHash, int64 InSize)
		: RelativePath(InPath), HashHex(InHash), SizeBytes(InSize) {}

	bool operator==(const FMod_FileEntry& Other) const
	{
		return RelativePath == Other.RelativePath && HashHex == Other.HashHex && SizeBytes == Other.SizeBytes;
	}
};

/**
 * The integrity/trust manifest for ONE content pack, computed load-free by hashing the pack's files.
 *
 * It is pure data: an ordered file list (path + hash + size), a rolled-up manifest hash over those
 * entries, the pack identity, and an OPTIONAL signer identity + opaque signature blob. The manifest
 * never holds a UObject ref, never replicates, and never executes anything — it only DESCRIBES bytes
 * so a verifier seam can rule on them before the manager mounts the pack.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FMod_PackManifest
{
	GENERATED_BODY()

	/** The pack this manifest describes (child of DP.Mod.Pack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTag PackId;

	/** Every hashed file in the pack, in a stable (sorted) order so the rolled-up hash is reproducible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	TArray<FMod_FileEntry> Files;

	/** Hex digest computed over the ordered Files entries (the pack's overall content fingerprint). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FString ManifestHash;

	/**
	 * OPTIONAL signer identity (child of a project-defined DP.Mod.Signer.* namespace). Invalid when the
	 * pack is unsigned. A real verifier maps this to a trusted public key; the inert default ignores it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag SignerId;

	/**
	 * OPTIONAL opaque signature bytes over ManifestHash. Empty when the pack is unsigned. The seam never
	 * interprets these — only the host's verifier (which owns the keystore/crypto) does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	TArray<uint8> Signature;

	/** True when a signer id and a non-empty signature blob are both present. */
	bool IsSigned() const { return SignerId.IsValid() && Signature.Num() > 0; }
};

/**
 * The verdict a verifier returns for a manifest: a trust level plus a machine-readable reason tag for
 * tooling/UI. Plain data; never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FMod_TrustVerdict
{
	GENERATED_BODY()

	/** The resolved trust level. Untrusted blocks the mount; HashOk/Signed allow it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	EMod_TrustLevel Level = EMod_TrustLevel::Unverified;

	/**
	 * Machine-readable reason (e.g. DP.Mod.Reason.HashMismatch, DP.Mod.Reason.UntrustedSigner). Drives
	 * tooling messages; never used for identity or persistence. Invalid for a clean verdict.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag Reason;

	FMod_TrustVerdict() = default;
	FMod_TrustVerdict(EMod_TrustLevel InLevel, FGameplayTag InReason = FGameplayTag())
		: Level(InLevel), Reason(InReason) {}

	/** True when this verdict permits the pack to mount (anything that is not actively Untrusted). */
	bool AllowsMount() const { return Level != EMod_TrustLevel::Untrusted; }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ModSignatureVerifier : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam: an OPTIONAL trust-policy verifier the ModContent manifest path consults BEFORE a pack mounts.
 *
 * Keeping crypto and keystore access OUT of the genre-neutral ModContent module is the whole point: a
 * host that ships signed first-party DLC implements this seam (backed by its own public-key store) and
 * registers it under DP.Service.Mod.Signature; ModContent resolves it weakly (TWeakInterfacePtr,
 * pruned on use) exactly like its other provider seams.
 *
 * INERT DEFAULT: when no verifier is registered the consumer falls back to hash-integrity only (the
 * default *_Implementation here returns HashOk for any manifest), mirroring the IMod_PackValidator
 * "absent => Pass" discipline. So removing the verifier weakens signing but never crashes or
 * unconditionally rejects — the manager's own hard guards (allowlist, sandbox, version, validation)
 * still apply.
 *
 * The verifier MUST be pure / side-effect-free and game-thread: it reasons over the supplied
 * FMod_PackManifest only and never mounts, loads, or executes pack content.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ModSignatureVerifier
{
	GENERATED_BODY()

public:
	/**
	 * Rule on a pack manifest. The default returns HashOk (treat hashes as the only evidence). A real
	 * verifier validates the signer signature and returns Signed, or Untrusted on tamper/denied-signer.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	FMod_TrustVerdict VerifyManifest(const FMod_PackManifest& Manifest) const;

	/** Stable identity of this trust policy (child of a project DP.Mod.TrustPolicy namespace) for provenance. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag GetTrustPolicyId() const;
};
