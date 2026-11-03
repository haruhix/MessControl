#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/** Real original resource, not a fabricated generic sound for an event. */
struct MESSCONTROL_API FMCVFXOriginalSFXContract
{
    FName ID;
    FText Label;
    FSoftObjectPath Resource;
    /** A real palette selector; None means Resource is the direct SoundBase. */
    FName PaletteEvent;
    bool bSuppressionWired=false;
};

/** Native source declaration is not a saved assignment or audible-output certificate. */
enum class EMCVFXOriginalAudioKnowledge : uint8 { Unknown, DeclaredSource, ConfirmedAbsent };
struct MESSCONTROL_API FMCVFXOriginalAudioStatus
{
    EMCVFXOriginalAudioKnowledge Knowledge=EMCVFXOriginalAudioKnowledge::Unknown;
    bool bIndependentNativeGate=false;
    FString Evidence;
};

/** Shared runtime/editor definition. Catalogue, native gates and providers read the same identity. */
struct MESSCONTROL_API FMCVFXSourceContract
{
    FName SourceID;
    FText Label;
    FName Category;
    FString SourceOwner;
    FString Description;
    FSoftObjectPath Library;
    TArray<FName> Events;
    TMap<FName,FMCVFXOriginalSFXContract> OriginalAudio;
};

namespace MCVFXSourceContracts
{
    MESSCONTROL_API void Register(FMCVFXSourceContract Contract);
    MESSCONTROL_API const FMCVFXSourceContract* Find(FName SourceID);
    MESSCONTROL_API TArray<FMCVFXSourceContract> GetAll();
    /** Audited absence is separate from a mapped SoundBase/palette source. Unknown
     * events remain false. Evidence is descriptive provenance, never a load path. */
    MESSCONTROL_API bool IsOriginalSFXConfirmedSilent(FName SourceID,FName Event,FString* Evidence=nullptr);
    /** One common classification for native policy and Composer. No load, inference
     * from missing mappings, or source-wide propagation to another event. */
    MESSCONTROL_API FMCVFXOriginalAudioStatus GetOriginalAudioStatus(FName SourceID,FName Event);
    /** True for a declared independently gated native source OR exact audited silence. */
    MESSCONTROL_API bool CanSuppressOriginalSFX(FName SourceID,FName Event);
    MESSCONTROL_API FSoftObjectPath WorldLibraryPath();
    MESSCONTROL_API FSoftObjectPath ToolLibraryPath();
    MESSCONTROL_API FSoftObjectPath HubLibraryPath();
    MESSCONTROL_API FSoftObjectPath InterfaceLibraryPath();
}
