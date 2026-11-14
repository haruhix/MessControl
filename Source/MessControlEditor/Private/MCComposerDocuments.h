#pragma once

#include "CoreMinimal.h"

class FMCVFXActionSession;
struct FMCVFXCanonicalProfileState;

/** Editor-lifetime documents. View navigation never reinitializes or discards a draft. */
namespace MCComposerDocuments
{
    TSharedPtr<FMCVFXActionSession> Acquire(FName ActionID,FString& OutError);
    /** Read only. Does not initialize a document, load assets or change navigation/preview. */
    TSharedPtr<FMCVFXActionSession> Find(FName ActionID);
    /** Inspection includes drafts for actions currently hidden in both windows. */
    FString GetSnapshotJson();
    /** A known successful owner write may refresh unchanged siblings' owner-file digest only. */
    void NotifyOwnedCommit(const FMCVFXCanonicalProfileState& ExpectedBefore);
    void Shutdown();
}
