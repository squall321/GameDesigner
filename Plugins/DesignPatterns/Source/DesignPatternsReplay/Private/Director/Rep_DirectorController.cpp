// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Director/Rep_DirectorController.h"
#include "Spectator/Rep_SpectatorController.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "Core/DPLog.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"

void URep_DirectorController::Initialize(URep_SpectatorController* InSpectator, APlayerController* InLocalController)
{
	Spectator = InSpectator;
	LocalController = InLocalController;
}

void URep_DirectorController::RefreshTargets(const TArray<AActor*>& InTargets)
{
	Targets.Reset();
	for (AActor* Target : InTargets)
	{
		if (Target)
		{
			Targets.Add(Target);
		}
	}
	CurrentIndex = 0;
	DwellSeconds = 0.f;

	if (Mode == ERep_DirectorMode::FollowPlayer && Targets.Num() > 0)
	{
		ApplyFraming();
	}
}

// ---------------------------------------------------------------------------------------------
// Mode control
// ---------------------------------------------------------------------------------------------

void URep_DirectorController::SetMode(ERep_DirectorMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}
	Mode = NewMode;
	DwellSeconds = 0.f;

	// Free-cam disables auto-cycle (it is viewer-driven).
	if (Mode == ERep_DirectorMode::FreeCam)
	{
		bAutoCycle = false;
	}

	ApplyFraming();
	OnModeChanged.Broadcast();
}

void URep_DirectorController::CycleNextTarget()
{
	PruneTargets();
	if (Targets.Num() == 0)
	{
		return;
	}
	if (Mode != ERep_DirectorMode::FollowPlayer && Mode != ERep_DirectorMode::Tactical && Mode != ERep_DirectorMode::Cinematic)
	{
		Mode = ERep_DirectorMode::FollowPlayer;
	}
	CurrentIndex = (CurrentIndex + 1) % Targets.Num();
	DwellSeconds = 0.f;
	ApplyFraming();
	OnModeChanged.Broadcast();
}

void URep_DirectorController::CyclePreviousTarget()
{
	PruneTargets();
	if (Targets.Num() == 0)
	{
		return;
	}
	if (Mode != ERep_DirectorMode::FollowPlayer && Mode != ERep_DirectorMode::Tactical && Mode != ERep_DirectorMode::Cinematic)
	{
		Mode = ERep_DirectorMode::FollowPlayer;
	}
	CurrentIndex = (CurrentIndex - 1 + Targets.Num()) % Targets.Num();
	DwellSeconds = 0.f;
	ApplyFraming();
	OnModeChanged.Broadcast();
}

void URep_DirectorController::FocusTarget(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// Add it if not already present, and select it.
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		if (Targets[Index].Get() == Target)
		{
			FoundIndex = Index;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE)
	{
		FoundIndex = Targets.Add(Target);
	}

	CurrentIndex = FoundIndex;
	Mode = ERep_DirectorMode::FollowPlayer;
	DwellSeconds = 0.f;
	ApplyFraming();
	OnModeChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------
// Auto-cycle
// ---------------------------------------------------------------------------------------------

void URep_DirectorController::SetAutoCycle(bool bEnabled)
{
	bAutoCycle = bEnabled && Mode != ERep_DirectorMode::FreeCam;
	DwellSeconds = 0.f;
}

void URep_DirectorController::Tick(float DeltaSeconds)
{
	if (!bAutoCycle || Targets.Num() <= 1)
	{
		return;
	}

	DwellSeconds += DeltaSeconds;

	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	const float Dwell = Settings ? Settings->DirectorAutoCycleSeconds : 8.f;
	if (DwellSeconds >= FMath::Max(0.5f, Dwell))
	{
		CycleNextTarget();
	}
}

AActor* URep_DirectorController::GetCurrentTarget() const
{
	if (Targets.IsValidIndex(CurrentIndex))
	{
		return Targets[CurrentIndex].Get();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

bool URep_DirectorController::EnsureSpectating()
{
	URep_SpectatorController* Spec = Spectator.Get();
	APlayerController* PC = LocalController.Get();
	if (!Spec || !PC)
	{
		return false;
	}
	if (!Spec->IsSpectating())
	{
		Spec->EnterSpectator(PC);
	}
	return Spec->IsSpectating();
}

void URep_DirectorController::ApplyFraming()
{
	URep_SpectatorController* Spec = Spectator.Get();
	if (!Spec)
	{
		return;
	}

	switch (Mode)
	{
	case ERep_DirectorMode::FreeCam:
		// Enter free-cam framing with no specific target; the seam adapter (or engine fallback) lets the
		// viewer fly. We still enter spectator so input is detached from the recorded view target.
		EnsureSpectating();
		break;

	case ERep_DirectorMode::FollowPlayer:
	case ERep_DirectorMode::Tactical:
	case ERep_DirectorMode::Cinematic:
	{
		// All target-framed modes route through FocusOnActor. The richer tactical/cinematic intent is
		// expressed by the target choice + (for adapters that support it) the spectator camera seam's own
		// mode handling; in the inert engine fallback all three resolve to a plain view-target focus.
		if (EnsureSpectating())
		{
			if (AActor* Target = GetCurrentTarget())
			{
				Spec->FocusOnActor(Target);
			}
		}
		break;
	}

	default:
		break;
	}
}

void URep_DirectorController::PruneTargets()
{
	const AActor* Current = GetCurrentTarget();

	Targets.RemoveAll([](const TWeakObjectPtr<AActor>& T) { return !T.IsValid(); });

	// Re-point CurrentIndex at the same actor if it survived, else clamp.
	if (Current)
	{
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
		{
			if (Targets[Index].Get() == Current)
			{
				CurrentIndex = Index;
				return;
			}
		}
	}
	CurrentIndex = Targets.Num() > 0 ? FMath::Clamp(CurrentIndex, 0, Targets.Num() - 1) : 0;
}
