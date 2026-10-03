#include "MCToothMovementComponent.h"
#include "MCCoffeeFlood.h"
#include "MCToothCharacter.h"
#include "MCBrushContactComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCToothAnimInstance.h"
#include "MCThroat.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

FRootMotionSource* FMCLocomotionRootMotionSource::Clone() const { return new FMCLocomotionRootMotionSource(*this); }
bool FMCLocomotionRootMotionSource::Matches(const FRootMotionSource* Other) const { return FRootMotionSource::Matches(Other); }
bool FMCLocomotionRootMotionSource::UpdateStateFrom(const FRootMotionSource* Other,bool Catchup)
{
    if(!FRootMotionSource_ConstantForce::UpdateStateFrom(Other,Catchup)) return false;
    Force=static_cast<const FMCLocomotionRootMotionSource*>(Other)->Force;
    return true;
}
UScriptStruct* FMCLocomotionRootMotionSource::GetScriptStruct() const { return StaticStruct(); }
bool FMCLocomotionRootMotionSource::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    const bool Result=FRootMotionSource_ConstantForce::NetSerialize(Ar,Map,Success);
    uint8 FinishMode=static_cast<uint8>(FinishVelocityParams.Mode);
    Ar<<Settings.Flags;Ar<<FinishMode;Ar<<FinishVelocityParams.ClampVelocity;Ar<<FinishVelocityParams.SetVelocity;
    if(Ar.IsLoading()) FinishVelocityParams.Mode=static_cast<ERootMotionFinishVelocityMode>(FinishMode);
    Success=Success && !Ar.IsError();
    return Result && Success;
}

