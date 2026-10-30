#include "MCAirSkillAction.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"

AMCAirSkillAction::AMCAirSkillAction()
{
    PropAsset=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/Props/SM_Buster_Air.SM_Buster_Air")));
    PropTransform=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(.55));
    // Buster guide root.x pose/rest registered to the current hero mesh. The
    // reduced prop preserves the guide's right wrist; scale is applied once.
    HeldMount=FTransform(FQuat(0,0,-.208149508,.978097022).GetNormalized(),FVector(47.77260,21.21782,15.97477));
    bHasLeftGrip=true;bHasRightGrip=true;
    RightGripTransform=FTransform(FQuat(-.050949611,.023708601,.694874763,.716931760).GetNormalized(),FVector(-11.817457,3.768684,.259149));
    LeftGripTransform=FTransform(FQuat(.308887243,-.245725051,.744695902,.538178444).GetNormalized(),FVector(5.409300,-48.653744,-27.043056));
    LoopVFX=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Air_Loop.NS_Air_Loop")));
    ImpactVFX=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Air_Impact.NS_Air_Impact")));
    WarningVFX=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Air_Warning.NS_Air_Warning")));
    EndVFX=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Air_Rupture.NS_Air_Rupture")));
}
UMCAirSkillEffect::UMCAirSkillEffect() { ActionClass=AMCAirSkillAction::StaticClass(); }
bool AMCAirSkillAction::CanStart() const
{
    return Super::CanStart() && Hero()->ToothPhysics && Hero()->ToothPhysics->CanAct()
        && !Hero()->IsSeatedOnRaft() && !Hero()->SwallowedBy && !Hero()->MimicCaptor;
}
void AMCAirSkillAction::StartCommitted()
{
    Super::StartCommitted();
    BagCapacity=FMath::Clamp(FMath::RoundToInt(Param(TEXT("BagCapacity"),6)),1,16);
    HoldStartedAt=Now();NextScanAt=Now();bSucking=true;
    UpdateHeldPose();UpdateLoopVFX(LastAim,0);StartLoop();ForceNetUpdate();
}
float AMCAirSkillAction::GetFillFraction() const {return FMath::Clamp(float(StoredCount)/FMath::Max(1,BagCapacity),0.f,1.f);}
void AMCAirSkillAction::UpdateHeldPose()
{
    if(!Hero()) return;
    FVector ViewOrigin,Direction;
    if(!GetAim(ViewOrigin,Direction)) Direction=LastAim;
    FRotator Aim=Direction.Rotation();Aim.Pitch=FMath::Clamp(Aim.Pitch,-35.f,25.f);
    FTransform Mount=HeldMount;
    // Retain old table sliders as adjustments, without reintroducing the old
    // shoulder-height pivot or moving the nozzle separately from the model.
    Mount.AddToTranslation(FVector(Param(TEXT("HeldX"),35)-35,Param(TEXT("HeldY"),70)-70,Param(TEXT("HeldZ"),15)-15));
    FTransform Pose=PropTransform*Mount*Hero()->GetActorTransform();
    const FVector Barrel=Pose.TransformVectorNoScale(IntakeForwardLocal).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FQuat Correction=FQuat::FindBetweenNormals(Barrel,Aim.Vector());
    Pose.SetRotation((Correction*Pose.GetRotation()).GetNormalized());
    SetActorTransform(PropTransform.Inverse()*Pose,false,nullptr,ETeleportType::TeleportPhysics);
    LastAim=GetIntakeForward();FlowDirection=LastAim;
    if(LoopComponent) LoopComponent->SetWorldLocation(IntakePoint());
}
FVector AMCAirSkillAction::GetIntakePoint() const {return IntakePoint();}
FVector AMCAirSkillAction::GetIntakeForward() const
{
    return PropMesh?PropMesh->GetComponentTransform().TransformVectorNoScale(IntakeForwardLocal)
        .GetSafeNormal(SMALL_NUMBER,GetActorForwardVector()):GetActorForwardVector();
}
FVector AMCAirSkillAction::IntakePoint() const
{
    const FVector Local(Param(TEXT("IntakeX"),128.7575f),Param(TEXT("IntakeY"),-11.1948f),Param(TEXT("IntakeZ"),-30.4591f));
    return PropMesh->GetComponentTransform().TransformPosition(Local);
}
void AMCAirSkillAction::ScanFragments(float Step)
{
    if(!HasAuthority() || !bSucking || bReleased || !Hero()) return;
    const FVector Origin=IntakePoint();
    FCollisionQueryParams BarrelQuery(SCENE_QUERY_STAT(MCAirBarrelClearance),false,this);
    BarrelQuery.AddIgnoredActor(Hero());FHitResult BarrelBlocker;
    if(GetWorld()->LineTraceSingleByChannel(BarrelBlocker,Hero()->GetActorLocation()+FVector(0,0,15),Origin,ECC_Visibility,BarrelQuery)) return;
    const float Range=FMath::Clamp(Param(TEXT("SuctionRange"),600),100.f,1400.f);
    const float Cone=FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Param(TEXT("SuctionCone"),32),5.f,70.f)));
    const float Acceleration=FMath::Clamp(Param(TEXT("SuctionAcceleration"),1800),100.f,6000.f);
    TArray<AMCFoodActor*> Candidates;
    for(TActorIterator<AMCFoodActor> It(GetWorld());It;++It) {
        auto* F=*It;
        if(!F->bFragment || F->bBrushTool || F->FoodData.Kind!=EMCFoodKind::Food || F->IsNutEncounterFragment()
            || F->StackCarrier || !F->Holders.IsEmpty() || F->IsInActiveContainer() || F->IsMouthEntryActive()
            || (F->Phase!=EMCFoodPhase::Free && F->Phase!=EMCFoodPhase::Falling) || !F->Body->IsSimulatingPhysics()) continue;
        const FVector Delta=F->Body->GetComponentLocation()-Origin;
        if(Delta.SizeSquared()>FMath::Square(Range) || FVector::DotProduct(Delta.GetSafeNormal(),LastAim)<Cone) continue;
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCAirSuction),false,this);
        Query.AddIgnoredActor(Hero());Query.AddIgnoredActor(F);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Origin,F->Body->GetComponentLocation(),ECC_Visibility,Query)) continue;
        Candidates.Add(F);
    }
    Candidates.Sort([Origin](const AMCFoodActor& A,const AMCFoodActor& B) {
        return FVector::DistSquared(Origin,A.GetActorLocation())<FVector::DistSquared(Origin,B.GetActorLocation());
    });
    for(auto* F:Candidates) {
        if(Contents.Num()>=BagCapacity) break;
        const FVector Delta=Origin-F->Body->GetComponentLocation();
        const float Capture=FMath::Clamp(Param(TEXT("CaptureRadius"),70),25.f,120.f)+F->Body->GetScaledBoxExtent().GetMin();
        if(Delta.SizeSquared()<=FMath::Square(Capture)) {
            if(F->TryEnterActiveContainer(this,Hero(),GetActionID())) Contents.Add(F);
        } else {
            const FVector Velocity=F->Body->GetPhysicsLinearVelocity();
            const FVector Desired=Delta.GetSafeNormal()*FMath::Min(850.,Delta.Size()*5.);
            const FVector Impulse=(Desired-Velocity).GetClampedToMaxSize(Acceleration*Step);
            F->Body->AddImpulse(Impulse,NAME_None,true);
        }
    }
    StoredCount=Contents.Num();ForceNetUpdate();
}
FVector AMCAirSkillAction::SafeReleasePoint(AMCFoodActor* Food,FVector Proposed) const
{
    const FVector From=Hero()?Hero()->GetActorLocation()+FVector(0,0,15):GetActorLocation();
    const float Radius=Food&&Food->Body?FMath::Clamp(Food->Body->GetScaledBoxExtent().GetMin(),8.,45.):15.f;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MCAirRelease),false,this);
    if(Hero()) Q.AddIgnoredActor(Hero());if(Food) Q.AddIgnoredActor(Food);
    FHitResult Hit;
    if(GetWorld()->SweepSingleByChannel(Hit,From,Proposed,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(Radius),Q))
        return Hit.Location+Hit.Normal*4;
    return Proposed;
}
void AMCAirSkillAction::ReleaseContents(bool bBlow,bool Rupture)
{
    if(!HasAuthority() || bReleased) return;
    bReleased=true;bSucking=false;bRuptured=Rupture;MarkPresentationReleased();StopLoop();
    PresentationRecoveryEndsAt=(bBlow || Rupture)?Now()+(Rupture?.38:.28):-1;
    const auto Snapshot=Contents;Contents.Reset();StoredCount=0;
    FVector Origin=IntakePoint();const FVector Right=FVector::CrossProduct(FVector::UpVector,LastAim).GetSafeNormal();
    for(int32 I=0;I<Snapshot.Num();++I) if(auto* F=Snapshot[I].Get();IsValid(F) && !F->IsDisposed()) {
        const float Spread=(I-(Snapshot.Num()-1)*.5f)*.09f;
        const float Angle=I*2.f*PI/FMath::Max(1,Snapshot.Num());
        const FVector Direction=Rupture?FVector(FMath::Cos(Angle),FMath::Sin(Angle),.9).GetSafeNormal()
            :(LastAim+Right*Spread+FVector(0,0,.05)).GetSafeNormal();
        const FVector Proposed=bBlow?Origin+LastAim*20+Right*Spread*60:GetActorLocation()+Direction*80;
        F->ExitActiveContainer(this,GetActionID(),SafeReleasePoint(F,Proposed),Direction,
            bBlow?FMath::Clamp(Param(TEXT("BlowSpeed"),1450),400.f,2800.f):Rupture?500.f:100.f,bBlow);
    }
    if(bBlow || Rupture) PlaySkillCue(Rupture?TEXT("End"):TEXT("Impact"),Origin,Rupture?FVector::UpVector:LastAim,Rupture?140.f:100.f);
    if(PresentationRecoveryEndsAt<0) ReleasePresentation();ForceNetUpdate();
}
void AMCAirSkillAction::ReleaseInput()
{
    if(!HasAuthority() || IsFinished() || !bSucking) return;
    if(!Hero() || !SourceState() || SourceState()->GetPawn()!=Hero() || !Hero()->CanWork()
        || !Hero()->ToothPhysics || !Hero()->ToothPhysics->CanAct()) {
        Cancel(EMCActiveSkillEndReason::InputCanceled);return;
    }
    UpdateHeldPose();ReleaseContents(true,false);
}
void AMCAirSkillAction::OnFinished(EMCActiveSkillEndReason Reason)
{
    if(Reason==EMCActiveSkillEndReason::WorldEnded && GetWorld() && GetWorld()->bIsTearingDown) {
        Contents.Reset();StoredCount=0;bReleased=true;Super::OnFinished(Reason);return;
    }
    if(HasAuthority() && !bReleased) ReleaseContents(false,false);
    Super::OnFinished(Reason);
}
void AMCAirSkillAction::Tick(float Dt)
{
    Super::Tick(Dt);if(IsFinished() || !IsCommitted()) return;
    if(LoopComponent) LoopComponent->SetWorldLocation(IntakePoint());
    UpdateLoopVFX(HasAuthority()?LastAim:FVector(FlowDirection),GetFillFraction());
    if(!HasAuthority()) return;
    if(!bSucking) {
        if(PresentationRecoveryEndsAt>=0) {
            UpdateHeldPose();
            if(Now()>=PresentationRecoveryEndsAt) {ReleasePresentation();Finish();}
        }
        return;
    }
    UpdateHeldPose();
    if(!Hero() || !Hero()->CanWork() || !Hero()->ToothPhysics->CanAct()) {Cancel(EMCActiveSkillEndReason::InputCanceled);return;}
    if(Now()>=NextScanAt) {
        constexpr float Step=.06f;NextScanAt=Now()+Step;
        Contents.RemoveAll([this](const TObjectPtr<AMCFoodActor>& F){return !IsValid(F) || F->IsDisposed() || F->ActiveContainer!=this;});
        ScanFragments(Step);
    }
    if(StoredCount>=BagCapacity) {
        if(FullSince<0) {FullSince=Now();bOverflowWarning=true;PlaySkillCue(TEXT("Warning"),IntakePoint(),LastAim,100);ForceNetUpdate();}
        if(Now()-FullSince>=FMath::Clamp(Param(TEXT("OverflowGrace"),1.1f),.4f,3.f)) {
            ReleaseContents(false,true);return;
        }
    } else {FullSince=-1;bOverflowWarning=false;}
    if(Now()-HoldStartedAt>=FMath::Clamp(Param(TEXT("MaxHoldSeconds"),12),2.f,30.f)) ReleaseInput();
}
void AMCAirSkillAction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCAirSkillAction,StoredCount);DOREPLIFETIME(AMCAirSkillAction,BagCapacity);
    DOREPLIFETIME(AMCAirSkillAction,bSucking);DOREPLIFETIME(AMCAirSkillAction,bOverflowWarning);DOREPLIFETIME(AMCAirSkillAction,bRuptured);
    DOREPLIFETIME(AMCAirSkillAction,FlowDirection);
}
