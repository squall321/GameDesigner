// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/HUD_InputContextSubsystem.h"

#include "Input/HUD_InputActionMapDataAsset.h"
#include "Settings/HUD_DeveloperSettings.h"
#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"

#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"

// Platform input device seam (soft dependency — used only to tag emitted intents with the device class).
#include "Input/UPlat_InputRouterSubsystem.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4 and in CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UHUD_InputContextSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplyDefaultsFromSettings();
	UE_LOG(LogDP, Log, TEXT("HUD_InputContextSubsystem initialized for LP '%s'."),
		*GetNameSafe(GetLocalPlayer()));
}

void UHUD_InputContextSubsystem::Deinitialize()
{
	// Tear down every active layer (remove contexts + unbind actions) before shutdown.
	TArray<FGameplayTag> Layers;
	ActiveLayers.GetKeys(Layers);
	for (const FGameplayTag& Layer : Layers)
	{
		RemoveContext(Layer);
	}
	ActiveLayers.Empty();
	ActionMap = nullptr;

	Super::Deinitialize();
}

void UHUD_InputContextSubsystem::ApplyDefaultsFromSettings()
{
	const UHUD_DeveloperSettings* Settings = UHUD_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	if (!Settings->DefaultInputActionMap.IsNull())
	{
		// Synchronous load of the configured default map (small data asset; happens once on init).
		if (UHUD_InputActionMapDataAsset* Map = Settings->DefaultInputActionMap.LoadSynchronous())
		{
			SetActionMap(Map);
		}
	}

	for (const FGameplayTag& Layer : Settings->DefaultActiveLayers)
	{
		AddContext(Layer);
	}
}

void UHUD_InputContextSubsystem::SetActionMap(UHUD_InputActionMapDataAsset* InMap)
{
	// Remove any active layers first — they are re-resolvable against the new map by tag.
	TArray<FGameplayTag> WasActive;
	ActiveLayers.GetKeys(WasActive);
	for (const FGameplayTag& Layer : WasActive)
	{
		RemoveContext(Layer);
	}

	ActionMap = InMap;

	// Re-activate previously-active layers that still exist in the new map.
	if (ActionMap)
	{
		for (const FGameplayTag& Layer : WasActive)
		{
			if (ActionMap->FindLayer(Layer))
			{
				AddContext(Layer);
			}
		}
	}
}

UEnhancedInputLocalPlayerSubsystem* UHUD_InputContextSubsystem::GetEnhancedInputSubsystem() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	return LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

UEnhancedInputComponent* UHUD_InputContextSubsystem::GetEnhancedInputComponent() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	if (!LP)
	{
		return nullptr;
	}
	if (const APlayerController* PC = LP->GetPlayerController(LP->GetWorld()))
	{
		return Cast<UEnhancedInputComponent>(PC->InputComponent);
	}
	return nullptr;
}

bool UHUD_InputContextSubsystem::AddContext(FGameplayTag LayerTag)
{
	if (!LayerTag.IsValid() || ActiveLayers.Contains(LayerTag))
	{
		return false; // unknown/invalid tag or already active (idempotent)
	}

	if (!ActionMap)
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_InputContext::AddContext('%s'): no action map set."),
			*LayerTag.ToString());
		return false;
	}

	const FHUD_InputContextLayer* LayerDef = ActionMap->FindLayer(LayerTag);
	if (!LayerDef)
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_InputContext::AddContext: layer '%s' not in action map."),
			*LayerTag.ToString());
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* EnhancedInput = GetEnhancedInputSubsystem();
	if (!EnhancedInput)
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_InputContext::AddContext: no EnhancedInput subsystem for LP."));
		return false;
	}

	FActiveLayer Active;

	// Add the mapping context at the layer's configured priority (higher masks lower).
	if (UInputMappingContext* IMC = LayerDef->MappingContext.LoadSynchronous())
	{
		EnhancedInput->AddMappingContext(IMC, LayerDef->Priority);
		Active.MappingContext = IMC;
	}

	// Bind this layer's action->intent routes on the player's Enhanced-Input component.
	if (UEnhancedInputComponent* InputComp = GetEnhancedInputComponent())
	{
		for (const FHUD_ActionIntentBinding& Binding : LayerDef->Bindings)
		{
			if (!Binding.Action || !Binding.IntentTag.IsValid())
			{
				continue;
			}
			const ETriggerEvent Trigger = Binding.bOnStartedOnly ? ETriggerEvent::Started : ETriggerEvent::Triggered;
			const uint32 Handle = BindActionRoute(InputComp, Binding.Action, Trigger, Binding.IntentTag);
			if (Handle != 0)
			{
				Active.BindingHandles.Add(Handle);
			}
		}
	}
	else
	{
		UE_LOG(LogDP, Verbose,
			TEXT("HUD_InputContext::AddContext('%s'): no EnhancedInputComponent; context added, no action routing."),
			*LayerTag.ToString());
	}

	ActiveLayers.Add(LayerTag, MoveTemp(Active));
	UE_LOG(LogDP, Verbose, TEXT("HUD_InputContext: added layer '%s' (priority=%d)."),
		*LayerTag.ToString(), LayerDef->Priority);
	return true;
}

