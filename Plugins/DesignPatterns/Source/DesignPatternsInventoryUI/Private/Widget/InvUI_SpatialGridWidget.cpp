// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_SpatialGridWidget.h"
#include "Intent/InvUI_SpatialIntentComponent.h"
#include "ViewModel/InvUI_EncumbranceViewModel.h"
#include "ViewModel/InvUI_GridViewModel.h"
#include "Seam/InvUI_SpatialFootprintProvider.h"
#include "Registry/InvUI_ContainerRegistry.h"
#include "Settings/InvUI_Settings.h"
#include "Core/DPLog.h"

UInvUI_SpatialGridWidget::UInvUI_SpatialGridWidget()
{
	RotateKey = EKeys::R;
}

void UInvUI_SpatialGridWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Pull the data-driven cell size unless the designer overrode it on this widget instance.
	if (const UInvUI_Settings* Settings = UInvUI_Settings::Get())
	{
		if (CellPixelSize <= 0.f)
		{
			CellPixelSize = Settings->GetEffectiveCellPixelSize();
		}
	}
	if (CellPixelSize <= 0.f)
	{
		CellPixelSize = 64.f; // documented defensive fallback
	}
}

void UInvUI_SpatialGridWidget::BindSpatial(UInvUI_SpatialIntentComponent* InIntent,
	UInvUI_EncumbranceViewModel* InEncumbrance)
{
	SpatialIntent = InIntent;
	EncumbranceViewModel = InEncumbrance;

	// The window binds the encumbrance VM to the container; here we just ensure a recompute so the
	// weight/volume readout reflects current state immediately after the spatial bind.
	if (EncumbranceViewModel != nullptr)
	{
		EncumbranceViewModel->Recompute();
	}
}

bool UInvUI_SpatialGridWidget::RotateFocusedSlot()
{
	UInvUI_SpatialIntentComponent* Intent = SpatialIntent.Get();
	if (Intent == nullptr || !CurrentFocusSlot.IsValid())
	{
		return false;
	}
	Intent->RequestRotate(GetContainerId(), CurrentFocusSlot);
	return true;
}

void UInvUI_SpatialGridWidget::GetSpanForSlot(FGameplayTag SlotTag, int32& OutColumnSpan, int32& OutRowSpan) const
{
	OutColumnSpan = 1;
	OutRowSpan = 1;

	if (!SlotTag.IsValid())
	{
		return;
	}

	// Re-derive the span from the slot's footprint exactly as the spatial layout did: resolve the
	// live container by the grid's container id through the registry (no private VM access), read the
	// slot, then take the footprint from its ItemPayload or the optional provider seam.
	UInvUI_ContainerRegistry* Registry = UInvUI_ContainerRegistry::Get(this);
	if (Registry == nullptr)
	{
		return;
	}
	TScriptInterface<IInvUI_ItemContainer> Container = Registry->ResolveContainer(GetContainerId());
	UObject* ContainerObj = Container.GetObject();
	if (ContainerObj == nullptr)
	{
		return;
	}

	FInvUI_SlotState State;
	if (!IInvUI_ItemContainer::Execute_GetSlot(ContainerObj, SlotTag, State) || !State.IsOccupied())
	{
		return;
	}

	FInvUI_SpatialFootprint Footprint;
	bool bGotFootprint = false;
	if (State.ItemPayload.GetScriptStruct() == FInvUI_SpatialFootprint::StaticStruct())
	{
		Footprint = State.ItemPayload.Get<FInvUI_SpatialFootprint>();
		bGotFootprint = true;
	}
	else if (ContainerObj->GetClass()->ImplementsInterface(UInvUI_SpatialFootprintProvider::StaticClass()))
	{
		bGotFootprint = IInvUI_SpatialFootprintProvider::Execute_GetSlotFootprint(ContainerObj, SlotTag, Footprint);
	}

	if (bGotFootprint)
	{
		const FIntPoint Extent = Footprint.GetExtent();
		OutColumnSpan = FMath::Max(1, Extent.X);
		OutRowSpan = FMath::Max(1, Extent.Y);
	}
}

FReply UInvUI_SpatialGridWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (RotateKey.IsValid() && InKeyEvent.GetKey() == RotateKey)
	{
		if (RotateFocusedSlot())
		{
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
