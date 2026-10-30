#include "MCSuperCarVisualComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UMCSuperCarVisualComponent::UMCSuperCarVisualComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    LeftHandRotation=FQuat(.214194551,.073015526,.061793357,.972096205).GetNormalized().Rotator();
    RightHandRotation=FQuat(-.073014133,-.214194626,-.972096682,-.061786652).GetNormalized().Rotator();
    const FName Names[]={TEXT("Chassis"),TEXT("WheelFrontLeft"),TEXT("WheelFrontRight"),TEXT("WheelRearLeft"),TEXT("WheelRearRight"),
        TEXT("BrushFrontRight"),TEXT("BrushFrontLeft"),TEXT("BrushRearRight"),TEXT("BrushRearLeft")};
    const TCHAR* Meshes[]={TEXT("SM_SuperCar_Chassis"),TEXT("SM_SuperCar_FrontWheel_L"),TEXT("SM_SuperCar_FrontWheel_R"),
        TEXT("SM_SuperCar_RearWheel_L"),TEXT("SM_SuperCar_RearWheel_R"),TEXT("SM_SuperCar_FrontBrush_R"),TEXT("SM_SuperCar_FrontBrush_L"),TEXT("SM_SuperCar_RearBrush_R"),TEXT("SM_SuperCar_RearBrush_L")};
    const FVector Centers[]={FVector(0,0,71.802843),FVector(63.619930,-91.735655,33.884513),FVector(63.619930,92.140454,33.884513),
        FVector(-72.654802,-98.029518,45.339170),FVector(-72.654802,100.916421,45.339170),
        FVector(136.774635,36.487821,23.280300),FVector(136.774635,-36.372638,23.280300),
        FVector(-141.185427,32.079047,30.488247),FVector(-140.445089,-32.854849,24.714354)};
    for(int32 I=0;I<UE_ARRAY_COUNT(Names);++I) {
        PartAssets.Add(Names[I],TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FString::Printf(TEXT("/Game/Gameplay/ActiveSkills/Props/%s.%s"),Meshes[I],Meshes[I]))));
        PartTransforms.Add(Names[I],FTransform(Centers[I]));
    }
}

void UMCSuperCarVisualComponent::OnRegister()
{
    Super::OnRegister();RebuildVisuals();
}

void UMCSuperCarVisualComponent::RebuildVisuals()
{
    auto* Owner=GetOwner();
    if(IsTemplate() || !Owner || !GetWorld() || GetWorld()->IsNetMode(NM_DedicatedServer)) return;
    TArray<FName> Removed;
    for(const auto& Entry:MeshParts) if(!PartAssets.Contains(Entry.Key)) Removed.Add(Entry.Key);
    for(FName Name:Removed) {if(auto* Part=GetPart(Name)) Part->DestroyComponent();MeshParts.Remove(Name);}
    for(const auto& Entry:PartAssets) {
        auto* Part=GetPart(Entry.Key);
        if(!Part) {
            const FName Name=MakeUniqueObjectName(Owner,UStaticMeshComponent::StaticClass(),FName(*(GetName()+TEXT("_")+Entry.Key.ToString())));
            Part=NewObject<UStaticMeshComponent>(Owner,Name,RF_Transient);
            Part->SetupAttachment(this);Part->SetMobility(EComponentMobility::Movable);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetGenerateOverlapEvents(false);Part->SetCanEverAffectNavigation(false);
            Owner->AddInstanceComponent(Part);MeshParts.Add(Entry.Key,Part);Part->RegisterComponent();
        }
        Part->SetStaticMesh(Entry.Value.LoadSynchronous());
        Part->SetRelativeTransform(PartTransforms.FindRef(Entry.Key));
    }
    ApplyPose();
}

UStaticMeshComponent* UMCSuperCarVisualComponent::GetPart(FName Name) const
{
    const auto* Part=MeshParts.Find(Name);return Part && IsValid(Part->Get())?Part->Get():nullptr;
}

void UMCSuperCarVisualComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    const auto Parts=MeshParts;MeshParts.Empty();
    for(const auto& Entry:Parts) if(IsValid(Entry.Value.Get())) Entry.Value->DestroyComponent();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UMCSuperCarVisualComponent::SetTravelDistance(float Distance)
{
    if(!FMath::IsFinite(Distance)) return;TravelDistance=Distance;ApplyPose();
}
void UMCSuperCarVisualComponent::SetSteeringAngle(float Degrees)
{
    if(!FMath::IsFinite(Degrees)) return;
    CurrentSteeringAngle=FMath::Clamp(Degrees,-FMath::Clamp(MaxSteeringAngle,0.f,45.f),FMath::Clamp(MaxSteeringAngle,0.f,45.f));ApplyPose();
}
void UMCSuperCarVisualComponent::SetBrushSpeed(float RPM)
{
    if(FMath::IsFinite(RPM)) BrushRPM=FMath::Clamp(RPM,-600.f,600.f);
}
void UMCSuperCarVisualComponent::UpdateTravel(float DeltaDistance,float SteeringDegrees,float DeltaSeconds)
{
    if(!FMath::IsFinite(DeltaDistance) || !FMath::IsFinite(SteeringDegrees) || !FMath::IsFinite(DeltaSeconds)) return;
    const float Dt=FMath::Clamp(DeltaSeconds,0.f,.25f),Limit=FMath::Clamp(MaxSteeringAngle,0.f,45.f);
    TravelDistance+=DeltaDistance;
    CurrentSteeringAngle=FMath::FInterpTo(CurrentSteeringAngle,FMath::Clamp(SteeringDegrees,-Limit,Limit),Dt,FMath::Max(SteeringResponse,0.f));
    BrushDegrees=FMath::Fmod(BrushDegrees+FMath::Clamp(BrushRPM,-600.f,600.f)*6.f*Dt,360.f);ApplyPose();
}

void UMCSuperCarVisualComponent::ApplyPose()
{
    for(const auto& Entry:MeshParts) {
        auto* Part=Entry.Value.Get();if(!IsValid(Part)) continue;
        FTransform Pose=PartTransforms.FindRef(Entry.Key);const FString Name=Entry.Key.ToString();
        if(Name.StartsWith(TEXT("Wheel"))) {
            const bool Front=Name.StartsWith(TEXT("WheelFront")) || Name==TEXT("WheelsFront");
            const float Radius=FMath::Clamp(Front?FrontWheelRadius:RearWheelRadius,5.f,100.f);
            const float Roll=FMath::Fmod(float(FMath::RadiansToDegrees(TravelDistance/Radius)),360.f);
            // Steering is composed before local axle roll, so each wheel spins
            // around its own steered axle instead of orbiting its paired mate.
            const FQuat Steer=FRotator(0,Name.StartsWith(TEXT("WheelFront"))?CurrentSteeringAngle:0,0).Quaternion();
            Pose.SetRotation((Pose.GetRotation()*Steer*FRotator(-Roll,0,0).Quaternion()).GetNormalized());
        } else if(Name.StartsWith(TEXT("Brush"))) {
            const float Direction=Name.EndsWith(TEXT("Left"))?-1.f:1.f;
            Pose.SetRotation((Pose.GetRotation()*FRotator(0,Direction*BrushDegrees,0).Quaternion()).GetNormalized());
        } else if(Name==TEXT("FrontRoller") || Name==TEXT("RearRoller")) {
            const float Roll=FMath::Fmod(float(FMath::RadiansToDegrees(TravelDistance/FMath::Clamp(LegacyRollerRadius,5.f,100.f))),360.f);
            Pose.SetRotation((Pose.GetRotation()*FRotator(-Roll,0,0).Quaternion()).GetNormalized());
        }
        Part->SetRelativeTransform(Pose);
    }
}

FTransform UMCSuperCarVisualComponent::GetSeatFrame() const {return FTransform(SeatLocal)*GetComponentTransform();}
FTransform UMCSuperCarVisualComponent::GetHandFrame(bool bLeft) const
{return FTransform(bLeft?LeftHandRotation:RightHandRotation,bLeft?LeftHandLocal:RightHandLocal)*GetComponentTransform();}
FTransform UMCSuperCarVisualComponent::GetFootFrame(bool bLeft) const {return FTransform(bLeft?LeftFootLocal:RightFootLocal)*GetComponentTransform();}
