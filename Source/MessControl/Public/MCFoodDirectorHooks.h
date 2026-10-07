#pragma once

#include "CoreMinimal.h"

class UWorld;
class UMCDayPlan;
class AMCFoodActor;

/** Model estimates for admission, not measured player throughput or a physics forecast. */
struct MESSCONTROL_API FMCFoodPipelineTuning
{
    float KnifeDamage=25.f;
    float PickaxeDamage=40.f;
    float KnifeSwingSeconds=.70f;
    float PickaxeSwingSeconds=.65f;
    float PickupPieceSeconds=.275f;
    float DeliveryTripSeconds=.4f;
    float CarrySpeed=340.f;
    float PepperCarrySpeed=288.57f;
    float PathFactor=1.25f;
    float EffectiveWorkFraction=.84f;
    int32 StackCapacity=6;
};

/** Whole/Fragments describe ordinary food; Carried/Swallowing/Stuck overlap those counts.
 * OutstandingActors counts live ingredients, including spicy/foreign objects, but not tools.
 * Disposed fracture parents are ignored. UnresolvedHazards counts disposed pepper sources
 * whose fire/lesion is still active; those consequences are budgeted separately by Director.
 */
struct MESSCONTROL_API FMCFoodPipelineLoad
{
    int32 Whole=0;
    int32 Fragments=0;
    int32 Carried=0;
    int32 Swallowing=0;
    int32 Stuck=0;
    int32 Spicy=0;
    int32 UnresolvedHazards=0;
    int32 OutstandingActors=0;
    float EstimatedWorkerSeconds=0;
    int32 EstimatedTrips=0;
    // Mouth counts/reservations are world-wide even when ingredients are filtered by Batch.
    int32 ThroatQueued=0;
    bool bThroatMovement=false;
    bool bThroatBusy=false;
};

/** One call attempts one explicit whole item. A failed attempt leaves no issued work.
 * Requires a valid menu row, authored tongue footprint and mouth-entry trajectory.
 * The caller owns admission, row selection, Batch lineage and the seeded random stream.
 */
MESSCONTROL_API AMCFoodActor* MCSpawnDirectedFoodEntry(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,int32 Batch,FRandomStream& Random);

/** One whole item rests against an available tooth on valid tongue tissue. The 30/70
 * band is selected once; an unavailable band leaves no issued work. No entry flight.
 */
MESSCONTROL_API AMCFoodActor* MCSpawnDirectedStuckFood(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,int32 Batch,FRandomStream& Random);

/** Conservative work for one configured row anywhere in the legal landing bands.
 * Uses the same cutting, pickup and stack transport model as the actual pipeline.
 * Missing menu, tongue or matching delivery exit returns MAX_flt (cannot admit).
 * Ordinary whole food is assumed to require cutting, including small variants.
 */
MESSCONTROL_API float MCForecastDirectedFoodWork(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,bool bStuck=false,const FMCFoodPipelineTuning& Tuning=FMCFoodPipelineTuning());

/** Authority-only, observational snapshot; never blocks intake, changes input or moves food.
 * Delivery effort groups ordinary loose/future pieces into possible mixed stacks. It ignores
 * exact pickup routes, collision and team assignment; tune against runtime/player telemetry.
 */
MESSCONTROL_API FMCFoodPipelineLoad MCMeasureFoodPipeline(UWorld* World,int32 Batch=INDEX_NONE,
    const FMCFoodPipelineTuning& Tuning=FMCFoodPipelineTuning());