uint32 UHUD_InputContextSubsystem::BindActionRoute(UEnhancedInputComponent* InputComp, UInputAction* Action,
	ETriggerEvent TriggerEvent, FGameplayTag IntentTag)
{
	if (!InputComp || !Action)
	{
		return 0;
	}
	// Bind a lambda that forwards the triggered action to PublishIntent with the captured intent tag.
	const FEnhancedInputActionEventBinding& Bound = InputComp->BindAction(Action, TriggerEvent, this,
		&UHUD_InputContextSubsystem::HandleActionTriggered, IntentTag);
	return Bound.GetHandle();
}

bool UHUD_InputContextSubsystem::RemoveContext(FGameplayTag LayerTag)
{
	FActiveLayer Active;
	if (!ActiveLayers.RemoveAndCopyValue(LayerTag, Active))
	{
		return false; // not active / unknown
	}

	if (UEnhancedInputLocalPlayerSubsystem* EnhancedInput = GetEnhancedInputSubsystem())
	{
		if (UInputMappingContext* IMC = Active.MappingContext.Get())
		{
			EnhancedInput->RemoveMappingContext(IMC);
		}
	}

	if (UEnhancedInputComponent* InputComp = GetEnhancedInputComponent())
	{
		for (const uint32 Handle : Active.BindingHandles)
		{
			InputComp->RemoveBindingByHandle(Handle);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("HUD_InputContext: removed layer '%s'."), *LayerTag.ToString());
	return true;
}

void UHUD_InputContextSubsystem::HandleActionTriggered(const FInputActionInstance& Instance, FGameplayTag IntentTag)
{
	// Reduce the action value to a scalar magnitude (1.0 for digital buttons; axis magnitude for analog).
	const float Magnitude = static_cast<float>(Instance.GetValue().GetMagnitude());
	PublishIntent(IntentTag, Magnitude);
}

int32 UHUD_InputContextSubsystem::ResolveCurrentDeviceClass() const
{
	// Soft integration with the Platform input device seam: resolve the router subsystem; if the Platform
	// module is absent/early this returns -1 and intents still publish without a device hint.
	if (const UPlat_InputRouterSubsystem* Router =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UPlat_InputRouterSubsystem>(this))
	{
		return static_cast<int32>(Router->GetCurrentInputDevice());
	}
	return -1;
}

void UHUD_InputContextSubsystem::PublishIntent(FGameplayTag IntentTag, float AnalogValue)
{
	if (!IntentTag.IsValid())
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FHUD_InputIntentPayload Payload;
	Payload.IntentTag = IntentTag;
	Payload.AnalogValue = AnalogValue;
	Payload.DeviceClass = ResolveCurrentDeviceClass();

	// Broadcast on the intent's own tag so listeners can subscribe narrowly; the local player owns this
	// subsystem, so it is a valid instigator for the message.
	Bus->BroadcastPayload(IntentTag, FInstancedStruct::Make(Payload), this);

	UE_LOG(LogDP, VeryVerbose, TEXT("HUD_InputContext: published intent '%s' (analog=%.2f, device=%d)."),
		*IntentTag.ToString(), AnalogValue, Payload.DeviceClass);
}
