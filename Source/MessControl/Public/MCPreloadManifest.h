#pragma once

#include "CoreMinimal.h"

class UWorld;
class USoundBase;
struct FMCEventSFXSamplePlan;

/** Presentation dependencies of the current world and its authored DA/DT roots; never an asset-registry scan. */
struct MESSCONTROL_API FMCPreloadManifest
{
    static void Collect(UWorld* World, TArray<FSoftObjectPath>& OutPaths);
    /** Includes nested row structs/containers, loaded hard data assets and class-default subobjects. */
    static void CollectSoftReferences(UObject* Object, TArray<FSoftObjectPath>& OutPaths,UWorld* Location=nullptr);
    /** Resident-only resource closure validation, also used by Composer authoring before Apply. */
    static bool BuildEventAudioPlan(USoundBase* Source,FMCEventSFXSamplePlan& OutPlan,FString& Error);
    /** Explicit prebarrier transient WavePlayer cache preparation; authoring may opt into dependency loads. */
    static bool PrepareEventAudioPlan(USoundBase* Source,FMCEventSFXSamplePlan& OutPlan,FString& Error,bool& bWaiting,bool bAllowDependencyLoad=false);
};
