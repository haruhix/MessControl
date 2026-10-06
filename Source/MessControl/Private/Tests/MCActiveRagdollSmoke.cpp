#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCDevCommands.h"
#include "MCBrushContactComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsControlComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickActiveRagdollValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World; TWeakObjectPtr<ACameraActor> Camera;
        double Age=0,NextLog=0,NextPing=6; int32 Stage=0,Seen=0,Captured=0,FootSamples=0,PingSamples=0;
        bool Started=false,Jumped=false,ClientGuard=false,Failed=false,FinalPeersObserved=false,FinalPeersValid=true;
        float MaxBodyOffset=0,MaxFootDepth=0,BadPoseSeconds=0,NextOffsetDiagnostic=60;
        float PingTotal=0,MinPing=MAX_flt,MaxPing=0;
        TMap<TWeakObjectPtr<AMCToothCharacter>,uint8> PeerStates;
    }; static FRun R;
    if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!GS || !PC || !H || !Tongue) return;
    const bool Host=World->GetNetMode()!=NM_Client;
    const bool SoftOnly=FParse::Param(FCommandLine::Get(),TEXT("MCActiveRagdollSoftOnly"));
    float ExpectedPing=0; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPingMs="),ExpectedPing);
    int32 Expected=1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto Finish=[&](bool Pass) {
        UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_RAGDOLL_%s net=%d soft_only=%d seen=%d peer_states=%d body_offset=%.2f foot_depth=%.2f foot_samples=%d bad_pose=%.3f client_guard=%d ping_min=%.1f ping_avg=%.1f ping_max=%.1f ping_samples=%d"),
            Pass?TEXT("PASS"):TEXT("FAIL"),int32(World->GetNetMode()),SoftOnly,R.Seen,R.PeerStates.Num(),R.MaxBodyOffset,R.MaxFootDepth,R.FootSamples,R.BadPoseSeconds,R.ClientGuard,
            R.PingSamples?R.MinPing:0,R.PingSamples?R.PingTotal/R.PingSamples:0,R.MaxPing,R.PingSamples);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(R.Age>100) { Finish(false); return; }
    if(Host && !R.Started && GS->PlayerArray.Num()>=Expected && R.Age>3) {
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        GS->bDevManualEvents=true; GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0;
        GS->DayStartedAt=GS->GetServerWorldTimeSeconds()+2; GS->ForceNetUpdate();
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        int32 I=0;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
            R.Failed|=It->ToothPhysics->GetActiveRagdollMode()!=EMCActiveRagdollMode::Soft;
            FHitResult Floor; const FVector P(-420,(I++-(Expected-1)*.5f)*140,0);
            if(!Tongue->SurfacePoint(P,Floor)) { R.Failed=true; continue; }
            It->GetCharacterMovement()->StopMovementImmediately();
            It->SetActorLocationAndRotation(Floor.ImpactPoint+FVector(0,0,61),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            It->GetCharacterMovement()->SetMovementMode(MOVE_Walking); It->Status->Initialize(100);
        }
        R.Started=true;
    }
    if(!Host && GS->bDevManualEvents && !R.Started) {
        const auto Before=H->ToothPhysics->GetActiveRagdollMode(); PC->RequestDevAction(EMCDevAction::ActiveRagdoll,2);
        R.Failed|=Before!=EMCActiveRagdollMode::Soft;
        R.ClientGuard=!PC->CanUseDevPanel() && H->ToothPhysics->GetActiveRagdollMode()==Before; R.Started=true;
    }
    if(!R.Started) return;
    const double T=GS->GetServerWorldTimeSeconds()-GS->DayStartedAt;
    if(T<0) return;
    // Owner ping is measured RTT averaged by the engine; sample after its warmup.
    if(!Host && T>=R.NextPing) {
        R.NextPing=T+1;
        if(const auto* Player=H->GetPlayerState<APlayerState>()) {
            const float Ping=Player->GetPingInMilliseconds();
            if(FMath::IsFinite(Ping) && Ping>0) {
                R.MinPing=FMath::Min(R.MinPing,Ping); R.MaxPing=FMath::Max(R.MaxPing,Ping);
                R.PingTotal+=Ping; ++R.PingSamples;
            }
        }
    }
    auto Mode=[&](int32 Value) { PC->RequestDevAction(EMCDevAction::ActiveRagdoll,Value); };
    if(Host) {
        if(R.Stage==0 && T>2) { if(!SoftOnly) Mode(1); ++R.Stage; }
        if(R.Stage==1 && T>7) { if(!SoftOnly) Mode(2); ++R.Stage; }
        if(R.Stage==2 && T>12) {
            for(TActorIterator<AMCToothCharacter> It(World);It;++It) It->ToothPhysics->ApplyHit(FVector(-300,30,250),It->GetActorLocation());
            ++R.Stage;
        }
        if(R.Stage==3 && T>20) { if(!SoftOnly) Mode(0); ++R.Stage; }
    }
    // Each owner uses ordinary CharacterMovement input, including client prediction.
    if(H->ToothPhysics->CanAct() && T>2 && T<11.5) {
        H->AddMovementInput(FVector(FMath::Cos(T*2),FMath::Sin(T*2),0));
        if(T>4 && !R.Jumped) { H->Jump(); R.Jumped=true; }
    }
    if(SoftOnly && (R.Seen&64) && H->ToothPhysics->CanAct() && T>21 && T<23) {
        H->AddMovementInput(FVector(FMath::Cos(T*2),FMath::Sin(T*2),0));
        if(H->GetVelocity().Size2D()>60) R.Seen|=512;
    }
    if(H->GetCharacterMovement()->IsFalling() && T>4 && T<7) R.Seen|=128;
    if(H->GetVelocity().Size2D()>60 && T>2 && T<11.5) R.Seen|=256;
    if(H->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll) R.Seen|=16;
    if(H->ToothPhysics->GetBodyState()==EMCBodyState::Recovering) R.Seen|=32;
    if(T>16 && T<20 && H->ToothPhysics->CanAct()) R.Seen|=64;
    const auto Active=H->ToothPhysics->GetActiveRagdollMode(); R.Seen|=1<<uint8(Active);
    if(SoftOnly && Active!=EMCActiveRagdollMode::Soft) R.Failed=true;
    if(SoftOnly && T>2) {
        if(!Host && (!H->IsLocallyControlled() || H->GetLocalRole()!=ROLE_AutonomousProxy)) R.Failed=true;
        bool BadStandingPose=false;
        for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
            uint8& States=R.PeerStates.FindOrAdd(TWeakObjectPtr<AMCToothCharacter>(*It));
            if(It->ToothPhysics->GetActiveRagdollMode()!=EMCActiveRagdollMode::Soft) R.Failed=true;
            else States|=1;
            if(!Host && *It!=H && It->GetLocalRole()!=ROLE_SimulatedProxy) R.Failed=true;
            const auto State=It->ToothPhysics->GetBodyState();
            if(State==EMCBodyState::Ragdoll) {
                States|=2;
                if(It->GetMesh()->IsSimulatingPhysics(It->RigBone(TEXT("body")))!=Host) R.Failed=true;
            }
            if(State==EMCBodyState::Recovering) States|=4;
            if(State==EMCBodyState::Standing && (States&6)==6) States|=8;
            if(State!=EMCBodyState::Standing) continue;
            auto* Mesh=It->GetMesh(); const FVector Body=It->ToothPhysics->PhysicalLocation();
            const auto* Asset=Mesh->GetPhysicsAsset();
            bool Bad=Body.ContainsNaN() || It->GetActorTransform().ContainsNaN() || Mesh->GetComponentTransform().ContainsNaN() || !Asset;
            if(Asset) for(const USkeletalBodySetup* Setup:Asset->SkeletalBodySetups) {
                const auto* BI=Mesh->GetBodyInstance(Setup->BoneName);
                const FTransform Visible=Mesh->GetSocketTransform(Setup->BoneName);
                Bad|=!BI || Visible.ContainsNaN() || FVector::Distance(Visible.GetLocation(),Body)>180;
                if(BI) { const FTransform Physical=BI->GetUnrealWorldTransform(); Bad|=Physical.ContainsNaN() || FVector::Distance(Physical.GetLocation(),Body)>180; }
            }
            R.MaxBodyOffset=FMath::Max(R.MaxBodyOffset,float(FVector::Distance(Body,It->GetActorLocation())));
            BadStandingPose|=Bad;
            if(T>3 && T<11.5) {
                const auto* Core=Mesh->GetBodyInstance(It->RigBone(TEXT("body")));
                bool Ready=Core && !Core->IsInstanceSimulatingPhysics() && Core->PhysicsBlendWeight==0;
                for(const FName Role:{FName("foot_l"),FName("foot_r")}) {
                    const auto* Foot=Mesh->GetBodyInstance(It->RigBone(Role));
                    Ready&=Foot && Foot->IsInstanceSimulatingPhysics() && Foot->PhysicsBlendWeight>.99f;
                }
                auto* Motors=It->FindComponentByClass<UPhysicsControlComponent>();
                Ready&=Motors && Motors->IsRegistered() && Motors->HasBegunPlay();
                if(Motors) for(const FName Role:{FName("arm_l"),FName("arm_r"),FName("leg_l"),FName("leg_r")}) {
                    const auto Names=Motors->GetControlNamesInSet(Role); Ready&=!Names.IsEmpty();
                    for(const FName Name:Names) { FPhysicsControlData Data; Ready&=Motors->GetControlData(Name,Data) && !Data.bUseSkeletalAnimation; }
                }
                if(Ready) States|=16;
                else if(!R.Failed) UE_LOG(LogTemp,Error,TEXT("MC_ACTIVE_STANDING_FAIL actor=%s own=%d role=%d t=%.3f"),*It->GetName(),*It==H,int32(It->GetLocalRole()),T);
                R.Failed|=!Ready;
            }
        }
        if(BadStandingPose) R.BadPoseSeconds+=World->GetDeltaSeconds();
    }
    if(H->ToothPhysics->CanAct() && T>1) {
        auto* Mesh=H->GetMesh(); const FVector Body=H->ToothPhysics->PhysicalLocation(); bool Bad=Body.ContainsNaN();
        for(const USkeletalBodySetup* Setup:Mesh->GetPhysicsAsset()->SkeletalBodySetups) {
            const auto* BI=Mesh->GetBodyInstance(Setup->BoneName);
            const FVector P=BI?BI->GetUnrealWorldTransform().GetLocation():Body;
            Bad|=P.ContainsNaN() || FVector::Distance(P,Body)>180;
        }
        R.MaxBodyOffset=FMath::Max(R.MaxBodyOffset,float(FVector::Distance(Body,H->GetActorLocation())));
        if(R.MaxBodyOffset>R.NextOffsetDiagnostic) {
            R.NextOffsetDiagnostic=R.MaxBodyOffset+10;
            const auto* Core=Mesh->GetBodyInstance(H->RigBone(TEXT("body")));
            const FVector Physical=Core?Core->GetUnrealWorldTransform().GetLocation():Body;
            FPhysicsControlTarget Target;
            const FName Set=TEXT("Balance");
            if(auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>()) Motors->GetControlTarget(Motors->GetControlNamesInSet(Set)[0],Target);
            const FVector Goal=H->GetCapsuleComponent()->GetComponentTransform().TransformPosition(Target.TargetPosition);
            const auto* CapsuleBody=H->GetCapsuleComponent()->GetBodyInstance();
            const FVector CapsulePhysics=CapsuleBody?CapsuleBody->GetUnrealWorldTransform().GetLocation():H->GetActorLocation();
            const FVector CapsuleVelocity=CapsuleBody?CapsuleBody->GetUnrealWorldVelocity():FVector::ZeroVector;
            FPhysicsControlData Data; bool Enabled=false;
            if(auto* Motors=H->FindComponentByClass<UPhysicsControlComponent>()) {
                const auto Name=Motors->GetControlNamesInSet(Set)[0]; Motors->GetControlData(Name,Data); Enabled=Motors->GetControlEnabled(Name);
            }
            UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_OFFSET t=%.3f mode=%d cap=%s bone=%s physical=%s goal=%s mesh_relative=%s velocity=%s capsule_physics=%s motor=%d strength=%.1f force=%.1f parent_velocity=%s target_velocity=%s mass=%.2f"),
                T,uint8(Active),*H->GetActorLocation().ToCompactString(),*Body.ToCompactString(),*Physical.ToCompactString(),
                *Goal.ToCompactString(),*Mesh->GetRelativeLocation().ToCompactString(),*H->GetVelocity().ToCompactString(),
                *CapsulePhysics.ToCompactString(),Enabled,Data.LinearStrength,Data.MaxForce,*CapsuleVelocity.ToCompactString(),
                *Target.TargetVelocity.ToCompactString(),Core?Core->GetBodyMass():0);
        }
        if(Active!=EMCActiveRagdollMode::Off && T>3 && T<11.5) {
            const auto* Core=Mesh->GetBodyInstance(H->RigBone(TEXT("body")));
            const auto* Foot=Mesh->GetBodyInstance(H->RigBone(TEXT("foot_l")));
            if(Core && Foot && !Core->IsInstanceSimulatingPhysics() && Foot->IsInstanceSimulatingPhysics() && Core->PhysicsBlendWeight==0 && Foot->PhysicsBlendWeight>.99f) R.Seen|=8;
            for(FName Side:{FName("toe_l"),FName("toe_r")}) {
                const FVector P=Mesh->GetSocketLocation(H->RigBone(Side)); FHitResult Floor;
                if(Tongue->SurfacePoint(P,Floor)) { ++R.FootSamples; R.MaxFootDepth=FMath::Max(R.MaxFootDepth,float(Floor.ImpactPoint.Z-P.Z)); }
            }
        }
        if(Bad && !SoftOnly) R.BadPoseSeconds+=World->GetDeltaSeconds();
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("MCActiveRagdollCapture")) && Host) {
        if(!R.Camera.IsValid()) {
            R.Camera=World->SpawnActor<ACameraActor>(); R.Camera->GetCameraComponent()->SetFieldOfView(48); PC->SetViewTarget(R.Camera.Get());
        }
        const FVector Aim=H->GetActorLocation()+FVector(0,0,10),Offset(320,-360,230);
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());
        const double Times[]={1.5,5.5,10,12.35,14.6,18,22};
        if(R.Captured<7 && T>=Times[R.Captured]) {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/FString::Printf(TEXT("Artifacts/ActiveRagdoll/%02d.png"),R.Captured++),true,false);
        }
    }
    if(R.Age>=R.NextLog) {
        R.NextLog=R.Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_RAGDOLL net=%d t=%.2f mode=%d seen=%d offset=%.2f depth=%.2f bad=%.3f"),
            int32(World->GetNetMode()),T,uint8(Active),R.Seen,R.MaxBodyOffset,R.MaxFootDepth,R.BadPoseSeconds);
        if(T>2 && T<11.5) for(TActorIterator<AMCToothCharacter> It(World);It;++It) {
            auto* Motors=It->FindComponentByClass<UPhysicsControlComponent>();
            const auto Names=Motors?Motors->GetControlNamesInSet(TEXT("arm_l")):TArray<FName>(); FPhysicsControlData Data;
            const auto* Core=It->GetMesh()->GetBodyInstance(It->RigBone(TEXT("body")));
            const auto* Foot=It->GetMesh()->GetBodyInstance(It->RigBone(TEXT("foot_l")));
            UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_MUSCLES actor=%s own=%d role=%d controls=%d ready=%d initialized=%d begun=%d registered=%d core_sim=%d core_blend=%.3f foot_sim=%d foot_blend=%.3f brushing=%d brush_presenting=%d brush_blend=%.3f tool=%d dt=%.3f"),
                *It->GetName(),*It==H,int32(It->GetLocalRole()),Names.Num(),Motors && !Names.IsEmpty() && Motors->GetControlData(Names[0],Data),
                Motors && Motors->HasBeenInitialized(),Motors && Motors->HasBegunPlay(),Motors && Motors->IsRegistered(),
                Core && Core->IsInstanceSimulatingPhysics(),Core?Core->PhysicsBlendWeight:-1,Foot && Foot->IsInstanceSimulatingPhysics(),Foot?Foot->PhysicsBlendWeight:-1,
                It->bBrushing,It->BrushContact->IsPresenting(),It->BrushContact->Alpha(),int32(It->Inventory->Selected),World->GetDeltaSeconds());
        }
    }
    // Capture recovery while every client is still connected; clients exit at T=24.
    if(SoftOnly && T>=23 && T<24) {
        bool Standing=R.PeerStates.Num()>=Expected;
        for(const auto& Peer:R.PeerStates) Standing&=Peer.Value==31 && Peer.Key.IsValid() && Peer.Key->ToothPhysics->GetBodyState()==EMCBodyState::Standing;
        R.FinalPeersObserved=true;R.FinalPeersValid&=Standing;
    }
    if(T>(Host?27:24)) {
        const int32 RequiredSeen=SoftOnly?1018:511;
        const float AveragePing=R.PingSamples?R.PingTotal/R.PingSamples:0;
        const bool PingValid=Host || ExpectedPing<=0 || (R.PingSamples>=10 && AveragePing>=ExpectedPing*.88f && AveragePing<=ExpectedPing+100);
        bool PeersValid=!SoftOnly || R.FinalPeersObserved && R.FinalPeersValid && R.PeerStates.Num()>=Expected;
        for(const auto& Peer:R.PeerStates) PeersValid&=Peer.Value==31;
        Finish(!R.Failed && R.Seen==RequiredSeen && R.BadPoseSeconds<.05f && R.MaxBodyOffset<100 && R.FootSamples>60 && R.MaxFootDepth<18 && (Host || R.ClientGuard) && PingValid && PeersValid);
    }
}
#endif
