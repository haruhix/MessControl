#include "MCToothPhysicsComponent.h"
#include "MCPerkComponent.h"
#include "MCToothMovementComponent.h"
#include "MCCoffeeFlood.h"
#include "MCGazeComponent.h"
#include "MCGripComponent.h"
#include "MCInventoryComponent.h"
#include "MCExpressionComponent.h"
#include "MCFishingRodComponent.h"
#include "MCRewardChest.h"
#include "MCToothCharacter.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodActor.h"
#include "MCSlipperyFood.h"
#include "MCBrushContactComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/OverlapResult.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ConfigCacheIni.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
TAutoConsoleVariable<int32> CVarMCSlipDebug(TEXT("MC.Slip.Debug"),0,
    TEXT("Log native slippery-fall gate masks, candidates and clearance rejections."),ECVF_Default);
TAutoConsoleVariable<int32> CVarMCSlipEmptyHandsMode(TEXT("MC.Slip.EmptyHandsMode"),1,
    TEXT("Empty hands: 0=legacy severe skid, 1=moving exposure with one probability roll. Carried food always falls."),ECVF_Default);
TAutoConsoleVariable<float> CVarMCSlipFirstRiskSeconds(TEXT("MC.Slip.FirstRiskSeconds"),1.5f,
    TEXT("Moving slippery exposure before the first empty-hands probability check."),ECVF_Default);
TAutoConsoleVariable<float> CVarMCSlipRepeatRiskSeconds(TEXT("MC.Slip.RepeatRiskSeconds"),3.f,
    TEXT("New moving exposure required after an empty-hands probability check."),ECVF_Default);
TAutoConsoleVariable<float> CVarMCSlipFallChance(TEXT("MC.Slip.FallChance"),.5f,
    TEXT("Authority-only fall probability at an exposure boundary, not per tick."),ECVF_Default);
}

