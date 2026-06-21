// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spectator/Rep_SpectatorController.h"
#include "Seam/Rep_SpectatorCamera.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPLog.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/Pawn.h"

bool URep_SpectatorController::EnterSpectator(APlayerController* LocalController)
{
	if (bSpectating)
	{
		return true; // already active; re-entry is a no-op
	}
	if (!LocalController)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay spectator: EnterSpectator with no controller."));
		return false;
	}

	Controller = LocalController;
	PriorViewTarget = LocalController->GetViewTarget();
	PriorPawn = LocalController->GetPawn();

	ResolveCameraSeam();

	if (UObject* CamObj = CameraSeam.GetObject())
	{
		// Camera module / game adapter present: let it drive a free-fly framing.
		CameraHandle = IRep_SpectatorCamera::Execute_EnterSpectatorCamera(CamObj, LocalController);
		if (CameraHandle.IsValid())
		{
			bSpectating = true;
			bUsingFallbackPawn = false;
			UE_LOG(LogDP, Log, TEXT("Replay spectator: entered via camera seam."));
			return true;
		}
		// Seam declined (invalid handle): fall through to the engine fallback.
		UE_LOG(LogDP, Verbose, TEXT("Replay spectator: camera seam declined; using engine fallback."));
	}

	// INERT DEFAULT: no usable camera seam — possess an engine spectator pawn for free-fly.
	if (EnterFallbackPawn())
	{
		bSpectating = true;
		bUsingFallbackPawn = true;
		return true;
	}

	// Could not engage any spectator path; restore bookkeeping.
	Controller.Reset();
	PriorViewTarget.Reset();
	PriorPawn.Reset();
	return false;
}

void URep_SpectatorController::ExitSpectator()
{
	if (!bSpectating)
	{
		return;
	}

	APlayerController* PC = Controller.Get();

	if (bUsingFallbackPawn)
	{
		ExitFallbackPawn();
	}
	else if (UObject* CamObj = CameraSeam.GetObject())
	{
		IRep_SpectatorCamera::Execute_ExitSpectatorCamera(CamObj, PC, CameraHandle);
	}

	// Restore the prior view target if it still exists.
	if (PC && PriorViewTarget.IsValid())
	{
		PC->SetViewTarget(PriorViewTarget.Get());
	}

	CameraHandle.Invalidate();
	CameraSeam.Reset();
	Controller.Reset();
	PriorViewTarget.Reset();
	PriorPawn.Reset();
	bSpectating = false;
	bUsingFallbackPawn = false;

	UE_LOG(LogDP, Log, TEXT("Replay spectator: exited."));
}

void URep_SpectatorController::FocusOnActor(AActor* Target)
{
	if (!bSpectating || !Target)
	{
		return;
	}
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}

	if (UObject* CamObj = CameraSeam.GetObject())
	{
		if (IRep_SpectatorCamera::Execute_FocusOnActor(CamObj, PC, Target))
		{
			return; // seam handled the focus
		}
	}
	// Fallback: just retarget the engine view.
	PC->SetViewTarget(Target);
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

void URep_SpectatorController::ResolveCameraSeam()
{
	CameraSeam.Reset();

	const APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	const UWorld* World = PC->GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (!GI)
	{
		return;
	}

	if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
	{
		if (UObject* Provider = Locator->ResolveService(Rep_NativeTags::Service_SpectatorCamera))
		{
			if (Provider->GetClass()->ImplementsInterface(URep_SpectatorCamera::StaticClass()))
			{
				CameraSeam = TWeakInterfacePtr<IRep_SpectatorCamera>(*Provider);
			}
		}
	}
}

bool URep_SpectatorController::EnterFallbackPawn()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return false;
	}
	UWorld* World = PC->GetWorld();
	if (!World)
	{
		return false;
	}

	// Resolve the spectator pawn class (settings override, else engine default).
	TSubclassOf<APawn> PawnClass = ASpectatorPawn::StaticClass();
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		if (!Settings->FallbackSpectatorPawnClass.IsNull())
		{
			if (UClass* Loaded = Settings->FallbackSpectatorPawnClass.LoadSynchronous())
			{
				PawnClass = Loaded;
			}
		}
	}

	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn at the current view location so free-fly starts where the viewer was looking.
	FVector Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(Loc, Rot);

	APawn* NewPawn = World->SpawnActor<APawn>(PawnClass, Loc, Rot, Params);
	if (!NewPawn)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay spectator: failed to spawn fallback spectator pawn."));
		return false;
	}

	FallbackPawn = NewPawn;
	PC->Possess(NewPawn);
	PC->SetViewTarget(NewPawn);
	return true;
}

void URep_SpectatorController::ExitFallbackPawn()
{
	APlayerController* PC = Controller.Get();

	if (PC && PriorPawn.IsValid())
	{
		PC->Possess(PriorPawn.Get());
	}

	if (APawn* Pawn = FallbackPawn.Get())
	{
		Pawn->Destroy();
	}
	FallbackPawn.Reset();
}
