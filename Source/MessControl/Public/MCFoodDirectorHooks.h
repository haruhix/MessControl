#pragma once

#include "CoreMinimal.h"

class UWorld;
class UMCDayPlan;
class AMCFoodActor;
class AMCDayDirector;

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
    /** Read current collection limits, tool perks and movement from living workers. */
    bool bUseLivePlayerStats=true;
    float CleaningSpeedMultiplier=1.f;
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
    int32 EstimatedTrips=0; // Spicy/foreign disposal only; ordinary food needs no trip.
    int32 TeamStackCapacity=0; // Collection diagnostics, not ordinary destruction admission.
    int32 FreeStackSlots=0;
    int32 FullStacks=0;
    // Mouth counts/reservations are world-wide even when ingredients are filtered by Batch.
    int32 ThroatQueued=0;
    bool bThroatMovement=false;
    bool bThroatBusy=false;
};

/** One call attempts one explicit whole item plus its ordinary tongue stain. A failed attempt leaves no issued work.
 * Requires a valid menu row, authored tongue footprint and mouth-entry trajectory.
 * The caller owns admission, row selection, Batch lineage and the seeded random stream.
 * Ordinary food requires a matching mechanics executor (explicit, or unambiguous in-world).
 */
MESSCONTROL_API AMCFoodActor* MCSpawnDirectedFoodEntry(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,int32 Batch,FRandomStream& Random,AMCDayDirector* DirtServices=nullptr);

/** One whole item rests against an available tooth on valid tongue tissue. The 30/70
 * band is selected once; an unavailable band leaves no issued work. No entry flight.
 */
MESSCONTROL_API AMCFoodActor* MCSpawnDirectedStuckFood(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,int32 Batch,FRandomStream& Random,AMCDayDirector* DirtServices=nullptr);

/** Ordinary portions commit one existing-mechanics tongue stain in the same batch.
 * Failed portions leave no stain. Queries exclude ulcers and already-clean stains.
 */
MESSCONTROL_API int32 MCCountDirectedFoodDirt(UWorld* World,int32 Batch);
MESSCONTROL_API void MCDestroyDirectedFoodDirt(const AMCFoodActor* Food);
MESSCONTROL_API void MCDestroyDirectedFoodDirt(UWorld* World,int32 Batch);
MESSCONTROL_API FMCFoodPipelineTuning MCResolveFoodPipelineTuning(UWorld* World,
    const FMCFoodPipelineTuning& Tuning=FMCFoodPipelineTuning());

/** Conservative work for one configured row anywhere in the legal landing bands, including its ordinary stain.
 * Ordinary food budgets approach and tool damage until destruction: no fragments, pickup or delivery trips.
 * Spicy/foreign objects retain their disposal route. Missing menu/tongue, or a missing
 * disposal exit for those hazards, returns MAX_flt (cannot admit).
 */
MESSCONTROL_API float MCForecastDirectedFoodWork(UWorld* World,const UMCDayPlan* Plan,
    FName RowName,bool bStuck=false,const FMCFoodPipelineTuning& Tuning=FMCFoodPipelineTuning(),
    float CleaningWorkerSeconds=8.f);

/** Authority-only, observational snapshot; never blocks intake, changes input or moves food.
 * Ordinary food budgets remaining HP, real personal tools and approach to its visible bounds;
 * collection capacity is diagnostic and does not imply future fragment/delivery work.
 * Automatic swallowing/absorption remains outstanding without active destruction effort.
 * Hazard transport retains its disposal route. Exact collision/navigation/team assignment
 * is not predicted; tune against runtime/player telemetry.
 */
MESSCONTROL_API FMCFoodPipelineLoad MCMeasureFoodPipeline(UWorld* World,int32 Batch=INDEX_NONE,
    const FMCFoodPipelineTuning& Tuning=FMCFoodPipelineTuning());
