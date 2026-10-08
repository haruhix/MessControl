#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCGameMode.h"
#include "MCPlayerState.h"
#include "MCTutorialDirector.h"
#include "MCFoodBodyComponent.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCDeliveryZoneVisualComponent.h"
#include "MCReactionVFX.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "MCThroat.h"
#include "MCTongue.h"
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
    Body=CreateDefaultSubobject<UMCFoodBodyComponent>(TEXT("FoodBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(48,32,30)); Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    // Impacts use OnComponentHit. Per-shape overlap queries are unnecessary for food.
    Body->SetGenerateOverlapEvents(false);
    Body->SetNotifyRigidBodyCollision(true); Body->BodyInstance.bUseCCD=true;
    Body->SetLinearDamping(.7f); Body->SetAngularDamping(2.f);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoodMesh")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeLocation(FVector(0,0,-25)); Visual->SetRelativeScale3D(FVector(1.5));
    Visual->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Stylized_Vegetables/Meshes/SM_Broccoli"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    GripSurface=CreateDefaultSubobject<UMCFoodGripComponent>(TEXT("GripSurface")); GripSurface->SetupAttachment(Visual);
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
    OnRep_Item(); Body->SetMassOverrideInKg(NAME_None,Settings.Mass,true);
    Body->OnComponentHit.AddDynamic(this,&AMCFoodActor::OnHit); OnRep_Phase();
    if (HasAuthority() && !ItemName.IsNone() && SpoilAt<=0 && FoodData.Kind==EMCFoodKind::Food) SpoilAt=GetWorld()->GetTimeSeconds()+FoodData.SpoilSeconds;
}
void AMCFoodActor::Initialize(bool bJam,FVector ExtractionDirection)
{
    if (!HasAuthority()) return;
    bJamOnLanding=bJam; PullDirection=ExtractionDirection.GetSafeNormal2D();
    if (PullDirection.IsNearlyZero()) PullDirection=FVector(0,1,0);
}
void AMCFoodActor::BeginMouthEntry(FVector LaunchVelocity,float PushSpeed,TOptional<FVector> ExpectedLanding)
{
    if (!HasAuthority() || !HasActorBegunPlay() || IsDisposed() || bBrushTool || StackCarrier || !Holders.IsEmpty()
        || Phase!=EMCFoodPhase::Falling || LaunchVelocity.ContainsNaN() || LaunchVelocity.IsNearlyZero() || !FMath::IsFinite(PushSpeed)) return;
    bMouthEntry=true; MouthEntryPushSpeed=FMath::Clamp(PushSpeed,0.f,180.f);
    MouthEntryLanding.Reset();
    if(ExpectedLanding.IsSet() && !ExpectedLanding.GetValue().ContainsNaN()) MouthEntryLanding=ExpectedLanding;
    MouthEntryEndsAt=GetWorld()->GetTimeSeconds()+8;
    bLandingPending=false;
    Body->SetEnableGravity(true); Body->SetLinearDamping(0);
    Body->SetPhysicsLinearVelocity(LaunchVelocity);
    PrePhysicsVelocity=LaunchVelocity;
    Body->WakeAllRigidBodies(); ForceNetUpdate();
}
void AMCFoodActor::EndMouthEntry()
{
    if (!bMouthEntry) return;
    if(bEntryImpactManaged) EntryImpactSafeUntil=GetWorld()->GetTimeSeconds()+.8;
    bMouthEntry=false; MouthEntryPushSpeed=0; MouthEntryEndsAt=0;
    MouthEntryLanding.Reset();
    Body->SetLinearDamping(.7f);
}
void AMCFoodActor::MarkRiverSwept(float EscapeZ)
{
    if (!HasAuthority() || IsDisposed() || !FMath::IsFinite(EscapeZ)) return;
    if (!bRiverSwept) { bRiverSwept=true; RiverEscapeZ=EscapeZ; ForceNetUpdate(); }
    else RiverEscapeZ=FMath::Min(RiverEscapeZ,EscapeZ);
}
float AMCFoodActor::OutOfArenaZ() const
{
    if (!EscapeTongue.IsValid())
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
            if (!It->CurrentVertices().IsEmpty()) { EscapeTongue=*It; break; }
    // Imported tongues can sit hundreds of centimetres below world zero. A
    // resting ingredient on that real floor has not fallen out of the arena.
    const float FloorCutoff=EscapeTongue.IsValid()?float(EscapeTongue->Surface->Bounds.GetBox().Min.Z-250.f):-250.f;
    return bRiverSwept?FMath::Min(RiverEscapeZ,FloorCutoff):FloorCutoff;
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
    ReplicatedActorScale=GetActorScale3D();
    Super::PreReplication(ChangedPropertyTracker);
    CarryPresentation.Holder=StackCarrier?StackCarrier.Get():Phase==EMCFoodPhase::Carried && Holders.Num()==1?Holders[0].Get():nullptr;
    if (IsValid(CarryPresentation.Holder))
    {
        // Send the landing pose while the hop is in flight. A delayed packet must
        // never pull a finished client animation back along its old trajectory.
        const FTransform Pose=StackCarrier && IsStackPickupActive()?StackCarrier->FoodCollection->StackPose(this):GetActorTransform();
        const FTransform Relative=Pose.GetRelativeTransform(CarryPresentation.Holder->GetActorTransform());
        CarryPresentation.Location=Relative.GetLocation(); CarryPresentation.Rotation=Relative.Rotator();
    }
}
void AMCFoodActor::OnRep_ActorScale()
{
    SetActorScale3D(ReplicatedActorScale);
}
void AMCFoodActor::UpdateCarryPresentation(float Dt)
{
    auto* Carrier=CarryPresentation.Holder.Get();
    if (HasAuthority() || !IsValid(Carrier) || !(StackCarrier==Carrier || Phase==EMCFoodPhase::Carried && Carrier->Grip->Holds(this))) return;
    const FTransform RenderedCarrier=Carrier->StandingMeshTransform().Inverse()*Carrier->GetMesh()->GetComponentTransform();
    if(StackCarrier && IsStackPickupActive())
    {
        // The cue includes its enforced flat orientation and offset. Rebase the
        // server landing pose onto the same smoothed carrier as ordinary carry.
        const FTransform Goal=CarryPresentation.Holder==Carrier
            ?FTransform(CarryPresentation.Rotation,FVector(CarryPresentation.Location))*RenderedCarrier
            :FTransform(StackRestRotation(RenderedCarrier.GetRotation()),RenderedCarrier.TransformPosition(FVector(73,12,20+StackPickup.SlotHeight)+FVector(StackPickup.SlotOffset)));
        const FTransform Pose=StackPickupPose(Goal);
        SetActorLocationAndRotation(Pose.GetLocation(),Pose.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
        PresentationCarrier=Carrier;SmoothedCarryRelative=Pose.GetRelativeTransform(RenderedCarrier);
        return;
    }
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
    if (bMouthEntry && (Phase!=EMCFoodPhase::Falling || StackCarrier || !Holders.IsEmpty())) EndMouthEntry();
    const bool bGone=IsDisposed() || Phase==EMCFoodPhase::Equipped;
    const bool bSimulate=HasAuthority() && !StackCarrier && (Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free || Phase==EMCFoodPhase::Carried);
    // Stop Chaos before disabling collision, including disposal while the item is still moving.
    if (!bSimulate) Body->SetSimulatePhysics(false);
    SetActorHiddenInGame(bGone); Body->SetCollisionEnabled((bGone || Phase==EMCFoodPhase::Swallowing)?ECollisionEnabled::NoCollision:Phase==EMCFoodPhase::Absorbing?ECollisionEnabled::QueryOnly:ECollisionEnabled::QueryAndPhysics);
    Body->SetCollisionResponseToChannel(ECC_Pawn,Phase==EMCFoodPhase::Absorbing?ECR_Ignore:ECR_Block);
    // A carried kinematic body must not shove neighbouring food out of a pile.
    // Server contact probes still detect real incoming bodies above the impulse threshold.
    Body->SetCollisionResponseToChannel(ECC_PhysicsBody,StackCarrier?ECR_Overlap:ECR_Block);
    // Simulated proxies are kinematic; only the server applies springs and impact damage.
    if (bSimulate) Body->SetSimulatePhysics(true);
}
bool AMCFoodActor::TryGrab(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || bBrushTool || !UsesLegacyGrip() || !IsValid(Hero) || IsDisposed() || Phase==EMCFoodPhase::Swallowing || Phase==EMCFoodPhase::Equipped || !Hero->CanWork() || (!Holders.Contains(Hero) && !Hero->Grip->CanAcquire(this))) return false;
    if (Holders.Contains(Hero)) return true;
    if (Phase==EMCFoodPhase::Carried) return false;
    if (FVector::Dist(Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()),Hero->GetActorLocation())>Settings.GrabReach) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodGrab),false,Hero); Params.AddIgnoredActor(this);
    const FVector GrabPoint=Phase==EMCFoodPhase::Absorbing?Visual->Bounds.GetBox().GetClosestPointTo(Hero->GetActorLocation()):GetActorLocation();
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),GrabPoint,ECC_Visibility,Params)) return false;
    if (!Hero->Grip || !Hero->Grip->BeginGrip(this)) return false;
    EndMouthEntry();
    Holders.Add(Hero); Hero->HeldFood=Hero->Grip->Frame.Food;
    LastHandledBy=Hero->GetPlayerState<AMCPlayerState>();
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
    // Closest-point queries require convex shapes. Use the existing simulation
    // body as an aim hint, then trace the detailed, deformed triangle surface.
    if (Body->GetClosestPointOnCollision(From,Closest)>=0)
    {
        if(!bBrushTool && ItemMesh)
        {
            const FVector Scale=bFragment?FoodData.FragmentScale:FoodData.Scale;
            const FTransform RestPose(FQuat::Identity,-ItemMesh->GetBounds().Origin*Scale,Scale);
            const FVector MeshPoint=RestPose.InverseTransformPosition(Body->GetComponentTransform().InverseTransformPosition(Closest));
            Closest=Visual->GetComponentTransform().TransformPosition(MeshPoint);
        }
        TryRay(Closest);
    }
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
bool AMCFoodActor::FindToolContact(const AMCToothCharacter* Hero,float Reach,FHitResult& Hit) const
{
    if (!IsValid(Hero) || !GripSurface || !GripSurface->GetStaticMesh() || Reach<=0) return false;
    const FVector From=Hero->GetActorLocation(),Forward=Hero->GetActorForwardVector().GetSafeNormal2D();
    const FBox Bounds=Visual->Bounds.GetBox();
    if (FVector::DistSquared(From,Bounds.GetClosestPointTo(From))>FMath::Square(Reach)) return false;
    if (Forward.IsNearlyZero()) return false;
    // The downward chop covers several heights. The sphere gives a small amount
    // of aim tolerance while still requiring contact with actual mesh triangles.
    const float Radius=FMath::Min(45.f,Reach*.25f);
    bool Found=false; double Best=DBL_MAX;
    for (float Height:{-45.f,15.f,75.f})
    {
        const FVector Start=From+Forward*Radius+FVector(0,0,Height);
        const FVector End=From+Forward*(Reach-Radius)+FVector(0,0,Height);
        FHitResult Candidate;
        if (!GripSurface->SweepComponent(Candidate,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(Radius),true)) continue;
        const FVector Offset=Candidate.ImpactPoint-From;
        const double Distance=Offset.SizeSquared();
        if (FVector::DotProduct(Offset,Forward)<-2.f || Distance>FMath::Square(Reach) || Distance>=Best) continue;
        FHitResult Obstacle;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodToolContact),false,Hero); Params.AddIgnoredActor(this);
        if (GetWorld()->LineTraceSingleByChannel(Obstacle,From,Candidate.ImpactPoint,ECC_Visibility,Params)) continue;
        Best=Distance; Hit=Candidate; Found=true;
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
    Phase=EMCFoodPhase::Disposed;
    FuseEndsAt=0; PausedFuse=0; bFusePaused=false; FuseOwner.Reset(); bLandingPending=false;
    OnRep_Phase(); ForceNetUpdate(); SetLifeSpan(IsHazardResolved()?3:0);
}
void AMCFoodActor::AwardDelivery()
{
    if (!HasAuthority() || bDeliveryScored || bBrushTool || IsWrongIngredient()) return;
    bDeliveryScored=true;
    if (auto* Tutorial=AMCTutorialDirector::Find(GetWorld()))
        if (LastHandledBy) Tutorial->NotifyAction(Cast<AMCToothCharacter>(LastHandledBy->GetPawn()),EMCTutorialAction::FoodDelivered,this);
    if (auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTaskToPlayerState(LastHandledBy,EMCScoreTask::Food);
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
void AMCFoodActor::OnHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit)
{
    if(StackCarrier) {
        if(HasAuthority()) StackCarrier->FoodCollection->HandleStackCollision(this,Other,OtherComponent,Impulse,Hit);
        return;
    }
    // Require a downward approach to a supporting surface. Stack contact and
    // horizontal solver impulses are not landing or received damage.
    if(HasAuthority() && !IsDisposed() && !bBrushTool && !StackCarrier && Holders.IsEmpty()
        && Hit.ImpactNormal.Z>.55f && PrePhysicsVelocity.Z<-140)
        ReactToImpact(FMath::Clamp(-PrePhysicsVelocity.Z/650.f,.15f,1.f));
    if (!HasAuthority() || IsDisposed() || Phase==EMCFoodPhase::Carried || !IsValid(Other)) return;
    // Only an incoming event walnut can crack another whole event walnut.
    // Resting contacts, carried pieces and fragments cannot chain this hazard.
    if(bMouthEntry && !bFragment && Holders.IsEmpty() && ActorHasTag(TEXT("MCNutRain")) && PrePhysicsVelocity.Z<-140)
        if(auto* Nut=Cast<AMCFoodActor>(Other); Nut && !Nut->IsDisposed() && !Nut->bFragment
            && !Nut->StackCarrier && Nut->Holders.IsEmpty() && Nut->ActorHasTag(TEXT("MCNutRain")))
        {
            const FVector Approach=-Hit.ImpactNormal.GetSafeNormal();
            const float ClosingSpeed=FVector::DotProduct(PrePhysicsVelocity-Nut->GetVelocity(),Approach);
            if(Hit.bBlockingHit && FMath::IsFinite(ClosingSpeed) && ClosingSpeed>=Settings.ImpactSpeed) {
                const FVector Direction=(Nut->GetActorLocation()-GetActorLocation()).GetSafeNormal();
                Nut->HitFood(FMath::Max(1.f,Nut->Health),Direction);
                HitFood(FMath::Max(1.f,Health),-Direction);
                return;
            }
        }
    if (bMouthEntry && !Cast<AMCToothCharacter>(Other) && OtherComponent && Hit.bBlockingHit
        && (OtherComponent->GetCollisionObjectType()==ECC_WorldStatic || OtherComponent->GetCollisionObjectType()==ECC_WorldDynamic))
    {
        // A real supporting tongue contact resolves a managed impact once. The
        // hazard marks itself before applying damage; solver contacts may repeat.
        if(Cast<AMCTongue>(Other) && Hit.ImpactNormal.Z>.55f && PrePhysicsVelocity.Z<-140)
            OnEntryLanding.Broadcast(Hit);
        if (FoodData.Kind==EMCFoodKind::Food && OtherComponent->GetCollisionObjectType()==ECC_WorldStatic
            && Hit.ImpactNormal.Z>.6f && PrePhysicsVelocity.Z<-140 && Body->IsSimulatingPhysics())
        {
            // Flight can span the whole arena. Retain a little post-solver motion,
            // without turning the landed piece into a high-speed rolling attack.
            const FVector LandingVelocity=Body->GetPhysicsLinearVelocity().GetClampedToMaxSize(160.f);
            Body->SetPhysicsLinearVelocity(LandingVelocity);
            PrePhysicsVelocity=LandingVelocity;
        }
        EndMouthEntry();
    }
    if ((Phase==EMCFoodPhase::Falling || (FoodData.Kind==EMCFoodKind::Spicy && PrePhysicsVelocity.Z<-140))
        && Hit.ImpactNormal.Z>.6f && OtherComponent && OtherComponent->GetCollisionObjectType()==ECC_WorldStatic)
        bLandingPending=true;
    UMCToothStatusComponent* Target=Other->FindComponentByClass<UMCToothStatusComponent>();
    if(bEntryImpactManaged && Cast<AMCToothCharacter>(Other)
        && (bMouthEntry || GetWorld()->GetTimeSeconds()<EntryImpactSafeUntil)) return;
    if (!Target || !Target->IsAlive() || Phase==EMCFoodPhase::Stuck || bBrushTool) return;
    if (const auto* Arena=Cast<AMCArenaTooth>(Other); Arena && !Arena->IsAvailable()) return;
    if (const auto* Hero=Cast<AMCToothCharacter>(Other); Hero && (GetWorld()->GetTimeSeconds()<StackReleaseSafeUntil || Holders.Contains(Hero) || StackCarrier==Hero || Hero->FoodCollection->IsSettlingRelease(this))) return;
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
    if (bMouthEntry && FoodData.Kind==EMCFoodKind::Food)
        if (auto* Hero=Cast<AMCToothCharacter>(Other))
        {
            // The nut cataclysm is an avoidable impact hazard. Ordinary food
            // keeps its gentle entry behavior; both use the contact/speed gate above.
            if (ActorHasTag(TEXT("MCNutRain")))
                Target->Damage(FMath::Min(Settings.MaxDamage,Speed*Settings.DamagePerSpeed*Settings.Mass/9.f),Direction);
            if (!Hero->IsMimicCaptured() && Hero->ToothPhysics->GetBodyState()!=EMCBodyState::Recovering)
            {
                FVector IncomingDirection=PrePhysicsVelocity.GetSafeNormal2D();
                if (IncomingDirection.IsNearlyZero()) IncomingDirection=Direction;
                const float Push=MouthEntryPushSpeed*FMath::Clamp(IncomingSpeed/600.f,.5f,1.f);
                const FVector Velocity=(IncomingDirection*Push+FVector(0,0,Push*(25.f/140.f)))
                    .GetClampedToMaxSize(Hero->ToothPhysics->Settings.FallThreshold*.8f);
                if (Hero->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll)
                    Hero->GetMesh()->AddImpulseToAllBodiesBelow(Velocity,Hero->RigBone(TEXT("body")),true,true);
                else Hero->LaunchCharacter(Velocity,false,false);
                Hero->ForceNetUpdate();
            }
            return;
        }
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
    if (bMouthEntry && (GetWorld()->GetTimeSeconds()>=MouthEntryEndsAt || !Body->IsSimulatingPhysics()
        || Phase!=EMCFoodPhase::Falling || StackCarrier || !Holders.IsEmpty())) EndMouthEntry();
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
    if (Phase!=EMCFoodPhase::Carried && !StackCarrier) PresentationCarrier.Reset();
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
        const float EscapeZ=OutOfArenaZ();
        if (bBrushTool && GetActorLocation().Z<EscapeZ) { Dispose(); return; }
        // The river leaves swept pieces where they settle. If a piece actually
        // falls out below the arena, remove it instead of respawning it upstream.
        if (bRiverSwept && GetActorLocation().Z<EscapeZ) { Dispose(); return; }
        if (!bRiverSwept && GetActorLocation().Z<EscapeZ)
        { EndMouthEntry(); for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]); SetActorLocation(FVector(0,0,Settings.DropHeight),false,nullptr,ETeleportType::TeleportPhysics); Body->SetPhysicsLinearVelocity(FVector::ZeroVector); }
        PrePhysicsVelocity=Body->GetPhysicsLinearVelocity();
    }
    if(GetNetMode()==NM_DedicatedServer) return;
    FString Caption=Phase==EMCFoodPhase::Stuck?FString::Printf(TEXT("LMB + MOVE TO CENTRE\nPULL %.0f%% | %d GRIPS"),PullProgress*100,Holders.Num()):Phase==EMCFoodPhase::Carried?TEXT("RELEASE LMB: DROP | Q: THROW"):TEXT("HOLD LMB: PICK UP / DRAG");
    if (!UsesLegacyGrip()) Caption=StackCarrier?TEXT("LMB: DROP STACK | Q: THROW"):
        IsHardFood()?TEXT("PICKAXE: BREAK FOR XP"):TEXT("KNIFE: BREAK FOR XP");
    if (!ItemName.IsNone()) Caption=FString::Printf(TEXT("%s | HP %.0f | %.1f kg\n%s | %s"),*FoodData.Label.ToString(),Health,Settings.Mass,*Caption,bSpoiled?TEXT("SPOILED"):FoodData.Kind==EMCFoodKind::Spicy?*FString::Printf(TEXT("%.1fs %s"),FuseRemaining(),bFusePaused?TEXT("PAUSED"):TEXT("THROW INTO THROAT")):FoodData.Kind==EMCFoodKind::ForeignObject?TEXT("FOREIGN OBJECT"):*FString::Printf(TEXT("SPOIL %.0fs"),FMath::Max(0.,SpoilAt-(GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds()))));
    if (bBrushTool) Caption=TEXT("LMB: PICK UP BRUSH\nQ: THROW OVERBOARD");
    if(FoodData.Kind==EMCFoodKind::Spicy) {
        Caption=FuseEndsAt>0 || bFusePaused
            ?FString::Printf(TEXT("SPICY PEPPER | %.1fs%s\nTHROW THROUGH EXIT"),FuseRemaining(),bFusePaused?TEXT(" [PAUSED]"):TEXT(""))
            :FString::Printf(TEXT("SPICY PEPPER\nTHROW THROUGH EXIT | %.0fs AFTER LANDING"),FoodData.FuseSeconds);
        if(!FMath::IsNearlyEqual(Label->WorldSize,18.f)) Label->SetWorldSize(18);
        Label->SetTextRenderColor(FuseRemaining()<=3 && !bFusePaused?FColor(255,75,35):FColor(255,220,90));
        const FVector LabelLocation(0,0,Body->GetUnscaledBoxExtent().Z+30);
        if(!Label->GetRelativeLocation().Equals(LabelLocation,.001)) Label->SetRelativeLocation(LabelLocation);
    }
    if(Caption!=LastLabelCaption) {LastLabelCaption=Caption;Label->SetText(FText::FromString(Caption));}
    Label->SetVisibility(Phase!=EMCFoodPhase::Swallowing);
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCFoodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodActor,Settings); DOREPLIFETIME(AMCFoodActor,Phase);
    DOREPLIFETIME(AMCFoodActor,ReplicatedActorScale);
    DOREPLIFETIME(AMCFoodActor,CarryPresentation);
    DOREPLIFETIME(AMCFoodActor,StackCarrier);DOREPLIFETIME(AMCFoodActor,StackPickup);DOREPLIFETIME(AMCFoodActor,ImpactAt);DOREPLIFETIME(AMCFoodActor,ImpactStrength);
    DOREPLIFETIME(AMCFoodActor,PullProgress); DOREPLIFETIME(AMCFoodActor,PullDirection); DOREPLIFETIME(AMCFoodActor,Holders);
    DOREPLIFETIME(AMCFoodActor,ItemMesh); DOREPLIFETIME(AMCFoodActor,FoodData); DOREPLIFETIME(AMCFoodActor,ItemName); DOREPLIFETIME(AMCFoodActor,Health);
    DOREPLIFETIME(AMCFoodActor,bFragment); DOREPLIFETIME(AMCFoodActor,bBrushTool); DOREPLIFETIME(AMCFoodActor,bSpoiled); DOREPLIFETIME(AMCFoodActor,SpoilAt);
    DOREPLIFETIME(AMCFoodActor,bRiverSwept);
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
    DeliveryZoneVisual=CreateDefaultSubobject<UMCDeliveryZoneVisualComponent>(TEXT("DeliveryZoneVisual"));
    Label->SetRelativeRotation(FRotator(0,180,0)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(30);
    Label->SetText(FText::FromString(TEXT("FOOD >>> THROAT\nBRING FOOD HERE"))); Label->SetTextRenderColor(FColor(115,255,210));
}
void AMCFoodDisposal::GetDeliveryZoneGeometry(FTransform& OutTransform,FVector& OutHalfExtent,bool& bOutCircular) const
{
    OutTransform=Volume->GetComponentTransform();OutHalfExtent=Volume->GetUnscaledBoxExtent();bOutCircular=false;
}
bool AMCFoodDisposal::CacheDeliveryZoneOutline() const
{
    if(!DeliveryOutlineTongue.IsValid() && GetWorld())
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {DeliveryOutlineTongue=*It;break;}
    const auto* Tongue=DeliveryOutlineTongue.Get();
    // BeginPlay order can leave the tongue's native vertex buffers temporarily empty.
    // Do not cache that result: the next query must see its completed surface.
    if(!IsValid(Tongue) || !Tongue->Surface || Tongue->CurrentVertices().IsEmpty() || Tongue->TriangleIndices().IsEmpty()) return false;
    FTransform Geometry;FVector Extent;bool Circular;
    GetDeliveryZoneGeometry(Geometry,Extent,Circular);
    const FTransform SurfaceTransform=Tongue->Surface->GetComponentTransform();
    const auto& Vertices=Tongue->CurrentVertices();const auto& Indices=Tongue->TriangleIndices();
    if(bDeliveryOutlineCached && DeliveryOutlineSource.Get()==Tongue->SourceMesh
        && DeliveryOutlineSurfaceTransform.Equals(SurfaceTransform) && DeliveryOutlineZoneTransform.Equals(Geometry)
        && DeliveryOutlineExtent.Equals(Extent) && DeliveryOutlineVertexCount==Vertices.Num() && DeliveryOutlineIndexCount==Indices.Num()
        && bDeliveryOutlineBrush==bBrushBin && bDeliveryOutlineCircular==Circular) return DeliveryOutlineOuter.Num()>1;

    DeliveryOutlineOuter.Reset();DeliveryOutlineInner.Reset();bDeliveryOutlineCached=false;
    TArray<FVector> WorldVertices;WorldVertices.Reserve(Vertices.Num());FBox Bounds(ForceInit);
    for(const FVector& Vertex:Vertices) {const FVector P=SurfaceTransform.TransformPosition(Vertex);WorldVertices.Add(P);Bounds+=P;}
    constexpr int32 Sections=32;
    const double Step=(Bounds.Max.Y-Bounds.Min.Y)/Sections;
    if(Step<=KINDA_SMALL_NUMBER) return false;
    TArray<double> Lower,Upper;Lower.Init(TNumericLimits<double>::Max(),Sections+1);Upper.Init(-TNumericLimits<double>::Max(),Sections+1);
    // Intersect the actual authored triangles with horizontal XY scan lines.
    // Tongue motion only changes vertex Z, so this silhouette is stable across peers.
    for(int32 Triangle=0;Triangle+2<Indices.Num();Triangle+=3)
    {
        if(!WorldVertices.IsValidIndex(Indices[Triangle]) || !WorldVertices.IsValidIndex(Indices[Triangle+1]) || !WorldVertices.IsValidIndex(Indices[Triangle+2])) continue;
        const FVector Points[]={WorldVertices[Indices[Triangle]],WorldVertices[Indices[Triangle+1]],WorldVertices[Indices[Triangle+2]]};
        const double MinY=FMath::Min3(Points[0].Y,Points[1].Y,Points[2].Y),MaxY=FMath::Max3(Points[0].Y,Points[1].Y,Points[2].Y);
        const int32 First=FMath::Clamp(FMath::FloorToInt((MinY-Bounds.Min.Y)/Step)-1,0,Sections);
        const int32 Last=FMath::Clamp(FMath::CeilToInt((MaxY-Bounds.Min.Y)/Step)+1,0,Sections);
        for(int32 Row=First;Row<=Last;++Row)
        {
            const double Y=Bounds.Min.Y+Row*Step;
            for(int32 Edge=0;Edge<3;++Edge)
            {
                const FVector& A=Points[Edge];const FVector& B=Points[(Edge+1)%3];
                const double DY=B.Y-A.Y;
                if(FMath::Abs(DY)<=KINDA_SMALL_NUMBER)
                {
                    if(FMath::Abs(Y-A.Y)>KINDA_SMALL_NUMBER) continue;
                    Lower[Row]=FMath::Min(Lower[Row],FMath::Min(A.X,B.X));Upper[Row]=FMath::Max(Upper[Row],FMath::Max(A.X,B.X));
                }
                else
                {
                    const double T=(Y-A.Y)/DY;if(T<0 || T>1) continue;
                    const double X=FMath::Lerp(A.X,B.X,T);
                    Lower[Row]=FMath::Min(Lower[Row],X);Upper[Row]=FMath::Max(Upper[Row],X);
                }
            }
        }
    }
    TArray<int32> ValidRows;
    for(int32 Row=0;Row<=Sections;++Row) if(Lower[Row]<=Upper[Row]) ValidRows.Add(Row);
    if(ValidRows.Num()<2) return false;
    const double TargetY=FMath::Clamp(Geometry.GetLocation().Y,Bounds.Min.Y+ValidRows[0]*Step,Bounds.Min.Y+ValidRows.Last()*Step);
    double ReferenceOuter=bBrushBin?Lower[ValidRows[0]]:Upper[ValidRows[0]];
    for(int32 I=1;I<ValidRows.Num();++I) if(TargetY<=Bounds.Min.Y+ValidRows[I]*Step)
    {
        const int32 Before=ValidRows[I-1],After=ValidRows[I];
        const double Alpha=(TargetY-(Bounds.Min.Y+Before*Step))/((After-Before)*Step);
        ReferenceOuter=FMath::Lerp(bBrushBin?Lower[Before]:Upper[Before],bBrushBin?Lower[After]:Upper[After],Alpha);break;
    }
    const double DesiredInnerX=Geometry.TransformPosition(FVector(bBrushBin?Extent.X:-Extent.X,0,0)).X;
    const double ReferenceDepth=bBrushBin?DesiredInnerX-ReferenceOuter:ReferenceOuter-DesiredInnerX;
    if(ReferenceDepth<=KINDA_SMALL_NUMBER) return false;
    const double HalfHeight=(Bounds.Max.Y-Bounds.Min.Y)*.5,CenterY=(Bounds.Max.Y+Bounds.Min.Y)*.5;
    const double ReferenceNorm=(TargetY-CenterY)/HalfHeight;
    const double ReferenceFade=FMath::Pow(FMath::Max(.001,1-ReferenceNorm*ReferenceNorm),.6);
    for(int32 Row:ValidRows)
    {
        const double Y=Bounds.Min.Y+Row*Step,Norm=(Y-CenterY)/HalfHeight;
        const double Fade=FMath::Pow(FMath::Max(0.,1-Norm*Norm),.6);
        // Both caps taper closed at the rim and leave a clear middle on narrow sections.
        const double Depth=FMath::Min(ReferenceDepth*Fade/ReferenceFade,(Upper[Row]-Lower[Row])*.4);
        const double OuterX=bBrushBin?Lower[Row]:Upper[Row],InnerX=OuterX+(bBrushBin?Depth:-Depth);
        DeliveryOutlineOuter.Add(FVector(OuterX,Y,Geometry.GetLocation().Z));
        DeliveryOutlineInner.Add(FVector(InnerX,Y,Geometry.GetLocation().Z));
    }
    DeliveryOutlineSource=Tongue->SourceMesh;DeliveryOutlineSurfaceTransform=SurfaceTransform;DeliveryOutlineZoneTransform=Geometry;
    DeliveryOutlineExtent=Extent;DeliveryOutlineVertexCount=Vertices.Num();DeliveryOutlineIndexCount=Indices.Num();
    bDeliveryOutlineBrush=bBrushBin;bDeliveryOutlineCircular=Circular;bDeliveryOutlineCached=true;
    return true;
}
bool AMCFoodDisposal::GetDeliveryZoneOutline(TArray<FVector>& OutOuter,TArray<FVector>& OutInner) const
{
    if(!CacheDeliveryZoneOutline()) {OutOuter.Reset();OutInner.Reset();return false;}
    OutOuter=DeliveryOutlineOuter;OutInner=DeliveryOutlineInner;return true;
}
bool AMCFoodDisposal::ContainsDeliveryCap(FVector Position,bool& bOutHasCap) const
{
    bOutHasCap=CacheDeliveryZoneOutline();if(!bOutHasCap) return false;
    if(Position.Y<DeliveryOutlineOuter[0].Y || Position.Y>DeliveryOutlineOuter.Last().Y) return false;
    for(int32 I=1;I<DeliveryOutlineOuter.Num();++I) if(Position.Y<=DeliveryOutlineOuter[I].Y)
    {
        const double Alpha=(Position.Y-DeliveryOutlineOuter[I-1].Y)/(DeliveryOutlineOuter[I].Y-DeliveryOutlineOuter[I-1].Y);
        const double OuterX=FMath::Lerp(DeliveryOutlineOuter[I-1].X,DeliveryOutlineOuter[I].X,Alpha);
        const double InnerX=FMath::Lerp(DeliveryOutlineInner[I-1].X,DeliveryOutlineInner[I].X,Alpha);
        return Position.X>=FMath::Min(OuterX,InnerX) && Position.X<=FMath::Max(OuterX,InnerX);
    }
    return false;
}
bool AMCFoodDisposal::DeliverySurfaceFloorZ(FVector Position,double& OutFloorZ) const
{
    const auto* Tongue=DeliveryOutlineTongue.Get();FHitResult Hit;
    if(!IsValid(Tongue) || !Tongue->SurfacePoint(Position,Hit)) return false;
    OutFloorZ=Hit.ImpactPoint.Z;return true;
}
bool AMCFoodDisposal::ContainsDeliveryPosition(FVector Position) const
{
    if(Position.ContainsNaN()) return false;
    const FVector Local=Volume->GetComponentTransform().InverseTransformPosition(Position);
    const FVector Extent=Volume->GetUnscaledBoxExtent();
    bool HasCap=false;const bool InCap=ContainsDeliveryCap(Position,HasCap);
    if(HasCap)
    {
        if(!InCap) return false;
        double FloorZ;return DeliverySurfaceFloorZ(Position,FloorZ) && Position.Z>=FloorZ-80 && Local.Z<=Extent.Z;
    }
    return FMath::Abs(Local.X)<=Extent.X && FMath::Abs(Local.Y)<=Extent.Y && FMath::Abs(Local.Z)<=Extent.Z;
}
FVector AMCFoodDisposal::GetDeliveryDirection() const
{
    FTransform Transform;FVector Extent;bool Circular;
    GetDeliveryZoneGeometry(Transform,Extent,Circular);
    return Transform.TransformVectorNoScale(bBrushBin?-FVector::ForwardVector:FVector::ForwardVector).GetSafeNormal2D();
}
bool AMCFoodDisposal::IsFoodInDeliveryZone(const AMCFoodActor* Food) const
{
    if(!IsValid(Food) || Food->GetWorld()!=GetWorld()) return false;
    const auto InZone=[this](const AMCToothCharacter* Carrier) {
        return IsValid(Carrier) && Carrier->CanWork() && ContainsDeliveryPosition(Carrier->GetActorLocation());
    };
    if(Food->StackCarrier) return InZone(Food->StackCarrier);
    if(Food->EquippedBy) return InZone(Food->EquippedBy);
    for(const auto& Holder:Food->Holders) if(InZone(Holder)) return true;
    return ContainsDeliveryPosition(Food->GetActorLocation());
}
bool AMCFoodDisposal::CanAcceptDelivery(const AMCFoodActor* Food) const
{
    if(!HasAuthority() || !IsValid(Food) || Food->IsDisposed() || Food->IsMouthEntryActive() || !IsFoodInDeliveryZone(Food)) return false;
    // Keep the working brush equipped while cleaning inside the broad exit cap.
    // A brush enters disposal only after the player's explicit Q throw/drop.
    if(Food->Phase!=EMCFoodPhase::Free && Food->Phase!=EMCFoodPhase::Falling && Food->Phase!=EMCFoodPhase::Carried) return false;
    // The front exit now accepts the same rejected ingredients in normal play as in the lesson.
    // Fresh ingredients stay available for delivery to the green throat zone.
    return bBrushBin?(Food->bBrushTool || Food->IsWrongIngredient() || Food->FoodData.Kind==EMCFoodKind::Spicy):!Food->bBrushTool;
}
bool AMCFoodDisposal::AcceptDelivery(AMCFoodActor* Food)
{
    if(!CanAcceptDelivery(Food)) return false;
    auto* Worker=Food->GetLastHandledBy()?Cast<AMCToothCharacter>(Food->GetLastHandledBy()->GetPawn()):nullptr;
    if(!Worker) Worker=Food->StackCarrier?Food->StackCarrier.Get():Food->EquippedBy.Get();
    // The throat's existing handoff removes just this layer from the collection.
    // Setting StackCarrier to null alone leaves a disposed item in Pieces.
    if(Food->StackCarrier && !Food->BeginSwallow()) return false;
    Food->Dispose();
    if(bBrushBin && AMCTutorialDirector::IsTutorialTarget(Food)) {
        if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld()))
            Tutorial->NotifyAction(Worker,Food->bSpoiled?EMCTutorialAction::SpoiledDiscarded:EMCTutorialAction::TrashDiscarded,Food);
    } else if(Worker) Worker->NotifyTaskFeedback(true,Food->GetActorLocation());
    return true;
}
void AMCFoodDisposal::ReturnFreshFood(AMCFoodActor* Food)
{
    FVector ReturnPoint=Food->GetActorLocation();
    if(CacheDeliveryZoneOutline())
    {
        const auto* Tongue=DeliveryOutlineTongue.Get();
        const FVector FoodExtent=Food->Body->Bounds.BoxExtent;
        const double FootprintMargin=FVector2D(FoodExtent.X,FoodExtent.Y).Size()+8;
        const double InitialY=FMath::Clamp(ReturnPoint.Y,DeliveryOutlineInner[0].Y,DeliveryOutlineInner.Last().Y);
        FTransform Geometry;FVector Extent;bool Circular;GetDeliveryZoneGeometry(Geometry,Extent,Circular);
        const double CenterY=FMath::Clamp(Geometry.GetLocation().Y,DeliveryOutlineInner[0].Y,DeliveryOutlineInner.Last().Y);
        bool FoundSupport=false;
        // A cap closes to a point at its ends. Moving a large item a fixed X
        // distance there can cross the entire tongue, so verify its full footprint.
        for(int32 Step=0;Step<=8 && !FoundSupport;++Step)
        {
            const double Y=FMath::Lerp(InitialY,CenterY,Step/8.);
            double InnerX=DeliveryOutlineInner.Last().X;
            for(int32 I=1;I<DeliveryOutlineInner.Num();++I) if(Y<=DeliveryOutlineInner[I].Y)
            {
                const double Alpha=(Y-DeliveryOutlineInner[I-1].Y)/(DeliveryOutlineInner[I].Y-DeliveryOutlineInner[I-1].Y);
                InnerX=FMath::Lerp(DeliveryOutlineInner[I-1].X,DeliveryOutlineInner[I].X,Alpha);break;
            }
            for(int32 Inward=0;Inward<=3 && !FoundSupport;++Inward)
            {
                const FVector Candidate(InnerX+FoodExtent.X+120+Inward*FootprintMargin*.25,Y,ReturnPoint.Z);
                FHitResult Hit;
                if(Tongue->InteriorSurfacePoint(Candidate,FootprintMargin,Hit))
                {
                    ReturnPoint=Candidate;ReturnPoint.Z=FMath::Max(ReturnPoint.Z,Hit.ImpactPoint.Z+FoodExtent.Z+5);
                    FoundSupport=true;
                }
            }
        }
        // Preserve the current holding state if the authored surface cannot fit the item.
        if(!FoundSupport) return;
    }
    else
    {
        const FVector Extent=Volume->GetUnscaledBoxExtent();
        FVector Local=Volume->GetComponentTransform().InverseTransformPosition(ReturnPoint);
        Local.X=Extent.X+Food->Body->Bounds.BoxExtent.X+120;Local.Y=FMath::Clamp(Local.Y,-Extent.Y,Extent.Y);
        ReturnPoint=Volume->GetComponentTransform().TransformPosition(Local);
    }
    auto* Worker=Food->GetLastHandledBy()?Cast<AMCToothCharacter>(Food->GetLastHandledBy()->GetPawn()):nullptr;
    if(Food->StackCarrier) {
        if(!Food->BeginSwallow()) return;
        Food->CancelSwallow();
    }
    for(int32 I=Food->Holders.Num()-1;I>=0;--I) Food->Release(Food->Holders[I]);
    if(auto* Tutorial=AMCTutorialDirector::Find(GetWorld())) Tutorial->NotifyIncorrectSort(Worker,Food);
    else if(Worker) Worker->NotifyTaskFeedback(false,Food->GetActorLocation());
    Food->SetActorLocation(ReturnPoint,false,nullptr,ETeleportType::TeleportPhysics);
    Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Food->ForceNetUpdate();
}
void AMCFoodDisposal::Tick(float Dt)
{
    Super::Tick(Dt);
    const bool TutorialActive=AMCTutorialDirector::IsSafeTutorial(GetWorld());
    Label->SetText(FText::FromString(bBrushBin?TEXT("ВЫХОД\nМУСОР И ИСПОРЧЕННОЕ"):TEXT("ВХОД\nСВЕЖАЯ ЕДА")));
    if (bBrushBin && !bExitConfigured)
    {
        bExitConfigured=true;
        // Match the visible lane inside the mouth, before the containment wall.
        // Saved art may choose a larger footprint; runtime-created exits use the same minimum on peers.
        const FVector Extent=Volume->GetUnscaledBoxExtent();
        Volume->SetBoxExtent(FVector(FMath::Max(260.,Extent.X),FMath::Max(1000.,Extent.Y),FMath::Max(300.,Extent.Z)));
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
    {
        // A fresh incoming piece crosses the front cap on its way into the mouth.
        // Delivery and the wrong-sort return resume only after that flight ends.
        if (It->IsMouthEntryActive()) continue;
        if(CanAcceptDelivery(*It)) {AcceptDelivery(*It);continue;}
        if(bBrushBin && !It->bBrushTool && !It->IsWrongIngredient() && IsFoodInDeliveryZone(*It)
            && (It->Phase==EMCFoodPhase::Free || It->Phase==EMCFoodPhase::Falling || It->Phase==EMCFoodPhase::Carried)
            && (!It->StackCarrier || TutorialActive && AMCTutorialDirector::IsTutorialTarget(*It)))
        {
            ReturnFreshFood(*It);
        }
    }
}
void AMCFoodDisposal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodDisposal,bBrushBin); }

