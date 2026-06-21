// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Weather/WS_WeatherStateDataAsset.h"

UWS_WeatherStateDataAsset::UWS_WeatherStateDataAsset()
{
	// Field initializers in the header carry the cosmetic defaults; nothing to compute here.
}

void UWS_WeatherStateDataAsset::PostLoad()
{
	Super::PostLoad();

	// The data registry indexes by the base DataTag. Designers author identity as StateTag (which is
	// constrained to WS.Weather.State), so mirror it onto DataTag here when DataTag was left unset. We
	// never clobber a deliberately-authored DataTag.
	if (!DataTag.IsValid() && StateTag.IsValid())
	{
		DataTag = StateTag;
	}
}

#if WITH_EDITOR
void UWS_WeatherStateDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UWS_WeatherStateDataAsset, StateTag))
	{
		// Keep the registry key in lockstep with the authored identity while editing.
		DataTag = StateTag;
	}
}
#endif
