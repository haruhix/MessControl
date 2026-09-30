#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCFoodActor.h"
#include "MCHazardWave.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickGameplayV3Validation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AActor> Wall;
        TWeakObjectPtr<AMCToothCharacter> Grounded;
        TWeakObjectPtr<AMCFoodActor> Pepper;
        TWeakObjectPtr<AMCHazardWave> Wave;
        TWeakObjectPtr<ACameraActor> Camera;
        double At=0; float Age=0,StartZ=0,HP=0,LowHP=0;
        int32 Stage=-1; bool Climb=false,Hang=false,Jump=false,Dodge=false,Detonation=false,WaveJump=false;
    };
    static FRun R;
    if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* PC=World->GetFirstPlayerController(); auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* GS=World->GetGameState<AMCGameState>(); AMCTongue* Tongue=nullptr;
    for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!H || !GS || !Tongue || R.Age<3) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;
    const double Now=GS->GetServerWorldTimeSeconds(),T=Now-R.At;
    auto* Move=CastChecked<UMCToothMovementComponent>(H->GetCharacterMovement());
    auto Floor=[&](FVector P) { FHitResult Hit; return Tongue->SurfacePoint(P,Hit)?Hit.ImpactPoint:P; };
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("MCGameplayV3Capture"));
    auto View=[&](FVector Aim,FVector Offset) {
        if(!Capture) return;
        if(!R.Camera.IsValid()) {
            R.Camera=World->SpawnActor<ACameraActor>(); R.Camera->GetCameraComponent()->SetFieldOfView(60);
            R.Camera->GetCameraComponent()->SetAspectRatio(1.5f); PC->SetViewTarget(R.Camera.Get());
        }
        R.Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation());
    };
    auto Shot=[&](const TCHAR* Name) {
        if(Capture) FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/GameplayV3")/Name,false,false);
    };
    if(R.Stage<0) {
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        H->ServerSetPrimary(false); H->Status->Initialize(100);
        const FVector Base=Floor(FVector(-100,0,0));
        auto* Wall=World->SpawnActor<AActor>(); auto* Body=NewObject<UStaticMeshComponent>(Wall);
        Wall->SetRootComponent(Body); Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Body->SetCollisionProfileName(TEXT("BlockAll")); Body->RegisterComponent();
        Wall->SetActorLocation(Base+FVector(0,0,230)); Wall->SetActorScale3D(FVector(.4,2.5,4.6)); R.Wall=Wall;
        H->SetActorLocationAndRotation(Base+FVector(-90,0,145),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        Move->SetMovementMode(MOVE_Falling); Move->SetWantsClimb(true); R.StartZ=H->GetActorLocation().Z;
        View(Base+FVector(-70,0,200),FVector(-350,-430,120)); R.Stage=0; R.At=Now;
    }
    else if(R.Stage==0) {
        if(T<1.5) H->AddMovementInput(FVector::UpVector);
        else if(T<2.3) R.Hang|=Move->IsClimbing() && FMath::Abs(Move->Velocity.Z)<1;
        R.Climb|=Move->IsClimbing() && H->GetActorLocation().Z-R.StartZ>100 && H->AnimationClimb>.8f;
        if(T>=1.5 && T-World->GetDeltaSeconds()<1.5) {
            View(H->GetActorLocation()+FVector(0,0,15),FVector(-350,-430,120)); Shot(TEXT("Climbing.png"));
        }
        if(T>2.3) { H->Jump(); R.Stage=1; R.At=Now; }
    }
    else if(R.Stage==1) {
        R.Jump|=Move->IsFalling() && Move->Velocity.X<-100 && Move->Velocity.Z>150;
        if(T>.5) {
            H->StopJumping(); Move->SetWantsClimb(false); R.Wall->Destroy();
            const FVector Origin=Floor(FVector(-500,0,0));
            H->SetActorLocation(Origin+FVector(0,-140,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
            Move->StopMovementImmediately(); Move->SetMovementMode(MOVE_Walking); R.HP=H->Status->State.Health;
            R.Grounded=World->SpawnActor<AMCToothCharacter>(Origin+FVector(0,140,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),FRotator::ZeroRotator);
            R.Grounded->GetCharacterMovement()->DisableMovement(); R.LowHP=R.Grounded->Status->State.Health;
            auto* Wave=World->SpawnActor<AMCHazardWave>(Origin+FVector(0,0,5),FRotator::ZeroRotator);
            Wave->MaxRadius=300; Wave->Damage=12; Wave->WarningSeconds=.6f; R.Wave=Wave;
            View(Origin+FVector(0,0,85),FVector(-390,-340,260)); R.Stage=2; R.At=Now;
        }
    }
    else if(R.Stage==2) {
        if(T>.6 && !R.WaveJump) { H->Jump(); R.WaveJump=true; }
        if(T>=1.2 && T-World->GetDeltaSeconds()<1.2) Shot(TEXT("JumpDodge.png"));
        if(T<=1.5) return;
        R.Dodge=H->Status->State.Health==R.HP && R.Grounded->Status->State.Health<R.LowHP;
        H->StopJumping(); Move->DisableMovement(); R.Grounded->Destroy();
        const FVector P=Floor(FVector(-400,0,0));
        H->SetActorLocation(P+FVector(-320,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("SpicyPepper"),TEXT("Hazard smoke")):nullptr;
        if(!Row) { UE_LOG(LogTemp,Error,TEXT("MC_VALIDATION_FAIL GAMEPLAY_V3 missing pepper row")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        const FTransform Transform(P+FVector(0,0,55));
        auto* Pepper=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
        FRandomStream Random(41); Pepper->ConfigureItem(TEXT("SpicyPepper"),*Row,Random); Pepper->Phase=EMCFoodPhase::Free;
        Pepper->FinishSpawning(Transform); Pepper->Body->SetSimulatePhysics(false); Pepper->Label->SetHiddenInGame(true); Pepper->ArmSpicy(); R.Pepper=Pepper;
        View(P+FVector(0,0,40),FVector(-240,-260,200)); R.Stage=3; R.At=Now;
    }
    else if(R.Stage==3 && T>6.5) { Shot(TEXT("PepperWarning.png")); R.Stage=4; }
    else if(R.Stage==4 && T>8.2) {
        for(TActorIterator<AMCHazardWave> It(World);It;++It) if(It->bSpicy) R.Detonation|=R.Pepper.IsValid() && R.Pepper->IsDisposed();
        Shot(TEXT("PepperPulse.png")); R.Stage=5;
    }
    else if(R.Stage==5 && T>9) {
        const bool Pass=R.Climb && R.Hang && R.Jump && R.Dodge && R.Detonation;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s GAMEPLAY_V3 climb=%d hang=%d jump=%d dodge=%d pepper=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),R.Climb,R.Hang,R.Jump,R.Dodge,R.Detonation);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
    if(R.Age>150) { UE_LOG(LogTemp,Error,TEXT("MC_VALIDATION_FAIL GAMEPLAY_V3 timeout stage=%d"),R.Stage); FPlatformMisc::RequestExitWithStatus(false,1); }
}
#endif
