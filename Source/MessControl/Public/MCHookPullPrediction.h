#pragma once
#include "CoreMinimal.h"
#include "GameFramework/RootMotionSource.h"
#include "MCHookPullPrediction.generated.h"

/** Server receipt at the END of one acknowledged CMC move, not packet-send time. */
struct FMCHookPullReceipt
{
    FGuid ActionID,Epoch;
    uint32 Revision=0;
    bool bActive=false,bFreeFlight=false;
    uint8 PreviousMode=0,PreviousCustomMode=0;
    FVector Goal=FVector::ZeroVector;
    float Speed=850,StopDistance=120,Duration=2.5f,StartMoveTime=0,SourceTime=0;
    uint16 ServerSourceID=0;
    bool Serialize(FArchive& Ar);
    bool IsWellFormed() const;
};

/** Hook only. The accepted goal is read-only gameplay input; no collision/contact decisions. */
USTRUCT()
struct MESSCONTROL_API FMCHookPullRootMotionSource : public FRootMotionSource_ConstantForce
{
    GENERATED_BODY()
    FGuid ActionID,Epoch;
    uint32 Revision=0;
    FVector Goal=FVector::ZeroVector;
    float Speed=850,StopDistance=120;
    virtual FRootMotionSource* Clone() const override;
    virtual UScriptStruct* GetScriptStruct() const override;
    virtual bool Matches(const FRootMotionSource* Other) const override;
    virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;
    virtual bool UpdateStateFrom(const FRootMotionSource* Other,bool bCatchup=false) override;
    virtual void PrepareRootMotion(float SimulationTime,float MovementTickTime,const ACharacter& Character,
        const UCharacterMovementComponent& Movement) override;
    virtual bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& bSuccess) override;
};
template<> struct TStructOpsTypeTraits<FMCHookPullRootMotionSource> : TStructOpsTypeTraitsBase2<FMCHookPullRootMotionSource>
{enum {WithNetSerializer=true,WithCopy=true};};

namespace MCHookPullPrediction
{
    MESSCONTROL_API FName SourceName(const FGuid& ActionID);
    MESSCONTROL_API TSharedPtr<FMCHookPullRootMotionSource> MakeSource(const FMCHookPullReceipt& Receipt);
    /** History clones reuse a component-issued local ID. Never allocate a different ID per saved move. */
    MESSCONTROL_API void ReplaceHistory(FRootMotionSourceGroup& Group,const FMCHookPullReceipt& Receipt,
        uint16 LocalID,bool bAddActive);
    MESSCONTROL_API bool IsHook(const TSharedPtr<FRootMotionSource>& Source);
}
