// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/HUD_NotificationSource.h"

// Default implementations for the BlueprintNativeEvents so a C++ implementer can selectively
// override and a pure-Blueprint implementer still links. These are intentionally no-ops: a source
// that does not buffer nor need bind/unbind callbacks gets sensible empty behaviour.

void IHUD_NotificationSource::OnNotificationSinkBound_Implementation(UHUD_NotificationSubsystem* /*Sink*/)
{
}

void IHUD_NotificationSource::OnNotificationSinkUnbound_Implementation()
{
}

void IHUD_NotificationSource::DrainPendingNotifications_Implementation(TArray<FHUD_Notification>& /*OutPending*/)
{
}
