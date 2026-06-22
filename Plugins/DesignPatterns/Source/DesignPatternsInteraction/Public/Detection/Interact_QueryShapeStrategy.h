// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_Types.h"
#include "Interact_QueryShapeStrategy.generated.h"

class AActor;

/**
 * BROAD-PHASE candidate gatherer (Strategy pattern), authored as an inline-instanced subobject on the
 * interactor exactly like UInteract_FocusStrategy.
 *
 * IMPORTANT DESIGN CONTRACT: a query-shape strategy gathers RAW candidate actors for a query shape
 * (sphere / line / cone / cursor) and nothing more. The interactor then runs its EXISTING private
 * per-candidate scoring (distance / angle / LOS / Priority) on the returned actors, so client and
 * server compute identical FInteract_Candidate metadata regardless of which shape was used. A null
 * QueryShape member on the component ⇒ the component runs today's private DetectCandidates sphere
 * path unchanged (full backward parity).
 *
 * EditInlineNew + Abstract so designers swap the concrete shape in the component details panel.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories)
class DESIGNPATTERNSINTERACTION_API UInteract_QueryShapeStrategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Gather up to MaxCandidates raw candidate actors for the query shape. Implementations must NOT
	 * score or filter for interactability beyond the cheap shape test — that is the interactor's job.
	 * Output is de-duplicated by the caller. Treat all inputs as read-only.
	 *
	 * @param WorldContext  Any object with a valid world (the interactor component).
	 * @param Query         The view location/direction and instigator the shape is cast from.
	 * @param Params        Detection tunables (range, cone, channel) the shape may use.
	 * @param MaxCandidates Upper bound on the number of actors to return (cost cap).
	 * @param OutActors     Filled with weak refs to the gathered actors (cleared first).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact|QueryShape")
	void GatherCandidateActors(
		const UObject* WorldContext,
		const FInteract_Query& Query,
		const FInteract_DetectionParams& Params,
		int32 MaxCandidates,
		TArray<TWeakObjectPtr<AActor>>& OutActors) const;
	virtual void GatherCandidateActors_Implementation(
		const UObject* WorldContext,
		const FInteract_Query& Query,
		const FInteract_DetectionParams& Params,
		int32 MaxCandidates,
		TArray<TWeakObjectPtr<AActor>>& OutActors) const;

protected:
	/** Resolve the world from the context object, or null. Shared helper for concrete shapes. */
	static const UWorld* ResolveWorld(const UObject* WorldContext);

	/** Resolve the instigator actor (to ignore in traces), or null. */
	static AActor* ResolveInstigator(const FInteract_Query& Query);
};

/**
 * Reproduces the shipped sphere overlap broad-phase: an OverlapMultiByChannel sphere of Params.Range
 * around the view location on Params.Channel. This is the default-parity shape (matches today's
 * private DetectCandidates gathering before per-candidate scoring).
 */
UCLASS(meta = (DisplayName = "Query Shape: Sphere"))
class DESIGNPATTERNSINTERACTION_API UInteract_QueryShape_Sphere : public UInteract_QueryShapeStrategy
{
	GENERATED_BODY()

public:
	virtual void GatherCandidateActors_Implementation(
		const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
		int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const override;
};

/**
 * Crosshair line broad-phase: a forward sphere-sweep (LineTrace with a small radius) from the view
 * location along the view direction up to Params.Range, returning each actor the sweep touches.
 * Suited to precise "look-at to interact" first-person flows.
 */
UCLASS(meta = (DisplayName = "Query Shape: Line"))
class DESIGNPATTERNSINTERACTION_API UInteract_QueryShape_Line : public UInteract_QueryShapeStrategy
{
	GENERATED_BODY()

public:
	/** Radius (cm) of the forward sweep; 0 = a thin line trace. Tunable, not a magic constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|QueryShape", meta = (ClampMin = "0.0", Units = "cm"))
	float TraceRadius = 8.f;

	virtual void GatherCandidateActors_Implementation(
		const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
		int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const override;
};

/**
 * Cone broad-phase: a sphere overlap narrowed by Params.ConeHalfAngleDeg around the view direction.
 * Equivalent to the sphere shape pre-filtered to the acceptance cone (cheap angular cull before the
 * interactor's full scoring), so dense scenes return fewer raw candidates.
 */
UCLASS(meta = (DisplayName = "Query Shape: Cone"))
class DESIGNPATTERNSINTERACTION_API UInteract_QueryShape_Cone : public UInteract_QueryShapeStrategy
{
	GENERATED_BODY()

public:
	virtual void GatherCandidateActors_Implementation(
		const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
		int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const override;
};

/**
 * Widget/world-under-cursor broad-phase for cursor-driven games. Deprojects the LOCAL player
 * controller's mouse position to a world ray and traces it, returning the hit actor.
 *
 * LOCAL ONLY: this resolves the instigator's controller's cursor, so it is meaningful only on the
 * owning client. The server NEVER uses this shape to authorise an interaction — a cursor pick is
 * routed through UInteract_InteractorComponent::ServerInteractAt(verb, targetHandle), which
 * re-validates reachability server-side, so the cursor can never bypass authority.
 */
UCLASS(meta = (DisplayName = "Query Shape: Widget Under Cursor"))
class DESIGNPATTERNSINTERACTION_API UInteract_QueryShape_WidgetUnderCursor : public UInteract_QueryShapeStrategy
{
	GENERATED_BODY()

public:
	/** Trace channel for the cursor deprojection ray (defaults to the detection channel when unset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|QueryShape")
	FGameplayTag CursorChannel;

	/** Maximum distance (cm) the cursor ray is traced. Tunable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|QueryShape", meta = (ClampMin = "0.0", Units = "cm"))
	float CursorTraceDistance = 5000.f;

	virtual void GatherCandidateActors_Implementation(
		const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
		int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const override;
};
