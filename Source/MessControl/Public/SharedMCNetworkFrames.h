#pragma once
#include "CoreMinimal.h"
class UWorld;

namespace MCNetworkFrames
{
/** Shared saved/gameplay loading epoch; invalid before the GameState publishes one. */
MESSCONTROL_API FGuid WorldLoadEpoch(const UWorld* World);
}

/** Who owns canonical movement. This is not a second movement solver. */
enum class EMCPresentationOwner : uint8 { CMC, WorldAnchoredCMC, BodyFrame, Transit };
enum class EMCNetworkCorrectionDecision : uint8 { Ignore, Reconcile, SnapAndReset };

/** A server snapshot at the native acknowledged move boundary. No render data. */
struct MESSCONTROL_API FMCNetworkCorrectionContext
{
    FGuid LoadEpoch;
    uint32 OwnerRevision=0,ResetRevision=0;
    EMCPresentationOwner Owner=EMCPresentationOwner::CMC;
    bool IsWellFormed() const {return LoadEpoch.IsValid() && OwnerRevision && ResetRevision && uint8(Owner)<=uint8(EMCPresentationOwner::Transit);}
    bool Equals(const FMCNetworkCorrectionContext& Other) const
    {return LoadEpoch==Other.LoadEpoch && OwnerRevision==Other.OwnerRevision && ResetRevision==Other.ResetRevision && Owner==Other.Owner;}
    bool CanAdvanceFrom(const FMCNetworkCorrectionContext& Previous,const FGuid& ExpectedEpoch) const
    {
        if(!IsWellFormed() || (ExpectedEpoch.IsValid() && LoadEpoch!=ExpectedEpoch)) return false;
        if(!Previous.IsWellFormed()) return true;
        if(LoadEpoch!=Previous.LoadEpoch) return ExpectedEpoch.IsValid() && LoadEpoch==ExpectedEpoch;
        return int32(OwnerRevision-Previous.OwnerRevision)>=0 && int32(ResetRevision-Previous.ResetRevision)>=0
            && (OwnerRevision!=Previous.OwnerRevision || Owner==Previous.Owner);
    }
    bool Serialize(FArchive& Ar)
    {
        uint8 Kind=uint8(Owner);Ar<<LoadEpoch;Ar.SerializeIntPacked(OwnerRevision);Ar.SerializeIntPacked(ResetRevision);Ar.SerializeBits(&Kind,2);
        if(Ar.IsLoading()) Owner=EMCPresentationOwner(Kind);
        return !Ar.IsError() && IsWellFormed();
    }
};

/** Plain snapshots can be copied into animation proxies; getters run on the game thread. */
struct MESSCONTROL_API FMCActorSimulationFrame
{
    FGuid LoadEpoch;
    uint32 OwnerRevision=0,ResetRevision=0;
    EMCPresentationOwner Owner=EMCPresentationOwner::CMC;
    FTransform GameplayActor=FTransform::Identity;
    FVector PhysicalCenter=FVector::ZeroVector,CanonicalVelocity=FVector::ZeroVector;
    bool bHasCanonicalPhysicsSample=false;
    /** Server clock at the source sample. Native remote CMC does not expose it. */
    double SourceTime=0;
    bool bHasSourceTime=false;
    /** World-local game-thread capture time; never a physics sample timestamp. */
    double SimulationTime=0;
};
struct MESSCONTROL_API FMCActorPresentationFrame
{
    FGuid LoadEpoch;
    uint32 OwnerRevision=0,ResetRevision=0,FrameSerial=0;
    EMCPresentationOwner Owner=EMCPresentationOwner::CMC;
    FTransform GameplayActor=FTransform::Identity,PresentedActor=FTransform::Identity;
    double PresentationTime=0;
    FTransform FromActorLocal(const FTransform& Local) const {return Local*PresentedActor;}
    FTransform FromCanonicalWorld(const FTransform& World) const {return World.GetRelativeTransform(GameplayActor)*PresentedActor;}
    FTransform ToCanonicalWorld(const FTransform& World) const {return World.GetRelativeTransform(PresentedActor)*GameplayActor;}
    FVector FromCanonicalWorld(FVector World) const {return PresentedActor.TransformPosition(GameplayActor.InverseTransformPosition(World));}
    FVector ToCanonicalWorld(FVector World) const {return GameplayActor.TransformPosition(PresentedActor.InverseTransformPosition(World));}
};
