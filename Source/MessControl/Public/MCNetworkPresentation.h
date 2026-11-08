#pragma once
#include "CoreMinimal.h"
#include "SharedMCNetworkFrames.h"
class USkeletalMeshComponent;
class USceneComponent;

/** One local correction transaction shared by all native CMC positional responses. */
class MESSCONTROL_API FMCOwnerReconciliationPresentation
{
public:
    bool Begin(USkeletalMeshComponent* Mesh,const FMCNetworkCorrectionContext& Context);
    USkeletalMeshComponent* BeginScope();
    void EndScope(bool bAwaitingReplay,double Now);
    void Tick(double Now);
    void Release();
    bool IsOwned() const;
    bool IsAwaitingReplay() const {return bAwaitingReplay;}
    FVector Offset() const {return IsOwned()?WorldOffset:FVector::ZeroVector;}
private:
    TWeakObjectPtr<USkeletalMeshComponent> VisualMesh;
    TWeakObjectPtr<USceneComponent> Parent;
    FName Socket;
    FMCNetworkCorrectionContext Context;
    FTransform BaseRelative,LastWritten,ScopeWorld;
    FVector WorldOffset=FVector::ZeroVector,StartOffset=FVector::ZeroVector,TransactionOffset=FVector::ZeroVector;
    FQuat WorldRotationOffset=FQuat::Identity,StartRotationOffset=FQuat::Identity,TransactionRotationOffset=FQuat::Identity;
    double StartedAt=0,LastTickAt=-DBL_MAX;
    float Duration=.12f;
    bool bActive=false,bScopeOpen=false,bAwaitingReplay=false,bTransactionOpen=false,bHasDecay=false;
    bool bAbsoluteRotation=false,bAbsoluteScale=false;
    void WriteOffset(FVector OffsetValue,FQuat RotationOffsetValue);
};
