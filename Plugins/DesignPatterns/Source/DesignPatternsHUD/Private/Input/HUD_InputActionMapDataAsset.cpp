// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/HUD_InputActionMapDataAsset.h"

const FHUD_InputContextLayer* UHUD_InputActionMapDataAsset::FindLayer(FGameplayTag LayerTag) const
{
	if (!LayerTag.IsValid())
	{
		return nullptr;
	}
	return Layers.FindByPredicate(
		[&LayerTag](const FHUD_InputContextLayer& Layer)
		{
			return Layer.LayerTag == LayerTag;
		});
}

FName UHUD_InputActionMapDataAsset::GetDataAssetType_Implementation() const
{
	// Collapse all HUD input maps into a single asset-manager bucket so a game can scan them as one type.
	return FName(TEXT("HUD_InputActionMap"));
}
