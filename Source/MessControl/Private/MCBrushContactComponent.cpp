#include "MCBrushContactComponent.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "EngineUtils.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Misc/App.h"

UMCBrushContactComponent::UMCBrushContactComponent()
{
    PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true);
}
void UMCBrushContactComponent::BeginPlay()
{
    Super::BeginPlay(); Hero=Cast<AMCToothCharacter>(GetOwner());
    for(TActorIterator<AMCTongue> It(GetWorld());It;++It) { Tongue=*It; break; }
    if (!Hero || GetNetMode()==NM_DedicatedServer) return;
    Foam=NewObject<UInstancedStaticMeshComponent>(Hero,TEXT("BrushFoam"));
    Foam->SetupAttachment(Hero->GetRootComponent()); Foam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Foam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Foam->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushFoam.M_BrushFoam")));
    Foam->SetCastShadow(false); Foam->RegisterComponent(); Hero->AddInstanceComponent(Foam);
    PrimaryComponentTick.AddPrerequisite(Hero,Hero->PrimaryActorTick);
}
FTransform UMCBrushContactComponent::SurfaceTransform() const
{
    if(const auto* Tooth=Cast<AMCArenaTooth>(Target)) return Tooth->Visual->GetComponentTransform();
    return IsValid(Target)?Target->GetActorTransform():FTransform::Identity;
}
FVector UMCBrushContactComponent::ContactPoint() const
{ return SurfaceTransform().TransformPosition(LocalPoint); }
FVector UMCBrushContactComponent::ContactNormal() const
{ return SurfaceTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal(); }
FVector UMCBrushContactComponent::BristlePoint() const
{ return Hero?Hero->Brush->GetComponentTransform().TransformPosition(FVector(72,0,-20)):FVector::ZeroVector; }
bool UMCBrushContactComponent::IsTouchingSurface() const
{
    if(!IsValid(Target)) return false;
    // Headless simulation has no evaluated visible wrist. The server still
    // validates the same surface, reach and mask; rendered play waits for contact.
    if(!FApp::CanEverRender() || !GetWorld()->GetGameViewport()) return true;
    return Blend>.98f && FVector::DistSquared(BristlePoint(),ContactPoint())<=FMath::Square(16.f);
}
FTransform UMCBrushContactComponent::HandGoal(FVector Point,FVector Normal) const
{
    // The authored brush runs along +X, with bristles pointing down -Z.
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,Normal).GetSafeNormal();
    const FVector Side=FVector::CrossProduct(Up,Normal).GetSafeNormal();
    // Point the head away from the resting wrist. A fixed tangent direction
    // put the handle inside the adjacent crown on half of the mouth's row.
    const auto& Ref=Hero->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    FTransform Rest=FTransform::Identity;
    for(int32 I=Ref.FindBoneIndex(Hero->RigBone(TEXT("hand_r")));I>=0;I=Ref.GetParentIndex(I)) Rest=Rest*Ref.GetRefBonePose()[I];
    FVector HandleHome=Hero->GetMesh()->GetComponentTransform().TransformPosition(Rest.GetLocation());
    // The resting wrist is low; aiming toward it puts a low stain's handle
    // inside the raised gum. Keep the working handle near the upper chest.
    HandleHome.Z=FMath::Max(HandleHome.Z,Hero->GetActorLocation().Z+60);
    const FVector LengthAxis=FVector::VectorPlaneProject(Point-HandleHome,Normal).GetSafeNormal(.001,Side);
    // On the tongue, keep the handle behind the bristles. World up has no
    // tangent on a horizontal surface, so use the character's facing instead.
    FVector FloorAxis=FVector::VectorPlaneProject((Point-Hero->GetActorLocation()).GetSafeNormal2D(),Normal).GetSafeNormal();
    if(FloorAxis.IsNearlyZero()) FloorAxis=FVector::VectorPlaneProject(Hero->GetActorForwardVector(),Normal).GetSafeNormal();
    const float FloorBlend=FMath::SmoothStep(.55f,.9f,float(Normal.Z));
    FVector Axis=FMath::Lerp(LengthAxis,FloorAxis,FloorBlend).GetSafeNormal();
    const FTransform InHand=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    const FVector Scale=InHand.GetScale3D()*Hero->GetMesh()->GetComponentScale();
    const bool Continue=bHandPresented && Blend>.5f && IsValid(Target) && FVector::DistSquared(Point,ContactPoint())<FMath::Square(60.f)
        && FVector::DistSquared(PresentationBase.GetLocation(),Hero->GetMesh()->GetComponentLocation())<FMath::Square(200.f);
    if(Continue) {
        const FVector PreviousAxis=(InHand*PresentedHand).GetRotation().GetAxisX();
        Axis=FVector::VectorPlaneProject(PreviousAxis,Normal).GetSafeNormal(.001,Axis);
    }
    FTransform Preferred=FTransform::Identity;
    FTransform Best=FTransform::Identity; float BestScore=MAX_flt;
    const FVector RestHome=Hero->GetMesh()->GetComponentTransform().TransformPosition(Rest.GetLocation());
    FCollisionQueryParams Clearance(SCENE_QUERY_STAT(MCBrushHandleRoom),false,Hero);
    if(Target) Clearance.AddIgnoredActor(Target);
    for(float Roll:{0.f,25.f,-25.f,50.f,-50.f,90.f,-90.f,135.f,-135.f,180.f}) {
        const FQuat Rotation=FRotationMatrix::MakeFromXZ(Axis.RotateAngleAxis(Roll,Normal),Normal).ToQuat();
        const FTransform BrushWorld(Rotation,Point+Normal*2-Rotation.RotateVector(FVector(72,0,-20)*Scale),Scale);
        const FTransform Hand=InHand.Inverse()*BrushWorld;
        if(Roll==0) Preferred=Hand;
        const FVector Offset=Hand.GetLocation()-RestHome;
        if(!Offset.Equals(ClampHandOffset(Offset),.01f)) continue;
        bool Blocked=GetWorld()->OverlapBlockingTestByChannel(Hand.GetLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(9),Clearance);
        for(float X:{15.f,48.f,70.f}) if(!Blocked)
            Blocked=GetWorld()->OverlapBlockingTestByChannel(BrushWorld.TransformPosition(FVector(X,0,0)),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5),Clearance);
        if(!Blocked) {
            FHitResult PathHit;
            const bool PathBlocked=GetWorld()->SweepSingleByChannel(PathHit,Continue?PresentedHand.GetLocation():RestHome,
                Hand.GetLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(9),Clearance);
            // A free endpoint can still be behind the neighboring crown. Prefer
            // an orientation with a clear wrist path before minimizing rotation.
            const float Score=(Continue?float(FVector::DistSquared(Hand.GetLocation(),PresentedHand.GetLocation()))
                +2500*FMath::Square(Hand.GetRotation().AngularDistance(PresentedHand.GetRotation())):FMath::Square(Roll))
                +(PathBlocked?100000.f:0.f);
            if(Score<BestScore) { BestScore=Score; Best=Hand; }
            if(!Continue && Roll==0 && !PathBlocked) return Hand;
        }
    }
    return BestScore<MAX_flt?Best:Preferred;
}
FVector UMCBrushContactComponent::ClampHandOffset(FVector Offset) const
{
    const FVector Horizontal=FVector(Offset.X,Offset.Y,0).GetClampedToMaxSize(MaxHandTravel);
    return Horizontal+FVector(0,0,FMath::Clamp(Offset.Z,-MaxHandVerticalTravel,MaxHandVerticalTravel));
}
bool UMCBrushContactComponent::CanReach(FVector Point,FVector Normal) const
{
    const auto* H=Hero?Hero.Get():Cast<AMCToothCharacter>(GetOwner());
    if (!H || !H->GetMesh()->GetSkeletalMeshAsset() || !Hero) return false;
    const auto& Ref=H->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    auto Rest=[&](FName Role) { FTransform T=FTransform::Identity; for(int32 I=Ref.FindBoneIndex(H->RigBone(Role));I>=0;I=Ref.GetParentIndex(I)) T=T*Ref.GetRefBonePose()[I]; return H->GetMesh()->GetComponentTransform().TransformPosition(T.GetLocation()); };
    const FVector Offset=HandGoal(Point,Normal).GetLocation()-Rest(TEXT("hand_r"));
    return !Offset.ContainsNaN() && Offset.Equals(ClampHandOffset(Offset),.01f);
}
bool UMCBrushContactComponent::CanAcquireSurface(const AActor* Surface) const
{
    const auto* H=Hero?Hero.Get():Cast<AMCToothCharacter>(GetOwner());
    if(!H || !IsValid(Surface) || !H->CanWork()) return false;
    const UPrimitiveComponent* Bounds=nullptr;
    if(const auto* Tooth=Cast<AMCArenaTooth>(Surface)) { if(!Tooth->IsAvailable()) return false; Bounds=Tooth->Body; }
    else if(const auto* Patch=Cast<AMCMouthSurface>(Surface)) { if(Patch->bUlcer || Patch->IsClean()) return false; Bounds=Patch->Area; }
    if(!Bounds) return false;
    const FVector D=Bounds->Bounds.GetBox().GetClosestPointTo(H->GetActorLocation())-H->GetActorLocation();
    return D.Size2D()<=SurfaceReach && FMath::Abs(D.Z)<=MaxHandVerticalTravel+70
        && (Target==Surface || FVector::DotProduct(D.GetSafeNormal2D(),H->GetActorForwardVector())>-.5f);
}
void UMCBrushContactComponent::Contact(AActor* Surface,FVector Point,FVector Normal)
{
    if (!GetOwner()->HasAuthority() || !IsValid(Surface)) return;
    Target=Surface; const auto T=SurfaceTransform();
    LocalPoint=T.InverseTransformPosition(Point); LocalNormal=T.InverseTransformVectorNoScale(Normal).GetSafeNormal();
    ContactAt=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
void UMCBrushContactComponent::Release() { if(GetOwner()->HasAuthority()) ContactAt=-100; }
void UMCBrushContactComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(!Hero) return;
    const double Now=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const bool Active=IsValid(Target) && Now-ContactAt<.3 && Hero->bBrushing && Hero->CanWork() && !Hero->HeldFood
        && CanAcquireSurface(Target) && CanReach(ContactPoint(),ContactNormal());
    Blend=FMath::FInterpConstantTo(Blend,Active?1.f:0.f,Dt,3.f);
    if(!Active && Blend<=0 && !bHandPresented && GetOwner()->HasAuthority()) Target=nullptr;
    if(!Foam) return;
    SpawnClock+=Dt;
    if(Active && IsTouchingSurface() && SpawnClock>=.035f) {
        SpawnClock=0; const FVector N=ContactNormal();
        const FVector Side=FVector::CrossProduct(N,FMath::Abs(N.Z)>.9f?Hero->GetActorForwardVector():FVector::UpVector).GetSafeNormal();
        for(int32 I=0;I<2 && Bubbles.Num()<90;++I)
            Bubbles.Add({ContactPoint()+N*3+Side*Random.FRandRange(-10,10)+FVector::UpVector*Random.FRandRange(-8,8),
                N*Random.FRandRange(6,22)+Side*Random.FRandRange(-16,16)+FVector::UpVector*Random.FRandRange(5,20),0,float(Random.FRandRange(.35f,.85f)),float(Random.FRandRange(1.2f,3.5f))});
    }
    Foam->ClearInstances();
    for(int32 I=Bubbles.Num()-1;I>=0;--I) {
        auto& B=Bubbles[I]; B.Age+=Dt; if(B.Age>=B.Life) {Bubbles.RemoveAtSwap(I);continue;}
        B.Velocity.Z-=65*Dt; B.Position+=B.Velocity*Dt;
        const float Size=B.Radius*(1-FMath::SmoothStep(B.Life*.55f,B.Life,B.Age))/50;
        Foam->AddInstance(FTransform(FQuat::Identity,B.Position,FVector(Size)),true);
    }
}
void UMCBrushContactComponent::BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt) const
{
    if(!Hero || !Hero->ToothPhysics->CanAct()) { bHandPresented=false; return; }
    const FTransform World=Hero->GetMesh()->GetComponentTransform();
    if(bHandPresented && FVector::DistSquared(World.GetLocation(),PresentationBase.GetLocation())>FMath::Square(200.f)) bHandPresented=false;
    const bool Contact=IsValid(Target) && Blend>.001f && CanAcquireSurface(Target);
    if(!Contact && !bHandPresented) return;
    const int32 Hand=Ref.FindBoneIndex(Hero->RigBone(TEXT("hand_r")));
    const int32 Lower=Ref.FindBoneIndex(Hero->RigBone(TEXT("forearm_r")));
    if(Hand<0) return;
    TArray<FTransform> CS; CS.SetNum(Pose.Num());
    for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];
    FTransform Rest=FTransform::Identity;
    for(int32 I=Hand;I>=0;I=Ref.GetParentIndex(I)) Rest=Rest*Ref.GetRefBonePose()[I];
    const FVector Home=World.TransformPosition(Rest.GetLocation());
    const FTransform Animated=CS[Hand]*World;
    FTransform Desired=Animated;
    if(Contact) {
        FTransform ContactGoal=HandGoal(ContactPoint(),ContactNormal());
        ContactGoal.SetLocation(Home+ClampHandOffset(ContactGoal.GetLocation()-Home));
        Desired.Blend(Animated,ContactGoal,FMath::SmoothStep(0.f,1.f,Blend));
    }
    auto KeepOutsideSurface=[&](FTransform& Hand,bool Destination=false) {
        if(!IsValid(Target) || !Hero->Brush->GetStaticMesh()
            || FVector::Dist2D(ContactPoint(),Hero->GetActorLocation())>SurfaceReach+30) return;
        const FVector N=ContactNormal(),Point=ContactPoint();
        const FTransform BrushWorld=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform()*Hand;
        const FBox Box=Hero->Brush->GetStaticMesh()->GetBoundingBox();
        const auto* Tooth=Cast<AMCArenaTooth>(Target);
        auto Distance=[&](FVector P) {
            // A new stain's tangent plane can cut across empty space beside a
            // curved crown. During reach/return, use the actual enamel there;
            // otherwise reacquiring a stain teleported a safe resting wrist.
            if(!Destination && Blend<.98f && Tooth) {
                FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MCBrushReturnSurface),true,Hero);
                if(Tooth->BrushSurface->LineTraceComponent(Hit,P+N*160,P-N*160,Q))
                    return float(FVector::DotProduct(P-Hit.ImpactPoint,N));
                return 1000.f;
            }
            return float(FVector::DotProduct(P-Point,N));
        };
        float Clearance=Distance(Hand.GetLocation())-9.f;
        // Include the handle and all bristles, not only the nominal brush tip.
        for(int32 I=0;I<8;++I) {
            const FVector Corner(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z);
            Clearance=FMath::Min(Clearance,Distance(BrushWorld.TransformPosition(Corner))-1.f);
        }
        if(Clearance<0) Hand.AddToTranslation(N*-Clearance);
        if(Tongue.IsValid()) {
            float Lift=0; FHitResult Floor;
            if(Tongue->SurfacePoint(Hand.GetLocation(),Floor)) Lift=FMath::Max(Lift,float(Floor.ImpactPoint.Z+10-Hand.GetLocation().Z));
            const FTransform SafeBrush=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform()*Hand;
            for(int32 I=0;I<8;++I) {
                const FVector Corner(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z);
                const FVector P=SafeBrush.TransformPosition(Corner);
                if(Tongue->SurfacePoint(P,Floor)) Lift=FMath::Max(Lift,float(Floor.ImpactPoint.Z+1-P.Z));
            }
            Hand.AddToTranslation(FVector(0,0,Lift));
        }
    };
    KeepOutsideSurface(Desired,Contact);
    if(!bHandPresented) { PresentedHand=Animated; bHandPresented=true; }
    else PresentedHand=PresentedHand.GetRelativeTransform(PresentationBase)*World;
    PresentationBase=World;
    // Limit the FINAL wrist, including loss/reacquisition of a stain. Smoothing
    // only the contact target still allowed the blend back to locomotion to jump.
    const FVector Previous=PresentedHand.GetLocation();
    FVector Next=FMath::VInterpConstantTo(Previous,Desired.GetLocation(),Dt,340.f);
    FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCBrushHandPath),false,Hero);
    // The target uses its accurate contact plane; its broad body box is padded.
    if(Target) Query.AddIgnoredActor(Target);
    if(GetWorld()->SweepSingleByChannel(Obstacle,Previous,Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(9),Query)) {
        if(Obstacle.bStartPenetrating) Next=Previous+Obstacle.Normal*FMath::Min(Obstacle.PenetrationDepth+1,340.f*Dt);
        else {
            const FVector Safe=Obstacle.Location+Obstacle.Normal;
            const FVector Slide=Safe+FVector::VectorPlaneProject(Next-Safe,Obstacle.Normal);
            FHitResult Second;
            Next=GetWorld()->SweepSingleByChannel(Second,Safe,Slide,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(9),Query)?Safe:Slide;
        }
    }
    PresentedHand.SetLocation(Next);
    // Carry the return pose with the avatar. A world-space speed limit alone
    // can never catch up with a sprinting character after releasing the brush.
    PresentedHand.SetLocation(Home+ClampHandOffset(PresentedHand.GetLocation()-Home));
    const float Angle=PresentedHand.GetRotation().AngularDistance(Desired.GetRotation());
    PresentedHand.SetRotation(FQuat::Slerp(PresentedHand.GetRotation(),Desired.GetRotation(),FMath::Min(1.f,FMath::DegreesToRadians(360.f)*Dt/FMath::Max(Angle,.0001f))).GetNormalized());
    PresentedHand.SetScale3D(Desired.GetScale3D());
    KeepOutsideSurface(PresentedHand);
