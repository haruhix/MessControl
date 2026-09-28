#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsControlComponent.h"
#include "MCToothMeshComponent.generated.h"

/** Capsule movement advances the animation frame; balance motors move simulated bodies. */
UCLASS()
class MESSCONTROL_API UMCToothMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction) override
    {
        // This procedural pose has no root-motion montage or network-timed notifies.
        // Evaluate after the contact component, including on the owning client.
        bOnlyAllowAutonomousTickPose=false;
        Super::TickComponent(Dt,Type,TickFunction);
    }
protected:
    FVector PreviousWorldLocation=FVector::ZeroVector;
    bool bHasPreviousWorldLocation=false;
    virtual void OnUpdateTransform(EUpdateTransformFlags Flags,ETeleportType Teleport=ETeleportType::None) override
    {
        const FVector Position=GetComponentLocation();
        // Replicated capsule corrections propagate to children without a teleport
        // flag. Move the simulated skeleton with a large correction, rather than
        // leaving its balance motor to drag the body across the whole arena.
        if (bHasPreviousWorldLocation && PhysicsTransformUpdateMode==EPhysicsTransformUpdateMode::ComponentTransformIsKinematic
            && FVector::DistSquared(Position,PreviousWorldLocation)>FMath::Square(200.0))
        {
            Teleport=ETeleportType::TeleportPhysics;
            Flags=EUpdateTransformFlags(uint32(Flags)&~uint32(EUpdateTransformFlags::SkipPhysicsUpdate));
        }
        PreviousWorldLocation=Position; bHasPreviousWorldLocation=true;
        Super::OnUpdateTransform(Flags,Teleport);
    }
    virtual void BeginPlay() override
    {
        Super::BeginPlay();
        // Grip moves our animation tick after Chaos. Physics blending must also
        // wait for that new pose, otherwise it publishes last frame's hand pose.
        EndPhysicsTickFunction.AddPrerequisite(this,PrimaryComponentTick);
        // PhysicsControl caches the editable animation buffer. Let it read the
        // fresh target before physics blending swaps/finalizes those buffers.
        if (auto* Muscles=GetOwner()->FindComponentByClass<UPhysicsControlComponent>())
            EndPhysicsTickFunction.AddPrerequisite(Muscles,Muscles->PrimaryComponentTick);
    }
    virtual bool MoveComponentImpl(const FVector& Delta,const FQuat& Rotation,bool bSweep,FHitResult* Hit,EMoveComponentFlags Flags,ETeleportType Teleport) override
    {
        if (PhysicsTransformUpdateMode==EPhysicsTransformUpdateMode::ComponentTransformIsKinematic && Teleport==ETeleportType::None)
            Flags|=MOVECOMP_SkipPhysicsMove;
        return Super::MoveComponentImpl(Delta,Rotation,bSweep,Hit,Flags,Teleport);
    }
};
