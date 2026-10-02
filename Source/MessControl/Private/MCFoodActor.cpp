#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCReactionVFX.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

void FMCFoodSettings::Sanitize()
{
    auto Safe=[](float V,float D,float L,float H){return FMath::IsFinite(V)?FMath::Clamp(V,L,H):D;};
    DropHeight=Safe(DropHeight,650,200,2000); Mass=Safe(Mass,9,1,50);
    ImpactSpeed=Safe(ImpactSpeed,180,50,1500); DamagePerSpeed=Safe(DamagePerSpeed,.035f,0,1);
    MaxDamage=Safe(MaxDamage,40,0,1000); Knockback=Safe(Knockback,560,150,1500); HitCooldown=Safe(HitCooldown,.75f,.2f,10);
    GrabReach=Safe(GrabReach,145,50,250); BreakDistance=Safe(BreakDistance,340,GrabReach+50,600);
    Spring=Safe(Spring,14,1,50); Damping=Safe(Damping,6,1,30); PullSeconds=Safe(PullSeconds,3,.2f,30);
    PullConeDegrees=Safe(PullConeDegrees,35,10,90); CooperationMultiplier=Safe(CooperationMultiplier,1.5f,1,4);
}
AMCFoodActor::AMCFoodActor()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick=true;
    Body=CreateDefaultSubobject<UBoxComponent>(TEXT("FoodBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(48,32,30)); Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetNotifyRigidBodyCollision(true); Body->BodyInstance.bUseCCD=true;
    Body->SetLinearDamping(.7f); Body->SetAngularDamping(2.f);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoodMesh")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeLocation(FVector(0,0,-25)); Visual->SetRelativeScale3D(FVector(1.5));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Stylized_Vegetables/Meshes/SM_Broccoli"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    GripSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GripSurface")); GripSurface->SetupAttachment(Visual);
    GripSurface->SetVisibility(false); GripSurface->SetHiddenInGame(true); GripSurface->SetCastShadow(false);
    GripSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly); GripSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
    GripSurface->SetGenerateOverlapEvents(false);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("FoodInstruction")); Label->SetupAttachment(Body);
    Label->SetRelativeLocation(FVector(0,0,75)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(17);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetTextRenderColor(FColor(255,215,110));
    Profile=TSoftObjectPtr<UMCFoodProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_FoodPhysics.DA_FoodPhysics")));
}
void AMCFoodActor::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) { if (const auto* P=Profile.LoadSynchronous()) Settings=P->Settings; Settings.Sanitize(); }
    if (!ItemName.IsNone()) Settings.Mass=FoodData.Mass;
    if (bBrushTool) Settings.Mass=1;
    Body->SetMassOverrideInKg(NAME_None,Settings.Mass,true);
    Body->OnComponentHit.AddDynamic(this,&AMCFoodActor::OnHit); OnRep_Phase(); OnRep_Item();
    if (HasAuthority() && !ItemName.IsNone() && SpoilAt<=0 && FoodData.Kind==EMCFoodKind::Food) SpoilAt=GetWorld()->GetTimeSeconds()+FoodData.SpoilSeconds;
}
void AMCFoodActor::Initialize(bool bJam,FVector ExtractionDirection)
{
    if (!HasAuthority()) return;
    bJamOnLanding=bJam; PullDirection=ExtractionDirection.GetSafeNormal2D();
    if (PullDirection.IsNearlyZero()) PullDirection=FVector(0,1,0);
}
void AMCFoodActor::OnRep_ReplicatedMovement()
{
    if (HasAuthority()) return;
    // Our proxies deliberately do not simulate. AActor's default physics path sends
    // a rigid-body target, which stops moving them after OnRep_Phase disables Chaos.
    // Consume the same replicated transform as a kinematic presentation instead.
    Body->SetSimulatePhysics(false);
    const auto& Motion=GetReplicatedMovement();
    NetworkLocation=FRepMovement::RebaseOntoLocalOrigin(Motion.Location,this);
    NetworkRotation=Motion.Rotation.Quaternion();
    if (!bReceivedMotion || FVector::DistSquared(GetActorLocation(),NetworkLocation)>FMath::Square(600.f) || Phase==EMCFoodPhase::Stuck)
        SetActorLocationAndRotation(NetworkLocation,NetworkRotation,false,nullptr,ETeleportType::TeleportPhysics);
    bReceivedMotion=true;
}
void AMCFoodActor::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
    Super::PreReplication(ChangedPropertyTracker);
    CarryPresentation.Holder=StackCarrier?StackCarrier.Get():Phase==EMCFoodPhase::Carried && Holders.Num()==1?Holders[0].Get():nullptr;
    if (IsValid(CarryPresentation.Holder))
    {
        const FTransform Relative=GetActorTransform().GetRelativeTransform(CarryPresentation.Holder->GetActorTransform());
        CarryPresentation.Location=Relative.GetLocation(); CarryPresentation.Rotation=Relative.Rotator();
    }
}
void AMCFoodActor::UpdateCarryPresentation(float Dt)
{
    auto* Carrier=CarryPresentation.Holder.Get();
    if (HasAuthority() || !IsValid(Carrier) || !(StackCarrier==Carrier || Phase==EMCFoodPhase::Carried && Carrier->Grip->Holds(this))) return;
    const FTransform RenderedCarrier=Carrier->StandingMeshTransform().Inverse()*Carrier->GetMesh()->GetComponentTransform();
    if (PresentationCarrier.Get()!=Carrier)
    {
        PresentationCarrier=Carrier;
        SmoothedCarryRelative=GetActorTransform().GetRelativeTransform(RenderedCarrier);
    }
    const float Alpha=1-FMath::Exp(-25.f*Dt);
    SmoothedCarryRelative.SetLocation(FMath::Lerp(SmoothedCarryRelative.GetLocation(),FVector(CarryPresentation.Location),Alpha));
    SmoothedCarryRelative.SetRotation(FQuat::Slerp(SmoothedCarryRelative.GetRotation(),CarryPresentation.Rotation.Quaternion(),Alpha));
    const FTransform Pose=SmoothedCarryRelative*RenderedCarrier;
    // Only proxies use this presentation. Collision and release momentum remain server physics.
    SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
}
void AMCFoodActor::OnRep_Phase()
{
    const bool bGone=IsDisposed() || Phase==EMCFoodPhase::Equipped;
    const bool bSimulate=HasAuthority() && (Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free || Phase==EMCFoodPhase::Carried);
    // Stop Chaos before disabling collision, including disposal while the item is still moving.
    if (!bSimulate) Body->SetSimulatePhysics(false);
    SetActorHiddenInGame(bGone); Body->SetCollisionEnabled((bGone || Phase==EMCFoodPhase::Swallowing)?ECollisionEnabled::NoCollision:Phase==EMCFoodPhase::Absorbing?ECollisionEnabled::QueryOnly:ECollisionEnabled::QueryAndPhysics);
    Body->SetCollisionResponseToChannel(ECC_Pawn,StackCarrier || Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
    // Simulated proxies are kinematic; only the server applies springs and impact damage.
    if (bSimulate) Body->SetSimulatePhysics(true);
}
bool AMCFoodActor::TryGrab(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !UsesLegacyGrip() || !IsValid(Hero) || IsDisposed() || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Equipped || !Hero->CanWork() || (!bBrushTool && !Holders.Contains(Hero) && !Hero->Grip->CanAcquire(this))) return false;
    if (Holders.Contains(Hero)) return true;
    if (Phase==EMCFoodPhase::Carried) return false;
    const FVector Offset=GetActorLocation()-Hero->GetActorLocation();
    if (FVector::Dist(Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()),Hero->GetActorLocation())>Settings.GrabReach || (bBrushTool && FVector::DotProduct(Hero->GetActorForwardVector(),Offset.GetSafeNormal2D())<-.25f)) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodGrab),false,Hero); Params.AddIgnoredActor(this);
    const FVector GrabPoint=Phase==EMCFoodPhase::Absorbing?Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()):GetActorLocation();
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),GrabPoint,ECC_Visibility,Params)) return false;
    if (bBrushTool)
    {
        if (Hero->EquippedBrush) return false;
        EquippedBy=Hero; Hero->EquippedBrush=this; Phase=EMCFoodPhase::Equipped; OnRep_Phase();
        Hero->ForceNetUpdate(); ForceNetUpdate(); return true;
    }
    if (!Hero->Grip || !Hero->Grip->BeginGrip(this)) return false;
    Holders.Add(Hero); Hero->HeldFood=Hero->Grip->Frame.Food;
    AttendFood();
    Hero->ForceNetUpdate(); ForceNetUpdate(); return true;
}
bool AMCFoodActor::BeginCarry(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !UsesLegacyGrip() || !Hero || Holders.Num()!=1 || Holders[0]!=Hero || !Hero->Grip->IsReady(this) || !Hero->Grip->CanCarry(this)) return false;
    Phase=EMCFoodPhase::Carried; Hero->Grip->BeginLift(this); CarryBlockedSeconds=0; OnRep_Phase();
    Body->IgnoreActorWhenMoving(Hero,true); Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(this,true);
    ForceNetUpdate(); return true;
}
bool AMCFoodActor::FindGripSurface(FVector From,FHitResult& Hit) const
{
    if (!GripSurface || !GripSurface->GetStaticMesh()) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGripMesh),true); Params.bReturnFaceIndex=true;
    const FBox Bounds=Visual->Bounds.GetBox();
    FVector Near=Bounds.GetClosestPointTo(From),Center=Visual->Bounds.Origin;
    const double ZMargin=FMath::Min(4.,Bounds.GetExtent().Z*.5);
    Center.Z=FMath::Clamp(From.Z,Bounds.Min.Z+ZMargin,Bounds.Max.Z-ZMargin);
    bool Found=false; double Best=DBL_MAX;
    auto TryRay=[&](FVector Aim)
    {
        const FVector Direction=(Aim-From).GetSafeNormal();
        if (Direction.IsNearlyZero()) return;
        FHitResult Candidate;
        if (GripSurface->LineTraceComponent(Candidate,From,Aim+Direction*Visual->Bounds.SphereRadius*2,Params))
        {
            const double Distance=FVector::DistSquared(From,Candidate.ImpactPoint);
            if (Distance<Best) { Best=Distance; Hit=Candidate; Found=true; }
        }
    };
    FVector Closest;
    if (GripSurface->GetClosestPointOnCollision(From,Closest)>=0)
        TryRay(Closest);
    TryRay(Near); TryRay(Center);
    // A horizontal ray can hit a narrow stalk beyond arm reach although a floret
    // just above it is reachable. Search the actual outline before refusing a grip.
    for (const float Height:{-.7f,0.f,.7f})
    {
        FVector Aim=Visual->Bounds.Origin; Aim.Z+=Bounds.GetExtent().Z*Height;
        TryRay(Aim); TryRay(FMath::Lerp(Near,Aim,.5));
    }
    return Found;
}
void AMCFoodActor::Release(AMCToothCharacter* Hero)
{
    if (!HasAuthority()) return;
    const bool WasCarrier=Phase==EMCFoodPhase::Carried && Holders.Contains(Hero);
    if (IsValid(Hero)) { Body->IgnoreActorWhenMoving(Hero,false); Hero->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false); }
    Holders.Remove(Hero);
    if(!bBrushTool && SpoilAt<=0) SpoilAt=HazardNow()+FoodData.SpoilSeconds;
    if (WasCarrier)
    {
        // A newly released overhead load can brush its carrier on the way down.
        // Use the existing impact grace period for that player; other targets
        // still receive thrown/dropped impacts immediately.
        if (IsValid(Hero)) LastHit.Add(Hero,GetWorld()->GetTimeSeconds());
        Phase=EMCFoodPhase::Free; OnRep_Phase();
        // Chaos already owns the carried velocity. Release preserves linear and angular momentum.
    }
    if (IsValid(Hero) && Hero->Grip && Hero->Grip->Holds(this)) Hero->Grip->EndGrip(this);
    if (IsValid(Hero) && Hero->HeldFood==this) { Hero->HeldFood=nullptr; Hero->ForceNetUpdate(); }
    ForceNetUpdate();
}
void AMCFoodActor::Dispose()
{
    if (!HasAuthority() || IsDisposed()) return;
    SetStackCarrier(nullptr);
    for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
    if (IsValid(EquippedBy)) { EquippedBy->EquippedBrush=nullptr; EquippedBy->ForceNetUpdate(); } EquippedBy=nullptr;
    Phase=EMCFoodPhase::Disposed; OnRep_Phase(); ForceNetUpdate(); SetLifeSpan(IsHazardResolved()?3:0);
}
void AMCFoodActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
    {
        for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
        if (IsValid(EquippedBy) && EquippedBy->EquippedBrush==this) EquippedBy->EquippedBrush=nullptr;
    }
    Super::EndPlay(Reason);
}
void AMCFoodActor::OnHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent* OtherComponent,FVector,const FHitResult& Hit)
{
    // Require a downward approach to a supporting surface. Stack contact and
    // horizontal solver impulses are not landing or received damage.
    if(HasAuthority() && !IsDisposed() && !bBrushTool && !StackCarrier && Holders.IsEmpty()
        && Hit.ImpactNormal.Z>.55f && PrePhysicsVelocity.Z<-140)
        ReactToImpact(FMath::Clamp(-PrePhysicsVelocity.Z/650.f,.15f,1.f));
    if (!HasAuthority() || IsDisposed() || Phase==EMCFoodPhase::Carried || !IsValid(Other)) return;
    if ((Phase==EMCFoodPhase::Falling || (FoodData.Kind==EMCFoodKind::Spicy && PrePhysicsVelocity.Z<-140))
        && Hit.ImpactNormal.Z>.6f && OtherComponent && OtherComponent->GetCollisionObjectType()==ECC_WorldStatic)
        bLandingPending=true;
    UMCToothStatusComponent* Target=Other->FindComponentByClass<UMCToothStatusComponent>();
    if (!Target || !Target->IsAlive() || Phase==EMCFoodPhase::Stuck || bBrushTool) return;
    if (const auto* Arena=Cast<AMCArenaTooth>(Other); Arena && !Arena->IsAvailable()) return;
    if (const auto* Hero=Cast<AMCToothCharacter>(Other); Hero && (Holders.Contains(Hero) || StackCarrier==Hero || Hero->FoodCollection->IsSettlingRelease(this))) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (const double* Prev=LastHit.Find(Other); Prev && Now-*Prev<Settings.HitCooldown) return;
    // A player running into stationary food must not turn their own speed (or the
    // solver's separation impulse) into a new attack. Require the food's incoming
    // motion before collision resolution, then check that the two bodies approach.
    const FVector Approach=-Hit.ImpactNormal.GetSafeNormal();
    const float IncomingSpeed=FVector::DotProduct(PrePhysicsVelocity,Approach);
    if (!FMath::IsFinite(IncomingSpeed) || IncomingSpeed<Settings.ImpactSpeed) return;
    const float Speed=FVector::DotProduct(PrePhysicsVelocity-Other->GetVelocity(),Approach);
    if (!FMath::IsFinite(Speed)) return;
    if (Speed<Settings.ImpactSpeed) return;
    LastHit.Add(Other,Now); ++ConfirmedImpacts;
    FVector Direction=(Other->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
    Target->Damage(FMath::Min(Settings.MaxDamage,Speed*Settings.DamagePerSpeed*Settings.Mass/9.f),Direction);
    if (auto* Hero=Cast<AMCToothCharacter>(Other))
    {
        Hero->DropFood();
        Hero->ToothPhysics->ApplyHit(Direction*Settings.Knockback+FVector(0,0,Settings.Knockback*.5f),Hit.ImpactPoint);
    }
}
void AMCFoodActor::Tick(float Dt)
{
    Super::Tick(Dt);
    UpdateHazard(Dt);
    UpdateReaction(Dt);
    if(StackCarrier && !HasAuthority()) UpdateCarryPresentation(Dt);
    if(IsDisposed()) {
        if(HasAuthority() && !FireTrail.IsEmpty() && IsHazardResolved() && GetLifeSpan()<=0) SetLifeSpan(3);
        if(HasAuthority() && bAbsorbed && (!IsValid(AbsorbedUlcer) || AbsorbedUlcer->IsActorBeingDestroyed())) Destroy();
        return;
    }
    UpdateAbsorption(Dt);
    if(IsDisposed()) return;
    if(Phase==EMCFoodPhase::Absorbing) { Label->SetVisibility(false); return; }
    AMCToothCharacter* IgnoredCarrier=StackCarrier?StackCarrier.Get():Phase==EMCFoodPhase::Carried && Holders.Num()==1?Holders[0].Get():nullptr;
    if (CollisionIgnoredCarrier.Get()!=IgnoredCarrier)
    {
        if (auto* Previous=CollisionIgnoredCarrier.Get()) { Body->IgnoreActorWhenMoving(Previous,false); Previous->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false); }
        CollisionIgnoredCarrier=IgnoredCarrier;
        if (IgnoredCarrier) { Body->IgnoreActorWhenMoving(IgnoredCarrier,true); IgnoredCarrier->GetCapsuleComponent()->IgnoreActorWhenMoving(this,true); }
    }
    if (Phase!=EMCFoodPhase::Carried) PresentationCarrier.Reset();
    if (!HasAuthority() && bReceivedMotion && !StackCarrier && !(Phase==EMCFoodPhase::Carried && IsValid(CarryPresentation.Holder) && CarryPresentation.Holder->Grip->Holds(this)))
    {
        const float Alpha=1-FMath::Exp(-25.f*Dt);
        SetActorLocationAndRotation(FMath::Lerp(GetActorLocation(),NetworkLocation,Alpha),FQuat::Slerp(GetActorQuat(),NetworkRotation,Alpha),false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (HasAuthority() && !IsDisposed() && Phase!=EMCFoodPhase::Swallowing)
    {
        if (Phase==EMCFoodPhase::Equipped) { if (IsValid(EquippedBy)) SetActorLocation(EquippedBy->GetActorLocation()); return; }
        if (Phase==EMCFoodPhase::Stuck && IsValid(StuckTooth))
            if (const auto* Tooth=Cast<AMCArenaTooth>(StuckTooth); !Tooth || !Tooth->IsAvailable()) { Phase=EMCFoodPhase::Free; OnRep_Phase(); }
        if (bLandingPending && Phase==EMCFoodPhase::Falling)
        { bLandingPending=false; Phase=bJamOnLanding?EMCFoodPhase::Stuck:EMCFoodPhase::Free; OnRep_Phase(); ForceNetUpdate(); }
        if (const auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))
            for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
        FVector Force=FVector::ZeroVector; int32 Pullers=0,ReadyHolders=0;
        for (int32 I=Holders.Num()-1;I>=0;--I)
        {
            AMCToothCharacter* Hero=Holders[I];
            if (!UsesLegacyGrip() || !IsValid(Hero) || !Hero->CanWork() || !Hero->bHandling || FVector::Dist(Hero->GetActorLocation(),GetActorLocation())>Settings.BreakDistance)
            {
#if !UE_BUILD_SHIPPING
                if (FParse::Param(FCommandLine::Get(),TEXT("MCCore"))) UE_LOG(LogTemp,Display,TEXT("MC_CORE_RELEASE hero=%s distance=%.1f canwork=%d handling=%d"),*GetNameSafe(Hero),IsValid(Hero)?FVector::Dist(Hero->GetActorLocation(),GetActorLocation()):-1,IsValid(Hero)&&Hero->CanWork(),IsValid(Hero)&&Hero->bHandling);
#endif
                Release(Hero); continue;
            }
            if (Hero->Grip && Hero->Grip->IsReady(this))
            {
                ++ReadyHolders; Force+=Hero->Grip->DriveForce(this);
                const FVector Intent=Hero->Grip->InputDirection();
                if (!Intent.IsNearlyZero() && FVector::DotProduct(Intent,PullDirection)>=FMath::Cos(FMath::DegreesToRadians(Settings.PullConeDegrees))) ++Pullers;
            }
        }
        if (Phase==EMCFoodPhase::Stuck)
        {
            const float Now=GetWorld()->GetTimeSeconds();
            if (Pullers>0) { LastPullTime=Now; PullProgress=FMath::Min(1.f,PullProgress+Dt*(Pullers>1?Settings.CooperationMultiplier:1.f)/Settings.PullSeconds); }
            else if (Now-LastPullTime>1) PullProgress=FMath::Max(0.f,PullProgress-Dt*.2f);
            if (PullProgress>=1) { Phase=EMCFoodPhase::Free; OnRep_Phase(); Body->AddImpulse(PullDirection*120+FVector(0,0,80),NAME_None,true); ForceNetUpdate(); }
        }
        else if (ReadyHolders>0)
        {
            // Suspended food remains a rigid body, including its contact with walls.
            // Split support and the hand pull so gravity compensation cannot spin it.
            const float TeamScale=FMath::Sqrt(float(ReadyHolders));
            Body->AddForce(Force/TeamScale);
            FVector Torque=FVector::ZeroVector;
            for (const auto& Holder:Holders) if (IsValid(Holder) && Holder->Grip && Holder->Grip->IsReady(this))
            {
                Torque+=Holder->Grip->DriveTorque(this);
                if (Phase==EMCFoodPhase::Carried)
                {
                    const FVector Lever=(Holder->Grip->ForcePoint(this)-Body->GetCenterOfMass()).GetClampedToMaxSize(18);
                    const FVector HandForce=Holder->Grip->DriveForce(this)-FVector(0,0,-GetWorld()->GetGravityZ()*Body->GetMass());
                    Torque+=FVector::CrossProduct(Lever,HandForce)*.12;
                }
            }
            Body->WakeAllRigidBodies();
            Body->AddTorqueInRadians(Torque.GetClampedToMaxSize(1000000)/TeamScale);
        }
        // An escaped item returns to the arena, never counts as successfully disposed.
        // A placed brush bin owns the horizontal exit. A fixed X cutoff would
        // delete tools inside an artist arena whose front edge moved.
        if (bBrushTool && GetActorLocation().Z < -250) { Dispose(); return; }
        if (GetActorLocation().Z<-250)
        { for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]); SetActorLocation(FVector(0,0,Settings.DropHeight),false,nullptr,ETeleportType::TeleportPhysics); Body->SetPhysicsLinearVelocity(FVector::ZeroVector); }
        PrePhysicsVelocity=Body->GetPhysicsLinearVelocity();
    }
    FString Caption=Phase==EMCFoodPhase::Stuck?FString::Printf(TEXT("LMB + MOVE TO CENTRE\nPULL %.0f%% | %d GRIPS"),PullProgress*100,Holders.Num()):Phase==EMCFoodPhase::Carried?TEXT("RELEASE LMB: DROP | Q: THROW"):TEXT("HOLD LMB: PICK UP / DRAG");
    if (!UsesLegacyGrip()) Caption=Phase==EMCFoodPhase::Stuck?TEXT("RMB: FREE / CUT FOOD"):StackCarrier?TEXT("LMB: DROP STACK | Q: THROW"):TEXT("LMB: COLLECT STACK | RMB: CUT");
    if (!ItemName.IsNone()) Caption=FString::Printf(TEXT("%s | HP %.0f | %.1f kg\n%s | %s"),*FoodData.Label.ToString(),Health,Settings.Mass,*Caption,bSpoiled?TEXT("SPOILED"):FoodData.Kind==EMCFoodKind::Spicy?*FString::Printf(TEXT("%.1fs %s"),FuseRemaining(),bFusePaused?TEXT("PAUSED"):TEXT("THROW INTO THROAT")):FoodData.Kind==EMCFoodKind::ForeignObject?TEXT("FOREIGN OBJECT"):*FString::Printf(TEXT("SPOIL %.0fs"),FMath::Max(0.,SpoilAt-(GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds()))));
    if (bBrushTool) Caption=TEXT("LMB: PICK UP BRUSH\nQ: THROW OVERBOARD");
    if(FoodData.Kind==EMCFoodKind::Spicy) {
        Caption=FString::Printf(TEXT("%.1fs%s"),FuseRemaining(),bFusePaused?TEXT(" [PAUSED]"):TEXT(""));
        Label->SetWorldSize(13); Label->SetRelativeLocation(FVector(0,0,Body->GetUnscaledBoxExtent().Z+30));
    }
    Label->SetText(FText::FromString(Caption));
    Label->SetVisibility(Phase!=EMCFoodPhase::Swallowing);
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCFoodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodActor,Settings); DOREPLIFETIME(AMCFoodActor,Phase);
    DOREPLIFETIME(AMCFoodActor,CarryPresentation);
    DOREPLIFETIME(AMCFoodActor,StackCarrier);DOREPLIFETIME(AMCFoodActor,ImpactAt);DOREPLIFETIME(AMCFoodActor,ImpactStrength);
    DOREPLIFETIME(AMCFoodActor,PullProgress); DOREPLIFETIME(AMCFoodActor,PullDirection); DOREPLIFETIME(AMCFoodActor,Holders);
    DOREPLIFETIME(AMCFoodActor,ItemMesh); DOREPLIFETIME(AMCFoodActor,FoodData); DOREPLIFETIME(AMCFoodActor,ItemName); DOREPLIFETIME(AMCFoodActor,Health);
    DOREPLIFETIME(AMCFoodActor,bFragment); DOREPLIFETIME(AMCFoodActor,bBrushTool); DOREPLIFETIME(AMCFoodActor,bSpoiled); DOREPLIFETIME(AMCFoodActor,SpoilAt);
    DOREPLIFETIME(AMCFoodActor,Batch); DOREPLIFETIME(AMCFoodActor,EquippedBy); DOREPLIFETIME(AMCFoodActor,StuckTooth);
    DOREPLIFETIME(AMCFoodActor,AbsorbStartedAt); DOREPLIFETIME(AMCFoodActor,bAbsorbed); DOREPLIFETIME(AMCFoodActor,AbsorbedUlcer);
    DOREPLIFETIME(AMCFoodActor,AbsorptionTongue); DOREPLIFETIME(AMCFoodActor,AbsorptionAnchor);
    DOREPLIFETIME(AMCFoodActor,FuseEndsAt); DOREPLIFETIME(AMCFoodActor,PausedFuse); DOREPLIFETIME(AMCFoodActor,bFusePaused); DOREPLIFETIME(AMCFoodActor,HazardRound);
    DOREPLIFETIME(AMCFoodActor,BurnLesion);DOREPLIFETIME(AMCFoodActor,FireTrail);
}
AMCFoodDisposal::AMCFoodDisposal()
{
    PrimaryActorTick.bCanEverTick=true; bReplicates=true; bAlwaysRelevant=true;
    Volume=CreateDefaultSubobject<UBoxComponent>(TEXT("ThroatVolume")); SetRootComponent(Volume);
    Volume->SetBoxExtent(FVector(105,290,180)); Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ThroatLabel")); Label->SetupAttachment(Volume);
    Label->SetRelativeRotation(FRotator(0,180,0)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(30);
    Label->SetText(FText::FromString(TEXT("FOOD >>> THROAT\nBRING FOOD HERE"))); Label->SetTextRenderColor(FColor(115,255,210));
}
void AMCFoodDisposal::Tick(float Dt)
{
    Super::Tick(Dt);
    Label->SetText(FText::FromString(bBrushBin?TEXT("<<< BRUSHES OVERBOARD\nTHROW Q HERE"):TEXT("FOOD >>> THROAT\nNO BRUSHES")));
    if (bBrushBin && !bExitConfigured)
    {
        bExitConfigured=true;
        // The blockout has an invisible front containment wall. Only tools pass it;
        // never disable collision on visible, authored mouth geometry or on the floor.
        FHitResult Hit; const FVector P=GetActorLocation();
        if (GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(250,0,0),P-FVector(100,0,0),ECC_WorldStatic)
            && Hit.GetActor() && Hit.GetActor()->IsHidden() && Hit.GetComponent()
            && Hit.GetComponent()->Bounds.BoxExtent.X<80 && Hit.GetComponent()->Bounds.BoxExtent.Z>150)
            Hit.GetComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    }
    if (!HasAuthority()) return;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if ((It->Phase==EMCFoodPhase::Free || It->Phase==EMCFoodPhase::Carried) && It->bBrushTool==bBrushBin && Volume->Bounds.GetBox().IsInside(It->GetActorLocation())) It->Dispose();
}
void AMCFoodDisposal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodDisposal,bBrushBin); }

