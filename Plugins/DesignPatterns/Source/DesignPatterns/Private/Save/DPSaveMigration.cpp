// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/DPSaveMigration.h"
#include "Save/DPSaveGame.h"
#include "Core/DPLog.h"

void UDP_SaveMigration::RegisterStep(UDP_SaveMigrationStep* Step)
{
	if (!Step)
	{
		UE_LOG(LogDPSave, Warning, TEXT("RegisterStep ignored: null step."));
		return;
	}

	Steps.Add(Step);
	// Keep ascending by FromVersion so Migrate() can chain in a single forward pass.
	Steps.Sort([](const TObjectPtr<UDP_SaveMigrationStep>& A, const TObjectPtr<UDP_SaveMigrationStep>& B)
	{
		const int32 FromA = A ? A->GetFromVersion() : 0;
		const int32 FromB = B ? B->GetFromVersion() : 0;
		return FromA < FromB;
	});

	UE_LOG(LogDPSave, Verbose, TEXT("Registered save migration step %d -> %d (total=%d)."),
		Step->GetFromVersion(), Step->GetToVersion(), Steps.Num());
}

UDP_SaveMigrationStep* UDP_SaveMigration::RegisterStepClass(TSubclassOf<UDP_SaveMigrationStep> StepClass)
{
	if (!StepClass)
	{
		return nullptr;
	}
	UDP_SaveMigrationStep* Step = NewObject<UDP_SaveMigrationStep>(this, StepClass);
	RegisterStep(Step);
	return Step;
}

bool UDP_SaveMigration::Migrate(UDP_SaveGame* Save, int32 FromVersion, int32 ToVersion) const
{
	if (!Save)
	{
		UE_LOG(LogDPSave, Warning, TEXT("Migrate ignored: null save object."));
		return false;
	}
	if (FromVersion >= ToVersion)
	{
		return true; // already current
	}

	int32 Current = FromVersion;
	for (const TObjectPtr<UDP_SaveMigrationStep>& Step : Steps)
	{
		if (!Step)
		{
			continue;
		}
		const int32 StepFrom = Step->GetFromVersion();
		const int32 StepTo = Step->GetToVersion();

		// Apply the step that bridges exactly our current version forward.
		if (StepFrom == Current && StepTo > Current)
		{
			if (!Step->Apply(Save))
			{
				UE_LOG(LogDPSave, Error, TEXT("Migration step %d -> %d failed."), StepFrom, StepTo);
				return false;
			}
			UE_LOG(LogDPSave, Log, TEXT("Migrated save %d -> %d."), StepFrom, StepTo);
			Current = StepTo;

			if (Current >= ToVersion)
			{
				break;
			}
		}
	}

	if (Current < ToVersion)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("Migration incomplete: reached version %d but target is %d (missing step). "
			     "Falling back to the save object's own Migrate() hook for the remainder."),
			Current, ToVersion);
		// Not fatal: the subsystem still calls UDP_SaveGame::Migrate which can finish the job.
		return Current >= FromVersion;
	}

	return true;
}
