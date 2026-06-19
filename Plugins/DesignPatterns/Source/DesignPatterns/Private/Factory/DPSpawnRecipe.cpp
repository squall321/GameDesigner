// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Factory/DPSpawnRecipe.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

const FPrimaryAssetType UDP_SpawnRecipe::PrimaryAssetType = TEXT("DP_SpawnRecipe");

UDP_SpawnRecipe::UDP_SpawnRecipe()
{
}

TSubclassOf<AActor> UDP_SpawnRecipe::ResolveActorClass() const
{
	if (ActorClass.IsNull())
	{
		UE_LOG(LogDPFactory, Warning, TEXT("UDP_SpawnRecipe '%s' has no ActorClass set."), *GetName());
		return nullptr;
	}

	// Already loaded? Avoid the synchronous load entirely.
	if (UClass* Loaded = ActorClass.Get())
	{
		return Loaded;
	}

	UClass* LoadedClass = ActorClass.LoadSynchronous();
	if (!LoadedClass)
	{
		UE_LOG(LogDPFactory, Error, TEXT("UDP_SpawnRecipe '%s' failed to load ActorClass '%s'."),
			*GetName(), *ActorClass.ToString());
	}
	return LoadedClass;
}

FDP_SpawnParams UDP_SpawnRecipe::MakeDefaultParams() const
{
	FDP_SpawnParams Params;
	Params.IdentityTag = IdentityTag;
	Params.Transform = DefaultTransform;
	Params.CollisionHandling = DefaultCollisionHandling;
	Params.ContextTags = RecipeTags;
	return Params;
}

FPrimaryAssetId UDP_SpawnRecipe::GetPrimaryAssetId() const
{
	// Prefer the identity tag as the stable primary-asset name so the factory subsystem can
	// resolve recipes by tag without loading the asset; fall back to the asset's own name.
	const FName AssetName = IdentityTag.IsValid()
		? FName(*IdentityTag.ToString())
		: GetFName();
	return FPrimaryAssetId(PrimaryAssetType, AssetName);
}
