#include "MCMotionRecorder.h"
#include "MCToothCharacter.h"
#include "MCToothAnimInstance.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodyInstance.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs RecordMotion(TEXT("mc.Anim.Record"),
    TEXT("Record final bones, animation targets, contacts and physics weights: mc.Anim.Record [seconds=15] [label]. Zero stops recording."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        const float Seconds=Args.IsEmpty()?15.f:FCString::Atof(*Args[0]);
        const FString Label=Args.Num()>1?Args[1]:TEXT("motion");
        for (TActorIterator<AMCToothCharacter> It(World);It;++It)
        {
            auto* Recorder=It->FindComponentByClass<UMCMotionRecorder>();
            if (!Recorder && Seconds>0) { Recorder=NewObject<UMCMotionRecorder>(*It); Recorder->RegisterComponent(); }
            if (Recorder) { if (Seconds>0) Recorder->Start(Seconds,Label); else Recorder->Stop(); }
        }
    }));
#endif

UMCMotionRecorder::UMCMotionRecorder()
{
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UMCMotionRecorder::Start(float Seconds,const FString& Label)
{
    Stop(); Remaining=FMath::Clamp(Seconds,.1f,120.f);
    const auto* Hero=CastChecked<AMCToothCharacter>(GetOwner());
    if (auto* Anim=Cast<UMCToothAnimInstance>(Hero->GetMesh()->GetAnimInstance())) Anim->bRecordMotion=true;
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("MotionDiagnostics");
    IFileManager::Get().MakeDirectory(*Folder,true);
    Filename=Folder/(FPaths::MakeValidFileName(Label)+TEXT("_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+Hero->GetName()+TEXT(".csv"));
    Rows=TEXT("time,dt,role,state,speed,grip,pose,bone,actor_x,actor_y,actor_z,x,y,z,qx,qy,qz,qw,target_x,target_y,target_z,physics_x,physics_y,physics_z,weight,simulating,foot_hit,foot_planted,foot_target_z,surface\n");
    SetComponentTickEnabled(true);
    UE_LOG(LogTemp,Display,TEXT("MC_MOTION_START %s"),*Filename);
}
void UMCMotionRecorder::Stop()
{
    SetComponentTickEnabled(false); Remaining=0;
    if (const auto* Hero=Cast<AMCToothCharacter>(GetOwner()))
        if (auto* Anim=Cast<UMCToothAnimInstance>(Hero->GetMesh()->GetAnimInstance())) Anim->bRecordMotion=false;
    if (!Rows.IsEmpty())
    {
        const bool Saved=FFileHelper::SaveStringToFile(Rows,*Filename,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("MC_MOTION_%s %s"),Saved?TEXT("SAVED"):TEXT("SAVE_FAILED"),*Filename);
        Rows.Reset();
    }
}
void UMCMotionRecorder::EndPlay(const EEndPlayReason::Type Reason) { Stop(); Super::EndPlay(Reason); }
void UMCMotionRecorder::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,Type,TickFunction);
    const auto* H=CastChecked<AMCToothCharacter>(GetOwner()); auto* M=H->GetMesh();
    const auto* A=Cast<UMCToothAnimInstance>(M->GetAnimInstance());
    const FVector Origin=H->GetActorLocation();
    for (const FName Role:{FName("body"),FName("leg_l"),FName("knee_l"),FName("foot_l"),FName("leg_r"),FName("knee_r"),FName("foot_r"),FName("hand_l"),FName("hand_r")})
    {
        const FName Bone=H->RigBone(Role); const int32 Index=M->GetBoneIndex(Bone);
        const FTransform Final=M->GetBoneTransform(Index); const FVector P=Final.GetLocation(); const FQuat Q=Final.GetRotation();
        const FVector Target=A && A->DiagnosticPose.IsValidIndex(Index)?M->GetComponentTransform().TransformPosition(A->DiagnosticPose[Index].GetLocation()):P;
        const auto* B=M->GetBodyInstance(Bone); const FVector Physical=B?B->GetUnrealWorldTransform().GetLocation():P;
        const int32 Side=Role.ToString().EndsWith(TEXT("_r"))?1:0;
        const FMCFootContactDebug Foot=A?A->FootContacts[Side]:FMCFootContactDebug();
        Rows+=FString::Printf(TEXT("%.6f,%.6f,%d,%d,%.4f,%.4f,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%s\n"),
            GetWorld()->GetTimeSeconds(),Dt,int32(H->GetLocalRole()),int32(H->ToothPhysics->GetBodyState()),H->GetVelocity().Size2D(),H->Grip->Blend(),int32(H->Grip->Frame.Pose),*Role.ToString(),
            Origin.X,Origin.Y,Origin.Z,P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,Target.X,Target.Y,Target.Z,Physical.X,Physical.Y,Physical.Z,B?B->PhysicsBlendWeight:0.f,B && B->IsInstanceSimulatingPhysics(),Foot.bHit,Foot.bPlanted,Foot.Target.Z,*Foot.Surface.ToString());
    }
    Remaining-=Dt; if (Remaining<=0) Stop();
}
