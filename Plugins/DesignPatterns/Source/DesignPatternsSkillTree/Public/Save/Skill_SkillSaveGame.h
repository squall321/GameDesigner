// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "Component/Skill_LearnedRecord.h"
#include "Skill_SkillSaveGame.generated.h"

class USkill_SkillComponent;

/**
 * Save payload for a character's skill state.
 *
 * Wraps the core UDP_SaveGame (HARD RULE 6: engine-wrap, no new serialization) — this just adds two
 * SaveGame UPROPERTYs and the capture/restore plumbing between them and a live USkill_SkillComponent. The
 * DP save subsystem (UDP_SaveGameSubsystem) versions and persists the blob; this class adds no custom
 * (de)serialization of its own.
 *
 *   - SavedSkills : the learned skills as plain records (FSkill_LearnedRecord — never the replicated
 *                   fast-array item, which carries replication bookkeeping that must not be persisted).
 *   - SavedPoints : the owner's total earned points at capture time.
 *
 * Capture is read-only and may run anywhere; Restore writes authoritative state and is therefore
 * authority-guarded (it routes through USkill_SkillComponent::ImportFromSave, which itself guards
 * HasAuthority()).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSKILLTREE_API USkill_SkillSaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** The learned skills captured from a USkill_SkillComponent. Serialized via the standard SaveGame path. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "SkillTree|Save")
	TArray<FSkill_LearnedRecord> SavedSkills;

	/** The owner's total earned points at capture time. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "SkillTree|Save")
	int32 SavedPoints = 0;

	/**
	 * Read a live skill component's state into this save object. Pure gather (no authority needed): copies
	 * GetLearnedSkills() into SavedSkills and GetTotalEarnedPoints() into SavedPoints. Null-safe.
	 * @return true if Component was valid and state was captured.
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree|Save")
	bool CaptureFrom(USkill_SkillComponent* Component);

	/**
	 * Push this saved state back into a live skill component. AUTHORITY-GUARDED scatter: forwards to
	 * USkill_SkillComponent::ImportFromSave, which guards HasAuthority() (clients receive the result via
	 * replication, so a client call is a documented no-op). Null-safe.
	 * @return true if Component was valid (the import was attempted on authority).
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree|Save")
	bool RestoreInto(USkill_SkillComponent* Component) const;
};
