// Copyright DesignPatterns plugin. All Rights Reserved.

#include "SimGrid_NativeTags.h"

namespace SimGridTags
{
	UE_DEFINE_GAMEPLAY_TAG(Service_TileProvider,      "DP.Service.SimGrid.TileProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_TerritoryCarrier,  "DP.Service.SimGrid.TerritoryCarrier");

	UE_DEFINE_GAMEPLAY_TAG(Service_LayeredTileProvider, "DP.Service.SimGrid.LayeredTileProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_HeightProvider,    "DP.Service.SimGrid.HeightProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_ZoneCarrier,       "DP.Service.SimGrid.ZoneCarrier");
	UE_DEFINE_GAMEPLAY_TAG(Service_FogCarrier,        "DP.Service.SimGrid.FogCarrier");

	UE_DEFINE_GAMEPLAY_TAG(PathFail_NoPath,           "SimGrid.Path.Fail.NoPath");
	UE_DEFINE_GAMEPLAY_TAG(PathFail_BlockedEndpoint,  "SimGrid.Path.Fail.BlockedEndpoint");
	UE_DEFINE_GAMEPLAY_TAG(PathFail_NodeBudget,       "SimGrid.Path.Fail.NodeBudget");

	UE_DEFINE_GAMEPLAY_TAG(ZoneFail_NotBuildable,     "SimGrid.Zone.Fail.NotBuildable");
	UE_DEFINE_GAMEPLAY_TAG(ZoneFail_NotOwned,         "SimGrid.Zone.Fail.NotOwned");

	UE_DEFINE_GAMEPLAY_TAG(FogFail_NoCarrier,         "SimGrid.Fog.Fail.NoCarrier");

	UE_DEFINE_GAMEPLAY_TAG(Fail_OutOfBounds,          "SimGrid.Placement.Fail.OutOfBounds");
	UE_DEFINE_GAMEPLAY_TAG(Fail_CellOccupied,         "SimGrid.Placement.Fail.CellOccupied");
	UE_DEFINE_GAMEPLAY_TAG(Fail_TerrainNotAllowed,    "SimGrid.Placement.Fail.TerrainNotAllowed");
	UE_DEFINE_GAMEPLAY_TAG(Fail_MissingAdjacency,     "SimGrid.Placement.Fail.MissingAdjacency");
	UE_DEFINE_GAMEPLAY_TAG(Fail_NotOwnedZone,         "SimGrid.Placement.Fail.NotOwnedZone");
	UE_DEFINE_GAMEPLAY_TAG(Fail_StateUnknown,         "SimGrid.Placement.Fail.StateUnknown");
}
