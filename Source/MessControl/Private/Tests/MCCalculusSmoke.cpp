#if !UE_BUILD_SHIPPING
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothCalculusComponent.h"
#include "MCInventoryComponent.h"
#include "MCArenaTooth.h"
#include "MCDayDirector.h"
#include "MCFoodActor.h"
#include "MCMotionRecorder.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void MCTickApprovalRecorder(UWorld* World);

namespace
{
    uint32 CalculusGeometryHash(UProceduralMeshComponent* Mesh,int32& Triangles)
    {
        uint32 Hash=0; Triangles=0;
        if (!Mesh) return Hash;
        // Read the actual rendered geometry, rather than a test-only damage counter.
        for (int32 I=0;I<Mesh->GetNumSections();++I)
        {
            const FProcMeshSection* Section=Mesh->GetProcMeshSection(I);
            if (!Section || !Section->bSectionVisible) continue;
            Triangles+=Section->ProcIndexBuffer.Num()/3;
            for (const FProcMeshVertex& Vertex:Section->ProcVertexBuffer)
                Hash=HashCombineFast(Hash,FCrc::MemCrc32(&Vertex.Position,sizeof(Vertex.Position)));
            Hash=HashCombineFast(Hash,FCrc::MemCrc32(Section->ProcIndexBuffer.GetData(),Section->ProcIndexBuffer.Num()*sizeof(int32)));
        }
        return Hash;
    }
}

