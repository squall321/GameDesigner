// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Seam_Persistable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_Persistable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Universal save-participant seam — the core has no generic actor/subsystem persistence path, so this
 * fills the gap. Each high-level module ships a UDP_SaveGame subclass that, on the game thread in
 * OnPreSave, gathers its participants by calling CaptureState, and on OnPostLoad scatters back via
 * RestoreState (which must be authority-guarded so a client-side load is a no-op).
 *
 * State is carried as an FInstancedStruct so each participant defines its own save record type without
 * the save object knowing the concrete type.
 */
class DESIGNPATTERNSSEAMS_API ISeam_Persistable
{
	GENERATED_BODY()

public:
	/** Write this object's durable state into Out (called on the game thread during save). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	void CaptureState(FInstancedStruct& Out) const;

	/** Apply previously-captured state. Implementations MUST guard on authority before mutating. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	void RestoreState(const FInstancedStruct& In);

	/** A stable tag identifying this participant's record kind, so a save can route records back. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	FGameplayTag GetPersistenceKind() const;
};
