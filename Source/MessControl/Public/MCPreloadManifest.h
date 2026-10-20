#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Visual dependencies of the current world and its authored DA/DT roots; never an asset-registry scan. */
struct MESSCONTROL_API FMCPreloadManifest
{
    static void Collect(UWorld* World, TArray<FSoftObjectPath>& OutPaths);
    /** Includes nested row structs/containers, loaded hard data assets and class-default subobjects. */
    static void CollectSoftReferences(UObject* Object, TArray<FSoftObjectPath>& OutPaths);
};
