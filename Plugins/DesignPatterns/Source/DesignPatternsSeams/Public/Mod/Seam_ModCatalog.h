// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"   // TSoftObjectPtr<UTexture2D> icon ref (precedent: Seam_InputGlyphProvider.h)
#include "Seam_ModCatalog.generated.h"

/**
 * The availability state of one catalog item from the source's point of view. Drives store/UGC UI and
 * the manager's decision about whether a downloaded pack is ready to enter local discovery.
 */
UENUM(BlueprintType)
enum class EMod_CatalogItemState : uint8
{
	/** Listed in the catalog but not present locally; a RequestDownload would fetch it. */
	NotInstalled,

	/** A download is in progress; completion is signalled on the message bus, not by a blocking call. */
	Downloading,

	/** Present in the local (sandboxed) discovery directory; the manager's disk discovery can see it. */
	Installed,

	/** The last operation for this item failed (download error, write refused). Inspect logs/bus for why. */
	Failed
};

/**
 * One entry in a content catalog (a workshop/store listing). PURE DATA — PII-free, load-free, never
 * replicated. Carries the stable catalog id, the eventual pack id it installs as, a soft icon ref, and
 * a couple of cosmetic display fields. No UObject ref, no FInstancedStruct.
 *
 * The catalog id is the handle passed back to RequestDownload / GetCatalogState; it is a CHILD of a
 * source-defined DP.Mod.Catalog namespace (distinct from the pack id, which only exists once installed).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FMod_CatalogEntry
{
	GENERATED_BODY()

	/** Stable catalog/listing identity (child of DP.Mod.Catalog). The handle for download/state queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag CatalogItemId;

	/**
	 * The pack identity (child of DP.Mod.Pack) this listing installs as, when known. Lets UI cross-link a
	 * listing to an already-installed/mounted pack. May be invalid until the source resolves it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTag InstallsAsPackId;

	/** Current availability of this item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	EMod_CatalogItemState State = EMod_CatalogItemState::NotInstalled;

	/** Human-facing title for the listing. Cosmetic; localizable; never used for identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FText DisplayName;

	/** Human-facing author/attribution string. Cosmetic; never used for any security decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FString Author;

	/** Soft icon ref for the listing (not loaded by the seam). Resolved by UI on demand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	TSoftObjectPtr<UTexture2D> Icon;

	/** True when the catalog item id is valid (the minimum for the manager to act on it). */
	bool IsUsable() const { return CatalogItemId.IsValid(); }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ModCatalogSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam: a workshop / store / UGC bridge that enumerates downloadable content listings and fetches them
 * INTO a sandboxed local discovery directory — where the ModContent manager's normal disk discovery
 * then picks them up. It is the "where do downloadable packs come from" contract, kept in the leaf
 * Seams module so a platform/store adapter (Steam Workshop, a first-party CDN, a local folder) can
 * implement it WITHOUT hard-including DesignPatternsModContent.
 *
 * SAFETY is structural and matches the rest of ModContent:
 *   - The seam NEVER mounts or executes anything. It only fetches bytes onto disk; activation remains
 *     the manager's validate-before-activate decision over already-sandboxed content.
 *   - RequestDownload writes ONLY inside a configured discovery directory; the destination is the
 *     IMPLEMENTER's responsibility to keep sandbox-safe, and the manager additionally re-checks
 *     sandbox-root policy at mount.
 *   - Completion is asynchronous and reported on the message bus (DP.Bus.Mod.CatalogChanged), never via
 *     a blocking return — so a slow/offline backend never stalls the game thread.
 *
 * The default implementation is UMod_LocalFolderCatalogSource (in DesignPatternsModContent): a network-
 * free local-folder "store" where download is a sandbox-constrained file copy. Consumers hold this seam
 * as a TScriptInterface; GI subsystems hold it WEAKLY (TWeakInterfacePtr, pruned on use), exactly like
 * IMod_ContentSource.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ModCatalogSource
{
	GENERATED_BODY()

public:
	/**
	 * Append every catalog item this source currently knows about to Out. Game-thread, side-effect-free
	 * (no download), and tolerant of repeated calls (a re-enumerate). Returns the number appended.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	int32 EnumerateCatalog(UPARAM(ref) TArray<FMod_CatalogEntry>& Out) const;

	/**
	 * Begin fetching the given catalog item into a configured (sandboxed) discovery directory. Returns
	 * true if the request was accepted (queued); the actual completion is broadcast on the message bus.
	 * Never executes or mounts the content — it only places bytes for the manager to later discover.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	bool RequestDownload(FGameplayTag CatalogItemId);

	/** Current availability of a catalog item (NotInstalled / Downloading / Installed / Failed). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	EMod_CatalogItemState GetCatalogState(FGameplayTag CatalogItemId) const;

	/** Stable identity of this catalog source (child of DP.Mod.Source) for provenance / de-duplication. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag GetSourceId() const;
};