UMCToothMovementComponent::UMCToothMovementComponent()
{
    SetIsReplicatedByDefault(true);
    MaxSwimSpeed=320; GetNavAgentPropertiesRef().bCanSwim=true;
}
namespace
{
    const FName DashSourceName(TEXT("MCTapDash"));
    const FName SuctionSourceName(TEXT("MCThroatAmbientSuction"));
    bool SourceRunning(const TSharedPtr<FRootMotionSource>& Source)
    {
        return Source.IsValid() && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished)
            && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
            && (Source->Duration<0 || Source->GetTime()<Source->Duration);
    }
    double MovementServerTime(const UWorld* World)
    {
        const auto* State=World?World->GetGameState():nullptr;
        return State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0;
    }
    class FMCStrideMove final : public FSavedMove_Character
    {
    public:
        using Super=FSavedMove_Character;
        bool Sprint=false,Climb=false,Dash=false;
        float DashCooldown=0;
        FVector Suction=FVector::ZeroVector;
        virtual void Clear() override { Super::Clear(); Sprint=Climb=Dash=false;DashCooldown=0;Suction=FVector::ZeroVector; }
        virtual uint8 GetCompressedFlags() const override { return Super::GetCompressedFlags() | (Sprint?FLAG_Custom_0:0) | (Climb?FLAG_Custom_1:0) | (Dash?FLAG_Custom_2:0); }
        virtual bool CanCombineWith(const FSavedMovePtr& Move,ACharacter* Hero,float MaxDelta) const override
        {
            const auto* Other=static_cast<const FMCStrideMove*>(Move.Get());
            return !Dash && !Other->Dash && Sprint==Other->Sprint && Climb==Other->Climb && Suction.Equals(Other->Suction,.1)
                && Super::CanCombineWith(Move,Hero,MaxDelta);
        }
        virtual bool IsImportantMove(const FSavedMovePtr& LastAcked) const override { return Dash || Super::IsImportantMove(LastAcked); }
        virtual void SetMoveFor(ACharacter* Hero,float Dt,FVector const& Accel,FNetworkPredictionData_Client_Character& Data) override
        {
            Super::SetMoveFor(Hero,Dt,Accel,Data);
            auto* M=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
            Sprint=M->WantsToSprint();Climb=M->WantsClimb();Dash=M->WantsDash();DashCooldown=M->GetDashCooldownRemaining();
            Suction=M->CaptureSuctionForMove();
            if(Dash) bForceNoCombine=true;
        }
        virtual void PrepMoveFor(ACharacter* Hero) override
        {
            Super::PrepMoveFor(Hero);
            auto* M=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
            M->SetSprinting(Sprint);M->SetWantsClimb(Climb);M->SetWantsDash(Dash);M->RestoreDashPrediction(DashCooldown);M->RestoreSuctionForMove(Suction);
        }
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
{ Super::UpdateFromCompressedFlags(Flags); bWantsToSprint=(Flags&FSavedMove_Character::FLAG_Custom_0)!=0; bWantsToClimb=(Flags&FSavedMove_Character::FLAG_Custom_1)!=0; bWantsDash=(Flags&FSavedMove_Character::FLAG_Custom_2)!=0; }
void UMCToothMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,GroundSurface,COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,MovementIntent,COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,bSprintActive,COND_SimulatedOnly);
    DOREPLIFETIME(UMCToothMovementComponent,ClimbNormal);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,DashStartedAt,COND_SimulatedOnly);
    DOREPLIFETIME_CONDITION(UMCToothMovementComponent,DashDirection,COND_SimulatedOnly);
}
bool UMCToothMovementComponent::HasHeavyGrip() const
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    return Hero && Hero->Grip && (Hero->Grip->GrabbedPlayer || (Hero->HeldFood && !Hero->Grip->CanCarry(Hero->HeldFood)));
}
bool UMCToothMovementComponent::CanSprint() const
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    return Hero && Hero->CanWork() && !Hero->ClingTooth && !Hero->OrderJumpTarget && !Hero->bBrushing && !HasHeavyGrip() && !IsSwimming() && !IsClimbing() && !IsDashing();
}
bool UMCToothMovementComponent::CanDashAction() const
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    return Hero && Hero->CanWork() && !Hero->ClingTooth && !Hero->OrderJumpTarget && !Hero->bBrushing && !HasHeavyGrip()
        && !bWantsToClimb && !IsSwimming() && !IsClimbing() && (IsMovingOnGround() || IsFalling());
}
bool UMCToothMovementComponent::CanDash() const { return IsMovingOnGround() && CanDashAction() && DashCooldownRemaining<=0 && !IsDashing(); }
bool UMCToothMovementComponent::IsDashing() const
{
    if(CharacterOwner && CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy) {
        if(!CanDashAction()) { DashPresentationStartedAt=-100;return false; }
        const double Age=GetWorld()->GetTimeSeconds()-DashPresentationStartedAt;
        return DashPresentationStartedAt>=0 && Age>=0 && Age<GetDashDuration();
    }
    const auto Source=const_cast<UMCToothMovementComponent*>(this)->GetRootMotionSource(DashSourceName);
    return SourceRunning(Source);
}
float UMCToothMovementComponent::GetDashProgress() const
{
    if(!IsDashing()) return 0;
    if(CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy)
        return FMath::Clamp(float(GetWorld()->GetTimeSeconds()-DashPresentationStartedAt)/GetDashDuration(),0.f,1.f);
    const auto Source=const_cast<UMCToothMovementComponent*>(this)->GetRootMotionSource(DashSourceName);
    return Source.IsValid()?FMath::Clamp(Source->GetTime()/FMath::Max(.01f,Source->Duration),0.f,1.f):0;
}
void UMCToothMovementComponent::OnRep_DashStartedAt()
{
    if(!FMath::IsFinite(DashStartedAt) || DashStartedAt<0) {
        DashPresentationStartedAt=-100;LastPresentedDashStartedAt=-100;return;
    }
    // Relevancy can resend the same property. A fresh receipt begins one complete
    // cosmetic dash pose; gameplay displacement continues to follow the server RMS.
    if(DashStartedAt==LastPresentedDashStartedAt) return;
    LastPresentedDashStartedAt=DashStartedAt;DashPresentationStartedAt=-100;
    const double ServerAge=MovementServerTime(GetWorld())-DashStartedAt;
    if(ServerAge<-.25 || ServerAge>GetDashDuration()+.25 || !CanDashAction()) return;
    DashPresentationStartedAt=GetWorld()->GetTimeSeconds();
}
void UMCToothMovementComponent::StartDash()
{
    FVector Direction=Acceleration.GetSafeNormal2D();
    if(Direction.IsNearlyZero()) Direction=CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
    DashDirection=Direction;DashStartedAt=MovementServerTime(GetWorld());DashCooldownRemaining=FMath::Max(GetDashDuration(),FMath::Clamp(DashCooldown,.5f,3.f));
    bWantsToSprint=false;bSprintActive=false;
    auto Source=MakeShared<FMCLocomotionRootMotionSource>();
    Source->InstanceName=DashSourceName;Source->Priority=500;Source->AccumulateMode=ERootMotionAccumulateMode::Override;
    Source->Duration=GetDashDuration();Source->Force=Direction*FMath::Clamp(DashSpeed,500.f,1600.f);
    Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    Source->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::ClampVelocity;Source->FinishVelocityParams.ClampVelocity=WalkSpeed;
    ApplyRootMotionSource(Source);
    if(CharacterOwner->HasAuthority()) CharacterOwner->ForceNetUpdate();
}
void UMCToothMovementComponent::CancelDash()
{
    bWantsDash=false;
    DashPresentationStartedAt=-100;
    RemoveRootMotionSource(DashSourceName);
    if(DashStartedAt>-100) { DashStartedAt=-100;if(CharacterOwner && CharacterOwner->HasAuthority()) CharacterOwner->ForceNetUpdate(); }
}
FVector UMCToothMovementComponent::CaptureSuctionForMove()
{
    float Strength=0;FVector Sample=FVector::ZeroVector;
    if(CharacterOwner) AMCThroat::FindAmbientSuctionAt(GetWorld(),CharacterOwner->GetActorLocation(),Strength,Sample);
    PendingSuction=Sample;bHasSuctionSample=true;
    return Sample;
}
void UMCToothMovementComponent::RestoreSuctionForMove(FVector Sample)
{
    PendingSuction=Sample;bHasSuctionSample=true;
    const auto Source=GetRootMotionSource(SuctionSourceName);
    if(!SourceRunning(Source)) return;
    auto* Wind=static_cast<FMCLocomotionRootMotionSource*>(Source.Get());
    // PrepMoveFor can prepare authoritative root motion before restoring this
    // move's field snapshot. Replay skips PrepareRootMotion in PerformMovement.
    // Replace the cached force without advancing time or losing catchup scaling.
    const float OldMagnitude=float(Wind->Force.Size());
    const float Multiplier=OldMagnitude>UE_SMALL_NUMBER?float(Wind->RootMotionParams.GetRootMotionTransform().GetTranslation().Size())/OldMagnitude:1.f;
    Wind->Force=Sample;
    if(Wind->Status.HasFlag(ERootMotionSourceStatusFlags::Prepared)) Wind->RootMotionParams.Set(FTransform(Sample*Multiplier));
}
void UMCToothMovementComponent::UpdateAmbientSuction()
{
    const auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    const bool Eligible=Hero && Hero->CanWork() && !Hero->ClingTooth && !Hero->OrderJumpTarget
        && (IsMovingOnGround() || IsFalling() || IsSwimming());
    if(!bHasSuctionSample) CaptureSuctionForMove();
    const FVector Sample=Eligible?PendingSuction:FVector::ZeroVector;bHasSuctionSample=false;
    auto Current=GetRootMotionSource(SuctionSourceName);
    if(Sample.IsNearlyZero()) { RemoveRootMotionSource(SuctionSourceName);return; }
    if(SourceRunning(Current)) {
        static_cast<FMCLocomotionRootMotionSource*>(Current.Get())->Force=Sample;
        return;
    }
    auto Source=MakeShared<FMCLocomotionRootMotionSource>();
    Source->InstanceName=SuctionSourceName;Source->Priority=100;Source->AccumulateMode=ERootMotionAccumulateMode::Additive;
    Source->Duration=-1;Source->Force=Sample;
    ApplyRootMotionSource(Source);
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
    if(IsDashing()) return FRotator(0,FVector(DashDirection).Rotation().Yaw,0);
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
    auto* Hero=Cast<AMCToothCharacter>(CharacterOwner);
    if (!Hero || !Hero->ToothPhysics || !Hero->ToothPhysics->CanAct() || Hero->SwallowedBy) {
        CancelDash();UpdateAmbientSuction();return;
    }
    if(IsDashing() && !CanDashAction()) CancelDash();
    ClimbCooldown=FMath::Max(0.f,ClimbCooldown-Dt);
    // E takes ownership from the older coffee anchor. A swimmer must be able
    // to pull onto the wall rather than become pinned at their water position.
    if(bWantsToClimb && Hero->ClingTooth) {Hero->ClingTooth=nullptr;if(Hero->HasAuthority()) Hero->ForceNetUpdate();}
    const bool Free=Hero->CanWork() && !Hero->HeldFood && !Hero->Grip->GrabbedPlayer && !Hero->OrderJumpTarget && !Hero->ClingTooth;
    if(IsClimbing() && (!bWantsToClimb || !Free)) SetMovementMode(MOVE_Falling);
    if(bWantsToClimb && Free && ClimbCooldown<=0 && !IsClimbing()) {
        FHitResult Wall; if(FindClimbWall(Wall)) { ClimbNormal=Wall.ImpactNormal; Velocity=FVector::ZeroVector; SetMovementMode(MOVE_Custom,1); }
    }
    if(IsClimbing()) { CancelDash();UpdateAmbientSuction();bSprintActive=false; MovementIntent=Acceleration.GetSafeNormal(); return; }
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
    if(IsDashing() && !CanDashAction()) CancelDash();
}
void UMCToothMovementComponent::PerformMovement(float Dt)
{
    if(CharacterOwner && CharacterOwner->GetLocalRole()!=ROLE_SimulatedProxy) {
        DashCooldownRemaining=FMath::Max(0.f,DashCooldownRemaining-Dt);
        if(bWantsDash) {
            bWantsDash=false;
            if(CanDash() && !DeepWaterAt(CharacterOwner->GetActorLocation())) StartDash();
            // Saved root motion already restored the source on correction replay.
            else if(IsDashing()) {
                const auto Source=GetRootMotionSource(DashSourceName);
                if(Source.IsValid()) DashCooldownRemaining=FMath::Max(DashCooldownRemaining,FMath::Max(GetDashDuration(),FMath::Clamp(DashCooldown,.5f,3.f))-Source->GetTime());
            }
        }
        // Super caches whether sources exist before its state-before-movement
        // hook, so create first to prepare and apply them on this very move.
        UpdateAmbientSuction();
    }
    Super::PerformMovement(Dt);
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
        RestorePreAdditiveRootMotionVelocity();
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
        ApplyRootMotionToVelocity(Step);
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
