// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Indicators/HUD_WorldIndicatorSubsystem.h"

#include "Indicators/HUD_WorldIndicatorViewModel.h"
#include "Indicators/HUD_WorldIndicatorConfigDataAsset.h"
#include "Minimap/HUD_MarkerRegistrySubsystem.h"
#include "Seam/HUD_Trackable.h"
#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"

void UHUD_WorldIndicatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewModel = NewObject<UHUD_WorldIndicatorViewModel>(this);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_WorldIndicatorSubsystem::TickIndicators));
}

void UHUD_WorldIndicatorSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	ViewModel = nullptr;
	Config = nullptr;
	Super::Deinitialize();
}

void UHUD_WorldIndicatorSubsystem::SetConfig(UHUD_WorldIndicatorConfigDataAsset* InConfig)
{
	Config = InConfig;
	if (ViewModel && Config)
	{
		ViewModel->SetIconTable(Config->IconTable);
	}
}

UHUD_MarkerRegistrySubsystem* UHUD_WorldIndicatorSubsystem::ResolveRegistry() const
{
	// Resolve by stable service tag first (decoupled), then the world subsystem directly.
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UHUD_MarkerRegistrySubsystem* Reg =
				Locator->Resolve<UHUD_MarkerRegistrySubsystem>(HUDTags::Service_MarkerRegistry))
		{
			return Reg;
		}
	}
	return FDP_SubsystemStatics::GetWorldSubsystem<UHUD_MarkerRegistrySubsystem>(this);
}

APlayerController* UHUD_WorldIndicatorSubsystem::GetOwningPlayerController() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
}

bool UHUD_WorldIndicatorSubsystem::TickIndicators(float /*DeltaTime*/)
{
	// Gate on a valid PC + config before doing any work (cheap early-out when not in play).
	if (Config && GetOwningPlayerController())
	{
		RefreshForViewer();
	}
	return true; // keep ticking
}

void UHUD_WorldIndicatorSubsystem::RefreshForViewer()
{
	if (!ViewModel || !Config)
	{
		return;
	}

	TArray<FProjected> Projected;
	ProjectAndClassify(Projected);

	TArray<FHUD_WorldIndicatorView> Views;
	ClusterScreenIndicators(Projected, Views);

	ViewModel->SetIndicators(Views);
}

void UHUD_WorldIndicatorSubsystem::ProjectAndClassify(TArray<FProjected>& Out) const
{
	Out.Reset();

	UHUD_MarkerRegistrySubsystem* Registry = ResolveRegistry();
	APlayerController* PC = GetOwningPlayerController();
	if (!Registry || !PC)
	{
		return;
	}

	// Viewport extents in pixels.
	int32 ViewX = 0, ViewY = 0;
	PC->GetViewportSize(ViewX, ViewY);
	if (ViewX <= 0 || ViewY <= 0)
	{
		return;
	}
	const FVector2D ViewSize(ViewX, ViewY);
	const FVector2D ViewCenter = ViewSize * 0.5f;

	// Viewer location for distance + occlusion.
	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	const float EdgeInset = Config->EdgeInsetPixels;
	const float FadeStart = Config->FadeStartDistance;
	const float FadeEnd = Config->GetEffectiveFadeEnd();

	TArray<TScriptInterface<IHUD_Trackable>> Live;
	Registry->GetLiveTrackables(Live);

	int32 TracesRemaining = Config->bOcclusionEnabled ? Config->MaxTracesPerRefresh : 0;

	for (const TScriptInterface<IHUD_Trackable>& Trackable : Live)
	{
		UObject* Obj = Trackable.GetObject();
		if (!Obj || !IHUD_Trackable::Execute_IsVisibleOnMap(Obj))
		{
			continue;
		}

		const FVector World = IHUD_Trackable::Execute_GetWorldLocation(Obj);
		const float Distance = static_cast<float>(FVector::Dist(ViewLoc, World));

		// Distance fade -> cull beyond fade end.
		if (Distance >= FadeEnd)
		{
			continue;
		}
		const float DistanceFade = (Distance <= FadeStart)
			? 1.f
			: 1.f - FMath::Clamp((Distance - FadeStart) / (FadeEnd - FadeStart), 0.f, 1.f);

		FProjected P;
		P.MarkerTag = IHUD_Trackable::Execute_GetMarkerTag(Obj);
		P.WorldLocation = World;
		P.DistanceUU = Distance;
		P.Opacity = DistanceFade;

		FVector2D Screen;
		const bool bProjected = PC->ProjectWorldLocationToScreen(World, Screen, /*bPlayerViewportRelative*/ false);
		const bool bInFront = bProjected;
		const bool bInsideViewport = bProjected
			&& Screen.X >= 0.f && Screen.X <= ViewSize.X
			&& Screen.Y >= 0.f && Screen.Y <= ViewSize.Y;

		if (bInFront && bInsideViewport)
		{
			P.bOnScreen = true;
			P.ScreenPosition = Screen;

			// Single capped occlusion trace per on-screen target.
			if (TracesRemaining > 0 && IsOccluded(World, PC))
			{
				P.Opacity *= Config->OccludedOpacity;
				--TracesRemaining;
			}
			else if (TracesRemaining > 0)
			{
				--TracesRemaining;
			}
		}
		else
		{
			// Off-screen (or behind camera): clamp to the viewport edge along the bearing to the target and
			// emit an arrow. When behind the camera, ProjectWorldLocationToScreen can mirror the point, so we
			// recompute the direction from the camera-relative bearing.
			P.bOnScreen = false;

			FVector2D Dir;
			if (bProjected)
			{
				Dir = (Screen - ViewCenter);
			}
			else
			{
				// Behind camera: derive a 2D bearing from the yaw difference to the target.
				const FVector ToTarget = (World - ViewLoc);
				const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y);
				const FVector Up = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Z);
				Dir = FVector2D(FVector::DotProduct(ToTarget, Right), -FVector::DotProduct(ToTarget, Up));
			}
			if (Dir.IsNearlyZero())
			{
				Dir = FVector2D(0.f, -1.f); // default up
			}
			Dir.Normalize();

			// Clamp the center-relative direction to the inset viewport rectangle.
			const FVector2D HalfExtent = (ViewSize * 0.5f) - FVector2D(EdgeInset, EdgeInset);
			const float ScaleX = (FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER) ? (HalfExtent.X / FMath::Abs(Dir.X)) : TNumericLimits<float>::Max();
			const float ScaleY = (FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER) ? (HalfExtent.Y / FMath::Abs(Dir.Y)) : TNumericLimits<float>::Max();
			const float Scale = FMath::Min(ScaleX, ScaleY);
			P.ScreenPosition = ViewCenter + Dir * Scale;

			// Arrow angle: 0 = up, clockwise positive (atan2 of the screen direction).
			P.ArrowAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(Dir.X, -Dir.Y));
		}

		Out.Add(MoveTemp(P));
	}
}

