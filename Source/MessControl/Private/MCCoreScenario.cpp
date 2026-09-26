#include "MCCoreScenario.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCGripComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

AMCCoreScenario::AMCCoreScenario()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
}
FString AMCCoreScenario::Caption() const
{
    const TCHAR* Names[]={TEXT("CONNECTING / 4 PLAYERS"),TEXT("01 / COFFEE: TWO PLAYERS, FOUR CONTACTS"),TEXT("02 / CARE: HEAL AND SECURE"),TEXT("03 / FALLING FOOD: IMPACT AND RAGDOLL"),TEXT("04 / RESPAWN: ONE REAL ARENA TOOTH"),TEXT("05 / GRIP: PULL STUCK FOOD TOGETHER"),TEXT("06 / DRAG FOOD TO THE THROAT"),TEXT("ALL FOUR SYSTEMS / COMPLETE")};
    return Names[FMath::Clamp(Stage,0,7)];
}
void AMCCoreScenario::MoveHero(int32 Index,FVector Position,FRotator Rotation)
{
    if (!Heroes.IsValidIndex(Index) || !IsValid(Heroes[Index])) return;
    auto* Hero=Heroes[Index].Get(); Hero->DropFood(); Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics); Hero->ForceNetUpdate();
}
void AMCCoreScenario::NextStage()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); ++Stage; StageAt=GS->GetServerWorldTimeSeconds(); ForceNetUpdate();
    if (Stage==1)
    {
        // Artist blockouts can contain fewer than eight sockets. Exercise their
        // real actors and count delta, without indexing an absent third tooth.
        Target=GS->ArenaTeeth[FMath::Min(2,GS->ArenaTeeth.Num()-1)]; Target->SetCoffee(1);
        FVector P=Target->GetActorLocation(); const float Side=FMath::Sign(P.Y);
        P.Y-=Side*(Target->Body->Bounds.BoxExtent.Y+75); P.Z=95;
        MoveHero(0,P+FVector(-42,0,0),FRotator(0,Side*90,0)); MoveHero(1,P+FVector(42,0,0),FRotator(0,Side*90,0));
        MoveHero(2,FVector(-400,250,95),FRotator::ZeroRotator); MoveHero(3,FVector(0,0,95),FRotator::ZeroRotator);
    }
    if (Stage==2) { Target->ReceiveArenaHit(75,FVector(0,1,0)); Heroes[2]->Status->Damage(50); Heroes[2]->Status->Loosen(); }
    if (Stage==3)
    {
        MoveHero(3,FVector(0,0,95),FRotator::ZeroRotator);
        Food=GetWorld()->SpawnActor<AMCFoodActor>(FVector(0,0,700),FRotator::ZeroRotator);
    }
    if (Stage==4)
    {
        GS->ArenaTeeth[0]->SetCoffee(1); GS->ArenaTeeth[0]->ReceiveArenaHit(25,FVector::ForwardVector);
        Heroes[3]->Status->Damage(10000); if (Food) Food->Dispose();
    }
    if (Stage==5)
    {
        FVector Floor(-400,-180,0);
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It) { FHitResult Hit; if (It->SurfacePoint(Floor,Hit)) Floor=Hit.ImpactPoint; break; }
        MoveHero(0,Floor+FVector(-86,0,61),FRotator::ZeroRotator); MoveHero(1,Floor+FVector(86,0,61),FRotator::ZeroRotator);
        const FTransform Transform(Floor+FVector(0,0,51));
        Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
        FMCFoodRow Row; Row.Mass=4; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
        Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
        FRandomStream Random(1); Food->ConfigureItem(TEXT("CoreGrip"),Row,Random);
        Food->Phase=EMCFoodPhase::Stuck; Food->PullDirection=FVector(0,1,0); UGameplayStatics::FinishSpawningActor(Food,Transform);
    }
    UE_LOG(LogTemp,Display,TEXT("MC_CORE_STAGE %d %s"),Stage,*Caption());
}
void AMCCoreScenario::Tick(float Dt)
{
#if !UE_BUILD_SHIPPING
    Super::Tick(Dt); Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS) return;
    const double Now=GS->GetServerWorldTimeSeconds(), Elapsed=Now-StageAt;
    const bool bCapture=HasAuthority() && FParse::Param(FCommandLine::Get(),TEXT("MCCoreCapture"));
    bool bRenderingReady=true;
