#include "MCToothMovementComponent.h"
#include "MCCoffeeFlood.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCToothAnimInstance.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"

UMCToothMovementComponent::UMCToothMovementComponent()
{
    SetIsReplicatedByDefault(true);
    MaxSwimSpeed=320; GetNavAgentPropertiesRef().bCanSwim=true;
}
namespace
{
    class FMCStrideMove final : public FSavedMove_Character
    {
    public:
        using Super=FSavedMove_Character;
        bool Sprint=false,Climb=false;
        virtual void Clear() override { Super::Clear(); Sprint=Climb=false; }
        virtual uint8 GetCompressedFlags() const override { return Super::GetCompressedFlags() | (Sprint?FLAG_Custom_0:0) | (Climb?FLAG_Custom_1:0); }
        virtual bool CanCombineWith(const FSavedMovePtr& Move,ACharacter* Hero,float MaxDelta) const override
        { return Sprint==static_cast<const FMCStrideMove*>(Move.Get())->Sprint && Climb==static_cast<const FMCStrideMove*>(Move.Get())->Climb && Super::CanCombineWith(Move,Hero,MaxDelta); }
        virtual void SetMoveFor(ACharacter* Hero,float Dt,FVector const& Accel,FNetworkPredictionData_Client_Character& Data) override
        { Super::SetMoveFor(Hero,Dt,Accel,Data); const auto* M=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement()); Sprint=M->WantsToSprint(); Climb=M->WantsClimb(); }
        virtual void PrepMoveFor(ACharacter* Hero) override
        { Super::PrepMoveFor(Hero); auto* M=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement()); M->SetSprinting(Sprint); M->SetWantsClimb(Climb); }
    };
    class FMCStridePrediction final : public FNetworkPredictionData_Client_Character
    {
    public:
        explicit FMCStridePrediction(const UCharacterMovementComponent& Movement):FNetworkPredictionData_Client_Character(Movement) {}
        virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FMCStrideMove()); }
    };
}
FNetworkPredictionData_Client* UMCToothMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData) const_cast<UMCToothMovementComponent*>(this)->ClientPredictionData=new FMCStridePrediction(*this);
    return ClientPredictionData;
}
void UMCToothMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{ Super::UpdateFromCompressedFlags(Flags); bWantsToSprint=(Flags&FSavedMove_Character::FLAG_Custom_0)!=0; bWantsToClimb=(Flags&FSavedMove_Character::FLAG_Custom_1)!=0; }
void UMCToothMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,GroundSurface,COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,MovementIntent,COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,bSprintActive,COND_SimulatedOnly);
    DOREPLIFETIME(UMCToothMovementComponent,ClimbNormal);
}
bool UMCToothMovementComponent::HasHeavyGrip() const
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    return Hero && Hero->Grip && (Hero->Grip->GrabbedPlayer || (Hero->HeldFood && !Hero->Grip->CanCarry(Hero->HeldFood)));
}
bool UMCToothMovementComponent::CanSprint() const
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    return Hero && Hero->ToothPhysics && Hero->ToothPhysics->CanAct() && !Hero->ClingTooth && !Hero->bBrushing && !HasHeavyGrip() && !IsSwimming() && !IsClimbing();
}
float UMCToothMovementComponent::Traction() const { return GroundSurface==EMCGroundSurface::Slippery?.32f:1.f; }
FVector UMCToothMovementComponent::Intent() const
{
    return CharacterOwner && CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy?FVector(MovementIntent):Acceleration.GetSafeNormal2D();
}
float UMCToothMovementComponent::GetMaxSpeed() const
{
    if(IsClimbing()) return ClimbSpeed;
    if (!IsMovingOnGround() && !IsFalling()) return Super::GetMaxSpeed();
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if (!Hero || !Hero->Grip) return WalkSpeed;
    if (Hero->ClingTooth) return 0;
    float Speed=bWantsToSprint && CanSprint()?SprintSpeed:WalkSpeed;
    if (Hero->HeldFood && HasHeavyGrip()) Speed=FMath::Min(Speed,Hero->HeldFood->DragSpeed());
    if (Hero->Grip->GrabbedPlayer) Speed=FMath::Min(Speed,220.f);
    Speed/=FMath::Sqrt(1+Hero->Grip->LoadMass()/35.f);
    return Speed*(IsMovingOnGround() && GroundSurface==EMCGroundSurface::Sticky?.58f:1.f);
}
float UMCToothMovementComponent::GetMaxAcceleration() const
{
    if (!IsMovingOnGround()) return Super::GetMaxAcceleration();
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    const float Load=Hero && Hero->Grip?Hero->Grip->LoadMass():0;
    return GroundAcceleration*Traction()*(GroundSurface==EMCGroundSurface::Sticky?.72f:1.f)/(1+Load/22.f);
}
float UMCToothMovementComponent::GetMaxBrakingDeceleration() const
{
    if (!IsMovingOnGround()) return Super::GetMaxBrakingDeceleration();
    return GroundSurface==EMCGroundSurface::Slippery?90.f:GroundSurface==EMCGroundSurface::Sticky?1050.f:650.f;
}
void UMCToothMovementComponent::RefreshGroundSurface()
{
    GroundSurface=EMCGroundSurface::Normal;
    if (!CharacterOwner || !IsMovingOnGround()) return;
    const FVector Sole=UpdatedComponent->GetComponentLocation()-FVector(0,0,CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCLocomotionGround),false,CharacterOwner); Query.bReturnPhysicalMaterial=true;
    if (GetWorld()->LineTraceSingleByChannel(Hit,Sole+FVector(0,0,12),Sole-FVector(0,0,25),ECC_Visibility,Query))
        if (const auto* Material=Cast<UMCLocomotionMaterial>(Hit.PhysMaterial.Get())) GroundSurface=Material->GroundSurface;
    for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It)
        if (It->AffectsFooting(Sole) && (GroundSurface==EMCGroundSurface::Normal || It->GroundResponse==EMCGroundSurface::Sticky)) GroundSurface=It->GroundResponse;
    int32 Priority=MIN_int32; FString Selected;
    for (TActorIterator<AMCLocomotionSurface> It(GetWorld());It;++It)
        if (It->ContainsSole(Sole) && (It->Priority>Priority || (It->Priority==Priority && It->GetName()<Selected)))
        { Priority=It->Priority; Selected=It->GetName(); GroundSurface=It->Surface; }
}
FRotator UMCToothMovementComponent::ComputeOrientToMovementRotation(const FRotator& Current,float Dt,FRotator& Delta) const
{
    if(IsClimbing()) return FRotator(0,(-FVector(ClimbNormal)).Rotation().Yaw,0);
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    FRotator Desired=Super::ComputeOrientToMovementRotation(Current,Dt,Delta);
    FVector BrushDirection;
    if(Hero && Hero->BrushContact && Hero->BrushContact->WantsFacing(BrushDirection))
        return FRotator(0,BrushDirection.Rotation().Yaw,0);
    if (HasHeavyGrip() && Acceleration.SizeSquared2D()>1)
    {
        const FVector Target=Hero->HeldFood?Hero->HeldFood->GetActorLocation():Hero->Grip->GrabbedPlayer->GetActorLocation();
        const FVector ToLoad=(Target-Hero->GetActorLocation()).GetSafeNormal2D();
        // A backward pull keeps the chest facing the load; orbiting it turns the body gradually.
        if (!ToLoad.IsNearlyZero())
        {
            const FVector Input=Acceleration.GetSafeNormal2D();
            const float Side=1-FMath::SmoothStep(.3f,.7f,FMath::Abs(float(FVector::DotProduct(Input,ToLoad))));
            const float Yaw=ToLoad.Rotation().Yaw+(Hero->Grip->Frame.Pose==EMCGripPose::RearPull?180.f:0.f);
            Desired=FRotator(0,Yaw+FMath::FindDeltaAngleDegrees(Yaw,Input.Rotation().Yaw)*Side,0);
        }
    }
    return Desired;
}
AMCCoffeeFlood* UMCToothMovementComponent::DeepWaterAt(FVector P,bool Continuing) const
{
    if (!CharacterOwner || P.ContainsNaN()) return nullptr;
    const float Half=CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It)
    {
        if (!It->Contains(P)) continue;
        const float Surface=It->SurfaceHeightAt(P);
        if (Surface-(P.Z-Half)<Half*(Continuing?.45f:.95f)) continue;
        FHitResult Floor; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCSwimDepth),false,CharacterOwner);
        if (GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,Half+1000),ECC_WorldStatic,Params)
            && Surface-Floor.ImpactPoint.Z<Half*(Continuing?1.f:1.2f)) continue;
        return *It;
    }
    return nullptr;
}
void UMCToothMovementComponent::UpdateCharacterStateBeforeMovement(float Dt)
{
    Super::UpdateCharacterStateBeforeMovement(Dt);
    // Simulated peers receive the movement mode; they have no local E input.
    if(CharacterOwner && CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy) return;
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if (!Hero || !Hero->ToothPhysics || !Hero->ToothPhysics->CanAct()) return;
    ClimbCooldown=FMath::Max(0.f,ClimbCooldown-Dt);
    const bool Free=Hero->CanWork() && !Hero->HeldFood && !Hero->Grip->GrabbedPlayer && !Hero->OrderJumpTarget && !Hero->ClingTooth;
    if(IsClimbing() && (!bWantsToClimb || !Free)) SetMovementMode(MOVE_Falling);
    if(bWantsToClimb && Free && ClimbCooldown<=0 && !IsClimbing() && !IsSwimming()) {
        FHitResult Wall; if(FindClimbWall(Wall)) { ClimbNormal=Wall.ImpactNormal; Velocity=FVector::ZeroVector; SetMovementMode(MOVE_Custom,1); }
    }
    if(IsClimbing()) { bSprintActive=false; MovementIntent=Acceleration.GetSafeNormal(); return; }
    RefreshGroundSurface();
    bSprintActive=bWantsToSprint && CanSprint();
    MovementIntent=Acceleration.GetSafeNormal2D();
    // Keep turn speed predictable on the owner and server. A constraint based on
    // delayed food transforms can leave their facing directions permanently different.
    RotationRate.Yaw=IsSwimming()?180:HasHeavyGrip()?FMath::Min(60.f,Hero->Grip->Settings.TurnRate):Hero->Grip->LoadMass()>0?90:bSprintActive?240:380;
    if (DeepWaterAt(UpdatedComponent->GetComponentLocation(),IsSwimming()))
    {
        if (!IsSwimming()) SetMovementMode(MOVE_Swimming);
    }
    else if (IsSwimming()) SetMovementMode(MOVE_Falling);
}
void UMCToothMovementComponent::TickCharacterPose(float Dt)
{
    // Movement packets may arrive before Chaos and grip contacts update. This
    // procedural animation has no root motion or notifies: evaluate it once in
    // the mesh tick after the grip, not once per network move with stale targets.
    if (CharacterOwner && Cast<UMCToothAnimInstance>(CharacterOwner->GetMesh()->GetAnimInstance())) return;
    Super::TickCharacterPose(Dt);
}
void UMCToothMovementComponent::CalcVelocity(float Dt,float Friction,bool bFluid,float BrakingDeceleration)
{
    if (IsMovingOnGround()) Friction=GroundSurface==EMCGroundSurface::Slippery?.22f:GroundSurface==EMCGroundSurface::Sticky?6.f:2.6f;
    Super::CalcVelocity(Dt,Friction,bFluid,BrakingDeceleration);
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if (!Hero || !Hero->Grip || !Hero->ToothPhysics->CanAct()) return;
    // External pulls act after voluntary braking; otherwise idle characters cancel
    // all modest forces every frame and cannot be dragged by another player.
    FVector A=Hero->Grip->ReactionAcceleration()-Hero->Grip->PlayerPullAcceleration()*.45f;
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if (It->Grip && It->Grip->GrabbedPlayer==Hero) A+=It->Grip->PlayerPullAcceleration();
    Velocity+=FVector(A.X,A.Y,0).GetClampedToMaxSize(1500)*Dt;
    Velocity=Hero->Grip->ConstrainGripVelocity(Velocity,Dt);
}
void UMCToothMovementComponent::PhysSwimming(float Dt,int32 Iterations)
{
    if (Dt<MIN_TICK_TIME || !HasValidData()) return;
    auto* Hero=Cast<AMCToothCharacter>(CharacterOwner); if (!Hero) return;
    float Remaining=Dt;
    while (Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations)
    {
        ++Iterations; const float Step=FMath::Min(Remaining,.033f); Remaining-=Step;
        const FVector Before=UpdatedComponent->GetComponentLocation();
        auto* Water=DeepWaterAt(Before,true);
        if (!Water) { SetMovementMode(MOVE_Falling); StartNewPhysics(Remaining+Step,Iterations); return; }
        if (Hero->ClingTooth) { Velocity=FVector::ZeroVector; return; }
        const FVector Input=FVector(Acceleration.X,Acceleration.Y,0).GetClampedToMaxSize(GetMaxAcceleration())/FMath::Max(1.f,GetMaxAcceleration());
        // The old ragdoll paddle could not overcome the drain. A deliberate stroke
        // is stronger, while an idle swimmer still drifts towards the throat.
        const FVector Drive=Input*Water->Paddle*Water->WaterSettings.SwimStrokeMultiplier+Water->FlowAtPosition(Before,Hero);
        // Track a rising drink with its vertical speed. Damping against zero
        // velocity previously left the face submerged during fast filling.
        const FVector RelativeVelocity=Velocity-FVector(0,0,Water->SurfaceVerticalSpeedAt(Before));
        const FVector A=Water->WaterSettings.FloatAcceleration(Water->SurfaceHeightAt(Before),Before,RelativeVelocity,Drive,0,Water->WaterSettings.SwimFloatDepth);
        Velocity+=A*Step;
        const FVector Horizontal=FVector(Velocity.X,Velocity.Y,0).GetClampedToMaxSize(MaxSwimSpeed);
        Velocity=FVector(Horizontal.X,Horizontal.Y,FMath::Clamp(Velocity.Z,-220.,260.));
        FHitResult Hit;
        SafeMoveUpdatedComponent(Velocity*Step,UpdatedComponent->GetComponentQuat(),true,Hit);
        if (Hit.IsValidBlockingHit())
        {
            HandleImpact(Hit,Step,Velocity*Step);
            if (!Hero->ToothPhysics->CanAct()) return;
            SlideAlongSurface(Velocity*Step,1-Hit.Time,Hit.Normal,Hit,true);
            Velocity=(UpdatedComponent->GetComponentLocation()-Before)/Step;
        }
    }
}
