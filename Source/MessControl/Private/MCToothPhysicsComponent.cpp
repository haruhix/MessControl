#include "MCToothPhysicsComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ConstructorHelpers.h"

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
    if (Tooth->HasAuthority()) { Settings=Profile?Profile->Settings:FMCPhysicsSettings(); Settings.Sanitize(); }
    Muscles=Tooth->FindComponentByClass<UPhysicsControlComponent>();
    FPhysicsControlData Data;
    Data.AngularStrength=Settings.MuscleStrength; Data.AngularDampingRatio=Settings.Damping;
    Data.bDisableCollision=true; Data.bOnlyControlChildObject=true;
    if (Muscles && Tooth->GetMesh()->GetPhysicsAsset())
    {
        const auto Controls=Muscles->CreateControlsFromSkeletalMeshBelow(Tooth->GetMesh(),TEXT("body"),false,EPhysicsControlType::ParentSpace,Data,TEXT("Limbs"));
        FPhysicsControlNames Names; Muscles->AddControlsToSet(Names,Controls,TEXT("Limbs"));
        // Newly spawned actors can be created after the mesh tick this frame. Prime the cache
        // before the first control update so targets never read an empty skeleton buffer.
        Tooth->GetMesh()->RefreshBoneTransforms(); Muscles->UpdateTargetCaches(0.f);
    }
    OnRep_Settings(); EnterStanding();
    if (!Tooth->HasAuthority() && Frame.State!=EMCBodyState::Standing) OnRep_Frame();
}
float UMCToothPhysicsComponent::ServerTime() const
{
    const AGameStateBase* State=GetWorld()->GetGameState();
    return State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
void UMCToothPhysicsComponent::SetMuscles(bool bEnable)
{
    if (Muscles) Muscles->SetControlsInSetEnabled(TEXT("Limbs"),bEnable);
}
void UMCToothPhysicsComponent::OnRep_Settings()
{
    Settings.Sanitize(); if (!Tooth) return;
    if (Muscles)
    {
        FPhysicsControlData Data; Data.AngularStrength=Settings.MuscleStrength; Data.AngularDampingRatio=Settings.Damping;
        Data.bDisableCollision=true; Data.bOnlyControlChildObject=true;
        Muscles->SetControlDatasInSet(TEXT("Limbs"),Data);
    }
    for (const FName Bone:{FName("body"),FName("arm_l"),FName("arm_r"),FName("hand_l"),FName("hand_r"),FName("leg_l"),FName("leg_r")})
        Tooth->GetMesh()->SetMassOverrideInKg(Bone,Settings.Mass*(Bone==TEXT("body")?0.625f:0.0625f));
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
    Frame.State=NewState; ++Frame.Revision; Frame.StateStartedAt=ServerTime(); LocalState=NewState;
    Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::EnterRagdoll()
{
    ++KnockdownCount;
    Tooth->bBrushing=false; Tooth->bHandling=false;
    Tooth->DropFood(); Tooth->ResetContact();
    Tooth->GetCharacterMovement()->StopMovementImmediately(); Tooth->GetCharacterMovement()->DisableMovement();
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Tooth->SetReplicateMovement(false);
    auto* Mesh=Tooth->GetMesh();
    SetMuscles(false); Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
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
    if (!Tooth || !Tooth->HasAuthority() || LocalState==EMCBodyState::Recovering || ServerTime()<RecoveryInvulnerableUntil) return;
    if (VelocityChange.ContainsNaN() || HitLocation.ContainsNaN()) return;
    VelocityChange=VelocityChange.GetClampedToMaxSize(1400.f);
    if (LocalState==EMCBodyState::Standing && VelocityChange.Size()<Settings.FallThreshold)
    {
        Tooth->LaunchCharacter(VelocityChange,false,false);
        Tooth->GetMesh()->AddImpulseToAllBodiesBelow(VelocityChange*0.35f,TEXT("body"),true,false);
        return;
    }
    if (LocalState!=EMCBodyState::Ragdoll)
    {
        const FVector Momentum=Tooth->GetVelocity(); SetState(EMCBodyState::Ragdoll); EnterRagdoll();
        Tooth->GetMesh()->SetAllPhysicsLinearVelocity(Momentum);
    }
    LastHitTime=ServerTime();
    Tooth->GetMesh()->AddImpulseToAllBodiesBelow(VelocityChange,TEXT("body"),true,true);
    FVector Spin=FVector::CrossProduct(FVector::UpVector,VelocityChange.GetSafeNormal2D())*300.f;
    Spin.Z=FMath::Clamp((HitLocation-PhysicalLocation()).Y*3.f,-120.f,120.f);
    Tooth->GetMesh()->SetAllPhysicsAngularVelocityInDegrees(Spin,true);
    CaptureFrame(); Tooth->ForceNetUpdate();
}
FVector UMCToothPhysicsComponent::PhysicalLocation() const
{
    return Tooth?Tooth->GetMesh()->GetBoneLocation(TEXT("body")):FVector::ZeroVector;
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
    if (!Tooth || !Tooth->Status->IsAlive() || Tooth->bInCoffee || !Tooth->HasAuthority() || LocalState!=EMCBodyState::Ragdoll) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGetUp),false,Tooth);
    FHitResult Floor; const FVector Center=PhysicalLocation();
    if (!GetWorld()->LineTraceSingleByChannel(Floor,Center+FVector(0,0,60),Center-FVector(0,0,350),ECC_WorldStatic,Params) || Floor.ImpactNormal.Z<0.65f) return false;
    const float Half=Tooth->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Destination=Floor.ImpactPoint+FVector(0,0,Half+3.f);
    bool bClear=false;
    // Try nearby grounded space, never restore the capsule through a wall or another player.
    for (const FVector Offset:{FVector::ZeroVector,FVector(80,0,0),FVector(-80,0,0),FVector(0,80,0),FVector(0,-80,0)})
    {
        const FVector Candidate=Destination+Offset;
        FHitResult Support;
        if (!GetWorld()->LineTraceSingleByChannel(Support,Candidate,Candidate-FVector(0,0,Half+12),ECC_WorldStatic,Params) || Support.ImpactNormal.Z<0.65f) continue;
        if (!GetWorld()->OverlapBlockingTestByProfile(Candidate,FQuat::Identity,TEXT("Pawn"),FCollisionShape::MakeCapsule(34,Half),Params))
        { Destination=Candidate; bClear=true; break; }
    }
    if (!bClear) return false;
    CaptureFrame();
    // Convert only the root: other bone transforms are already parent-relative.
    const FTransform NewMesh(FRotator(0,Tooth->GetActorRotation().Yaw,0),Destination-FVector(0,0,Half));
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
    Tooth->SetActorLocationAndRotation(Frame.CapsuleLocation,FRotator(0,Frame.CapsuleYaw,0),false,nullptr,ETeleportType::TeleportPhysics);
    Mesh->AttachToComponent(Tooth->GetCapsuleComponent(),FAttachmentTransformRules::KeepWorldTransform);
    Mesh->SetRelativeTransform(FTransform(FVector(0,0,-58)));
    DisplayPose.Reset(); for (const auto& Bone:Frame.Bones) DisplayPose.Add(Bone.Transform());
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Tooth->GetCharacterMovement()->DisableMovement();
    if (Tooth->SoundPalette) Tooth->SoundPalette->Play(this,TEXT("StandUp"),Tooth->GetActorLocation());
}
void UMCToothPhysicsComponent::EnterStanding()
{
    if (!Tooth || !Tooth->GetMesh()->GetSkeletalMeshAsset()) return;
    auto* Mesh=Tooth->GetMesh(); Mesh->SetAllBodiesSimulatePhysics(false);
    Mesh->AttachToComponent(Tooth->GetCapsuleComponent(),FAttachmentTransformRules::KeepWorldTransform);
    Mesh->SetRelativeTransform(FTransform(FVector(0,0,-58)));
    Tooth->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    // Limb bodies follow the procedural target with spring/damper controls. The body stays kinematic.
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetAllBodiesBelowSimulatePhysics(TEXT("body"),true,false);
    Mesh->SetAllBodiesBelowPhysicsBlendWeight(TEXT("body"),1.f,false,false);
    SetMuscles(true); DisplayPose.Reset();
    Tooth->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Tooth->SetReplicateMovement(true);
    RecoveryInvulnerableUntil=ServerTime()+0.6f;
}
float UMCToothPhysicsComponent::RecoveryAlpha() const { return FMath::Clamp((ServerTime()-Frame.StateStartedAt)/Settings.GetUpSeconds,0.f,1.f); }
void UMCToothPhysicsComponent::BuildPresentationPose(TArray<FTransform>& Pose) const
{
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
    if (!Tooth) return;
    if (LocalState!=Frame.State)
    {
        LocalState=Frame.State;
        if (LocalState==EMCBodyState::Ragdoll) { EnterRagdoll(); DisplayPose.Reset(); }
        else if (LocalState==EMCBodyState::Recovering) EnterRecovery();
        else
        {
            Tooth->SetActorLocationAndRotation(Frame.CapsuleLocation,FRotator(0,Frame.CapsuleYaw,0),false,nullptr,ETeleportType::TeleportPhysics);
            EnterStanding();
        }
    }
}
void UMCToothPhysicsComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt,TickType,ThisTickFunction); if (!Tooth) return;
    if (LocalState==EMCBodyState::Ragdoll)
    {
        if (Tooth->HasAuthority())
        {
            const FVector Center=PhysicalLocation();
            Tooth->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
            SendAccumulator+=Dt;
            if (SendAccumulator>=0.05f) { SendAccumulator=0; CaptureFrame(); Tooth->ForceNetUpdate(); }
            const float Age=ServerTime()-FMath::Max(Frame.StateStartedAt,LastHitTime);
            if (Age>=Settings.RagdollSeconds && (Tooth->GetMesh()->GetPhysicsLinearVelocity(TEXT("body")).Size()<160 || Age>Settings.RagdollSeconds+3)) TryRecover();
            // Falling out of the mouth is a real death, using the same reserve as impact deaths.
            if (Center.Z<-250)
            {
                Tooth->Status->Damage(Tooth->Status->State.MaxHealth);
            }
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
    else if (LocalState==EMCBodyState::Recovering && Tooth->HasAuthority() && RecoveryAlpha()>=1)
    {
        Frame.CapsuleLocation=Tooth->GetActorLocation(); Frame.Bones.Reset(); SetState(EMCBodyState::Standing); EnterStanding();
    }
}
bool UMCToothPhysicsComponent::CanAct() const
{
    return LocalState==EMCBodyState::Standing && (!Tooth || Tooth->Status->IsAlive());
}
void UMCToothPhysicsComponent::EnterDeath()
{
    if (!Tooth || !Tooth->HasAuthority()) return;
    if (LocalState!=EMCBodyState::Ragdoll) { SetState(EMCBodyState::Ragdoll); EnterRagdoll(); }
    Tooth->GetMesh()->AddImpulseToAllBodiesBelow(FVector(0,0,220),TEXT("body"),true,true);
    CaptureFrame(); Tooth->ForceNetUpdate();
}
void UMCToothPhysicsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCToothPhysicsComponent,Frame); DOREPLIFETIME(UMCToothPhysicsComponent,Settings);
}
