// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/Ent_EntitySpawnSubsystem.h"
#include "Entity/Ent_EntityComponent.h"
#include "Archetype/Ent_ArchetypeAsset.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Factory/DPSpawnFactorySubsystem.h"
#include "Factory/DPSpawnRecipe.h"
#include "Pool/DPObjectPoolSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

UEnt_EntityComponent* UEnt_EntitySpawnSubsystem::FindEntityComponent(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UEnt_EntityComponent>() : nullptr;
}

AActor* UEnt_EntitySpawnSubsystem::SpawnEntityFromArchetype(FGameplayTag ArchetypeTag, const FTransform& Transform, AActor* Owner)
{
	if (!HasWorldAuthority() || !ArchetypeTag.IsValid())
	{
		return nullptr;
	}
	const FGameplayTag FactoryTag = DefaultFactoryTag.IsValid() ? DefaultFactoryTag : ArchetypeTag;
	return SpawnInternal(FactoryTag, ArchetypeTag, /*AssetToApply=*/nullptr, Transform, Owner);
}

AActor* UEnt_EntitySpawnSubsystem::SpawnEntityFromArchetypeAsset(UEnt_ArchetypeAsset* Asset, const FTransform& Transform, AActor* Owner)
{
	if (!HasWorldAuthority() || !Asset)
	{
		return nullptr;
	}
	const FGameplayTag FactoryTag = DefaultFactoryTag.IsValid() ? DefaultFactoryTag : Asset->DataTag;
	return SpawnInternal(FactoryTag, Asset->DataTag, Asset, Transform, Owner);
}

AActor* UEnt_EntitySpawnSubsystem::SpawnInternal(FGameplayTag FactoryTag, FGameplayTag ArchetypeTagToApply, UEnt_ArchetypeAsset* AssetToApply, const FTransform& Transform, AActor* Owner)
{
	UDP_SpawnFactorySubsystem* Factory = FDP_SubsystemStatics::GetWorldSubsystem<UDP_SpawnFactorySubsystem>(this);
	if (!Factory)
	{
		UE_LOG(LogDPFactory, Warning, TEXT("[EntitySpawn] No spawn factory subsystem; cannot spawn '%s'."),
			*FactoryTag.ToString());
		return nullptr;
	}

	FDP_SpawnParams Params;
	Params.IdentityTag = FactoryTag;
	Params.Transform = Transform;
	Params.Owner = Owner;
	Params.bAllowPooling = bAllowPooling;
	if (ArchetypeTagToApply.IsValid())
	{
		Params.ContextTags.AddTag(ArchetypeTagToApply);
	}

	AActor* Spawned = Factory->Spawn(FactoryTag, Params);
	if (!Spawned)
	{
		UE_LOG(LogDPFactory, Warning, TEXT("[EntitySpawn] Factory produced no actor for '%s'."),
			*FactoryTag.ToString());
		return nullptr;
	}

	// Configure the entity component (authority): set identity tag + apply the trait set.
	if (UEnt_EntityComponent* Ent = FindEntityComponent(Spawned))
	{
		if (AssetToApply)
		{
			Ent->ApplyArchetype(AssetToApply);
		}
		else if (ArchetypeTagToApply.IsValid())
		{
			// No asset given: at least stamp the identity tag (trait set is whatever the actor authored).
			Ent->SetArchetypeTag(ArchetypeTagToApply);
		}
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("[EntitySpawn] Spawned actor '%s' has no entity component."),
			*GetNameSafe(Spawned));
	}

	return Spawned;
}

void UEnt_EntitySpawnSubsystem::WarmPoolForArchetype(FGameplayTag ArchetypeTag, TSubclassOf<AActor> WarmClass, int32 Count)
{
	if (!HasWorldAuthority() || !*WarmClass || Count <= 0)
	{
		return;
	}
	UDP_ObjectPoolSubsystem* Pool = FDP_SubsystemStatics::GetWorldSubsystem<UDP_ObjectPoolSubsystem>(this);
	if (!Pool)
	{
		UE_LOG(LogDPPool, Verbose, TEXT("[EntitySpawn] No object pool subsystem; skipping warmup for '%s'."),
			*ArchetypeTag.ToString());
		return;
	}
	Pool->WarmupAsync(WarmClass, Count);
}

FString UEnt_EntitySpawnSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("EntitySpawn: factoryTag=%s pooling=%s authority=%s"),
		DefaultFactoryTag.IsValid() ? *DefaultFactoryTag.ToString() : TEXT("<archetype>"),
		bAllowPooling ? TEXT("on") : TEXT("off"),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
