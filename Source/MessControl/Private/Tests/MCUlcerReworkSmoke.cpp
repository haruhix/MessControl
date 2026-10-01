#if !UE_BUILD_SHIPPING
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCDayDirector.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "NiagaraComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickUlcerReworkValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCFoodActor> Food;
        TWeakObjectPtr<AMCMouthSurface> Ulcer;
        TWeakObjectPtr<AMCDayDirector> Director;
        float Age=0,Saved=0; double At=0;
        int32 Stage=-1; bool Failed=false,SinkShot=false,ProgressShot=false,WaveShot=false,WideShot=false;
    };
    static FRun R; if(R.World!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>(); auto* PC=World->GetFirstPlayerController();
    auto* H=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    AMCTongue* Tongue=nullptr; for(TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
    if(!GS || !H || !Tongue || R.Age<3) return;
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    if(auto* Mode=World->GetAuthGameMode()) Mode->SetActorTickEnabled(false);
    GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0; GS->bPhysicalBrushes=false; GS->bDevManualEvents=true;
    const double Now=GS->GetServerWorldTimeSeconds();
    const bool CaptureWave=FParse::Param(FCommandLine::Get(),TEXT("MCUlcerWaveCapture"));
    auto Check=[&](bool OK,const TCHAR* Message) { R.Failed|=!OK; UE_LOG(LogTemp,Display,TEXT("MC_ULCER_CHECK %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Message); };
    auto Shot=[&](const TCHAR* Name) {
        if(FParse::Param(FCommandLine::Get(),TEXT("MCUlcerCapture"))) {
            const FString Dir=FPaths::ProjectDir()/TEXT("Artifacts/UlcerRework"); IFileManager::Get().MakeDirectory(*Dir,true);
            FScreenshotRequest::RequestScreenshot(Dir/Name,true,false);
        }
    };
    if(R.Stage<0) {
        GS->DayPlan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
        for(TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Ulcer smoke")):nullptr;
        if(!Row) { Check(false,TEXT("missing food table")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        FMCFoodRow Config=*Row; Config.SpoilSeconds=3; // Shorten only the unattended delay in this validation fixture.
        FHitResult Hit; if(!Tongue->SurfacePoint(FVector(-250,0,0),Hit)) { Check(false,TEXT("no tongue floor")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        FRandomStream Random(41); const FTransform Transform(Hit.ImpactPoint+FVector(0,0,65));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
        Food->ConfigureItem(TEXT("Egg"),Config,Random); Food->Phase=EMCFoodPhase::Free; Food->Batch=77; Food->FinishSpawning(Transform);
        Food->SetActorLocation(Hit.ImpactPoint+FVector(0,0,Food->Body->Bounds.BoxExtent.Z+3),false,nullptr,ETeleportType::TeleportPhysics);
        Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); R.Food=Food;
        H->GetCharacterMovement()->StopMovementImmediately(); H->GetCharacterMovement()->DisableMovement(); H->ServerSetPrimary(false);
        H->SetActorLocationAndRotation(Hit.ImpactPoint+FVector(-155,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        R.Director=World->SpawnActor<AMCDayDirector>(); R.Director->SetActorTickEnabled(false);
        GS->TasksTotal=1; GS->TasksLeft=1;
        if(FParse::Param(FCommandLine::Get(),TEXT("MCUlcerCapture"))) {
            auto* Camera=World->SpawnActor<ACameraActor>(); const FVector Aim=Hit.ImpactPoint+FVector(-50,0,60),Offset(-170,390,280);
            Camera->SetActorLocationAndRotation(Aim+Offset,(-Offset).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(55); PC->SetViewTarget(Camera);
        }
        Check(R.Director->CountFood(77)==1,TEXT("meal begins with one cleanup task")); Shot(TEXT("00_Food.png")); R.Stage=0; R.At=Now;
    }
    else if(R.Stage==0) {
        if(R.Food.IsValid() && R.Food->Phase==EMCFoodPhase::Absorbing && R.Food->AbsorptionProgress()>.4 && !R.SinkShot) {
            Check(R.Director->CountFood(77)==1,TEXT("sinking food remains unfinished work")); Shot(TEXT("01_Absorbing.png")); R.SinkShot=true;
        }
        if(R.Food.IsValid() && R.Food->bAbsorbed && R.Food->AbsorbedUlcer) {
            R.Ulcer=R.Food->AbsorbedUlcer; Check(R.SinkShot && R.Food->IsDisposed(),TEXT("food visibly absorbs and becomes an ulcer"));
            Check(R.Director->CountFood(77)==1 && R.Ulcer->Batch==77,TEXT("ulcer inherits the meal task"));
            if(CaptureWave) H->SetActorLocation(R.Ulcer->GetActorLocation()+FVector(-500,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
            Shot(TEXT("02_Ulcer.png")); R.Stage=1; R.At=Now;
        }
    }
    else if(R.Stage==1 && Now-R.At>1) {
        if(CaptureWave) {
            if(Now-R.At>=4.05 && !R.WaveShot) { Shot(TEXT("02a_LocalWave.png")); R.WaveShot=true; }
            if(Now-R.At>=4.4 && !R.WideShot) { Shot(TEXT("02b_LocalWaveWide.png")); R.WideShot=true; }
            if(Now-R.At<5.2) return;
            H->SetActorLocation(R.Ulcer->GetActorLocation()+FVector(-155,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        }
        Check(R.Ulcer.IsValid() && R.Ulcer->Healing==0,TEXT("idle ulcer never heals automatically"));
        H->Inventory->ServerSelect(EMCToolSlot::Spray); H->ServerSetPrimary(true); R.Stage=2; R.At=Now;
    }
    else if(R.Stage==2 && Now-R.At>=2) {
        bool Mist=false,Tool=false;
        TArray<UNiagaraComponent*> FX;H->GetComponents(FX);
        for(auto* E:FX) if(E->GetFName()==TEXT("TreatmentSprayNiagara")) Mist=E->IsActive();
        TArray<UStaticMeshComponent*> Equipment;H->GetComponents(Equipment);
        for(auto* E:Equipment) if(E->GetFName()==TEXT("InventoryTool")) {
            Tool=E->IsVisible();UE_LOG(LogTemp,Display,TEXT("MC_SPRAY_POSE tool=%s scale=%s visible=%d hand=%s"),*E->GetComponentLocation().ToString(),*E->GetComponentScale().ToString(),Tool,*H->GetMesh()->GetSocketLocation(H->RigBone(TEXT("hand_r"))).ToString());
        }
        Check(Mist && Tool,TEXT("held treatment presents its can and active Niagara mist"));
        TArray<UDecalComponent*> Decals;R.Ulcer->GetComponents(Decals);
        for(auto* D:Decals) if(auto* M=Cast<UMaterialInstanceDynamic>(D->GetDecalMaterial())) Check(M->K2_GetScalarParameterValue(TEXT("Frozen"))==0,TEXT("spray keeps the original ulcer tissue without frost"));
        R.Saved=R.Ulcer.IsValid()?R.Ulcer->Healing:0;
        Check(R.Saved>.25f && R.Saved<.33f,TEXT("two seconds of held spray advances treatment"));
        H->ServerSetPrimary(false); Shot(TEXT("03_Treatment.png")); R.Stage=3; R.At=Now;
    }
    else if(R.Stage==3 && Now-R.At>=2) {
        Check(R.Ulcer.IsValid() && FMath::IsNearlyEqual(R.Ulcer->Healing,R.Saved,.0001f),TEXT("release preserves treatment progress"));
        Shot(TEXT("04_Paused.png")); H->ServerSetPrimary(true); R.Stage=4; R.At=Now;
    }
    else if(R.Stage==4) {
        if(R.Ulcer.IsValid() && R.Ulcer->Healing>.8f && !R.ProgressShot) { Shot(TEXT("05_AlmostHealed.png")); R.ProgressShot=true; }
        if(!R.Ulcer.IsValid() || R.Ulcer->IsHealed()) {
            Check(R.ProgressShot && Now-R.At>4.5 && Now-R.At<6,TEXT("resumed treatment completes seven effective seconds"));
            Check(R.Director->CountFood(77)==0,TEXT("curing resolves the meal task")); GS->TasksLeft=0;
            H->ServerSetPrimary(false); Shot(TEXT("06_Healed.png")); R.Stage=5; R.At=Now;
        }
    }
    else if(R.Stage==5 && Now-R.At>1) {
        const FString Result=FString::Printf(TEXT("MC_ULCER_%s saved=%.4f"),R.Failed?TEXT("FAIL"):TEXT("PASS"),R.Saved);
        UE_LOG(LogTemp,Display,TEXT("%s"),*Result);
        FFileHelper::SaveStringToFile(Result+TEXT("\n"),*(FPaths::ProjectDir()/TEXT("Artifacts/UlcerRework/Validation.txt")));
        FPlatformMisc::RequestExitWithStatus(false,R.Failed?1:0);
    }
    if(R.Age>60) { UE_LOG(LogTemp,Error,TEXT("MC_ULCER_FAIL timeout stage=%d"),R.Stage); FPlatformMisc::RequestExitWithStatus(false,1); }
}
#endif
