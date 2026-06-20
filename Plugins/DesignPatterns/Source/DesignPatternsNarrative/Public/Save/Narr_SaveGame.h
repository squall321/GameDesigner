// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Narr_SaveGame.generated.h"

/**
 * One persisted narrative participant's record: its persistence-kind tag plus its opaque captured
 * FInstancedStruct payload. The save object stores a flat array of these and routes each back to the
 * participant whose GetPersistenceKind matches on load.
 */
USTRUCT()
struct DESIGNPATTERNSNARRATIVE_API FNarr_PersistedParticipant
{
	GENERATED_BODY()

	/** The participant's record kind (ISeam_Persistable::GetPersistenceKind), used to route on restore. */
	UPROPERTY(SaveGame)
	FGameplayTag Kind;

	/** The participant's captured state. A UPROPERTY so its inner references are GC-/serialize-visible. */
	UPROPERTY(SaveGame)
	FInstancedStruct Payload;

	FNarr_PersistedParticipant() = default;
	FNarr_PersistedParticipant(const FGameplayTag& InKind, const FInstancedStruct& InPayload)
		: Kind(InKind), Payload(InPayload) {}
};

/**
 * Narrative save object.
 *
 * On OnPreSave (game thread) it gathers every narrative ISeam_Persistable participant — the story
 * director and any narrative participant registered for save — by calling CaptureState, and stores the
 * resulting (kind, payload) records. On OnPostLoad it scatters them back via RestoreState, which the
 * participants themselves authority-guard (a client-side load is a no-op on each participant).
 *
 * The story director's CANONICAL flags live in the World hub's own save object; THIS object only
 * carries the narrative-side tracking records (the director's beat/arc cursor) so the graph traversal
 * resumes correctly. It deliberately does NOT duplicate the replicated/saved hub story flags.
 *
 * Gathering is on the GAME THREAD (the DP save subsystem calls OnPreSave/OnPostLoad there), so it is
 * safe to touch live UObjects. Participants are discovered through the service locator and the world's
 * actor/subsystem set rather than held as hard references, so the save object never roots live state.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSNARRATIVE_API UNarr_SaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** The captured participant records (one per narrative ISeam_Persistable). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Save")
	TArray<FNarr_PersistedParticipant> Participants;

	/**
	 * The world-context used to resolve participants during gather/scatter. Set by the caller BEFORE
	 * SaveNow/LoadNow so OnPreSave/OnPostLoad can reach the subsystems. Weak: the save object must not
	 * keep a world alive. Not serialized (transient wiring only).
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> SaveContext;

	/**
	 * Explicitly register a narrative participant to be captured on the next save (in addition to the
	 * always-included story director). Held weakly; stale entries are skipped at gather time. Use for
	 * narrative participants that are not otherwise discoverable (e.g. a manager UObject).
	 */
	void RegisterParticipant(const TScriptInterface<class ISeam_Persistable>& Participant);

	//~ Begin UDP_SaveGame
	/** Gather all narrative participants' state into Participants (game thread). */
	virtual void OnPreSave_Implementation() override;

	/** Scatter Participants back into the live narrative participants (game thread). */
	virtual void OnPostLoad_Implementation() override;
	//~ End UDP_SaveGame

private:
	/**
	 * Extra participants explicitly registered for capture. Non-owning (weak interface) so the save
	 * object never roots live state; resolved and de-duplicated against the story director at gather.
	 */
	TArray<TWeakInterfacePtr<class ISeam_Persistable>> ExtraParticipants;

	/** Capture one participant (if valid) and append its record. */
	void CaptureParticipant(const TScriptInterface<class ISeam_Persistable>& Participant);

	/** Find the live participant matching Kind and restore Payload into it (authority-guarded by it). */
	void RestoreToKind(const FGameplayTag& Kind, const FInstancedStruct& Payload);

	/** Resolve the world-context this save operates against (SaveContext, falling back to outer). */
	UObject* ResolveContext() const;

	/** Gather the always-included narrative participants (currently the story director). */
	void GatherBuiltinParticipants(TArray<TScriptInterface<class ISeam_Persistable>>& Out) const;
};