#if !UE_BUILD_SHIPPING
    if(Contact && GFrameCounter%180==0 && FParse::Param(FCommandLine::Get(),TEXT("MCBrushCoverage"))) {
        const FTransform PlannedBrush=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform()*Desired;
        UE_LOG(LogTemp,Display,TEXT("MC_BRUSH_SOLVER error=%.1f desiredError=%.1f obstacle=%s goal=%s hand=%s normal=%s"),
            FVector::Dist(BristlePoint(),ContactPoint()),FVector::Dist(PlannedBrush.TransformPosition(FVector(72,0,-20)),ContactPoint()),
            *GetNameSafe(Obstacle.GetActor()),*Desired.GetLocation().ToString(),*PresentedHand.GetLocation().ToString(),*ContactNormal().ToString());
    }
#endif
    // Collision correction may keep the wrist away from its animated home.
    // Dropping the solver at the corrected goal snapped back through that gap.
    if(!Contact && PresentedHand.Equals(Animated,.01f)) { bHandPresented=false; return; }
    const FTransform Goal=PresentedHand.GetRelativeTransform(World);
    // The floating mitten is weighted to the wrist AND forearm twist bones.
    // Move that entire branch rigidly; separating those bones tears the skin.
    const bool HasWristBranch=Lower>=0 && Ref.GetParentIndex(Hand)==Lower;
    const int32 Root=HasWristBranch?Lower:Hand,Parent=Ref.GetParentIndex(Root);
    const FTransform Solved=HasWristBranch?Ref.GetRefBonePose()[Hand].Inverse()*Goal:Goal;
    Pose[Root]=Parent>=0?Solved.GetRelativeTransform(CS[Parent]):Solved;
    if(HasWristBranch) Pose[Hand]=Ref.GetRefBonePose()[Hand];
}
void UMCBrushContactComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCBrushContactComponent,Target); DOREPLIFETIME(UMCBrushContactComponent,LocalPoint);
    DOREPLIFETIME(UMCBrushContactComponent,LocalNormal); DOREPLIFETIME(UMCBrushContactComponent,ContactAt);
}
