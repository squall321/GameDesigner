// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Prediction/UNet_InterpolatedTransformComponent.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UNet_InterpolatedTransformComponent::UNet_InterpolatedTransformComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UNet_InterpolatedTransformComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Skip the owning connection: that machine has the authoritative transform already and must not be
	// smoothed (it would fight its own prediction). Simulated proxies receive and interpolate.
	DOREPLIFETIME_CONDITION(UNet_InterpolatedTransformComponent, Snapshot, COND_SkipOwner);
}

void UNet_InterpolatedTransformComponent::BeginPlay()
{
	Super::BeginPlay();

	History.Capacity = FMath::Max(2, BufferCapacity);

	AActor* Owner = GetOwner();
	if (Owner && UNet_NetUtilsLibrary::HasAuthority(Owner) && !bDisabledOwnerMovementReplication)
	{
		// Take over proxy smoothing: stop the engine's coarse movement replication from fighting us.
		Owner->SetReplicateMovement(false);
		bDisabledOwnerMovementReplication = true;
	}
}

bool UNet_InterpolatedTransformComponent::ShouldInterpolate() const
{
	const AActor* Owner = GetOwner();
	// Only simulated proxies get smoothed. Authority and the autonomous (owning) proxy already hold the
	// true transform locally.
	return Owner && UNet_NetUtilsLibrary::IsSimulatedProxy(Owner);
}

void UNet_InterpolatedTransformComponent::ResetInterpolation()
{
	History.Reset();
}

void UNet_InterpolatedTransformComponent::ServerCaptureSnapshot()
{
	AActor* Owner = GetOwner();
	if (!Owner || !UNet_NetUtilsLibrary::HasAuthority(Owner))
	{
		return;
	}

	const FTransform Xform = Owner->GetActorTransform();
	Snapshot.Location = Xform.GetLocation();
	Snapshot.Rotation = Xform.Rotator();
	++Snapshot.Counter;

	// Snapshot is COND_SkipOwner; force a net update so the new delta reaches simulated proxies promptly.
	Owner->ForceNetUpdate();
}

void UNet_InterpolatedTransformComponent::OnRep_Snapshot()
{
	// A new authoritative sample arrived; record it in the LOCAL buffer with this machine's clock.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	History.Capacity = FMath::Max(2, BufferCapacity);
	History.Push(Now, (FVector)Snapshot.Location, Snapshot.Rotation);
}

void UNet_InterpolatedTransformComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// SERVER: drive the snapshot cadence.
	if (UNet_NetUtilsLibrary::HasAuthority(Owner))
	{
		const float Period = 1.f / FMath::Max(1.f, SnapshotFrequencyHz);
		TimeSinceLastSnapshot += DeltaTime;
		if (TimeSinceLastSnapshot >= Period)
		{
			TimeSinceLastSnapshot = 0.f;
			ServerCaptureSnapshot();
		}
		// A listen-server host's own simulated-proxy view is impossible (it is authority), so no smoothing.
		return;
	}

	// SIMULATED PROXY: render at (now - InterpDelay) from the local buffer.
	if (!ShouldInterpolate() || History.IsEmpty())
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double RenderTime = Now - (double)FMath::Max(0.f, InterpDelay);

	FVector OutLoc;
	FRotator OutRot;
	if (History.Sample(RenderTime, (double)FMath::Max(0.f, MaxExtrapolation), OutLoc, OutRot))
	{
		// Write the smoothed transform onto the proxy so anim/gameplay reading the actor see smooth motion.
		Owner->SetActorLocationAndRotation(OutLoc, OutRot, /*bSweep=*/false, nullptr, ETeleportType::None);
	}
}
