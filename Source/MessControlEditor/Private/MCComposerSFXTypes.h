#pragma once
#include "CoreMinimal.h"
#include "MCVFXSourceContracts.h"
#include "UObject/SoftObjectPath.h"

/** Actual original-source evidence supplied by a provider, never guessed from a palette label. */
struct FMCComposerOriginalSFXSource
{
    FName ID,NativeEvent;
    FGuid GroupID;
    FText Label;
    FSoftObjectPath Sound;
    FString SourceOwner,SourceProperty;
    bool bRuntimeHookMapped=false;
};
struct FMCComposerSFXCapabilities
{
    EMCVFXOriginalAudioKnowledge OriginalAudioKnowledge=EMCVFXOriginalAudioKnowledge::Unknown;
    bool bOriginalSourceKnown=false;
    bool bOriginalAudioHookMapped=false;
    /** Audited absence, distinct from an original source or suppression hook. */
    bool bOriginalAudioConfirmedSilent=false;
    FString OriginalAudioSilenceEvidence;
    bool bCanReplaceNativeAudio=false;
    bool bCanAuditionInContext=false;
    FString UnavailableReason;
    TArray<FMCComposerOriginalSFXSource> Originals;
};
