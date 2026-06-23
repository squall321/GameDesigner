// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DPTooltipTypes.generated.h"

/** How a shown tooltip is positioned relative to its hover source / the cursor. */
UENUM(BlueprintType)
enum class EDP_TooltipFollow : uint8
{
	/** Tooltip follows the cursor each frame (offset by a configurable amount). */
	FollowCursor,
	/** Tooltip is anchored to the hover-source widget's geometry and stays put. */
	AnchoredToSource
};

/** Which edge of the hover source an anchored tooltip prefers (before clamping to the safe zone). */
UENUM(BlueprintType)
enum class EDP_TooltipAnchorSide : uint8
{
	Above,
	Below,
	Left,
	Right
};
