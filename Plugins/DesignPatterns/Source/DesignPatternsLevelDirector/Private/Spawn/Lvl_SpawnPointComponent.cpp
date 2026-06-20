// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/Lvl_SpawnPointComponent.h"
#include "Core/DPLog.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#endif

ULvl_SpawnPointComponent::ULvl_SpawnPointComponent()
{
	// Spawn points are passive markers: no tick, no replication.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

bool ULvl_SpawnPointComponent::MatchesFilter(FGameplayTag Filter) const
{
	if (!bEnabled)
	{
		return false;
	}
	// Invalid filter means "match every point".
	if (!Filter.IsValid())
	{
		return true;
	}
	// Match when this point carries the filter tag or a child of it (hierarchy-aware).
	return FilterTags.HasTag(Filter);
}

void ULvl_SpawnPointComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	// Give designers a visible, clickable marker in the editor viewport without affecting cooked
	// builds. Created lazily on register; failure to find the engine sprite is non-fatal.
	const UWorld* W = GetWorld();
	if (!EditorSprite && GetOwner() && W && !W->IsGameWorld())
	{
		EditorSprite = NewObject<UBillboardComponent>(this, TEXT("Lvl_SpawnPointSprite"));
		if (EditorSprite)
		{
			// LoadObject (not ConstructorHelpers) because OnRegister can run outside CDO construction.
			if (UTexture2D* Sprite = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint")))
			{
				EditorSprite->SetSprite(Sprite);
			}
			EditorSprite->SetupAttachment(this);
			EditorSprite->RegisterComponent();
			EditorSprite->bIsScreenSizeScaled = true;
		}
	}
#endif
}
