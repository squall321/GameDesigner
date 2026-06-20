// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tiles/SimGrid_TileTypeDefinition.h"

FName USimGrid_TileTypeDefinition::GetDataAssetType_Implementation() const
{
	// Stable, subclass-agnostic bucket so games can enumerate all tile types as one PrimaryAssetType.
	return FName(TEXT("SimGrid_TileType"));
}
