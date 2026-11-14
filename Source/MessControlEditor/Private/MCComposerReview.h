#pragma once

#include "CoreMinimal.h"

class FMCVFXActionSession;

enum class EMCComposerReviewLane : uint8 { VFX, SFX };

struct FMCComposerReviewStatus
{
    bool bApproved=false;
    bool bStale=false;
    bool bCurrentKnown=false;
    /** Readable persisted/current-state explanation; suitable for a tooltip. */
    FString Detail;
};

/** Small file-backed metadata store; isolated instances support non-production validation. */
class FMCComposerReviewStore
{
public:
    explicit FMCComposerReviewStore(FString StoragePath);
    ~FMCComposerReviewStore();
    FMCComposerReviewStatus Query(FName Action,FName Event,EMCComposerReviewLane Lane,const FString& CurrentFingerprint);
    bool SetApproved(FName Action,FName Event,EMCComposerReviewLane Lane,bool bApproved,const FString& CurrentFingerprint,FString& OutError);
    void Reload();
    FString GetSnapshotJson();
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

/** Editor-only review metadata. Never writes a gameplay profile or starts preview. */
namespace MCComposerReview
{
    FMCComposerReviewStatus Query(FName Action,FName Event,EMCComposerReviewLane Lane,const FMCVFXActionSession* Document=nullptr);
    /** Only an explicit approval click may acquire the target document and persist a mark. */
    bool SetApproved(FName Action,FName Event,EMCComposerReviewLane Lane,bool bApproved,FString& OutError);
    FString GetStoragePath();
    FString GetSnapshotJson();
    /** Pure authoring values of one event/lane. No asset loads, graph layout or other lane. */
    FString Fingerprint(const FMCVFXActionSession& Document,FName Event,EMCComposerReviewLane Lane);
    /** Discard only the metadata cache and reread the persisted review file. */
    void Reload();
}
