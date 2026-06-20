// Copyright DesignPatterns plugin. All Rights Reserved.

#include "SimGrid_NativeTags.h"

namespace SimGridTags
{
	UE_DEFINE_GAMEPLAY_TAG(Service_TileProvider,      "DP.Service.SimGrid.TileProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_TerritoryCarrier,  "DP.Service.SimGrid.TerritoryCarrier");

	UE_DEFINE_GAMEPLAY_TAG(Fail_OutOfBounds,          "SimGrid.Placement.Fail.OutOfBounds");
	UE_DEFINE_GAMEPLAY_TAG(Fail_CellOccupied,         "SimGrid.Placement.Fail.CellOccupied");
	UE_DEFINE_GAMEPLAY_TAG(Fail_TerrainNotAllowed,    "SimGrid.Placement.Fail.TerrainNotAllowed");
	UE_DEFINE_GAMEPLAY_TAG(Fail_MissingAdjacency,     "SimGrid.Placement.Fail.MissingAdjacency");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NotOwnedZone,         "SimGrid.Placement.Fail.NotOwnedZone");
	UE_DEFINE_GAMEPLAY_TAG(Fail_StateUnknown,         "SimGrid.Placement.Fail.StateUnknown");
}