bool UHUD_WorldIndicatorSubsystem::IsOccluded(const FVector& Target, APlayerController* PC) const
{
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HUDIndicatorOcclusion), /*bTraceComplex*/ false);
	if (APawn* Pawn = PC->GetPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit, ViewLoc, Target, Config->OcclusionChannel.GetValue(), Params);
	// Treat a hit that lands near the target as "not occluded" (we hit the target itself).
	return bBlocked && (FVector::DistSquared(Hit.ImpactPoint, Target) > FMath::Square(50.f));
}

void UHUD_WorldIndicatorSubsystem::ClusterScreenIndicators(TArray<FProjected>& InOut, TArray<FHUD_WorldIndicatorView>& OutViews) const
{
	OutViews.Reset();
	if (!ViewModel)
	{
		return;
	}

	const float ClusterRadiusSq = FMath::Square(Config ? Config->ClusterPixelRadius : 0.f);
	TBitArray<> Consumed(false, InOut.Num());

	for (int32 i = 0; i < InOut.Num(); ++i)
	{
		if (Consumed[i])
		{
			continue;
		}

		FProjected& Base = InOut[i];
		FHUD_WorldIndicatorView View;
		View.MarkerTag = Base.MarkerTag;
		View.Icon = ViewModel->ResolveIconForTag(Base.MarkerTag);
		View.ScreenPosition = Base.ScreenPosition;
		View.bOnScreen = Base.bOnScreen;
		View.ArrowAngleDegrees = Base.ArrowAngleDegrees;
		View.Opacity = Base.Opacity;
		View.DistanceUU = Base.DistanceUU;
		View.ClusterCount = 1;

		// Cluster only same-tag on-screen indicators within the radius; off-screen arrows stay distinct.
		if (Base.bOnScreen && ClusterRadiusSq > 0.f)
		{
			for (int32 j = i + 1; j < InOut.Num(); ++j)
			{
				if (Consumed[j] || !InOut[j].bOnScreen || InOut[j].MarkerTag != Base.MarkerTag)
				{
					continue;
				}
				if (FVector2D::DistSquared(InOut[j].ScreenPosition, Base.ScreenPosition) <= ClusterRadiusSq)
				{
					Consumed[j] = true;
					++View.ClusterCount;
					// Keep the nearest target's distance/opacity for the cluster head.
					if (InOut[j].DistanceUU < View.DistanceUU)
					{
						View.DistanceUU = InOut[j].DistanceUU;
						View.Opacity = FMath::Max(View.Opacity, InOut[j].Opacity);
					}
				}
			}
		}

		OutViews.Add(MoveTemp(View));
	}
}

void UHUD_WorldIndicatorSubsystem::DumpTo(TArray<FString>& OutLines) const
{
	const int32 Count = ViewModel ? ViewModel->GetIndicators().Num() : 0;
	OutLines.Add(FString::Printf(TEXT("[HUD.WorldIndicator] Indicators=%d Config=%s"),
		Count, *GetNameSafe(Config)));
}
