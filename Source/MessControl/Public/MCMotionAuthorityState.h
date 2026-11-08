#pragma once
#include "CoreMinimal.h"
#include "SharedMCNetworkFrames.h"
#include "MCMotionAuthorityState.generated.h"
class AActor;

enum class EMCMotionClaimKind : uint8 { None, Throat, Mimic, RaftSeat, WorldAnchor, Body };

/** Atomic owner/claim descriptor; unresolved source references keep the new owner pending. */
USTRUCT()
struct MESSCONTROL_API FMCMotionAuthorityState
{
    GENERATED_BODY()
    UPROPERTY() FGuid LoadEpoch;
    UPROPERTY() uint32 OwnerRevision=0;
    UPROPERTY() uint32 ResetRevision=0;
    UPROPERTY() uint8 OwnerKind=uint8(EMCPresentationOwner::CMC);
    UPROPERTY() uint8 ClaimKind=uint8(EMCMotionClaimKind::None);
    UPROPERTY() TObjectPtr<AActor> SourceActor;
    UPROPERTY() int32 SubIndex=INDEX_NONE;
    UPROPERTY() FVector ClaimOrigin=FVector::ZeroVector;
    FMCNetworkCorrectionContext Context() const
    {FMCNetworkCorrectionContext C;C.LoadEpoch=LoadEpoch;C.OwnerRevision=OwnerRevision;C.ResetRevision=ResetRevision;C.Owner=EMCPresentationOwner(OwnerKind);return C;}
    bool IsWellFormed() const
    {
        if(!Context().IsWellFormed() || ClaimKind>uint8(EMCMotionClaimKind::Body)) return false;
        const auto O=EMCPresentationOwner(OwnerKind);const auto K=EMCMotionClaimKind(ClaimKind);
        return (O==EMCPresentationOwner::CMC && K==EMCMotionClaimKind::None)
            || (O==EMCPresentationOwner::WorldAnchoredCMC && K==EMCMotionClaimKind::WorldAnchor)
            || (O==EMCPresentationOwner::BodyFrame && K==EMCMotionClaimKind::Body && SubIndex>=0 && SubIndex<=3)
            || (O==EMCPresentationOwner::Transit && (K==EMCMotionClaimKind::Throat || K==EMCMotionClaimKind::Mimic || (K==EMCMotionClaimKind::RaftSeat && SubIndex>=0)));
    }
};