UMCToothPhysicsComponent::UMCToothPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostPhysics;
    SetIsReplicatedByDefault(true);
    static ConstructorHelpers::FObjectFinder<UMCPhysicsProfile> Asset(TEXT("/Game/Data/DA_ToothPhysics"));
    if (Asset.Succeeded()) Profile=Asset.Object;
}
void UMCToothPhysicsComponent::BeginPlay()
{
    Super::BeginPlay(); Tooth=CastChecked<AMCToothCharacter>(GetOwner());
    // Recovery changes attachment, component space and the presentation pose.
    // Publish all three before the mesh evaluates, never one frame apart.
    Tooth->GetMesh()->AddTickPrerequisiteComponent(this);
    if (Tooth->HasAuthority()) {
        // Fresh players, respawns and saved Blueprint instances use the chosen profile.
        ActiveRagdollMode=EMCActiveRagdollMode::Soft;
        Settings=Profile?Profile->Settings:FMCPhysicsSettings(); Settings.Sanitize();
    }
    Muscles=Tooth->FindComponentByClass<UPhysicsControlComponent>();
    const auto CreateMuscleControls=[this]()
    {
        JointTargets.Reset(); HandControls[0]=NAME_None; HandControls[1]=NAME_None; BalanceControl=NAME_None;
        FPhysicsControlData Data;
        Data.AngularStrength=Settings.MuscleStrength; Data.AngularDampingRatio=Settings.Damping;
        Data.bDisableCollision=true; Data.bOnlyControlChildObject=true;
        if (Muscles && Tooth->GetMesh()->GetPhysicsAsset())
        {
            for (const FName Role:{FName("arm_l"),FName("arm_r"),FName("leg_l"),FName("leg_r")})
            {
                const auto Controls=Muscles->CreateControlsFromSkeletalMeshBelow(Tooth->GetMesh(),Tooth->RigBone(Role),true,EPhysicsControlType::ParentSpace,Data,Role);
                FPhysicsControlNames Names;
                Muscles->AddControlsToSet(Names,Controls,TEXT("Limbs")); Muscles->AddControlsToSet(Names,Controls,Role);
                // Record the same physical hierarchy used by the creation helper.
                // Active mode receives the unblended pose, never the previous Chaos output.
                const auto& Skeleton=Tooth->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
                const auto* Asset=Tooth->GetMesh()->GetPhysicsAsset(); int32 ControlIndex=0;
                Tooth->GetMesh()->ForEachBodyBelow(Tooth->RigBone(Role),true,false,[&](const FBodyInstance* Body) {
                    const int32 Child=Skeleton.FindBoneIndex(Asset->SkeletalBodySetups[Body->InstanceBodyIndex]->BoneName);
                    if(Child<0) return;
                    int32 Parent=Skeleton.GetParentIndex(Child);
                    while(Parent>=0 && Asset->FindBodyIndex(Skeleton.GetBoneName(Parent))==INDEX_NONE) Parent=Skeleton.GetParentIndex(Parent);
                    if(Parent>=0 && Controls.IsValidIndex(ControlIndex)) JointTargets.Add({Controls[ControlIndex++],Role,Parent,Child});
                    if((Role==TEXT("arm_l") || Role==TEXT("arm_r")))
                        if(auto* Joint=Tooth->GetMesh()->FindConstraintInstance(Skeleton.GetBoneName(Child)))
                            if(!GripJoints.ContainsByPredicate([&](const FGripJoint& Saved) { return Saved.Name==Joint->JointName && Saved.Role==Role; }))
                                GripJoints.Add({Joint->JointName,Role,Joint->ProfileInstance});
                });
            }
            // Dedicated palm servos keep contact with a moving prop while the elbow
            // motors retain a procedural bend. Both act on real simulated bodies.
            FPhysicsControlData Palm; Palm.bUseSkeletalAnimation=false; Palm.bDisableCollision=true; Palm.bOnlyControlChildObject=true;
            Palm.LinearStrength=24; Palm.LinearDampingRatio=1.1f; Palm.MaxForce=14000;
            Palm.AngularStrength=22; Palm.AngularDampingRatio=1; Palm.MaxTorque=350000;
            for(int32 I=0;I<2;++I) {
                HandControls[I]=Muscles->CreateControl(Tooth->GetMesh(),Tooth->RigBone(TEXT("body")),Tooth->GetMesh(),
                    Tooth->RigBone(I==0?TEXT("hand_l"):TEXT("hand_r")),Palm,FPhysicsControlTarget(),TEXT("GripPalms"));
                Muscles->SetControlEnabled(HandControls[I],false);
            }
            // Newly spawned actors can be created after the mesh tick this frame. Prime the cache
            // before the first control update so targets never read an empty skeleton buffer.
            Tooth->GetMesh()->RefreshBoneTransforms(); Muscles->UpdateTargetCaches(0.f);
            FPhysicsControlData Balance;
            Balance.LinearStrength=12; Balance.LinearDampingRatio=.9f;
            Balance.AngularStrength=5; Balance.AngularDampingRatio=.65f;
            Balance.bUseSkeletalAnimation=false; Balance.bDisableCollision=true;
            Balance.bOnlyControlChildObject=true;
            const auto& Ref=Tooth->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
            FTransform BodyRest=FTransform::Identity;
            for (int32 B=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));B>=0;B=Ref.GetParentIndex(B)) BodyRest=BodyRest*Ref.GetRefBonePose()[B];
            BodyRest=BodyRest*Tooth->StandingMeshTransform();
            BalanceRest=BodyRest;
            FPhysicsControlTarget BalanceTarget; BalanceTarget.TargetPosition=BodyRest.GetLocation();
            BalanceTarget.TargetOrientation=BodyRest.Rotator(); BalanceTarget.bApplyControlPointToTarget=true;
            BalanceControl=Muscles->CreateControl(Tooth->GetCapsuleComponent(),NAME_None,Tooth->GetMesh(),
                Tooth->RigBone(TEXT("body")),Balance,BalanceTarget,TEXT("Balance"));
        }
    };
    CreateMuscleControls();
    OnRep_Settings(); EnterStanding();
    if (!Tooth->HasAuthority() && Frame.State!=EMCBodyState::Standing) OnRep_Frame();
    // Initial world loading can recreate physics after the listen host's pawn
    // begins play. PhysicsControl then discards its records; rebind once after
    // startup without resetting movement, grips or the replicated body state.
    const TWeakObjectPtr<UMCToothPhysicsComponent> WeakThis(this);
    GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis,CreateMuscleControls]()
    {
        auto* Self=WeakThis.Get();
        if(!Self || !Self->HasBegunPlay() || !IsValid(Self->Tooth) || Self->Tooth->IsActorBeingDestroyed() ||
            !IsValid(Self->Muscles) || !Self->Muscles->IsRegistered() || !Self->Tooth->GetMesh()->IsRegistered() ||
            Self->JointTargets.IsEmpty()) return;
        FPhysicsControlData Probe;
        if(!Self->Muscles->GetControlNamesInSet(TEXT("Limbs")).IsEmpty() &&
            Self->Muscles->GetControlData(Self->JointTargets[0].Control,Probe)) return;
        CreateMuscleControls();
        if(Self->LocalState==EMCBodyState::Standing && !Self->Tooth->SwallowedBy && !Self->Tooth->MimicCaptor) {
            Self->Tooth->GetMesh()->SetAllBodiesBelowSimulatePhysics(Self->Tooth->RigBone(TEXT("body")),true,true);
        }
        Self->OnRep_ActiveRagdollMode();
        UE_LOG(LogTemp,Display,TEXT("Tooth muscle controls rebound after startup: %s"),*Self->Tooth->GetName());
    });
}
float UMCToothPhysicsComponent::ServerTime() const
{
    const AGameStateBase* State=GetWorld()->GetGameState();
    return State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
void UMCToothPhysicsComponent::SetMuscles(bool bEnable)
{
    if (Muscles) {
        Muscles->SetControlsInSetEnabled(TEXT("Limbs"),bEnable);
        Muscles->SetControlsInSetEnabled(TEXT("Balance"),bEnable && !UsesActiveMuscles());
        Muscles->SetControlEnabled(HandControls[0],bEnable && bPhysicalGripLeft);
        Muscles->SetControlEnabled(HandControls[1],bEnable && bPhysicalGripRight);
    }
}
void UMCToothPhysicsComponent::ConfigureStandingBody()
{
    if(!Tooth || LocalState!=EMCBodyState::Standing || Tooth->SwallowedBy || Tooth->MimicCaptor) return;
    // The reference leaves the pelvis kinematic: locomotion supplies the stable
    // root, while muscles drive the simulated limbs. Full-body falls stay physical.
    const bool Active=UsesActiveMuscles();
    if(auto* Body=Tooth->GetMesh()->GetBodyInstance(Tooth->RigBone(TEXT("body")))) {
        if(Body->IsInstanceSimulatingPhysics()==Active) Body->SetInstanceSimulatePhysics(!Active,true,true);
        if(Active) Body->PhysicsBlendWeight=0;
    }
}
void UMCToothPhysicsComponent::OnRep_Settings()
{
    Settings.Sanitize(); if (!Tooth) return;
    if (Muscles)
    {
        const bool Active=UsesActiveMuscles();
        const bool Soft=ActiveRagdollMode==EMCActiveRagdollMode::Soft;
        FPhysicsControlData Data; Data.AngularStrength=Settings.MuscleStrength; Data.AngularDampingRatio=Settings.Damping;
        Data.bDisableCollision=true; Data.bOnlyControlChildObject=true;
        Data.bUseSkeletalAnimation=!Active;
        if(Active) {
            Data.AngularStrength*=Soft?.38f:.65f;
            Data.AngularDampingRatio=FMath::Clamp(Settings.Damping*(Soft?.85f:1.05f),.5f,1.4f);
            Data.MaxTorque=(Soft?200000.f:350000.f)*Settings.Mass/8;
        }
        Muscles->SetControlDatasInSet(TEXT("Limbs"),Data);
        if(Active) {
            Data.AngularStrength=Settings.MuscleStrength*(Soft?.65f:.9f);
            Muscles->SetControlDatasInSet(TEXT("leg_l"),Data); Muscles->SetControlDatasInSet(TEXT("leg_r"),Data);
        }
        FPhysicsControlData Balance; Balance.bUseSkeletalAnimation=false;
        Balance.bDisableCollision=true; Balance.bOnlyControlChildObject=true;
        Balance.LinearStrength=12; Balance.LinearDampingRatio=.9f;
        Balance.AngularStrength=5; Balance.AngularDampingRatio=.65f;
        Muscles->SetControlData(BalanceControl,Balance);
        ConfigureStandingBody();
        SetMuscles(LocalState==EMCBodyState::Standing && !Tooth->SwallowedBy && !Tooth->MimicCaptor);
        for(int32 I=0;I<2;++I) {
            const bool Physical=I==0?bPhysicalGripLeft:bPhysicalGripRight;
            const bool Occupied=I==0?bGripLeft:bGripRight;
            const FName Role=I==0?TEXT("arm_l"):TEXT("arm_r");
            if(Physical) {
                FPhysicsControlData Grip=Data; Grip.AngularStrength=Settings.MuscleStrength*1.5f;
                Grip.LinearStrength=12; Grip.LinearDampingRatio=1.1f; Grip.MaxForce=9000;
                Grip.bUseSkeletalAnimation=false;
                Muscles->SetControlDatasInSet(Role,Grip);
            }
            Muscles->SetControlsInSetEnabled(Role,LocalState==EMCBodyState::Standing && !Tooth->SwallowedBy && !Tooth->MimicCaptor && (!Occupied || Physical));
        }
        ConfigureGripConstraints();
    }
    if (const auto* Asset=Tooth->GetMesh()->GetPhysicsAsset())
        for (const USkeletalBodySetup* Body:Asset->SkeletalBodySetups)
        {
            const float Share=Body->BoneName==Tooth->RigBone(TEXT("body"))?.625f:.375f/FMath::Max(1,Asset->SkeletalBodySetups.Num()-1);
            Tooth->GetMesh()->SetMassOverrideInKg(Body->BoneName,Settings.Mass*Share);
        }
}
bool UMCToothPhysicsComponent::SetActiveRagdollMode(EMCActiveRagdollMode Mode)
{
    if(!Tooth || !Tooth->HasAuthority() || uint8(Mode)>uint8(EMCActiveRagdollMode::Firm)) return false;
    if(Mode==ActiveRagdollMode) return true;
    ActiveRagdollMode=Mode; OnRep_ActiveRagdollMode(); Tooth->ForceNetUpdate(); return true;
}
void UMCToothPhysicsComponent::OnRep_ActiveRagdollMode()
{
    if(!Tooth || !Muscles) return;
    // Free-locomotion tuning must not reset a palm already supporting a load.
    // The next animation update continues to supply its procedural grip target.
    if(LocalState==EMCBodyState::Standing && (bPhysicalGripLeft || bPhysicalGripRight)) {
        OnRep_Settings(); return;
    }
    // Reset explicit targets before handing the pose back to the animation cache.
    for(const auto& Joint:JointTargets) Muscles->SetControlTarget(Joint.Control,FPhysicsControlTarget(),false);
    FPhysicsControlTarget Target; Target.TargetPosition=BalanceRest.GetLocation(); Target.TargetOrientation=BalanceRest.Rotator();
    Target.bApplyControlPointToTarget=true; Muscles->SetControlTarget(BalanceControl,Target,false);
    OnRep_Settings();
    const auto& Ref=Tooth->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    SubmitAnimationTargets(Ref.GetRefBonePose(),Ref,0);
}
void UMCToothPhysicsComponent::SubmitAnimationTargets(const TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt)
{
    if(!Tooth || !Muscles || !UsesActiveMuscles() || LocalState!=EMCBodyState::Standing || Tooth->SwallowedBy || Tooth->MimicCaptor) return;
    TArray<FTransform> CS; CS.SetNum(Pose.Num());
    for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)<0?Pose[I]:Pose[I]*CS[Ref.GetParentIndex(I)];
    auto GripTarget=[&](FName Name,const FTransform& Parent,const FTransform& Child) {
        FTransform Goal=Child.GetRelativeTransform(Parent); FPhysicsControlTarget Previous;
        if(Dt>0 && Muscles->GetControlTarget(Name,Previous)) {
            // A finite reach speed prevents a corner regrip or replication update
            // from turning a palm servo into an instantaneous impulse.
            const FVector P=Previous.TargetPosition;
            Goal.SetLocation(P+(Goal.GetLocation()-P).GetClampedToMaxSize(200.f*Dt));
            const FQuat Q=Previous.TargetOrientation.Quaternion(); const float Angle=Q.AngularDistance(Goal.GetRotation());
            Goal.SetRotation(FQuat::Slerp(Q,Goal.GetRotation(),Angle>.001f?FMath::Min(1.f,6.f*Dt/Angle):1.f).GetNormalized());
        }
        const FTransform World=Goal*Parent;
        Muscles->SetControlTargetPoses(Name,Parent.GetLocation(),Parent.Rotator(),World.GetLocation(),World.Rotator(),Dt,true);
    };
    for(const auto& Joint:JointTargets) {
        const bool Occupied=Joint.Role==TEXT("arm_l")?bGripLeft:Joint.Role==TEXT("arm_r") && bGripRight;
        const bool Physical=Joint.Role==TEXT("arm_l")?bPhysicalGripLeft:Joint.Role==TEXT("arm_r") && bPhysicalGripRight;
        if((Occupied && !Physical) || !CS.IsValidIndex(Joint.Child) || !CS.IsValidIndex(Joint.Parent)) continue;
        if(Physical) GripTarget(Joint.Control,CS[Joint.Parent],CS[Joint.Child]);
        else Muscles->SetControlTargetPoses(Joint.Control,CS[Joint.Parent].GetLocation(),CS[Joint.Parent].Rotator(),
                CS[Joint.Child].GetLocation(),CS[Joint.Child].Rotator(),Dt,true);
    }
    const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));
    if(!CS.IsValidIndex(Body)) return;
    for(int32 I=0;I<2;++I) if(I==0?bPhysicalGripLeft:bPhysicalGripRight) {
        const int32 Hand=Ref.FindBoneIndex(Tooth->RigBone(I==0?TEXT("hand_l"):TEXT("hand_r")));
        if(CS.IsValidIndex(Hand)) GripTarget(HandControls[I],CS[Body],CS[Hand]);
    }
}
void UMCToothPhysicsComponent::ConfigureGripConstraints()
{
    if(!Tooth) return;
    for(const auto& Saved:GripJoints) if(auto* Joint=Tooth->GetMesh()->FindConstraintInstance(Saved.Name)) {
        Joint->CopyProfilePropertiesFrom(Saved.Profile);
        const bool Active=LocalState==EMCBodyState::Standing && !Tooth->SwallowedBy && !Tooth->MimicCaptor
            && (Saved.Role==TEXT("arm_l")?bPhysicalGripLeft:bPhysicalGripRight);
        if(Active) {
            // The artist's compact floating hands need bounded extension for
            // overhead lifts. Saved constraints are restored on release/fall.
            Joint->SetLinearLimits(LCM_Limited,LCM_Limited,LCM_Limited,36);
            Joint->SetAngularSwing1Motion(ACM_Free); Joint->SetAngularSwing2Motion(ACM_Free); Joint->SetAngularTwistMotion(ACM_Free);
        }
    }
}
void UMCToothPhysicsComponent::SetTuning(FMCPhysicsSettings NewSettings)
{
    if (!Tooth || !Tooth->HasAuthority()) return;
    NewSettings.Sanitize(); Settings=NewSettings; OnRep_Settings(); Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::SaveTuning() const
{
    const FString File=FPaths::ProjectSavedDir()/TEXT("PhysicsTuning.ini");
    const float Values[]={Settings.Knockback,Settings.Lift,Settings.FallThreshold,Settings.RagdollSeconds,Settings.GetUpSeconds,Settings.MuscleStrength,Settings.Damping,Settings.Mass};
    const TCHAR* Names[]={TEXT("Knockback"),TEXT("Lift"),TEXT("FallThreshold"),TEXT("RagdollSeconds"),TEXT("GetUpSeconds"),TEXT("MuscleStrength"),TEXT("Damping"),TEXT("Mass")};
    for (int32 I=0;I<8;++I) GConfig->SetFloat(TEXT("ToothPhysics"),Names[I],Values[I],File);
    GConfig->Flush(false,File);
}
void UMCToothPhysicsComponent::ResetTuning() { SetTuning(Profile?Profile->Settings:FMCPhysicsSettings()); }
void UMCToothPhysicsComponent::SetState(EMCBodyState NewState)
{
    BlockedRecoveryStartedAt=-1.f;
    if(NewState==EMCBodyState::Ragdoll) {Frame.bFromSlipperyFall=false;Frame.bAuthoredSlipRecovery=false;}
    Frame.State=NewState; ++Frame.Revision; Frame.StateStartedAt=ServerTime(); LocalState=NewState;
    Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::UpdateSlipperyFallGate(float Dt)
{
    if(!Tooth || !Tooth->HasAuthority()) return;
    const auto* Move=Cast<UMCToothMovementComponent>(Tooth->GetCharacterMovement());
    const float Speed=Tooth->GetVelocity().Size2D();
    const float Deceleration=FMath::Max(0.f,PreviousSlipSpeed-Speed)/FMath::Max(.008f,Dt);PreviousSlipSpeed=Speed;
    const FVector Direction=Tooth->GetVelocity().GetSafeNormal2D();
    const FVector Intent=Move?Move->Intent().GetSafeNormal2D():FVector::ZeroVector;
    const float Side=FMath::Abs(FVector::DotProduct(Direction,Tooth->GetActorRightVector()));
    const float Counter=Intent.IsNearlyZero()?0.f:1-FMath::Max(0.f,FVector::DotProduct(Direction,Intent));
    const bool Traversal=Tooth->IsRowing() || Tooth->IsYawning() || Tooth->IsDashing() || Tooth->OrderJumpTarget || Tooth->ClingTooth
        || (Tooth->FishingRod && Tooth->FishingRod->IsPresented()) || IsValid(Tooth->RewardInteraction)
        || Tooth->SwallowedBy;
    const bool LegacyContact=Tooth->bBrushing || Tooth->IsPrimaryHeld()
        || (Tooth->Expression && Tooth->Expression->BodyAlpha()>.01f);
    const bool Carrying=MCSlipperyFood::IsCarrying(Tooth);
    const bool Supported=CanAct() && Tooth->GetController() && Move && Move->IsMovingOnGround()
        && !Tooth->HasFrozenLegs() && !Tooth->IsFreezingToDeath() && Tooth->Status->PainAlpha()<.01f
        && Tooth->AnimationSwim<.01f && Tooth->AnimationClimb<.01f && !Traversal
        && Tooth->AnimationProfile && Tooth->AnimationProfile->Glide && Tooth->AnimationProfile->GlideStrong;
    const bool Slippery=Move && Move->GroundSurface==EMCGroundSurface::Slippery;
    const bool RiskMode=CVarMCSlipEmptyHandsMode.GetValueOnGameThread()!=0;
    // A genuine working brush contact is safe; holding LMB in empty space is not.
    const bool Cleaning=!Carrying && Tooth->BrushContact && Tooth->BrushContact->IsWorkReady();
    const bool Allowed=Supported && (Carrying || (RiskMode?!Cleaning:!LegacyContact));
    bool Candidate=false,RollDue=false;
    if(Carrying || RiskMode) {
        // The legacy gate remains selectable without feeding it during risk mode.
        SlipGate.Update(Dt,false,Slippery,Speed,FMath::Max(Side,Counter),Deceleration);
        const auto Decision=SlipRisk.Update(Dt,Allowed,Slippery,Speed>20.f,Carrying,
            CVarMCSlipFirstRiskSeconds.GetValueOnGameThread(),CVarMCSlipRepeatRiskSeconds.GetValueOnGameThread());
        RollDue=Decision==EMCSlipperyDecision::Roll;
        const float AuthoredChance=CVarMCSlipFallChance.GetValueOnGameThread();
        const float Chance=FMath::IsFinite(AuthoredChance)?FMath::Clamp(AuthoredChance,0.f,1.f):.5f;
        Candidate=Decision==EMCSlipperyDecision::CarryFall || (RollDue && (Chance>=1.f || (Chance>0.f && FMath::FRand()<Chance)));
        if(RollDue && CVarMCSlipDebug.GetValueOnGameThread()!=0)
            UE_LOG(LogTemp,Display,TEXT("MC_SLIP_ROLL pawn=%s time=%.3f chance=%.3f fall=%d next_moving_seconds=%.3f"),
                *GetNameSafe(Tooth),ServerTime(),Chance,Candidate,CVarMCSlipRepeatRiskSeconds.GetValueOnGameThread());
    } else {
        SlipRisk.TickCooldown(Dt);SlipRisk.ResetExposure();
        Candidate=SlipGate.Update(Dt,Allowed && SlipRisk.Cooldown<=0.f,Slippery,Speed,FMath::Max(Side,Counter),Deceleration);
    }
    if(CVarMCSlipDebug.GetValueOnGameThread()!=0 && Slippery && Speed>120.f) {
        const uint32 Exclusions=(!CanAct()?1u:0u)|(!Tooth->GetController()?2u:0u)|(!Move?4u:0u)
            |(Move && !Move->IsMovingOnGround()?8u:0u)|(Tooth->HasFrozenLegs()?16u:0u)|(Tooth->IsFreezingToDeath()?32u:0u)
            |(Tooth->Status->PainAlpha()>=.01f?64u:0u)|(Tooth->AnimationSwim>=.01f?128u:0u)|(Tooth->AnimationClimb>=.01f?256u:0u)
            |(!Allowed && Supported?512u:0u)|(!Tooth->AnimationProfile || !Tooth->AnimationProfile->Glide?1024u:0u)
            |(!Tooth->AnimationProfile || !Tooth->AnimationProfile->GlideStrong?2048u:0u);
        const uint32 ContactMask=(Tooth->bBrushing?1u:0u)|(Tooth->IsPrimaryHeld()?2u:0u)|(Tooth->IsRowing()?4u:0u)|(Tooth->IsYawning()?8u:0u)
            |(Tooth->IsDashing()?16u:0u)|(Tooth->OrderJumpTarget?32u:0u)|(Tooth->ClingTooth?64u:0u)
            |(Tooth->FishingRod && Tooth->FishingRod->IsPresented()?128u:0u)|(IsValid(Tooth->RewardInteraction)?256u:0u)
            |(Tooth->Expression && Tooth->Expression->BodyAlpha()>.01f?512u:0u);
        UE_LOG(LogTemp,Display,TEXT("MC_SLIP_GATE pawn=%s time=%.3f dt=%.3f allowed=%d mask=0x%08x contact=0x%08x speed=%.1f side=%.3f counter=%.3f decel=%.1f balance=%.3f severe=%.3f cooldown=%.3f candidate=%d mode=%d carrying=%d exposure=%.3f risk_cooldown=%.3f"),
            *GetNameSafe(Tooth),ServerTime(),Dt,Allowed,Exclusions,ContactMask,Speed,Side,Counter,Deceleration,SlipGate.BalanceAge,SlipGate.SevereAge,SlipGate.Cooldown,Candidate,RiskMode,Carrying,SlipRisk.Exposure,SlipRisk.Cooldown);
    }
    if(Candidate && !BeginSlipperyFall()) {
        // A blocked attempt did not spend an actual fall. Preparation was reset
        // by the candidate and still bounds retries; do not lock them for 6.5s.
        SlipGate.Cooldown=0.f;
        if(Carrying) SlipRisk.OnBlockedCarry();
        if(CVarMCSlipDebug.GetValueOnGameThread()!=0) UE_LOG(LogTemp,Display,TEXT("MC_SLIP_RETRY pawn=%s time=%.3f rejected_candidate_cooldown_cleared=1"),*GetNameSafe(Tooth),ServerTime());
    }
}
bool UMCToothPhysicsComponent::BeginSlipperyFall()
{
    const auto* Move=Tooth?Cast<UMCToothMovementComponent>(Tooth->GetCharacterMovement()):nullptr;
    const auto* Clip=Tooth && Tooth->AnimationProfile?Tooth->AnimationProfile->GlideStrong.Get():nullptr;
    const auto* Mesh=Tooth?Tooth->GetMesh()->GetSkeletalMeshAsset():nullptr;
    const float ClipLength=Clip?float(Clip->GetPlayLength()):0.f;
    const uint32 RejectMask=(!Tooth?1u:0u)|(Tooth && !Tooth->HasAuthority()?2u:0u)|(Tooth && !Tooth->GetController()?4u:0u)
        |(!CanAct()?8u:0u)|(!Move?16u:0u)|(Move && !Move->IsMovingOnGround()?32u:0u)
        |(Move && Move->GroundSurface!=EMCGroundSurface::Slippery?64u:0u)|(!Clip?128u:0u)
        |(!FMath::IsFinite(ClipLength) || ClipLength<.5f || ClipLength>3.f?256u:0u)|(!Mesh?512u:0u)
        |(Clip && Mesh && Clip->GetSkeleton()!=Mesh->GetSkeleton()?1024u:0u)|(Tooth && Tooth->HasFrozenLegs()?2048u:0u)
        |(Tooth && Tooth->IsFreezingToDeath()?4096u:0u)|(Tooth && Tooth->SwallowedBy?8192u:0u)|(Tooth && Tooth->MimicCaptor?16384u:0u);
    if(RejectMask) {
        if(CVarMCSlipDebug.GetValueOnGameThread()!=0) UE_LOG(LogTemp,Display,TEXT("MC_SLIP_REJECT pawn=%s phase=entry mask=0x%08x clip_seconds=%.3f"),*GetNameSafe(Tooth),RejectMask,ClipLength);
        return false;
    }
    // The unchanged capsule is conservative vertically but narrower than the
    // reclined mesh. Reject a fall where the complete authored footprint cannot fit.
    const float Scale=Tooth->GetActorScale3D().GetAbsMax();
    const FVector FloorNormal=Move->CurrentFloor.HitResult.ImpactNormal.GetSafeNormal();
    const FVector FloorPoint=Move->CurrentFloor.HitResult.ImpactPoint;
    const FQuat FloorRotation=FQuat::FindBetweenNormals(FVector::UpVector,FloorNormal.IsNearlyZero()?FVector::UpVector:FloorNormal)*Tooth->GetActorQuat();
    FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(MCSlipClearance),false,Tooth);
    // The load is released by EnterSlipping. It must not block its own carrier's
    // fall footprint before that release has happened.
    const auto IgnoreCarried=[&](AMCFoodActor* Food) {
        if(IsValid(Food) && (Food->StackCarrier==Tooth || Food->Holders.Contains(Tooth))) ClearanceParams.AddIgnoredActor(Food);
    };
    if(Tooth->FoodCollection) for(const auto& Food:Tooth->FoodCollection->Pieces) IgnoreCarried(Food);
    IgnoreCarried(Tooth->HeldFood);
    if(Tooth->Grip) { IgnoreCarried(Tooth->Grip->Frame.Food);IgnoreCarried(Tooth->Grip->Secondary.Food); }
    // A curved supporting surface can enter its own tangent footprint. Ignore
    // only that walkable component here; floor queries and capsule motion retain it.
    if(Move->IsWalkable(Move->CurrentFloor.HitResult)) ClearanceParams.AddIgnoredComponent(Move->CurrentFloor.HitResult.GetComponent());
    const auto* Capsule=Tooth->GetCapsuleComponent();
    const FVector ClearanceCenter=FloorPoint+FloorNormal*70.f*Scale;
    const FCollisionShape ClearanceShape=FCollisionShape::MakeBox(FVector(100,100,68)*Scale);
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    bool Blocked=false;
    if(CVarMCSlipDebug.GetValueOnGameThread()!=0) {
        TArray<FOverlapResult> Overlaps;
        Blocked=GetWorld()->OverlapMultiByChannel(Overlaps,ClearanceCenter,FloorRotation,Capsule->GetCollisionObjectType(),ClearanceShape,ClearanceParams,Responses);
        if(Blocked) for(const auto& Hit:Overlaps) if(Hit.bBlockingHit)
            UE_LOG(LogTemp,Display,TEXT("MC_SLIP_REJECT pawn=%s phase=clearance actor=%s component=%s support_component=%s same_support=%d center=%s floor=%s normal=%s"),
                *GetNameSafe(Tooth),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*GetNameSafe(Move->CurrentFloor.HitResult.GetComponent()),
                Hit.GetComponent()==Move->CurrentFloor.HitResult.GetComponent(),*ClearanceCenter.ToString(),*FloorPoint.ToString(),*FloorNormal.ToString());
    } else Blocked=GetWorld()->OverlapBlockingTestByChannel(ClearanceCenter,FloorRotation,Capsule->GetCollisionObjectType(),ClearanceShape,ClearanceParams,Responses);
    if(Blocked) return false;
    SlipVelocity=Tooth->GetVelocity().GetClampedToMaxSize2D(460.f);SlipVelocity.Z=0;
    Frame.CapsuleLocation=Tooth->GetActorLocation();Frame.CapsuleYaw=Tooth->GetActorRotation().Yaw;
    Frame.Bones.Reset();Frame.bFromSlipperyFall=true;Frame.bAuthoredSlipRecovery=true;
    Frame.SlipFloorNormal=Move->CurrentFloor.HitResult.ImpactNormal.GetSafeNormal();
    if(FVector(Frame.SlipFloorNormal).IsNearlyZero()) Frame.SlipFloorNormal=FVector::UpVector;
    Frame.SlipFloorPoint=Move->CurrentFloor.HitResult.ImpactPoint;
    SlipGate.Cooldown=6.5f;SlipRisk.OnFall();SetState(EMCBodyState::Slipping);EnterSlipping();return true;
}
void UMCToothPhysicsComponent::EnterSlipping()
{
    // The cartoon release must precede CancelGameplayInput, which normally
    // empties the collection. The same actors are released; none are spawned.
    if(Tooth->HasAuthority()) {
        MCSlipperyFood::ReleaseWithCartoonPop(Tooth);
        Tooth->CancelGameplayInput();Tooth->FoodCollection->Stop(false);Tooth->DropFood();Tooth->ResetContact();
    }
    Tooth->bBrushing=false;Tooth->bHandling=false;
    Tooth->GetCharacterMovement()->StopMovementImmediately();Tooth->GetCharacterMovement()->DisableMovement();
    SetMuscles(false);auto* Mesh=Tooth->GetMesh();Mesh->SetAllBodiesSimulatePhysics(false);Mesh->SetAllBodiesPhysicsBlendWeight(0.f);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Tooth->SetReplicateMovement(true);SlipSendAccumulator=0;
}
bool UMCToothPhysicsComponent::GetAuthoredSlipSample(float& Time,float& Alpha) const
{
    const auto* Clip=Tooth && Tooth->AnimationProfile?Tooth->AnimationProfile->GlideStrong.Get():nullptr;
    if(!Clip || !Frame.bAuthoredSlipRecovery) return false;
    if(LocalState==EMCBodyState::Slipping) {Time=FMath::Clamp(ServerTime()-Frame.StateStartedAt,0.f,Clip->GetPlayLength());Alpha=FMath::SmoothStep(0.f,.1f,Time);return true;}
    if(LocalState==EMCBodyState::Recovering) {Time=Clip->GetPlayLength();Alpha=1-RecoveryAlpha();return true;}
    return false;
}
void UMCToothPhysicsComponent::BuildSlippingPose(TArray<FTransform>& Pose) const
{
    float Time=0,Alpha=0;if(!GetAuthoredSlipSample(Time,Alpha) || LocalState!=EMCBodyState::Slipping) return;
    const auto* Clip=Tooth->AnimationProfile->GlideStrong.Get();const auto* Mesh=Tooth->GetMesh();
    const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();const auto& Source=Clip->GetSkeleton()->GetReferenceSkeleton();
    FAnimExtractContext Context(Time,false);
    for(int32 I=0;I<Pose.Num();++I) {
        const FName Name=Ref.GetBoneName(I);const FString Bone=Name.ToString();
        if(Bone.StartsWith(TEXT("c_")) && !Bone.Contains(TEXT("index")) && !Bone.Contains(TEXT("thumb"))) continue;
        const int32 Index=Source.FindBoneIndex(Name);if(Index<0) continue;
        FTransform Sample,Blended;Clip->GetBoneTransform(Sample,FSkeletonPoseBoneIndex(Index),Context,false);
        Blended.Blend(Pose[I],Sample,Alpha);Pose[I]=Blended;
    }
    const int32 Body=Ref.FindBoneIndex(Tooth->RigBone(TEXT("body")));if(Body<0) return;
    const FTransform World=Mesh->GetComponentTransform();
    const float Lift=FMath::Max(0.f,Clip->EvaluateCurveData(TEXT("MCSlide_ClearanceCm"),Context,false));
    Pose[Body].AddToTranslation(World.InverseTransformVectorNoScale(FVector(0,0,Lift)));
    const FVector Normal=FVector(Frame.SlipFloorNormal).GetSafeNormal();
    if(!Normal.IsNearlyZero()) {
        FTransform Parent=FTransform::Identity;
        for(int32 I=Ref.GetParentIndex(Body);I>=0;I=Ref.GetParentIndex(I)) Parent=Parent*Pose[I];
        FTransform BodyWorld=Pose[Body]*Parent*World;
        const FQuat Tilt=FQuat::FindBetweenNormals(FVector::UpVector,Normal);
        const FVector Pivot=Frame.SlipFloorPoint;
        BodyWorld.SetLocation(Pivot+Tilt.RotateVector(BodyWorld.GetLocation()-Pivot));
        BodyWorld.SetRotation((Tilt*BodyWorld.GetRotation()).GetNormalized());
        Pose[Body]=BodyWorld.GetRelativeTransform(Parent*World);
    }
}
void UMCToothPhysicsComponent::SlipToRagdoll()
{
    const FVector Momentum=SlipVelocity;SetState(EMCBodyState::Ragdoll);Frame.bFromSlipperyFall=true;EnterRagdoll();
    Tooth->GetMesh()->SetAllPhysicsLinearVelocity(Momentum);LastHitTime=ServerTime();CaptureFrame();Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::TickSlipping(float Dt)
{
    if(!Tooth->HasAuthority()) return;
    if(!Tooth->Status->IsAlive()) {EnterDeath();return;}
    if(!Tooth->GetController()) {SlipToRagdoll();return;}
    if(Tooth->HasFrozenLegs() || Tooth->IsFreezingToDeath()) {
        Frame.bFromSlipperyFall=false;Frame.bAuthoredSlipRecovery=false;SetState(EMCBodyState::Standing);EnterStanding();
        if(Tooth->IsFreezingToDeath()) {Tooth->GetCharacterMovement()->DisableMovement();SetMuscles(false);Tooth->GetMesh()->SetAllBodiesSimulatePhysics(false);}
        return;
    }
    auto* Move=Tooth->GetCharacterMovement();const auto* Capsule=Tooth->GetCapsuleComponent();
    FHitResult Floor;FCollisionQueryParams Params(SCENE_QUERY_STAT(MCSlipFloor),false,Tooth);
    const FVector P=Tooth->GetActorLocation();
    const bool Supported=GetWorld()->LineTraceSingleByObjectType(Floor,P,P-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+55.f),FCollisionObjectQueryParams(ECC_WorldStatic),Params)
        && Move->IsWalkable(Floor);
    if(!Supported) {SlipToRagdoll();return;}
    Frame.SlipFloorNormal=Floor.ImpactNormal;Frame.SlipFloorPoint=Floor.ImpactPoint;
    FVector Delta=FVector::VectorPlaneProject(SlipVelocity,Floor.ImpactNormal)*FMath::Max(0.f,Dt);
    const float Scale=Tooth->GetActorScale3D().GetAbsMax();
    const FVector Footprint=Floor.ImpactPoint+Floor.ImpactNormal*70.f*Scale;
    const FQuat FootprintRotation=FQuat::FindBetweenNormals(FVector::UpVector,Floor.ImpactNormal)*Tooth->GetActorQuat();
    FCollisionQueryParams ClearanceParams=Params;ClearanceParams.AddIgnoredComponent(Floor.GetComponent());
    FHitResult Clearance;
    if(!Delta.IsNearlyZero() && GetWorld()->SweepSingleByChannel(Clearance,Footprint,Footprint+Delta,FootprintRotation,Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeBox(FVector(100,100,68)*Scale),ClearanceParams,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) {
        Delta*=FMath::Max(0.f,Clearance.Time-.02f);SlipVelocity=FVector::ZeroVector;
    }
    FHitResult Hit;Move->SafeMoveUpdatedComponent(Delta,Tooth->GetActorQuat(),true,Hit);
    if(Hit.IsValidBlockingHit()) SlipVelocity=FVector::VectorPlaneProject(SlipVelocity,Hit.Normal)*.35f;
    SlipVelocity*=FMath::Exp(-3.f*FMath::Max(0.f,Dt));
    SlipSendAccumulator+=Dt;
    if(SlipSendAccumulator>=.05f) {SlipSendAccumulator=0;Frame.CapsuleLocation=Tooth->GetActorLocation();Tooth->ForceNetUpdate();}
    const auto* Clip=Tooth->AnimationProfile?Tooth->AnimationProfile->GlideStrong.Get():nullptr;
    const float Duration=Clip?Clip->GetPlayLength():1.708333f;
    if(ServerTime()-Frame.StateStartedAt>=Duration+.2f) {
        const auto& Ref=Tooth->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();TArray<FTransform> Final=Ref.GetRefBonePose();BuildSlippingPose(Final);
        Frame.Bones.SetNum(Final.Num());for(int32 I=0;I<Final.Num();++I) {Frame.Bones[I].Position=Final[I].GetLocation();Frame.Bones[I].Rotation=Final[I].Rotator();}
        Frame.CapsuleLocation=Tooth->GetActorLocation();SetState(EMCBodyState::Recovering);EnterRecovery();
    }
}
void UMCToothPhysicsComponent::EnterRagdoll()
{
    ++KnockdownCount;
    if(auto* Perks=UMCPerkComponent::ForActor(Tooth)) Perks->OnRogueKnockdown();
    Tooth->bBrushing=false; Tooth->bHandling=false;
    Tooth->DropFood(); Tooth->ResetContact();
    Tooth->GetCharacterMovement()->StopMovementImmediately(); Tooth->GetCharacterMovement()->DisableMovement();
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Tooth->SetReplicateMovement(false);
    auto* Mesh=Tooth->GetMesh();
    Mesh->PhysicsTransformUpdateMode=EPhysicsTransformUpdateMode::SimulationUpatesComponentTransform;
    SetMuscles(false); Mesh->SetAllBodiesSimulatePhysics(false);
    ConfigureGripConstraints();
    Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    // Full knockdowns need world support after standing limbs ignored scenery.
    Mesh->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
    Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
    Mesh->SetMorphTarget(TEXT("Squash"),0); Mesh->SetMorphTarget(TEXT("Stretch"),0);
    if (Tooth->HasAuthority())
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Mesh->SetAllBodiesSimulatePhysics(true); Mesh->SetAllBodiesPhysicsBlendWeight(1.f); Mesh->WakeAllRigidBodies();
    }
    else { Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetAllBodiesPhysicsBlendWeight(0.f); }
    if (Tooth->SoundPalette) Tooth->SoundPalette->Play(this,TEXT("Fall"),Tooth->GetActorLocation());
}
void UMCToothPhysicsComponent::ApplyHit(FVector VelocityChange,FVector HitLocation)
{
    if (!Tooth || !Tooth->HasAuthority() || Tooth->SwallowedBy || Tooth->IsMimicCaptured() || LocalState==EMCBodyState::Recovering || ServerTime()<RecoveryInvulnerableUntil) return;
    if (VelocityChange.ContainsNaN() || HitLocation.ContainsNaN()) return;
    VelocityChange=VelocityChange.GetClampedToMaxSize(1400.f);
    Tooth->NotifyCameraImpact(float(VelocityChange.Size()),VelocityChange);
    if(!VelocityChange.IsNearlyZero()) Tooth->FoodCollection->Spill(VelocityChange*.45f);
    if (Tooth->Gaze) Tooth->Gaze->NoticePoint(HitLocation-VelocityChange.GetSafeNormal2D()*140+FVector(0,0,40),1);
    if (LocalState==EMCBodyState::Standing && VelocityChange.Size()<Settings.FallThreshold)
    {
        Tooth->LaunchCharacter(VelocityChange,false,false);
        Tooth->GetMesh()->AddImpulseToAllBodiesBelow(VelocityChange*0.35f,Tooth->RigBone(TEXT("body")),true,false);
        return;
    }
    if (LocalState!=EMCBodyState::Ragdoll)
    {
        const FVector Momentum=Tooth->GetVelocity(); SetState(EMCBodyState::Ragdoll); EnterRagdoll();
        Tooth->GetMesh()->SetAllPhysicsLinearVelocity(Momentum);
    }
    LastHitTime=ServerTime();
    Tooth->GetMesh()->AddImpulseToAllBodiesBelow(VelocityChange,Tooth->RigBone(TEXT("body")),true,true);
    FVector Spin=FVector::CrossProduct(FVector::UpVector,VelocityChange.GetSafeNormal2D())*300.f;
    Spin.Z=FMath::Clamp((HitLocation-PhysicalLocation()).Y*3.f,-120.f,120.f);
    Tooth->GetMesh()->SetAllPhysicsAngularVelocityInDegrees(Spin,true);
    CaptureFrame(); Tooth->ForceNetUpdate();
}
FVector UMCToothPhysicsComponent::PhysicalLocation() const
{
    return Tooth?Tooth->GetMesh()->GetBoneLocation(Tooth->RigBone(TEXT("body"))):FVector::ZeroVector;
}
void UMCToothPhysicsComponent::CaptureFrame()
{
    auto* Mesh=Tooth->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    Frame.MeshLocation=Mesh->GetComponentLocation(); Frame.MeshRotation=Mesh->GetComponentRotation();
    Frame.CapsuleLocation=Tooth->GetActorLocation(); Frame.CapsuleYaw=Tooth->GetActorRotation().Yaw;
    Frame.Bones.SetNum(Ref.GetNum());
    for (int32 I=0;I<Ref.GetNum();++I)
    {
        const int32 Parent=Ref.GetParentIndex(I);
        const FTransform Local=Mesh->GetBoneTransform(I).GetRelativeTransform(Parent>=0?Mesh->GetBoneTransform(Parent):Mesh->GetComponentTransform());
        Frame.Bones[I].Position=Local.GetLocation(); Frame.Bones[I].Rotation=Local.Rotator();
    }
}
bool UMCToothPhysicsComponent::TryRecover()
{
    if (!Tooth || !Tooth->Status->IsAlive() || !Tooth->HasAuthority() || Tooth->SwallowedBy || Tooth->IsMimicCaptured() || LocalState!=EMCBodyState::Ragdoll
        || (Frame.bFromSlipperyFall && !Tooth->GetController())) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGetUp),false,Tooth);
    const FVector Center=PhysicalLocation();
    auto* Movement=Cast<UMCToothMovementComponent>(Tooth->GetCharacterMovement());
    auto* Water=Movement?Movement->DeepWaterAt(Center,true):nullptr;
    const auto* Capsule=Tooth->GetCapsuleComponent();
    const float Half=Capsule->GetScaledCapsuleHalfHeight();
    const float Radius=Capsule->GetScaledCapsuleRadius();
    const FCollisionResponseParams CapsuleResponses(Capsule->GetCollisionResponseToChannels());
    FVector Destination=Center;
    bool bClear=false;
    // Search independently at each point: a steep or obstructed centre must not
    // suppress all retries, and each standing pose needs its own floor height.
    TArray<FVector,TInlineAllocator<25>> Offsets;Offsets.Add(FVector::ZeroVector);
    for(int32 Ring=1;Ring<=3;++Ring) for(int32 I=0;I<8;++I) {
        const float Angle=I*PI/4,Distance=Ring*(Radius*2+12);
        Offsets.Add(FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0));
    }
    for(const FVector Offset:Offsets)
    {
        const FVector Probe=Center+Offset;FVector Candidate;
        FVector PathEnd=Probe;
        if(Water) {
            if(!Movement || Movement->DeepWaterAt(Probe,true)!=Water) continue;
            Candidate=FVector(Probe.X,Probe.Y,Movement->SwimmingRestHeightAt(Probe));
        } else {
            FHitResult Support;FCollisionQueryParams FloorParams=Params;bool bSupported=false;
            for(int32 Retry=0;Retry<32;++Retry) {
                if(!GetWorld()->LineTraceSingleByChannel(Support,Probe+FVector(0,0,60),Probe-FVector(0,0,350),ECC_WorldStatic,FloorParams)) break;
                // Players and loose food can mask the real floor under a ragdoll.
                // They still obstruct the capsule below; never stand on their heads.
                if(Cast<AMCFoodActor>(Support.GetActor()) || Cast<AMCToothCharacter>(Support.GetActor())) {
                    FloorParams.AddIgnoredActor(Support.GetActor());continue;
                }
                bSupported=Movement?Movement->IsWalkable(Support):Support.ImpactNormal.Z>=.65f;
                break;
            }
            if(!bSupported) continue;
            Candidate=Support.ImpactPoint+FVector(0,0,Half+3);
            PathEnd=Support.ImpactPoint+FVector(0,0,Radius+3);
        }
        // A per-component "Custom" preset has no global profile to query.
        // Match the standing capsule's actual channel and response overrides.
        if(GetWorld()->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,Capsule->GetCollisionObjectType(),FCollisionShape::MakeCapsule(Radius,Half),Params,CapsuleResponses)) continue;
        FHitResult Obstacle;
        if(!Offset.IsNearlyZero() && GetWorld()->SweepSingleByChannel(Obstacle,Center,PathEnd,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(8),Params)) continue;
        // The capsule fits and the fallen body can reach it without crossing a wall.
        { Destination=Candidate; bClear=true; break; }
    }
    if (!bClear) return false;
    CaptureFrame();
    // Convert only the root: other bone transforms are already parent-relative.
    const FTransform NewMesh=Tooth->StandingMeshTransform()*FTransform(FRotator(0,Tooth->GetActorRotation().Yaw,0),Destination);
    const FTransform RootWorld=Tooth->GetMesh()->GetBoneTransform(0);
    const FTransform RootLocal=RootWorld.GetRelativeTransform(NewMesh);
    Frame.Bones[0].Position=RootLocal.GetLocation(); Frame.Bones[0].Rotation=RootLocal.Rotator();
    Frame.CapsuleLocation=Destination; Frame.MeshLocation=NewMesh.GetLocation(); Frame.MeshRotation=NewMesh.Rotator();
    SetState(EMCBodyState::Recovering); EnterRecovery(); return true;
}
void UMCToothPhysicsComponent::EnterRecovery()
{
    ++RecoveryCount;
    auto* Mesh=Tooth->GetMesh(); SetMuscles(false); Mesh->SetAllBodiesSimulatePhysics(false); Mesh->SetAllBodiesPhysicsBlendWeight(0.f);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(!Frame.bAuthoredSlipRecovery) Tooth->SetActorLocationAndRotation(Frame.CapsuleLocation,FRotator(0,Frame.CapsuleYaw,0),false,nullptr,ETeleportType::TeleportPhysics);
    Mesh->AttachToComponent(Tooth->GetCapsuleComponent(),FAttachmentTransformRules::KeepWorldTransform);
    Mesh->SetRelativeTransform(Tooth->StandingMeshTransform());
    DisplayPose.Reset(); for (const auto& Bone:Frame.Bones) DisplayPose.Add(Bone.Transform());
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Tooth->GetCharacterMovement()->DisableMovement();
    if (Tooth->SoundPalette) Tooth->SoundPalette->Play(this,TEXT("StandUp"),Tooth->GetActorLocation());
}
void UMCToothPhysicsComponent::EnterStanding()
{
    if (!Tooth || !Tooth->GetMesh()->GetSkeletalMeshAsset()) return;
    auto* Mesh=Tooth->GetMesh(); Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->PhysicsTransformUpdateMode=EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
    Mesh->AttachToComponent(Tooth->GetCapsuleComponent(),FAttachmentTransformRules::KeepWorldTransform);
    Mesh->SetRelativeTransform(Tooth->StandingMeshTransform());
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    // The torso also follows a finite-strength balance motor: impacts and turns
    // can displace it while CharacterMovement supplies collision-safe locomotion.
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    // The capsule supplies floor and obstacle contact. Active limbs must not
    // catch on static scenery and throw the standing tooth out of balance.
    Mesh->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Ignore);
    // The capsule receives incoming food while standing; dangling toes must not kick it out of reach.
    Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Ignore);
    Mesh->SetAllBodiesBelowSimulatePhysics(Tooth->RigBone(TEXT("body")),true,true);
    Mesh->SetAllBodiesBelowPhysicsBlendWeight(Tooth->RigBone(TEXT("body")),1.f,false,true);
    bGripLeft=bGripRight=bPhysicalGripLeft=bPhysicalGripRight=false; StandingPhysicsWeight=1; ArmPhysicsWeights[0]=ArmPhysicsWeights[1]=1;
    ConfigureGripConstraints();
    OnRep_ActiveRagdollMode(); DisplayPose.Reset();
    ArmSettleSeconds[0]=ArmSettleSeconds[1]=0;
    GripReleaseSeconds[0]=GripReleaseSeconds[1]=0;
    auto* Movement=Cast<UMCToothMovementComponent>(Tooth->GetCharacterMovement());
    Tooth->GetCharacterMovement()->SetMovementMode(Movement && Movement->DeepWaterAt(Tooth->GetActorLocation(),true)?MOVE_Swimming:MOVE_Walking); Tooth->SetReplicateMovement(true);
    RecoveryInvulnerableUntil=ServerTime()+0.6f;
}
void UMCToothPhysicsComponent::SetGripArms(bool Left,bool Right,bool PhysicalLeft,bool PhysicalRight)
{
    if (!Tooth || LocalState!=EMCBodyState::Standing || Tooth->SwallowedBy || Tooth->MimicCaptor) { bGripLeft=bGripRight=bPhysicalGripLeft=bPhysicalGripRight=false; return; }
    PhysicalLeft&=Left; PhysicalRight&=Right;
    bool* Requested[]={&PhysicalLeft,&PhysicalRight}; const bool Occupied[]={Left,Right};
    for(int32 I=0;I<2;++I) {
        const bool WasPhysical=I==0?bPhysicalGripLeft:bPhysicalGripRight;
        if(*Requested[I]) GripReleaseSeconds[I]=.48f;
        else if(!Occupied[I] && WasPhysical && GripReleaseSeconds[I]>0) {
            GripReleaseSeconds[I]=FMath::Max(0.f,GripReleaseSeconds[I]-GetWorld()->GetDeltaSeconds());
            *Requested[I]=true;
        } else GripReleaseSeconds[I]=0;
    }
    if(bPhysicalGripLeft!=PhysicalLeft || bPhysicalGripRight!=PhysicalRight) {
        if(bPhysicalGripLeft && !PhysicalLeft) ArmSettleSeconds[0]=.18f;
        if(bPhysicalGripRight && !PhysicalRight) ArmSettleSeconds[1]=.18f;
        if(Muscles) for(int32 I=0;I<2;++I) if((I==0?PhysicalLeft && !bPhysicalGripLeft:PhysicalRight && !bPhysicalGripRight)) {
            const FName Role=I==0?TEXT("arm_l"):TEXT("arm_r");
            const auto* Mesh=Tooth->GetMesh(); const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            for(const auto& Joint:JointTargets) if(Joint.Role==Role) {
                const FTransform Parent=Mesh->GetSocketTransform(Ref.GetBoneName(Joint.Parent));
                const FTransform Child=Mesh->GetSocketTransform(Ref.GetBoneName(Joint.Child));
                Muscles->SetControlTargetPoses(Joint.Control,Parent.GetLocation(),Parent.Rotator(),Child.GetLocation(),Child.Rotator(),0,true);
            }
            const FTransform Body=Mesh->GetSocketTransform(Tooth->RigBone(TEXT("body")));
            const FTransform Hand=Mesh->GetSocketTransform(Tooth->RigBone(I==0?TEXT("hand_l"):TEXT("hand_r")));
            Muscles->SetControlTargetPoses(HandControls[I],Body.GetLocation(),Body.Rotator(),Hand.GetLocation(),Hand.Rotator(),0,true);
        }
        bPhysicalGripLeft=PhysicalLeft; bPhysicalGripRight=PhysicalRight;
        // Reaching with the second hand must not reset the first hand's motor
        // to reference pose. Reset offsets only when returning to the cache.
        if(!UsesActiveMuscles() && Muscles)
            for(const auto& Joint:JointTargets) Muscles->SetControlTarget(Joint.Control,FPhysicsControlTarget(),false);
        OnRep_Settings();
    }
    // Contact targets share the animated torso and foot-IK space. The root stays
    // kinematic during an object grip; physical legs remain hidden while reaching
    // so they cannot fight the foot contacts. Their mass and constraints still simulate.
    const bool Active=UsesActiveMuscles();
    const bool Dashing=Tooth->IsDashing();
    const float Goal=Dashing || Left || Right || bPhysicalGripLeft || bPhysicalGripRight?0.f:Active?1.f:.25f;
    // Let the physical pose settle before revealing it after release. A quick
    // drop/reacquire otherwise exposes the lagging feet for a few frames.
    const float BlendRate=Goal==0?16.f:6.f;
    // The short dash pose owns the visible limbs immediately; the hidden bodies
    // keep simulating and ease back into view after the landing pose.
    StandingPhysicsWeight=Dashing?0.f:FMath::Lerp(StandingPhysicsWeight,Goal,1.f-FMath::Exp(-BlendRate*GetWorld()->GetDeltaSeconds()));
    if (FMath::Abs(StandingPhysicsWeight-Goal)<.001f) StandingPhysicsWeight=Goal;
    if (auto* Body=Tooth->GetMesh()->GetBodyInstance(Tooth->RigBone(TEXT("body")))) Body->PhysicsBlendWeight=Active?0.f:StandingPhysicsWeight;
    for (const FName Role:{FName("leg_l"),FName("leg_r")})
        Tooth->GetMesh()->SetAllBodiesBelowPhysicsBlendWeight(Tooth->RigBone(Role),StandingPhysicsWeight*(Active?1.f:.2f),false,true);
    bool* Flags[]={&bGripLeft,&bGripRight}; const bool Values[]={Left,Right};
    for (int32 I=0;I<2;++I)
    {
        const FName Role=I==0?TEXT("arm_l"):TEXT("arm_r");
        // Object contacts keep the limb simulated and driven. Precision tools
        // use the authored contact pose while the hidden physical limb settles.
        if (*Flags[I]!=Values[I])
        {
            if (Muscles) Muscles->SetControlsInSetEnabled(Role,!Values[I] || (I==0?bPhysicalGripLeft:bPhysicalGripRight));
            // Releasing a stretched contact changes the cached motor target.
            // Give the dynamic limb time to settle before making it visible.
            ArmSettleSeconds[I]=Values[I]?0.f:.18f;
            *Flags[I]=Values[I];
        }
        ArmSettleSeconds[I]=FMath::Max(0.f,ArmSettleSeconds[I]-GetWorld()->GetDeltaSeconds());
        const bool Physical=I==0?bPhysicalGripLeft:bPhysicalGripRight;
        const float ArmGoal=Dashing?0.f:Physical?1.f:Values[I] || ArmSettleSeconds[I]>0?0.f:Active?1.f:.7f;
        ArmPhysicsWeights[I]=Dashing?0.f:FMath::Lerp(ArmPhysicsWeights[I],ArmGoal,1.f-FMath::Exp(-(ArmGoal==0?20.f:8.f)*GetWorld()->GetDeltaSeconds()));
        if (FMath::Abs(ArmPhysicsWeights[I]-ArmGoal)<.001f) ArmPhysicsWeights[I]=ArmGoal;
        Tooth->GetMesh()->SetAllBodiesBelowPhysicsBlendWeight(Tooth->RigBone(Role),ArmPhysicsWeights[I],false,true);
    }
}
float UMCToothPhysicsComponent::EffectiveRecoverySeconds(float Base) const
{const auto* Perks=UMCPerkComponent::ForActor(GetOwner());return Perks?Perks->RogueValue(EMCRogueStat::RagdollDuration,Base):Base;}
float UMCToothPhysicsComponent::RecoveryAlpha() const
{
    const float Duration=Frame.bFromSlipperyFall?FMath::Clamp(EffectiveRecoverySeconds(Settings.GetUpSeconds),.35f,1.25f):EffectiveRecoverySeconds(Settings.GetUpSeconds);
    return FMath::Clamp((ServerTime()-Frame.StateStartedAt)/Duration,0.f,1.f);
}
void UMCToothPhysicsComponent::BuildPresentationPose(TArray<FTransform>& Pose) const
{
    if(LocalState==EMCBodyState::Slipping) {BuildSlippingPose(Pose);return;}
    if (DisplayPose.Num()!=Pose.Num()) return;
    if (LocalState==EMCBodyState::Ragdoll && !Tooth->HasAuthority()) Pose=DisplayPose;
    else if (LocalState==EMCBodyState::Recovering)
    {
        const float T=RecoveryAlpha(); const float Ease=T*T*(3.f-2.f*T);
        for (int32 I=0;I<Pose.Num();++I) { FTransform Blended; Blended.Blend(DisplayPose[I],Pose[I],Ease); Pose[I]=Blended; }
    }
}
void UMCToothPhysicsComponent::OnRep_Frame()
{
    if (!Tooth || Tooth->SwallowedBy || Tooth->MimicCaptor) return;
    if (LocalState!=Frame.State)
    {
        LocalState=Frame.State;
        if (LocalState==EMCBodyState::Slipping) EnterSlipping();
        else if (LocalState==EMCBodyState::Ragdoll) { EnterRagdoll(); DisplayPose.Reset(); }
        else if (LocalState==EMCBodyState::Recovering) EnterRecovery();
        else
        {
            if(!Frame.bAuthoredSlipRecovery) Tooth->SetActorLocationAndRotation(Frame.CapsuleLocation,FRotator(0,Frame.CapsuleYaw,0),false,nullptr,ETeleportType::TeleportPhysics);
            EnterStanding();
        }
    }
}
void UMCToothPhysicsComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt,TickType,ThisTickFunction); if (!Tooth || Tooth->SwallowedBy || Tooth->MimicCaptor) return;
    UpdateSlipperyFallGate(Dt);
    if(LocalState==EMCBodyState::Slipping) {TickSlipping(Dt);return;}
    if(LocalState==EMCBodyState::Recovering && Frame.bFromSlipperyFall && Tooth->HasAuthority()) {
        if(!Tooth->Status->IsAlive()) {EnterDeath();return;}
        if(!Tooth->GetController()) {SlipToRagdoll();return;}
    }
    if (LocalState==EMCBodyState::Ragdoll)
    {
        if (Tooth->HasAuthority())
        {
            const FVector Center=PhysicalLocation();
            Tooth->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
            SendAccumulator+=Dt;
            if (SendAccumulator>=0.05f) { SendAccumulator=0; CaptureFrame(); Tooth->ForceNetUpdate(); }
            const float Age=ServerTime()-FMath::Max(Frame.StateStartedAt,LastHitTime);
            const float TotalAge=ServerTime()-Frame.StateStartedAt;
            // Repeated contacts may move the body but must not restart the stun forever.
            const float RecoveryDelay=EffectiveRecoverySeconds(Settings.RagdollSeconds);
            if (Tooth->Status->IsAlive() && (!Frame.bFromSlipperyFall || Tooth->GetController()) && ((Age>=RecoveryDelay && (Tooth->bInCoffee || Tooth->GetMesh()->GetPhysicsLinearVelocity(Tooth->RigBone(TEXT("body"))).Size()<160)) || TotalAge>=RecoveryDelay+3))
            {
                if (!TryRecover())
                {
                    const float Now=ServerTime();
                    if (BlockedRecoveryStartedAt<0.f) BlockedRecoveryStartedAt=Now;
                    // A body wedged on an unwalkable throat wall can have neither
                    // a clear get-up pose nor enough room to fall below the arena.
                    // Allow six seconds of normal retries, then use the ordinary
                    // death/reserve path. Contacts cannot restart this deadline.
                    constexpr float BlockedRecoverySeconds=6.f;
                    if (Now-BlockedRecoveryStartedAt>=BlockedRecoverySeconds)
                        Tooth->Status->Damage(Tooth->Status->State.MaxHealth,FVector::ForwardVector,nullptr,EMCRogueDamageKind::Ordinary,false);
                }
            }
            // The character's shared arena boundary samples PhysicalLocation
            // in every body state. A world-Z ragdoll-only plane could kill on a
            // lowered tongue and miss standing players outside the mouth.
        }
        else
        {
            const float Alpha=1.f-FMath::Exp(-20.f*Dt);
            Tooth->SetActorLocation(FMath::Lerp(Tooth->GetActorLocation(),FVector(Frame.CapsuleLocation),Alpha),false,nullptr,ETeleportType::TeleportPhysics);
            Tooth->GetMesh()->SetWorldLocationAndRotation(Frame.MeshLocation,Frame.MeshRotation,false,nullptr,ETeleportType::TeleportPhysics);
            if (DisplayPose.Num()!=Frame.Bones.Num()) { DisplayPose.Reset(); for (const auto& Bone:Frame.Bones) DisplayPose.Add(Bone.Transform()); }
            else for (int32 I=0;I<DisplayPose.Num();++I) { FTransform Result; Result.Blend(DisplayPose[I],Frame.Bones[I].Transform(),Alpha); DisplayPose[I]=Result; }
        }
    }
    else if (LocalState==EMCBodyState::Recovering && Tooth->HasAuthority() && Tooth->Status->IsAlive()
        && (!Frame.bFromSlipperyFall || Tooth->GetController()) && RecoveryAlpha()>=1)
    {
        Frame.CapsuleLocation=Tooth->GetActorLocation(); Frame.Bones.Reset(); SetState(EMCBodyState::Standing); EnterStanding();
    }
}
bool UMCToothPhysicsComponent::CanAct() const
{
    return LocalState==EMCBodyState::Standing && (!Tooth || (!Tooth->IsMimicCaptured() && Tooth->Status->IsAlive()));
}
void UMCToothPhysicsComponent::SetThroatCaptured(bool Captured)
{
    if(!Tooth) return;
    Frame.bFromSlipperyFall=false;Frame.bAuthoredSlipRecovery=false;
    if(Tooth->HasAuthority()) {
        Frame.CapsuleLocation=Tooth->GetActorLocation(); Frame.Bones.Reset(); SetState(EMCBodyState::Standing);
    } else LocalState=EMCBodyState::Standing;
    // A gulp owns the complete body, including a player knocked down beforehand.
    EnterStanding();
    if(Captured) {
        SetMuscles(false); Tooth->GetMesh()->SetAllBodiesSimulatePhysics(false);
        Tooth->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}
void UMCToothPhysicsComponent::EnterDeath()
{
    if (!Tooth || !Tooth->HasAuthority()) return;
    if (LocalState!=EMCBodyState::Ragdoll) { SetState(EMCBodyState::Ragdoll); EnterRagdoll(); }
    Tooth->GetMesh()->AddImpulseToAllBodiesBelow(FVector(0,0,220),Tooth->RigBone(TEXT("body")),true,true);
    CaptureFrame(); Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCToothPhysicsComponent,Frame); DOREPLIFETIME(UMCToothPhysicsComponent,Settings);
    DOREPLIFETIME(UMCToothPhysicsComponent,ActiveRagdollMode);
}
