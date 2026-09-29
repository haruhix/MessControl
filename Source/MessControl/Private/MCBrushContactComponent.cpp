#include "MCBrushContactComponent.h"
#include "MCArenaTooth.h"
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

UMCBrushContactComponent::UMCBrushContactComponent()
{
    PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true);
}
void UMCBrushContactComponent::BeginPlay()
{
    Super::BeginPlay(); Hero=Cast<AMCToothCharacter>(GetOwner());
    if (!Hero || GetNetMode()==NM_DedicatedServer) return;
    Foam=NewObject<UInstancedStaticMeshComponent>(Hero,TEXT("BrushFoam"));
    Foam->SetupAttachment(Hero->GetRootComponent()); Foam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Foam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Foam->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushFoam.M_BrushFoam")));
    Foam->SetCastShadow(false); Foam->RegisterComponent(); Hero->AddInstanceComponent(Foam);
    PrimaryComponentTick.AddPrerequisite(Hero,Hero->PrimaryActorTick);
}
FVector UMCBrushContactComponent::ContactPoint() const
{ return IsValid(Target)?Target->Visual->GetComponentTransform().TransformPosition(LocalPoint):FVector::ZeroVector; }
FVector UMCBrushContactComponent::ContactNormal() const
{ return IsValid(Target)?Target->Visual->GetComponentTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal():FVector::UpVector; }
FVector UMCBrushContactComponent::BristlePoint() const
{ return Hero?Hero->Brush->GetComponentTransform().TransformPosition(FVector(72,0,-20)):FVector::ZeroVector; }
FTransform UMCBrushContactComponent::HandGoal(FVector Point,FVector Normal) const
{
    // The authored brush runs along +X, with bristles pointing down -Z.
    const FVector Up=FVector::VectorPlaneProject(FVector::UpVector,Normal).GetSafeNormal();
    const FVector Side=FVector::CrossProduct(Up,Normal).GetSafeNormal();
    // Keep the handle at chest height. Low stains turn the brush head downward,
    // instead of pushing the floating hand and handle through the tongue.
    const float Vertical=FMath::Clamp((FVector::DotProduct(Point-Hero->GetActorLocation(),Up)-12.f)/80.f,-.72f,.72f);
    const FVector LengthAxis=Up*Vertical+Side*FMath::Sqrt(1-Vertical*Vertical);
    const FQuat BrushRotation=FRotationMatrix::MakeFromXZ(LengthAxis,Normal).ToQuat();
    const FTransform InHand=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    const FVector Scale=InHand.GetScale3D()*Hero->GetMesh()->GetComponentScale();
    const FTransform WorldBrush(BrushRotation,Point+Normal*2-BrushRotation.RotateVector(FVector(72,0,-20)*Scale),Scale);
    return InHand.Inverse()*WorldBrush;
}
bool UMCBrushContactComponent::CanReach(FVector Point,FVector Normal) const
{
    const auto* H=Hero?Hero.Get():Cast<AMCToothCharacter>(GetOwner());
    if (!H || !H->GetMesh()->GetSkeletalMeshAsset() || !Hero) return false;
    const auto& Ref=H->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    auto Rest=[&](FName Role) { FTransform T=FTransform::Identity; for(int32 I=Ref.FindBoneIndex(H->RigBone(Role));I>=0;I=Ref.GetParentIndex(I)) T=T*Ref.GetRefBonePose()[I]; return H->GetMesh()->GetComponentTransform().TransformPosition(T.GetLocation()); };
    return FVector::Dist(Rest(TEXT("hand_r")),HandGoal(Point,Normal).GetLocation())<=MaxHandTravel;
}
bool UMCBrushContactComponent::CanAcquireSurface(const AMCArenaTooth* Tooth) const
{
    const auto* H=Hero?Hero.Get():Cast<AMCToothCharacter>(GetOwner());
    if(!H || !IsValid(Tooth) || !Tooth->IsAvailable() || !H->CanWork()) return false;
    const FVector D=Tooth->Body->Bounds.GetBox().GetClosestPointTo(H->GetActorLocation())-H->GetActorLocation();
    return D.Size()<=SurfaceReach && (Target==Tooth || FVector::DotProduct(D.GetSafeNormal2D(),H->GetActorForwardVector())>-.5f);
}
void UMCBrushContactComponent::Contact(AMCArenaTooth* Tooth,FVector Point,FVector Normal)
{
    if (!GetOwner()->HasAuthority() || !IsValid(Tooth)) return;
    Target=Tooth; const auto T=Tooth->Visual->GetComponentTransform();
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
    if(!Active && Blend<=0 && GetOwner()->HasAuthority()) Target=nullptr;
    if(!Foam) return;
    SpawnClock+=Dt;
    if(Active && Blend>.8f && SpawnClock>=.035f) {
        SpawnClock=0; const FVector N=ContactNormal();
        const FVector Side=FVector::CrossProduct(N,FVector::UpVector).GetSafeNormal();
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
    const bool Contact=IsValid(Target) && Blend>.001f;
    if(!Contact && !bHandPresented) return;
    const int32 Hand=Ref.FindBoneIndex(Hero->RigBone(TEXT("hand_r")));
    const int32 Lower=Ref.FindBoneIndex(Hero->RigBone(TEXT("forearm_r")));
    if(Hand<0) return;
    TArray<FTransform> CS; CS.SetNum(Pose.Num());
    for(int32 I=0;I<Pose.Num();++I) CS[I]=Ref.GetParentIndex(I)>=0?Pose[I]*CS[Ref.GetParentIndex(I)]:Pose[I];
    const FTransform World=Hero->GetMesh()->GetComponentTransform();
    FTransform Rest=FTransform::Identity;
    for(int32 I=Hand;I>=0;I=Ref.GetParentIndex(I)) Rest=Rest*Ref.GetRefBonePose()[I];
    const FVector Home=World.TransformPosition(Rest.GetLocation());
    const FTransform Animated=CS[Hand]*World;
    FTransform Desired=Animated;
    if(Contact) {
        FTransform ContactGoal=HandGoal(ContactPoint(),ContactNormal());
        ContactGoal.SetLocation(Home+(ContactGoal.GetLocation()-Home).GetClampedToMaxSize(MaxHandTravel));
        Desired.Blend(Animated,ContactGoal,FMath::SmoothStep(0.f,1.f,Blend));
    }
    if(!bHandPresented) { PresentedHand=Animated; bHandPresented=true; }
    else PresentedHand=PresentedHand.GetRelativeTransform(PresentationBase)*World;
    PresentationBase=World;
    // Limit the FINAL wrist, including loss/reacquisition of a stain. Smoothing
    // only the contact target still allowed the blend back to locomotion to jump.
    PresentedHand.SetLocation(FMath::VInterpConstantTo(PresentedHand.GetLocation(),Desired.GetLocation(),Dt,380.f));
    // Carry the return pose with the avatar. A world-space speed limit alone
    // can never catch up with a sprinting character after releasing the brush.
    PresentedHand.SetLocation(Home+(PresentedHand.GetLocation()-Home).GetClampedToMaxSize(MaxHandTravel));
    const float Angle=PresentedHand.GetRotation().AngularDistance(Desired.GetRotation());
    PresentedHand.SetRotation(FQuat::Slerp(PresentedHand.GetRotation(),Desired.GetRotation(),FMath::Min(1.f,FMath::DegreesToRadians(540.f)*Dt/FMath::Max(Angle,.0001f))).GetNormalized());
    PresentedHand.SetScale3D(Desired.GetScale3D());
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