// One opt-in Editor gameplay check: the F3 fixture and held primary input are production paths.
void MCTickCalculusValidation(UWorld* World)
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<AMCArenaTooth> Tooth;
        TWeakObjectPtr<UMCMotionRecorder> Recorder;
        TWeakObjectPtr<ACameraActor> Camera;
        double At=0,CompletedAt=-1,GrownWarmupAt=-1,ChiselStartedAt=-1;
        float Age=0,ChiselHealth=0,MinTipError=MAX_flt;
        int32 Stage=-1,InitialPieces=0,InitialTriangles=0,ChiselSwings=0,ChiselHits=0,ClickSwings=0,ClickHits=0,TipSamples=0;
        uint32 InitialGeometry=0;
        bool Pressed=false,Released=false,GuardPassed=true,ClickPassed=false,GeometryChanged=false,GrownShot=false,ImpactShot=false,PartialShot=false,Failed=false;
    };
    static FRun R;
    if (R.World.Get()!=World) { R=FRun(); R.World=World; }
    R.Age+=World->GetDeltaSeconds();
    auto* GS=World->GetGameState<AMCGameState>();
    auto* PC=Cast<AMCPlayerController>(World->GetFirstPlayerController());
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto Finish=[&](bool Pass)
    {
        if (Hero) Hero->ServerSetPrimary(false);
        if (R.Recorder.IsValid()) R.Recorder->Stop();
        UE_LOG(LogTemp,Display,TEXT("MC_CALCULUS_%s guard=%d click=%d changed=%d remaining=%d swings=%d hits=%d tooth_hp=%.1f expected_hp=%.1f"),
            Pass?TEXT("PASS"):TEXT("FAIL"),R.GuardPassed,R.ClickPassed,R.GeometryChanged,
            R.Tooth.IsValid() && R.Tooth->Calculus?R.Tooth->Calculus->RemainingPieces():-1,
            Hero?Hero->ValidatedSwingCount-R.ChiselSwings:0,Hero?Hero->ConfirmedHitCount-R.ChiselHits:0,
            R.Tooth.IsValid()?R.Tooth->State.Health:-1.f,R.ChiselHealth);
        UE_LOG(LogTemp,Display,TEXT("MC_CALCULUS_CONTACT samples=%d closest_tip_error=%.2f cm"),R.TipSamples,R.MinTipError);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if (R.Age>90) { Finish(false); return; }
    if (!GS || !PC || !Hero || R.Age<3 || World->GetNetMode()!=NM_Standalone) return;
#if WITH_EDITOR
    if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { R.GrownWarmupAt=-1; return; }
#endif
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("MCCalculusCapture"));
    bool ShotRequested=false;
    auto Shot=[&](const TCHAR* Name)
    {
        if (!Capture) return;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("CalculusReview");
        IFileManager::Get().MakeDirectory(*Folder,true);
        FScreenshotRequest::RequestScreenshot(Folder/(FString(Name)+TEXT(".png")),false,false);
        ShotRequested=true;
    };
    auto Stage=[&](int32 Next,EMCToolSlot Slot)
    {
        Hero->ServerSetPrimary(false);
        Hero->Inventory->ServerSelect(Slot);
        R.Stage=Next; R.At=World->GetTimeSeconds(); R.Pressed=false; R.Released=false;
        if (R.Recorder.IsValid()) R.Recorder->Stage=Next==0?TEXT("brush_guard"):Next==1?TEXT("knife_guard"):Next==2?TEXT("pickaxe_click_45"):TEXT("pickaxe_chisel");
    };
    if (R.Stage<0)
    {
        World->GetAuthGameMode()->SetActorTickEnabled(false);
        for (TActorIterator<AMCDayDirector> It(World);It;++It) It->SetActorTickEnabled(false);
        Hero->CancelGameplayInput(); Hero->DropFood();
        for (TActorIterator<AMCFoodActor> It(World);It;++It) It->Dispose();
        GS->Phase=EMCShiftPhase::Working; GS->PhaseEndsAt=0; GS->bDevManualEvents=true; GS->bPhysicalBrushes=false;
        for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth)) Tooth->SetCoffee(0);
        const FText Fixture=World->GetAuthGameMode<AMCGameMode>()->ExecuteDevAction(PC,EMCDevAction::CalculusPractice);
        for (TActorIterator<AMCArenaTooth> It(World);It;++It) if (It->ActorHasTag(TEXT("MC_CalculusPractice"))) { R.Tooth=*It; break; }
        FVector Contact,Normal;
        if (!R.Tooth.IsValid() || !R.Tooth->Calculus || !R.Tooth->Calculus->FindContact(Hero,Contact,Normal))
        { UE_LOG(LogTemp,Error,TEXT("MC_CALCULUS_FIXTURE_FAIL %s"),*Fixture.ToString()); Finish(false); return; }
        auto* Calculus=R.Tooth->Calculus.Get();
        R.InitialPieces=Calculus->RemainingPieces();
        R.InitialGeometry=CalculusGeometryHash(Calculus->Deposits,R.InitialTriangles);
        if (R.InitialPieces<3 || R.InitialTriangles<=0) { Finish(false); return; }
        UE_LOG(LogTemp,Display,TEXT("MC_CALCULUS_FIXTURE tooth=%d pieces=%d triangles=%d hero=%s contact=%s"),
            R.Tooth->State.ToothId,R.InitialPieces,R.InitialTriangles,*Hero->GetActorLocation().ToString(),*Contact.ToString());
        auto* Recorder=NewObject<UMCMotionRecorder>(Hero); Hero->AddInstanceComponent(Recorder); Recorder->RegisterComponent(); Recorder->Start(65,TEXT("calculus_chisel")); R.Recorder=Recorder;
        if (Capture)
        {
            const FVector Inward=Normal.GetSafeNormal2D(),Side=FVector::CrossProduct(Inward,FVector::UpVector);
            const FVector Aim=(Contact+Hero->GetActorLocation())*.5+FVector(0,0,15);
            const FVector Eye=Aim+Inward*310+Side*230+FVector(0,0,155);
            R.Camera=World->SpawnActor<ACameraActor>();
            R.Camera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
            R.Camera->GetCameraComponent()->SetFieldOfView(48);
            PC->SetViewTarget(R.Camera.Get());
        }
        Stage(0,EMCToolSlot::Brush); return;
    }
    if (!R.Tooth.IsValid() || !R.Tooth->Calculus) { Finish(false); return; }
    if (R.Stage==0 && !R.GrownShot)
    {
        // Growth lazy-loads its material. Give the compiled shader time to reach the render thread before the intact preview.
        if (R.GrownWarmupAt<0) R.GrownWarmupAt=World->GetTimeSeconds();
        if (World->GetTimeSeconds()-R.GrownWarmupAt<.5) return;
        R.GrownShot=true; R.At=World->GetTimeSeconds(); Shot(TEXT("01_Grown"));
    }
    auto* Calculus=R.Tooth->Calculus.Get();
    FVector Contact,Normal;
    Hero->GetCharacterMovement()->StopMovementImmediately();
    const double T=World->GetTimeSeconds()-R.At;
    if (R.Stage<2)
    {
        if (T>.2 && !R.Pressed) { Hero->ServerSetPrimary(true); R.Pressed=true; }
        if (T>.6 && !R.Released) { Hero->ServerSetPrimary(false); R.Released=true; }
        if (T>1.25 && Hero->CanSwitchTool())
        {
            int32 Triangles=0;
            R.GuardPassed&=Calculus->RemainingPieces()==R.InitialPieces && CalculusGeometryHash(Calculus->Deposits,Triangles)==R.InitialGeometry;
            if (R.Stage==0) Stage(1,EMCToolSlot::Knife);
            else
            {
                R.ChiselHealth=R.Tooth->State.Health; R.ClickSwings=Hero->ValidatedSwingCount; R.ClickHits=Hero->ConfirmedHitCount;
                if (Calculus->FindContact(Hero,Contact,Normal))
                    Hero->SetActorRotation(FRotator(0,(Contact-Hero->GetActorLocation()).Rotation().Yaw+45.f,0));
                Stage(2,EMCToolSlot::Pickaxe);
            }
        }
        return;
    }
    if (R.Stage==2)
    {
        if (T>.2 && !R.Pressed) { Hero->ServerSetPrimary(true); R.Pressed=true; }
        if (T>.24 && !R.Released) { Hero->ServerSetPrimary(false); R.Released=true; }
        if (T>1.3 && Hero->CanSwitchTool())
        {
            R.ClickPassed=Hero->ValidatedSwingCount-R.ClickSwings==1 && Hero->ConfirmedHitCount-R.ClickHits==1;
            UE_LOG(LogTemp,Display,TEXT("MC_CALCULUS_CLICK pass=%d swings=%d hits=%d"),R.ClickPassed,
                Hero->ValidatedSwingCount-R.ClickSwings,Hero->ConfirmedHitCount-R.ClickHits);
            if (!R.ClickPassed) { Finish(false); return; }
            R.ChiselSwings=Hero->ValidatedSwingCount; R.ChiselHits=Hero->ConfirmedHitCount;
            Stage(3,EMCToolSlot::Pickaxe); return;
        }
    }
    else if (T>.3 && !R.Pressed)
    {
        R.ChiselStartedAt=World->GetTimeSeconds();
        Hero->ServerSetPrimary(true); R.Pressed=true;
    }
    if (FMath::Abs(Hero->GetToolSwingElapsed()-Hero->Inventory->SwingContactTime())<.1f
        && Hero->GetCalculusSwingContact(Contact,Normal))
    {
        const FVector Tip=Hero->Inventory->PickaxeContactTip();
        if (!Tip.IsNearlyZero())
        {
            R.MinTipError=FMath::Min(R.MinTipError,float(FVector::Dist(Tip,Contact))); ++R.TipSamples;
        }
    }
    R.Failed|=!FMath::IsNearlyEqual(R.Tooth->State.Health,R.ChiselHealth,.01f);
    int32 Triangles=0;
    const uint32 Geometry=CalculusGeometryHash(Calculus->Deposits,Triangles);
    R.GeometryChanged|=Geometry!=R.InitialGeometry && Calculus->RemainingFraction()<.999f;
    if (!R.ImpactShot && Calculus->RemainingFraction()<.999f && Calculus->HasCalculus())
    { R.ImpactShot=true; Shot(TEXT("02_FirstImpact")); }
    else if (!R.PartialShot && Calculus->RemainingPieces()<R.InitialPieces && Calculus->HasCalculus())
    { R.PartialShot=true; Shot(TEXT("02_Chipped")); }
    if (!Calculus->HasCalculus() && R.CompletedAt<0)
    {
        Hero->ServerSetPrimary(false); R.CompletedAt=World->GetTimeSeconds();
        UE_LOG(LogTemp,Display,TEXT("MC_CALCULUS_CLEARED seconds=%.3f swings=%d hits=%d"),
            R.CompletedAt-R.ChiselStartedAt,Hero->ValidatedSwingCount-R.ChiselSwings,Hero->ConfirmedHitCount-R.ChiselHits);
        Shot(TEXT("03_Cleared"));
    }
    else if (R.CompletedAt<0 && !ShotRequested) MCTickApprovalRecorder(World);
    if (R.CompletedAt>=0 && World->GetTimeSeconds()>R.CompletedAt+1.2)
        Finish(R.GuardPassed && R.ClickPassed && !R.Failed && R.GeometryChanged && Calculus->RemainingPieces()==0 && Triangles==0
            && Hero->ValidatedSwingCount-R.ChiselSwings>=2 && Hero->ConfirmedHitCount-R.ChiselHits>=2);
    else if (T>55) Finish(false);
}
#endif
