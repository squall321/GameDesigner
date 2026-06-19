// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Factory/DPSpawnFactory.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

DECLARE_CYCLE_STAT(TEXT("Factory CreateActor"), STAT_DP_FactoryCreateActor, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("Factory Actors Spawned"), STAT_DP_FactoryActorsSpawned, STATGROUP_DesignPatterns);

UDP_SpawnFactory::UDP_SpawnFactory()
{
}

TSubclassOf<AActor> UDP_SpawnFactory::ResolveActorClassForParams_Implementation(const FDP_SpawnParams& /*Params*/) const
{
	if (ExplicitActorClass.IsNull())
	{
		return nullptr;
	}
	if (UClass* Loaded = ExplicitActorClass.Get())
	{
		return Loaded;
	}
	return ExplicitActorClass.LoadSynchronous();
}

void UDP_SpawnFactory::OnActorPreFinish_Implementation(AActor* /*Actor*/, const FDP_SpawnParams& /*Params*/)
{
	// Default: nothing. Subclasses inject Params-derived state here before BeginPlay.
}

void UDP_SpawnFactory::OnActorReused_Implementation(AActor* /*Actor*/, const FDP_SpawnParams& /*Params*/)
{
	// Default: nothing. Subclasses re-arm pooled actors here.
}

AActor* UDP_SpawnFactory::CreateActor_Implementation(UObject* WorldContext, const FDP_SpawnParams& Params)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_FactoryCreateActor);

	if (!WorldContext)
	{
		UE_LOG(LogDPFactory, Error, TEXT("CreateActor called with null WorldContext on factory '%s'."), *GetName());
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		UE_LOG(LogDPFactory, Error, TEXT("CreateActor could not resolve a UWorld for factory '%s'."), *GetName());
		return nullptr;
	}

	const TSubclassOf<AActor> Class = ResolveActorClassForParams(Params);
	if (!Class)
	{
		UE_LOG(LogDPFactory, Error, TEXT("CreateActor: factory '%s' resolved no actor class for tag '%s'."),
			*GetName(), *Params.IdentityTag.ToString());
		return nullptr;
	}

	return SpawnDeferredAndFinish(World, Class, Params);
}

AActor* UDP_SpawnFactory::SpawnDeferredAndFinish(UWorld* World, TSubclassOf<AActor> Class, const FDP_SpawnParams& Params)
{
	if (!ensure(World) || !ensure(Class))
	{
		return nullptr;
	}

	// Deferred spawn so OnActorPreFinish can mutate the actor before BeginPlay.
	AActor* Actor = World->SpawnActorDeferred<AActor>(
		Class,
		Params.Transform,
		Params.Owner.Get(),
		Params.Instigator.Get(),
		Params.CollisionHandling);

	if (!Actor)
	{
		UE_LOG(LogDPFactory, Warning, TEXT("SpawnActorDeferred returned null for class '%s' (tag '%s')."),
			*Class->GetName(), *Params.IdentityTag.ToString());
		return nullptr;
	}

	OnActorPreFinish(Actor, Params);

	Actor->FinishSpawning(Params.Transform);

	INC_DWORD_STAT(STAT_DP_FactoryActorsSpawned);
	UE_LOG(LogDPFactory, Verbose, TEXT("Factory '%s' spawned '%s' for tag '%s'."),
		*GetName(), *Actor->GetName(), *Params.IdentityTag.ToString());

	return Actor;
}
