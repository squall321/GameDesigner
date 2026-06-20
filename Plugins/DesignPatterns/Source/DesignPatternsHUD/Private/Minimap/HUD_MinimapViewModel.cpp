// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Minimap/HUD_MinimapViewModel.h"

#include "Minimap/HUD_MarkerRegistrySubsystem.h"
#include "Seam/HUD_Trackable.h"
#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_MinimapViewModel's observable fields by name. */
	struct FHUD_MinimapViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_MinimapViewModel::EField::Num];

		static FFieldId MakeId(UHUD_MinimapViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_MinimapViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_MinimapViewModelDescriptor::FieldNames[(int32)UHUD_MinimapViewModel::EField::Num] =
	{
		FName(TEXT("Markers")),
	};

	static const FHUD_MinimapViewModelDescriptor GHUD_MinimapViewModelDescriptor;
}

UHUD_MinimapViewModel::UHUD_MinimapViewModel()
{
	// Default to the shipped top-down rotating projection. Instanced + owning so it is GC-kept.
	ProjectionStrategy = CreateDefaultSubobject<UHUD_MinimapProjectionStrategy>(TEXT("ProjectionStrategy"));
}

const UE::FieldNotification::IClassDescriptor& UHUD_MinimapViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_MinimapViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_MinimapViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_MinimapViewModelDescriptor::MakeId(Field);
}

void UHUD_MinimapViewModel::SetViewFrame(const FVector& InViewOriginWorld, float InViewYawDegrees)
{
	ViewOriginWorld = InViewOriginWorld;
	ViewYawDegrees = InViewYawDegrees;
}

void UHUD_MinimapViewModel::SetIconTable(const TArray<FHUD_MarkerIconRow>& InRows)
{
	IconTable = InRows;
}

void UHUD_MinimapViewModel::SetProjectionStrategy(UHUD_MinimapProjectionStrategy* InStrategy)
{
	if (InStrategy)
	{
		ProjectionStrategy = InStrategy;
	}
}

TSoftObjectPtr<UTexture2D> UHUD_MinimapViewModel::ResolveIconForTag(const FGameplayTag& MarkerTag) const
{
	if (!MarkerTag.IsValid())
	{
		return TSoftObjectPtr<UTexture2D>();
	}

	// Prefer an exact row; otherwise pick the most-specific parent row (longest matching tag) so a game can
	// register a broad "DP.HUD.Marker.Enemy" icon and override "DP.HUD.Marker.Enemy.Boss" specifically.
	const FHUD_MarkerIconRow* Best = nullptr;
	int32 BestDepth = -1;
	for (const FHUD_MarkerIconRow& Row : IconTable)
	{
		if (!Row.MarkerTag.IsValid())
		{
			continue;
		}
		if (Row.MarkerTag == MarkerTag)
		{
			return Row.Icon; // exact match wins immediately
		}
		if (MarkerTag.MatchesTag(Row.MarkerTag))
		{
			const int32 Depth = Row.MarkerTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Row;
			}
		}
	}
	return Best ? Best->Icon : TSoftObjectPtr<UTexture2D>();
}

void UHUD_MinimapViewModel::Refresh(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return;
	}

	// Resolve the marker registry by stable service tag first (decoupled), then fall back to the world
	// subsystem directly. The ViewModel never holds a registry pointer of its own.
	UHUD_MarkerRegistrySubsystem* Registry = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject))
	{
		Registry = Locator->Resolve<UHUD_MarkerRegistrySubsystem>(HUDTags::Service_MarkerRegistry);
	}
	if (!Registry)
	{
		Registry = FDP_SubsystemStatics::GetWorldSubsystem<UHUD_MarkerRegistrySubsystem>(WorldContextObject);
	}
	if (!Registry)
	{
		// No registry (editor/preview); clear if we previously had markers so the view empties out.
		if (Markers.Num() > 0)
		{
			Markers.Reset();
			BroadcastFieldValueChanged(GetFieldId(EField::Markers));
		}
		return;
	}

	if (!ProjectionStrategy)
	{
		// Defensive: should never happen (constructed as a default subobject), but never crash a refresh.
		UE_LOG(LogDP, Warning, TEXT("HUD_MinimapViewModel: missing projection strategy; skipping refresh."));
		return;
	}

	TArray<TScriptInterface<IHUD_Trackable>> Live;
	Registry->GetLiveTrackables(Live);

	TArray<FHUD_MinimapMarkerView> NewMarkers;
	NewMarkers.Reserve(Live.Num());

	FHUD_ProjectionContext Ctx;
	Ctx.ViewOriginWorld = ViewOriginWorld;
	Ctx.ViewYawDegrees = ViewYawDegrees;

	for (const TScriptInterface<IHUD_Trackable>& Trackable : Live)
	{
		UObject* Obj = Trackable.GetObject();
		if (!Obj)
		{
			continue;
		}

		// Per-frame visibility gate (stealth / undiscovered) — registration is for lifetime only.
		if (!IHUD_Trackable::Execute_IsVisibleOnMap(Obj))
		{
			continue;
		}

		Ctx.WorldLocation = IHUD_Trackable::Execute_GetWorldLocation(Obj);
		const FHUD_ProjectedPoint Projected = ProjectionStrategy->ProjectPoint(Ctx);

		FHUD_MinimapMarkerView View;
		View.MarkerTag = IHUD_Trackable::Execute_GetMarkerTag(Obj);
		View.Icon = ResolveIconForTag(View.MarkerTag);
		View.NormalizedPosition = Projected.NormalizedPosition;
		View.BearingDegrees = Projected.BearingDegrees;
		View.bWithinRange = Projected.bWithinRange;
		NewMarkers.Add(MoveTemp(View));
	}

	// Only broadcast when the projected set actually changed, to avoid waking bound views every frame.
	bool bChanged = (NewMarkers.Num() != Markers.Num());
	if (!bChanged)
	{
		for (int32 i = 0; i < NewMarkers.Num(); ++i)
		{
			const FHUD_MinimapMarkerView& A = NewMarkers[i];
			const FHUD_MinimapMarkerView& B = Markers[i];
			if (A.MarkerTag != B.MarkerTag ||
				A.bWithinRange != B.bWithinRange ||
				!A.NormalizedPosition.Equals(B.NormalizedPosition, 0.001f) ||
				!FMath::IsNearlyEqual(A.BearingDegrees, B.BearingDegrees, 0.1f) ||
				A.Icon.ToSoftObjectPath() != B.Icon.ToSoftObjectPath())
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		Markers = MoveTemp(NewMarkers);
		BroadcastFieldValueChanged(GetFieldId(EField::Markers));
	}
}