void AMCFoodActor::OnRep_Item()
{
    const FVector ItemScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
    if (ItemMesh) { Visual->SetStaticMesh(ItemMesh); Visual->SetRelativeLocation(FVector::ZeroVector); Visual->SetRelativeScale3D(ItemScale); }
    if (bBrushTool) { Body->SetCollisionObjectType(ECC_GameTraceChannel1); Body->SetBoxExtent(FVector(12,12,40)); Visual->SetRelativeLocation(FVector(0,0,-35)); Visual->SetRelativeScale3D(FVector(.8)); }
    else if (ItemMesh)
    {
        // Imported variants have different pivots and sizes. Keep the physical centre,
        // visible surface and hand contacts together, using each fragment's independent scale.
        const FBoxSphereBounds Bounds=ItemMesh->GetBounds();
        const FVector Scale=Visual->GetRelativeScale3D();
        Visual->SetRelativeLocation(-Bounds.Origin*Scale);
        Body->SetBoxExtent((Bounds.BoxExtent*Scale).ComponentMax(FVector(3)));
    }
    else if (!ItemName.IsNone())
    {
        Visual->SetRelativeScale3D(ItemScale*1.5);
        Visual->SetRelativeLocation(FVector(0,0,-25)*ItemScale);
        Body->SetBoxExtent((FoodData.HalfExtent*ItemScale).ComponentMax(FVector(3)));
    }
    Label->SetRelativeLocation(FVector(0,0,Body->GetUnscaledBoxExtent().Z+28));
    GripSurface->SetStaticMesh(Visual->GetStaticMesh());
}
void AMCFoodActor::ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment)
{
    if (!HasAuthority()) return;
    FoodData=Row; FoodData.Sanitize(); ItemName=Name; bFragment=Fragment;
    if (bFragment) { FoodData.Mass/=FoodData.Fragments; FoodData.Health=25; }
    Health=FoodData.Health; Settings.Mass=FoodData.Mass;
    const auto& Choices=bFragment?FoodData.FragmentMeshes:FoodData.WholeMeshes;
    TArray<UStaticMesh*> Available;
    for (const auto& Choice:Choices) if (!Choice.IsNull()) if (auto* Mesh=Choice.LoadSynchronous()) Available.Add(Mesh);
    ItemMesh=Available.IsEmpty()?nullptr:Available[Random.RandRange(0,Available.Num()-1)];
    if (!ItemMesh)
    {
        UE_LOG(LogTemp,Warning,TEXT("Food row %s has no usable %s mesh; using the prototype mesh"),*Name.ToString(),Fragment?TEXT("fragment"):TEXT("whole"));
        ItemMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/SM_Food.SM_Food"));
    }
    OnRep_Item(); ForceNetUpdate();
}
void AMCFoodActor::ConfigureBrush()
{
    bBrushTool=true; ItemMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/SM_Brush.SM_Brush")); Settings.Mass=1; OnRep_Item();
}
float AMCFoodActor::DragSpeed() const
{
    return Phase==EMCFoodPhase::Carried?380.f:Phase==EMCFoodPhase::Stuck?55.f:FMath::Clamp(440.f/(1+Settings.Mass/(9.f*FMath::Max(1,Holders.Num()))),70.f,330.f);
}
void AMCFoodActor::Throw(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || (EquippedBy!=Hero && !Holders.Contains(Hero))) return;
    const bool WasTool=bBrushTool && EquippedBy==Hero;
    for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
    if (WasTool) { Hero->EquippedBrush=nullptr; EquippedBy=nullptr; SetActorLocation(Hero->GetActorLocation()+Hero->GetActorForwardVector()*70+FVector(0,0,30)); }
    if (Phase!=EMCFoodPhase::Stuck) { Phase=EMCFoodPhase::Free; OnRep_Phase(); Body->SetPhysicsLinearVelocity(Body->GetPhysicsLinearVelocity()*.5+Hero->GetVelocity()*.5+Hero->GetActorForwardVector()*(WasTool?1000.f:420.f)+FVector(0,0,220)); }
    Hero->ForceNetUpdate(); ForceNetUpdate();
}
bool AMCFoodActor::HitFood(float Damage,FVector Direction)
{
    if (!HasAuthority() || bBrushTool || IsDisposed() || Phase==EMCFoodPhase::Swallowing || !FMath::IsFinite(Damage) || Damage<=0 || Direction.ContainsNaN()) return false;
    if(Phase==EMCFoodPhase::Stuck) {Phase=EMCFoodPhase::Free;StuckTooth=nullptr;OnRep_Phase();}
    ReactToImpact();
    AttendFood(); Health=FMath::Max(0.f,Health-Damage); ForceNetUpdate();
    if(FoodData.Kind==EMCFoodKind::Spicy && Health<=0) {Detonate();return true;}
    if (Health>0 || bFragment) { Body->AddImpulse(Direction.GetSafeNormal()*150+FVector(0,0,60),NAME_None,true); return true; }
    FRandomStream Random(FMath::Rand()); const FVector P=GetActorLocation();
    for (int32 I=0;I<FoodData.Fragments;++I)
    {
        const float Angle=I*2*PI/FoodData.Fragments; const FVector Offset(FMath::Cos(Angle)*42,FMath::Sin(Angle)*42,20);
        const FTransform T(P+Offset);
        auto* Part=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Part) continue;
        Part->ConfigureItem(ItemName,FoodData,Random,true); Part->Batch=Batch; Part->SpoilAt=SpoilAt; Part->bSpoiled=bSpoiled;
        UGameplayStatics::FinishSpawningActor(Part,T); Part->Body->SetPhysicsLinearVelocity(Offset*3);
    }
    Dispose(); return true;
}
bool AMCFoodActor::IsHardFood() const
{
    if(FoodData.Resistance!=EMCFoodResistance::Automatic) return FoodData.Resistance==EMCFoodResistance::Hard;
    return ItemName==TEXT("Carrot") || ItemName==TEXT("Nut") || ItemName==TEXT("Crust") || ItemName==TEXT("Tartar");
}
bool AMCFoodActor::BeginSwallow()
{
    if (!HasAuthority() || StackCarrier || !Holders.IsEmpty() || (Phase!=EMCFoodPhase::Free && Phase!=EMCFoodPhase::Falling)) return false;
    Phase=EMCFoodPhase::Swallowing; OnRep_Phase(); ForceNetUpdate(); return true;
}
void AMCFoodActor::CancelSwallow()
{
    if (!HasAuthority() || Phase!=EMCFoodPhase::Swallowing) return;
    Phase=EMCFoodPhase::Free; OnRep_Phase(); ForceNetUpdate();
}
void AMCFoodActor::Spoil()
{
    bSpoiled=true; ForceNetUpdate();
}
