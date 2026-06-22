// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Platform/Seam_AppLifecycle.h"

// Fail-inert native defaults for the app-lifecycle seam. A provider that does not override these reports
// "never suspended" and ignores listener (un)registration, so a missing/half-built adapter simply yields
// no auto-pause. The real Platform adapter overrides all of these to forward the engine focus delegates.

void ISeam_LifecycleListener::OnAppSuspended_Implementation()
{
}

void ISeam_LifecycleListener::OnAppResumed_Implementation()
{
}

bool ISeam_AppLifecycle::IsSuspended_Implementation() const
{
	return false;
}

void ISeam_AppLifecycle::RegisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& /*Listener*/)
{
}

void ISeam_AppLifecycle::UnregisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& /*Listener*/)
{
}
