#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/** Editor evidence for a real native renderer resource; never an editable Niagara layer. */
enum class EMCVFXNativeSourceKind:uint8 {Niagara,Material,Geometry};
struct FMCVFXNativeSourcePart
{
    FName ID;
    FText Label;
    EMCVFXNativeSourceKind Kind=EMCVFXNativeSourceKind::Geometry;
    FSoftObjectPath Asset;
    /** Checked project-relative native generator source for procedural parts. */
    FString SourceFile;
};

namespace MCVFXNativeSources
{
    TArray<FMCVFXNativeSourcePart> DescribeKnightShield(FName Event);
    TArray<FMCVFXNativeSourcePart> DescribeMageFireball(FName Event);
    TArray<FMCVFXNativeSourcePart> DescribePassiveFeedback(FName SourceID,FName Event);
    FText KindLabel(EMCVFXNativeSourceKind Kind);
}