#if WITH_EDITOR
    bRenderingReady=!bCapture || !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
    if (HasAuthority())
    {
        GS->Phase=EMCShiftPhase::Intermission; GS->PhaseEndsAt=Now+999;
        if (Stage==0 && GS->PlayerArray.Num()==4 && Age>6 && bRenderingReady)
        {
            Heroes.Empty();
            for (auto Player:GS->PlayerArray) if (auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn())) Heroes.Add(Hero);
            if (Heroes.Num()==4)
            {
                if (GS->ArenaTeeth.IsEmpty())
                {
                    UE_LOG(LogTemp,Error,TEXT("MC_CORE: map has no gameplay teeth"));
                    bFailed=true; Stage=7; StageAt=Now; ForceNetUpdate();
                }
                else NextStage();
            }
        }
        if (Stage==1 && Elapsed>4 && Target->Status->State.CoffeeLeft==0)
        {
            bFailed |= Heroes[0]->SuccessfulBrushContacts==0 || Heroes[1]->SuccessfulBrushContacts==0;
            NextStage();
        }
        else if (Stage==2 && Elapsed>4 && !Target->Status->NeedsCare(false) && !Heroes[2]->Status->NeedsCare(false)) NextStage();
        else if (Stage==3 && Elapsed>6 && Food->ConfirmedImpacts>0 && Heroes[3]->ToothPhysics->KnockdownCount>0 && Heroes[3]->ToothPhysics->CanAct()) NextStage();
        else if (Stage==4 && Elapsed>6 && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()-1)
        {
            const int32 SourceId=GS->ArenaTeeth[0]->State.ToothId;
            for (auto Player:GS->PlayerArray) if (auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn())) if (Hero->RespawnSourceId==SourceId) Heroes[3]=Hero;
            if (Heroes[3]->RespawnSourceId==SourceId) NextStage();
        }
        else if (Stage==5 && Elapsed>4 && Food->Phase==EMCFoodPhase::Free) NextStage();
        else if (Stage==6 && Food->IsDisposed()) NextStage();
        if (Stage>0 && Stage<7 && Elapsed>30) { bFailed=true; Stage=7; StageAt=Now; ForceNetUpdate(); }
    }
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    if (Hero && Hero->IsLocallyControlled())
    {
        const int32 Slot=Heroes.IndexOfByKey(Hero);
        if (Stage!=LocalStage || LocalHero!=Hero)
        {
            LocalStage=Stage; LocalHero=Hero;
            // Keep the grip held while switching from extraction to transport.
            Hero->StopBrush(); if (Stage!=6) Hero->StopHandle();
            const bool bSelf=Stage==2 && Slot==2;
            if (Hero->bSelfCare!=bSelf) Hero->ServerToggleSelfCare();
            if (Stage==1 && Slot>=0 && Slot<2) Hero->StartBrush();
            if ((Stage==2 && Slot>=0 && Slot<3) || ((Stage==5 || Stage==6) && Slot>=0 && Slot<2)) Hero->StartHandle();
        }
        if (Stage==5 && Elapsed<1 && Slot>=0 && Slot<2) Hero->SetActorRotation(FRotator::ZeroRotator);
        if ((Stage==5 || Stage==6) && Slot>=0 && Slot<2 && Food && !Hero->HeldFood) Hero->AddMovementInput((Food->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D());
        if (Stage==5 && Slot>=0 && Slot<2 && Hero->HeldFood && Food && Food->Phase==EMCFoodPhase::Stuck) Hero->AddMovementInput(FVector(0,1,0));
        if (Stage==6 && Slot>=0 && Slot<2 && Hero->HeldFood)
        {
            FVector Destination(965,0,95);
            for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (!It->bBrushBin)
            { Destination=It->GetActorLocation(); break; }
            // Both carriers steer the shared object toward the exit, not two competing pawn destinations.
            if (Food && FVector::DistSquared2D(Destination,Food->GetActorLocation())>FMath::Square(35.f)) Hero->AddMovementInput((Destination-Food->GetActorLocation()).GetSafeNormal2D());
        }
    }
    if (Target && Target->Status->State.CoffeeLeft==4 && Stage==1) Observed|=1;
    if (Target && Target->Status->State.CoffeeLeft==0 && Stage==1) Observed|=2;
    if (Target && Target->Status->IsLoose() && Stage==2) Observed|=4;
    if (Target && !Target->Status->NeedsCare(false) && Stage==2) Observed|=8;
    if (Heroes.Num()==4 && IsValid(Heroes[3]) && Stage==3 && Heroes[3]->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll && Heroes[3]->Status->State.Health<100) Observed|=16;
    if (!GS->ArenaTeeth.IsEmpty() && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()-1 && GS->ArenaTeeth[0] && GS->ArenaTeeth[0]->State.bConsumed) Observed|=32;
    if (Food && Stage==5 && Food->Holders.Num()==2 && Food->PullProgress>0) Observed|=64;
    if (Food && Stage==6 && Food->Phase==EMCFoodPhase::Free && Food->Holders.Num()>0) Observed|=128;
    if (Food && Food->IsDisposed() && Stage>=6) Observed|=256;
    if (bCapture && PC)
    {
        if (!CaptureCamera)
        {
            CaptureCamera=GetWorld()->SpawnActor<ACameraActor>(); CaptureCamera->GetCameraComponent()->SetFieldOfView(55);
            CaptureCamera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
            CaptureCamera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
            PC->SetViewTarget(CaptureCamera);
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("CoreDemoFrames")),true);
        }
        FVector Aim(0,0,100), Position(-2550,0,1150);
        if ((Stage==1 || Stage==2) && Target) { Aim=Target->GetActorLocation(); Position=Aim+FVector(-520,610,280); }
        if (Stage==3) Position=FVector(-880,430,400);
        if (Stage==5) { Aim=FVector(-400,-300,85); Position=FVector(-1050,180,500); }
        CaptureCamera->SetActorLocationAndRotation(Position,(Aim-Position).Rotation());
        if (Stage>0 && Now>=NextCapture)
        {
            NextCapture=Now+.1;
            if (LastCapture>=0) CaptureTiming+=FString::Printf(TEXT("duration %.6f\n"),Now-LastCapture);
            CaptureTiming+=FString::Printf(TEXT("file 'Frame%05d.png'\n"),CaptureFrame);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("CoreDemoFrames/Frame%05d.png"),CaptureFrame++),true,false);
            LastCapture=Now;
        }
    }
    if (Age>NextLog)
    {
        NextLog=Age+3;
        UE_LOG(LogTemp,Display,TEXT("MC_CORE net=%d stage=%d seen=%u failed=%d food=%d pull=%.2f grips=%d hero=%s"),int32(GetNetMode()),Stage,Observed,bFailed,Food?int32(Food->Phase):-1,Food?Food->PullProgress:0,Food?Food->Holders.Num():0,Hero?*Hero->GetActorLocation().ToString():TEXT("none"));
        if (Stage>=5 && Food && Hero) UE_LOG(LogTemp,Display,TEXT("MC_CORE_GRIP food=%s heroYaw=%.1f reason=%s"),*Food->GetActorLocation().ToCompactString(),Hero->GetActorRotation().Yaw,*Hero->Grip->DebugFailure);
    }
    if ((Stage==7 && Now-StageAt>(HasAuthority()?5:2)) || Age>140)
    {
        if (bCapture && CaptureFrame>0) FFileHelper::SaveStringToFile(CaptureTiming+FString::Printf(TEXT("duration 0.1\nfile 'Frame%05d.png'\n"),CaptureFrame-1),*(FPaths::ProjectSavedDir()/TEXT("CoreDemoFrames/Timing.txt")));
        const bool bPass=Stage==7 && !bFailed && Observed==511;
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s core seen=%u stage=%d failed=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Observed,Stage,bFailed);
        FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
    }
#endif
}
void AMCCoreScenario::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCCoreScenario,Stage); DOREPLIFETIME(AMCCoreScenario,StageAt);
    DOREPLIFETIME(AMCCoreScenario,Target); DOREPLIFETIME(AMCCoreScenario,Food); DOREPLIFETIME(AMCCoreScenario,Heroes); DOREPLIFETIME(AMCCoreScenario,bFailed);
}