void AMCFoodActor::OnRep_Item()
{
    const FVector ItemScale=bFragment?FoodData.FragmentScale:FoodData.Scale;
    auto* MeshBody=CastChecked<UMCFoodBodyComponent>(Body);
    if (ItemMesh) { Visual->SetStaticMesh(ItemMesh); Visual->SetRelativeLocation(FVector::ZeroVector); Visual->SetRelativeScale3D(ItemScale); }
    if (bBrushTool) { MeshBody->SetCollisionMesh(nullptr,FVector::OneVector); Body->SetCollisionObjectType(ECC_GameTraceChannel1); Body->SetBoxExtent(FVector(12,12,40)); Visual->SetRelativeLocation(FVector(0,0,-35)); Visual->SetRelativeScale3D(FVector(.8)); }
    else if (ItemMesh)
    {
        // Imported variants have different pivots and sizes. Keep the physical centre,
        // visible surface and hand contacts together, using each fragment's independent scale.
        const FBoxSphereBounds Bounds=ItemMesh->GetBounds();
        const FVector Scale=Visual->GetRelativeScale3D();
        Visual->SetRelativeLocation(-Bounds.Origin*Scale);
        MeshBody->SetCollisionMesh(ItemMesh,Scale,FoodData.FindCollisionData(ItemMesh));
    }
    else if (!ItemName.IsNone())
    {
        Visual->SetRelativeScale3D(ItemScale*1.5);
        Visual->SetRelativeLocation(FVector(0,0,-25)*ItemScale);
        if(UStaticMesh* Mesh=Visual->GetStaticMesh()) {Visual->SetRelativeLocation(-Mesh->GetBounds().Origin*Visual->GetRelativeScale3D());MeshBody->SetCollisionMesh(Mesh,Visual->GetRelativeScale3D());}
    }
    else if(UStaticMesh* Mesh=Visual->GetStaticMesh()) {Visual->SetRelativeLocation(-Mesh->GetBounds().Origin*Visual->GetRelativeScale3D());MeshBody->SetCollisionMesh(Mesh,Visual->GetRelativeScale3D());}
    Label->SetRelativeLocation(FVector(0,0,Body->GetUnscaledBoxExtent().Z+28));
    GripSurface->SetStaticMesh(Visual->GetStaticMesh());
    // Construction scripts can add colliders or restore a saved collision profile.
    // Apply on startup and item replication too, preserving all gameplay channels.
    TInlineComponentArray<UPrimitiveComponent*> Colliders(this);
    for (auto* Collider:Colliders) Collider->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
}
void AMCFoodActor::ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment)
{
    if (!HasAuthority()) return;
    FoodData=Row; FoodData.Sanitize(); ItemName=Name; bFragment=Fragment;
    bRiverSwept=false; RiverEscapeZ=-250;
    if (bFragment) { FoodData.Mass/=FoodData.Fragments; FoodData.Health=25; }
    Health=FoodData.Health; Settings.Mass=FoodData.Mass;
    const auto& Choices=bFragment?FoodData.FragmentMeshes:FoodData.WholeMeshes;
    TArray<int32> Available;
    for(int32 I=0;I<Choices.Num();++I) if(!Choices[I].IsNull()) Available.Add(I);
    ItemMesh=nullptr;
    // Select before loading: spawning one variant must not synchronously load all
    // other menu variants. Missing assets can still fall back to another choice.
    while(!Available.IsEmpty() && !ItemMesh)
    {
        const int32 Pick=Random.RandRange(0,Available.Num()-1);
        ItemMesh=Choices[Available[Pick]].LoadSynchronous();
        Available.RemoveAt(Pick);
    }
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
bool AMCFoodActor::HitFood(float Damage,FVector Direction,AMCToothCharacter* Worker)
{
    if (!HasAuthority() || bBrushTool || IsDisposed() || Phase==EMCFoodPhase::Swallowing || !FMath::IsFinite(Damage) || Damage<=0 || Direction.ContainsNaN()) return false;
    if(!IsValid(Worker) || Worker->GetWorld()!=GetWorld()) Worker=nullptr;
    if(Worker) LastHandledBy=Worker->GetPlayerState<AMCPlayerState>();
    EndMouthEntry();
    if(StackCarrier) StackCarrier->FoodCollection->Spill(Direction.GetSafeNormal()*180+FVector(0,0,60));
    if(Phase==EMCFoodPhase::Stuck) {Phase=EMCFoodPhase::Free;StuckTooth=nullptr;OnRep_Phase();}
    ReactToImpact();
    if(FoodData.Kind==EMCFoodKind::Spicy) {
        // Pepper stays one throwable hazard. Cutting and impacts preserve its fuse.
        Body->AddImpulse(Direction.GetSafeNormal()*150+FVector(0,0,60),NAME_None,true);
        return true;
    }
    AttendFood(); Health=FMath::Max(0.f,Health-Damage); ForceNetUpdate();
    if (Health>0) { Body->AddImpulse(Direction.GetSafeNormal()*150+FVector(0,0,60),NAME_None,true); return true; }
    // Completion is the final real tool hit, including on old fragment actors.
    // Use the delivery guard as well so a later intake cannot reward this item twice.
    // Spoiled ordinary food is still a destruction task; legacy sorting stays unchanged.
    if(FoodData.Kind==EMCFoodKind::Food && !bDeliveryScored) {
        bDeliveryScored=true;
        if(Worker) {
            if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>())
                Mode->AwardTaskToPlayerState(Worker->GetPlayerState<AMCPlayerState>(),EMCScoreTask::Food);
            Worker->NotifyTaskFeedback(true,GetActorLocation());
        }
    }
    if(FoodData.Kind==EMCFoodKind::Food)
        AMCReactionVFX::Spawn(GetWorld(),Visual->Bounds.Origin,EMCReactionEffect::FoodBreak,.65f,FMath::Clamp(float(Visual->Bounds.SphereRadius),35.f,150.f),Direction);
    Dispose(); return true;
}
bool AMCFoodActor::IsHardFood() const
{
    if(FoodData.Resistance!=EMCFoodResistance::Automatic) return FoodData.Resistance==EMCFoodResistance::Hard;
    return ItemName==TEXT("Carrot") || ItemName==TEXT("Nut") || ItemName==TEXT("Crust") || ItemName==TEXT("Tartar");
}
bool AMCFoodActor::BeginSwallow()
{
    if (!HasAuthority() || bBrushTool || !Holders.IsEmpty() || (StackCarrier && UsesLegacyGrip()) || (Phase!=EMCFoodPhase::Free && Phase!=EMCFoodPhase::Falling)) return false;
    auto* Collection=StackCarrier?StackCarrier->FoodCollection.Get():nullptr;
    if(Collection && !Collection->Contains(this)) return false;
    // Change phase before detaching so delivery never wakes a dense hand load.
    Phase=EMCFoodPhase::Swallowing;
    if(Collection) Collection->DetachForDelivery(this);
    OnRep_Phase(); ForceNetUpdate(); return true;
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
