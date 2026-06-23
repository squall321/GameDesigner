// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "DragDrop/DPDragDropOperation.h"
#include "DPDropZone.generated.h"

/**
 * UInterface boilerplate for IDP_DropZone. Implement on any UUserWidget subclass that should be a
 * valid drop target for the generic drag-drop framework.
 */
UINTERFACE(BlueprintType, MinimalAPI, meta = (DisplayName = "DP Drop Zone"))
class UDP_DropZone : public UInterface
{
	GENERATED_BODY()
};

/**
 * Tag-driven, data-authored drop-zone validation.
 *
 * A drop zone declares the set of payload-type tags it accepts and validates an incoming payload
 * against them. When a drop is accepted, OnAcceptPayload runs the zone-specific reaction (which
 * should ultimately PublishIntent on the bus for any authoritative change — never mutate gameplay
 * state directly from UI). The result is reported via the operation's NotifyDropResult so the drag
 * source and the drop target stay decoupled.
 *
 * All three are BlueprintNativeEvents with sane defaults so simple zones can be authored purely in
 * Blueprint while C++ zones override the *_Implementation.
 */
class DESIGNPATTERNSUI_API IDP_DropZone
{
	GENERATED_BODY()

public:
	/**
	 * Return true if this zone can accept Payload. The default implementation accepts a payload
	 * whose PayloadType matches (exactly or as a child of) any tag returned by GetAcceptedPayloadTypes.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|DragDrop")
	bool CanAcceptPayload(const FDP_DragPayload& Payload) const;
	virtual bool CanAcceptPayload_Implementation(const FDP_DragPayload& Payload) const;

	/**
	 * React to an accepted drop. Default is a no-op; override to PublishIntent / update local UI.
	 * Only called after CanAcceptPayload returned true.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|DragDrop")
	void OnAcceptPayload(const FDP_DragPayload& Payload);
	virtual void OnAcceptPayload_Implementation(const FDP_DragPayload& /*Payload*/) {}

	/** The set of payload-type tags this zone accepts (matched hierarchically). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|DragDrop")
	FGameplayTagContainer GetAcceptedPayloadTypes() const;
	virtual FGameplayTagContainer GetAcceptedPayloadTypes_Implementation() const { return FGameplayTagContainer(); }
};
