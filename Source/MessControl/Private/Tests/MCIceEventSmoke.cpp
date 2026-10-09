#include "MCIceEventSmoke.h"

#include "MCArenaTooth.h"
#include "MCBossCharacter.h"
#include "MCColdCola.h"
#include "MCCoffeeFlood.h"
#include "MCDayDirector.h"
#include "MCDayPlan.h"
#include "MCFirePatch.h"
#include "MCFoodActor.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCInventoryComponent.h"
#include "MCLocomotionSurface.h"
#include "MCMouthSurface.h"
#include "MCNutRainEvent.h"
#include "MCPlayerController.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
    template<class T> T* First(UWorld* World)
    {
        for(TActorIterator<T> It(World);It;++It) if(!It->IsActorBeingDestroyed()) return *It;
        return nullptr;
    }
    template<class T> int32 Count(UWorld* World)
    {
        int32 N=0; for(TActorIterator<T> It(World);It;++It) if(!It->IsActorBeingDestroyed()) ++N; return N;
    }
    const TCHAR* ActionName(EMCDevAction Action)
    {
        switch(Action)
        {
        case EMCDevAction::NutEncounter:return TEXT("NutEncounter");case EMCDevAction::IceEvent:return TEXT("IceEvent");
        case EMCDevAction::DropFood:return TEXT("Food");case EMCDevAction::Infection:return TEXT("SpoiledFood");
        case EMCDevAction::CoffeeDirt:return TEXT("CoffeeDirt");case EMCDevAction::CoffeeFlood:return TEXT("CoffeeFlood");
        case EMCDevAction::OldFlood:return TEXT("OldFlood");case EMCDevAction::SwimCoffee:return TEXT("SwimCoffee");
        case EMCDevAction::ColdCola:return TEXT("ColdCola");case EMCDevAction::Yawn:return TEXT("Yawn");
        case EMCDevAction::SpicyPepper:return TEXT("SpicyPepper");case EMCDevAction::StuckFood:return TEXT("StuckFood");
        case EMCDevAction::LooseTeeth:return TEXT("LooseTeeth");case EMCDevAction::RewardChest:return TEXT("RewardChest");
        case EMCDevAction::MimicChest:return TEXT("MimicChest");case EMCDevAction::BossAI:return TEXT("BossAI");
        case EMCDevAction::BossPhase3Activate:return TEXT("BossPhase3");case EMCDevAction::VomitMeal:return TEXT("VomitMeal");
        case EMCDevAction::Fire:return TEXT("Fire");case EMCDevAction::TongueUlcer:return TEXT("TongueUlcer");
        default:return TEXT("Unknown");
        }
    }
}

bool UMCIceEventSmoke::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
    return (FParse::Param(FCommandLine::Get(),TEXT("MCIceEventSmoke"))
        || FParse::Param(FCommandLine::Get(),TEXT("MCIceColdReworkSmoke"))) && Super::ShouldCreateSubsystem(Outer);
#else
    return false;
#endif
}

void UMCIceEventSmoke::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); StartedAt=FPlatformTime::Seconds();
    bColdRework=FParse::Param(FCommandLine::Get(),TEXT("MCIceColdReworkSmoke"));
    bRequireColdVFX=FParse::Param(FCommandLine::Get(),TEXT("MCIceRequireVFX"));
    FParse::Value(FCommandLine::Get(),TEXT("MCIceExpectedPlayers="),ExpectedPlayers);
    bCapture=FParse::Param(FCommandLine::Get(),TEXT("MCIceEventCapture"));
    CaptureFolder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("IceEventFrames"));
    FParse::Value(FCommandLine::Get(),TEXT("MCIceCaptureDir="),CaptureFolder);
    Actions={EMCDevAction::NutEncounter,EMCDevAction::IceEvent,EMCDevAction::DropFood,EMCDevAction::Infection,
        EMCDevAction::CoffeeDirt,EMCDevAction::CoffeeFlood,EMCDevAction::OldFlood,EMCDevAction::SwimCoffee,
        EMCDevAction::ColdCola,EMCDevAction::Yawn,EMCDevAction::SpicyPepper,EMCDevAction::StuckFood,
        EMCDevAction::LooseTeeth,EMCDevAction::RewardChest,EMCDevAction::MimicChest,EMCDevAction::BossAI,
        EMCDevAction::BossPhase3Activate,EMCDevAction::VomitMeal,EMCDevAction::Fire,EMCDevAction::TongueUlcer};
}

void UMCIceEventSmoke::Fail(const TCHAR* Reason)
{
    if(bFinished) return; bFinished=true;
    UE_LOG(LogTemp,Error,TEXT("%s role=%s stage=%d case=%d reason=%s health=%.1f freeze=%.3f candy=%.1f"),
        bColdRework?TEXT("MC_ICE_COLD_FAIL"):TEXT("MC_ICE_SMOKE_FAIL"),GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"),Stage,CaseIndex,Reason,
        Hero?Hero->Status->State.Health:-1,Ice?Ice->FreezeAmount(Hero):-1,Ice?Ice->CandyHealth:-1);
    FPlatformMisc::RequestExitWithStatus(false,1);
}

void UMCIceEventSmoke::Enter(int32 Next,const TCHAR* Label)
{
    Stage=Next; StageAt=GetWorld()->GetTimeSeconds(); StageLabel=Label;
    UE_LOG(LogTemp,Display,TEXT("%s stage=%d label=%s server_time=%.3f"),bColdRework?TEXT("MC_ICE_COLD_STAGE"):TEXT("MC_ICE_STAGE"),Stage,Label,StageAt);
}

bool UMCIceEventSmoke::RefreshHero()
{
    if(!Host) Host=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    Hero=Host?Cast<AMCToothCharacter>(Host->GetPawn()):nullptr;
    if(!Tongue) Tongue=First<AMCTongue>(GetWorld());
    return Hero && Hero->Status && Tongue && Tongue->Surface && !Tongue->CurrentVertices().IsEmpty();
}

bool UMCIceEventSmoke::Place(FVector Point,FVector Facing)
{
    FHitResult Floor;
    if(!Hero || !Tongue || !Tongue->InteriorSurfacePoint(Point,60,Floor)) return false;
    const FVector Position=Floor.ImpactPoint+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3);
    const FRotator Rotation(0,Facing.Rotation().Yaw,0);
    Hero->SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Host->SetControlRotation(Rotation); Hero->ForceNetUpdate(); return true;
}

bool UMCIceEventSmoke::Warm()
{
    if(bColdRework)
    {
        FVector Point;
        return ColdWarmPoint(Point) && Place(Point);
    }
    return Ice && Place(Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor));
}

bool UMCIceEventSmoke::Unsafe()
{
    if(!Ice) return false;
    const FVector Safe=Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor);
    for(int32 I=0;I<16;++I)
    {
        const float A=I*UE_TWO_PI/16; const FVector Candidate=Safe+FVector(FMath::Cos(A),FMath::Sin(A),0)*(Ice->CircleRadius+300);
        FHitResult Hit;
        if(!Tongue->InteriorSurfacePoint(Candidate,100,Hit) || Ice->IsSafePoint(Hit.ImpactPoint)) continue;
        bool Danger=false;
        for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-GetWorld()->GetTimeSeconds()<2.1
            && FVector::DistSquared2D(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Hit.ImpactPoint)<FMath::Square(Ice->IcicleRadius+100)) Danger=true;
        if(!Danger) return Place(Hit.ImpactPoint);
    }
    return false;
}

void UMCIceEventSmoke::ParkOtherPlayers()
{
    if(!Ice || !Tongue) return;
    FHitResult Floor; const FVector Safe=Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor);
    if(!Tongue->SurfacePoint(Safe,Floor)) return;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* Other=Cast<AMCToothCharacter>(It->Get()?It->Get()->GetPawn():nullptr);Other && Other!=Hero && Other->Status->IsAlive())
        {
            Other->CancelGameplayInput();
            FVector Position=Floor.ImpactPoint+FVector(0,70,0);
            for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-GetWorld()->GetTimeSeconds()<2.1
                && FVector::DistSquared2D(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Position)<FMath::Square(Ice->IcicleRadius+100))
            {
                FHitResult Escape;
                if(Tongue->InteriorSurfacePoint(Position+FVector(0,450,0),60,Escape)) Position=Escape.ImpactPoint;
            }
            Other->SetActorLocation(Position+FVector(0,0,Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
            Other->GetCharacterMovement()->StopMovementImmediately(); Other->ForceNetUpdate();
        }
}

void UMCIceEventSmoke::StartF3Case()
{
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    // Production Ice start/stop establishes the same clean manual sandbox for every support action.
    Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent); Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);
    RefreshHero(); Hero->CancelGameplayInput(); Place(Tongue->Surface->Bounds.Origin);
    const EMCDevAction Action=Actions[CaseIndex];
    const FText Result=Mode->ExecuteDevAction(Host,Action);
    RefreshHero();
    UE_LOG(LogTemp,Display,TEXT("MC_ICE_F3_START action=%s feedback=%s"),ActionName(Action),*Result.ToString());
    StageAt=GetWorld()->GetTimeSeconds();
}

bool UMCIceEventSmoke::CheckF3Case() const
{
    UWorld* World=GetWorld(); const auto* GS=World->GetGameState<AMCGameState>();
    switch(Actions[CaseIndex])
    {
    case EMCDevAction::NutEncounter:{auto* E=First<AMCNutRainEvent>(World);return E && !E->bFailed && E->Stage!=EMCNutRainStage::Idle;}
    case EMCDevAction::IceEvent:{auto* E=First<AMCIceEvent>(World);return E && !E->bFailed && E->IsActive();}
    case EMCDevAction::DropFood:case EMCDevAction::Infection:return Count<AMCFoodActor>(World)>0;
    case EMCDevAction::CoffeeDirt:
        for(TActorIterator<AMCArenaTooth> It(World);It;++It) if(It->Status->State.CoffeeLeft>0) return true;
        return Count<AMCMouthSurface>(World)>0;
    case EMCDevAction::CoffeeFlood:case EMCDevAction::OldFlood:case EMCDevAction::SwimCoffee:
        for(TActorIterator<AMCCoffeeFlood> It(World);It;++It) if(It->IsActive()) return true;
        return false;
    case EMCDevAction::ColdCola:{auto* E=First<AMCColdColaEvent>(World);return E && E->bActive;}
    case EMCDevAction::Yawn:return Tongue->IsYawnActive();
    case EMCDevAction::SpicyPepper:for(TActorIterator<AMCFoodActor> It(World);It;++It) if(It->ItemName==TEXT("SpicyPepper")) return true;return false;
    case EMCDevAction::StuckFood:for(TActorIterator<AMCFoodActor> It(World);It;++It) if(It->Phase==EMCFoodPhase::Stuck) return true;return false;
    case EMCDevAction::LooseTeeth:return Hero && Hero->Status->IsLoose();
    case EMCDevAction::RewardChest:
        for(TActorIterator<AMCRoguelikeDirector> It(World);It;++It) if(It->PendingRewards>0 || It->RewardsSpawned>0) return true;
        return false;
    case EMCDevAction::MimicChest:for(TActorIterator<AMCRewardChest> It(World);It;++It) if(It->ActorHasTag(TEXT("MC_DevMimic"))) return true;return false;
    case EMCDevAction::BossAI:case EMCDevAction::BossPhase3Activate:
        for(TActorIterator<AMCBossCharacter> It(World);It;++It) if(It->ActorHasTag(TEXT("MC_DevBoss")) && !It->IsActorBeingDestroyed()) return true;
        return false;
    case EMCDevAction::VomitMeal:return Count<AMCFoodActor>(World)>=2;
    case EMCDevAction::Fire:return Count<AMCFirePatch>(World)>0;
    case EMCDevAction::TongueUlcer:for(TActorIterator<AMCMouthSurface> It(World);It;++It) if(It->bUlcer) return true;return false;
    default:return GS && GS->bDevManualEvents;
    }
}

void UMCIceEventSmoke::FrameCamera()
{
    if(!bCapture || !Ice || !Tongue) return;
    if(!CaptureCamera) CaptureCamera=GetWorld()->SpawnActor<ACameraActor>();
    const FVector Candy=Tongue->GetActorTransform().TransformPosition(Ice->CandyAnchor);
    const FVector Safe=Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor);
    const FVector Player=Hero?Hero->GetActorLocation():Safe;
    const float Arrival=FMath::Clamp(float((GetWorld()->GetTimeSeconds()-Ice->StartedAt)/Ice->ArrivalSeconds),0.f,1.f);
    FVector Aim=FMath::Lerp(Candy,(Player+Safe)*.5f,.4f)+FVector(0,0,FMath::Lerp(360.f,120.f,Arrival));
    FVector Eye=Aim+FVector(-1250,-950,900);
    if(bColdRework && (Stage==104 || Stage==106))
    { Aim=Player+FVector(0,0,20);Eye=Aim+(Stage==106?FVector(350,-500,280):FVector(-700,-550,500)); }
    else if(bColdRework && Ice->bNextCircle && Ice->CircleIndex==0)
    { Aim=(Safe+Tongue->GetActorTransform().TransformPosition(Ice->NextSafeAnchor))*.5f;Eye=Aim+FVector(-1700,-1400,1900); }
    CaptureCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
    CaptureCamera->GetCameraComponent()->SetFieldOfView(60);
    Host->SetViewTarget(CaptureCamera);
}

void UMCIceEventSmoke::Capture()
{
    if(!bCapture || Stage<10 || !Host || !Host->IsLocalController()) return;
    if(bColdRework && !ColdShouldCapture()) return;
#if WITH_EDITOR
    if(bColdRework && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    const double Time=GetWorld()->GetTimeSeconds(); if(Time<NextCaptureAt || FScreenshotRequest::IsScreenshotRequested()) return;
    // The legacy capture uses slowed game time. The cold rework keeps normal
    // game speed and samples selected stages less frequently to bound PNG cost.
    NextCaptureAt=Time+(bColdRework?.12:.045);
    const FString Name=FString::Printf(TEXT("Frame%05d.png"),Frame++);
    FScreenshotRequest::RequestScreenshot(CaptureFolder/Name,true,false);
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f,%s\n"),*Name,Time,*StageLabel),
        *(CaptureFolder/TEXT("FrameTimes.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}

void UMCIceEventSmoke::TickClient()
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>(); if(!GS) return;
    if(auto* E=First<AMCIceEvent>(GetWorld()))
    {
        if(E->Stage==EMCIceEventStage::Arrival) ClientSeen|=1;
        if(E->Stage==EMCIceEventStage::Active) ClientSeen|=2;
        for(const auto& F:E->Players) if(F.Amount>.1f) ClientSeen|=4;
        if(!E->Strikes.IsEmpty()) ClientSeen|=8;
        if(E->bNextCircle || E->CircleIndex>0) ClientSeen|=16;
        if(E->CandyHealth<E->MaxCandyHealth && E->CandyHealth>0) ClientSeen|=32;
        if(E->IsComplete()) ClientSeen|=64;
    }
    if(GS->DirectorState.CurrentTitle==TEXT("MC_ICE_SMOKE_DONE"))
    {
        if(ClientSeen!=127 || Count<AMCIceEvent>(GetWorld())!=0) {Fail(TEXT("Remote peer missed replicated winter protocol or cleanup"));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_SMOKE_PASS role=client remote_seen=%u"),ClientSeen);
        bFinished=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void UMCIceEventSmoke::ColdPrimaryKey(bool Held)
{
    if(bColdKeyHeld==Held || !Host || !Host->IsLocalController()) return;
    Host->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::LeftMouseButton,
        Held?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
    bColdKeyHeld=Held;if(Held) ++ColdInputPresses;
}

bool UMCIceEventSmoke::ColdWarmPoint(FVector& Point,bool bAvoidStrikes) const
{
    if(!Ice || !Tongue) return false;
    const double Time=GetWorld()->GetTimeSeconds();
    for(int32 Zone=Ice->bNextCircle?1:0;Zone>=0;--Zone)
    {
        const int32 Index=Ice->CircleIndex+Zone;
        if(Ice->IsCircleBlocked(Index)) continue;
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Zone?Ice->NextSafeAnchor:Ice->SafeAnchor);
        const float Radius=Zone?Ice->NextSafeRadius():Ice->SafeRadius();
        for(int32 Sample=0;Sample<17;++Sample)
        {
            const float Angle=Sample*UE_TWO_PI/16;
            FHitResult Floor;const FVector Candidate=Center+(Sample?FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius*.85f:FVector::ZeroVector);
            if(!Tongue->InteriorSurfacePoint(Candidate,50,Floor) || !Ice->IsSafePoint(Floor.ImpactPoint)) continue;
            bool Danger=false;
            if(bAvoidStrikes) for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-Time<2.25
                && FVector::DistSquaredXY(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Floor.ImpactPoint)<FMath::Square(Ice->IcicleRadius+70)) Danger=true;
            if(!Danger) {Point=Floor.ImpactPoint;return true;}
        }
    }
    // A near-expiry circle can be smaller than an icicle's contact footprint.
    // Briefly step onto supported cold tissue instead of treating shelter as immunity.
    if(bAvoidStrikes) for(int32 Zone=Ice->bNextCircle?1:0;Zone>=0;--Zone)
    {
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Zone?Ice->NextSafeAnchor:Ice->SafeAnchor);
        for(int32 Sample=0;Sample<24;++Sample)
        {
            const float Angle=Sample*UE_TWO_PI/24;FHitResult Floor;
            if(!Tongue->InteriorSurfacePoint(Center+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*430,50,Floor)) continue;
            bool Danger=false;for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-Time<2.25
                && FVector::DistSquaredXY(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Floor.ImpactPoint)<FMath::Square(Ice->IcicleRadius+70)) Danger=true;
            if(!Danger) {Point=Floor.ImpactPoint;return true;}
        }
    }
    return false;
}

void UMCIceEventSmoke::ColdParkPeers()
{
    FVector Point;if(!ColdWarmPoint(Point)) return;
    if(Stage==105 && Ice->bNovaWarning && !Ice->IsInsideNova(Point))
    {
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Ice->CandyAnchor);
        const FVector Direction=Tongue->GetActorTransform().TransformVectorNoScale(Ice->NovaDirection).GetSafeNormal2D();
        FHitResult Floor;if(Tongue->InteriorSurfacePoint(Center+Direction*600,50,Floor)) Point=Floor.ImpactPoint;
    }
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* Other=Cast<AMCToothCharacter>(It->Get()?It->Get()->GetPawn():nullptr);
            Other && Other!=Hero && Other->Status->IsAlive() && !Other->HasFrozenLegs() && Other->CanWork())
        {
            FVector PeerPoint=Point;
            for(int32 Sample=0;Sample<16;++Sample)
            {
                const FVector Offset=FVector(0,110,0).RotateAngleAxis(Sample*22.5f,FVector::UpVector);
                FHitResult Floor;
                if(!Tongue->InteriorSurfacePoint(Point+Offset,50,Floor)) continue;
                if(!Ice->IsSafePoint(Floor.ImpactPoint) && !(Stage==105 && Ice->IsInsideNova(Floor.ImpactPoint))) continue;
                bool Danger=false;
                for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-GetWorld()->GetTimeSeconds()<2.25
                    && FVector::DistSquaredXY(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Floor.ImpactPoint)<FMath::Square(Ice->IcicleRadius+70)) Danger=true;
                if(!Danger) {PeerPoint=Floor.ImpactPoint;break;}
            }
            Other->SetActorLocation(PeerPoint+FVector(0,0,Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
            Other->GetCharacterMovement()->StopMovementImmediately();Other->ForceNetUpdate();
        }
}

void UMCIceEventSmoke::ColdObserveVFX(const AMCIceEvent& Event)
{
    if(!bRequireColdVFX || !Event.IsActive()) return;
    const auto* GS=GetWorld()->GetGameState();
    const double Time=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(Time<Event.StartedAt+1.) return;
    auto* VFX=First<AMCIceEventVFX>(GetWorld());
    if(Count<AMCIceEventVFX>(GetWorld())!=1 || !VFX || !VFX->HasRequiredAssets())
    {
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_VFX_DIAGNOSTIC controllers=%d controller=%s assets=%d event_stage=%d"),
            Count<AMCIceEventVFX>(GetWorld()),*GetNameSafe(VFX),VFX && VFX->HasRequiredAssets(),int32(Event.Stage));
        Fail(TEXT("Cold presentation did not resolve its authored assets or has duplicate controllers"));return;
    }
    ColdBurstPeak=FMath::Max(ColdBurstPeak,VFX->GetActiveBurstCount());
    if(ColdBurstPeak>8) {Fail(TEXT("Cold presentation exceeded its eight-burst budget"));return;}
    if(!bColdVFXSeen)
    {
        bColdVFXSeen=true;
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS vfx_assets=1 local_controller=1 role=%s"),
            GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"));
    }
}

void UMCIceEventSmoke::ColdObserve()
{
    ColdObserveVFX(*Ice);if(bFinished) return;
    const double Time=GetWorld()->GetTimeSeconds();
    for(const auto& Strike:Ice->Strikes) if(Strike.Id>ColdSerial)
    {
        if(Strike.Id!=ColdSerial+1) {Fail(TEXT("A real icicle warning was missed or IDs skipped"));return;}
        const double WarningAt=Strike.ImpactAt-Ice->IcicleWarningSeconds;
        if((Strike.Id-1)%3==0)
        {
            if(ColdBurstAt>0 && (WarningAt-ColdBurstAt<11.8 || WarningAt-ColdBurstAt>13.5)) {Fail(TEXT("Production burst cooldown is not twelve seconds"));return;}
            ColdBurstAt=WarningAt;
        }
        else if(WarningAt-ColdPreviousWarningAt<Ice->IcicleSeriesSpacingSeconds-.1 || WarningAt-ColdPreviousWarningAt>Ice->IcicleSeriesSpacingSeconds+.8)
        {Fail(TEXT("Three icicles did not form a spaced series"));return;}
        ColdSerial=Strike.Id;ColdPreviousWarningAt=WarningAt;
        if(ColdSerial==6) {bColdSeries=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS series=3 cooldown=12 warnings=6"));}
    }
    if(ColdCircleIndex!=Ice->CircleIndex)
    {
        if(ColdCircleIndex>=0 && !FMath::IsNearlyEqual(Ice->CircleStartedAt,ColdNextSpawn,.15)) {Fail(TEXT("Promoted zone restarted its lifetime instead of retaining its appearance time"));return;}
        ColdCircleIndex=Ice->CircleIndex;ColdCircleMask|=1u<<FMath::Min(ColdCircleIndex,5);
        const float Expected=FMath::Max(20.f,45.f-5.f*ColdCircleIndex);
        if(!FMath::IsNearlyEqual(Ice->CircleDuration(ColdCircleIndex),Expected)) {Fail(TEXT("Production warm-zone lifetime progression differs from 45 to 20"));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS zone=%d seconds=%.0f spawn=%.3f"),ColdCircleIndex,Ice->CircleDuration(ColdCircleIndex),Ice->CircleStartedAt);
        bColdMinimum=ColdCircleMask==63 && Ice->CircleDuration(ColdCircleIndex)==20;
    }
    if(Ice->bNextCircle)
    {
        ColdNextSpawn=Ice->NextCircleStartedAt;
        const FVector A=Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor),B=Tongue->GetActorTransform().TransformPosition(Ice->NextSafeAnchor);
        if(!bColdOverlap && Ice->IsSafePoint(A) && Ice->IsSafePoint(B))
        {
            if(FVector::DistXY(A,B)<Ice->CircleRadius*2 || !FMath::IsNearlyEqual(Ice->CircleStartedAt+Ice->CircleDuration(Ice->CircleIndex)-Ice->NextCircleStartedAt,10.,.15))
            {Fail(TEXT("Peripheral transfer lacks ten seconds of dual warmth or distinct distant zones"));return;}
            bColdOverlap=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS overlap=10 both_warm=1 crossing=%.1f at=%.3f"),FVector::DistXY(A,B),Time);
        }
    }
}

bool UMCIceEventSmoke::ColdShouldCapture() const
{
    if(bRequireColdVFX && Stage==121) return true;
    if(!Ice || Stage>=122) return false;
    const double Time=GetWorld()->GetTimeSeconds();
    if(Stage==120) return Time-StageAt<10 || Ice->CandyHealth<120 || Hero->HasFrozenLegs();
    if(Time-Ice->StartedAt>50 && Stage<120) return false;
    return Stage!=124;
}

void UMCIceEventSmoke::TickColdClient()
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();if(!GS || !RefreshHero()) return;
    if(bRequireColdVFX)
        if(auto* VFX=First<AMCIceEventVFX>(GetWorld());VFX && VFX->IsFinishingPresentation())
        {ClientSeen|=64;bColdCompletionVFXSeen=true;}
    bool Feet=false;
    if(auto* E=First<AMCIceEvent>(GetWorld()))
    {
        ColdObserveVFX(*E);if(bFinished) return;
        if(E->Stage==EMCIceEventStage::Arrival) ClientSeen|=1;if(E->Stage==EMCIceEventStage::Active) ClientSeen|=2;
        for(const auto& Player:E->Players) if(Player.Amount>.1f) ClientSeen|=4;
        if(!E->Strikes.IsEmpty()) ClientSeen|=8;if(E->bNextCircle) ClientSeen|=16;
        if(E->CandyHealth<E->MaxCandyHealth && E->CandyHealth>0) ClientSeen|=32;if(E->IsComplete()) ClientSeen|=64;
        if(!E->ZoneCrystals.IsEmpty()) ClientSeen|=128;if(E->bNovaWarning) ClientSeen|=256;
        if(E->CircleIndex>=5 && E->CircleDuration(E->CircleIndex)==20) ClientSeen|=2048;
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Feet|=It->HasFrozenLegs();
        if(Feet) {ClientSeen|=512;bColdClientHadFeet=true;}
        if(bColdClientHadFeet && !Feet) ClientSeen|=1024;
    }
    if(Hero->HasFrozenLegs()) {bColdClientOwnFeet=true;Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);ColdPrimaryKey(Hero->CanWork());}
    else {if(bColdClientOwnFeet && ColdInputPresses>0) ClientSeen|=4096;ColdPrimaryKey(false);}
    if(GS->DirectorState.CurrentTitle==TEXT("MC_ICE_COLD_DONE"))
    {
        if(bRequireColdVFX && (!bColdVFXSeen || !bColdCompletionVFXSeen || Count<AMCIceEventVFX>(GetWorld())))
        {Fail(TEXT("Remote cold presentation was not observed or survived cancellation"));return;}
        if(ClientSeen!=8191 || Count<AMCIceEvent>(GetWorld()) || Hero->HasFrozenLegs()) {Fail(TEXT("Remote peer missed cold state replication, own native foot rescue or cleanup"));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_PASS role=client remote_seen=%u native_lmb_presses=%d vfx=%d bursts_peak=%d completion_vfx=%d"),ClientSeen,ColdInputPresses,bColdVFXSeen,ColdBurstPeak,bColdCompletionVFXSeen);
        bFinished=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void UMCIceEventSmoke::TickColdRework(float)
{
    if(FPlatformTime::Seconds()-StartedAt>(bCapture?900:450)) {Fail(TEXT("Focused cold rework exceeded its bounded runtime"));return;}
    if(GetWorld()->GetNetMode()==NM_Client) {TickColdClient();return;}
#if WITH_EDITOR
    if(bCapture && !bPrepared && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Time=GetWorld()->GetTimeSeconds();
    if(!bPrepared)
    {
        if(!Mode || !GS || !RefreshHero() || Time<3 || GS->PlayerArray.Num()<ExpectedPlayers) return;
        if(!Mode->CanUseDevPanel(Host)) {Fail(TEXT("Production F3 rejected its host"));return;}
        Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent);Ice=First<AMCIceEvent>(GetWorld());RefreshHero();
        if(!Ice || Ice->bFailed || Ice->CircleSeconds!=45 || Ice->CircleOverlapSeconds!=10 || Ice->MinCircleSeconds!=20 || Ice->IcicleIntervalSeconds!=12)
        {Fail(TEXT("F3 did not start the unmodified production cold rework"));return;}
        if(bCapture) {IFileManager::Get().MakeDirectory(*CaptureFolder,true);FFileHelper::SaveStringToFile(TEXT("frame,server_time,stage\n"),*(CaptureFolder/TEXT("FrameTimes.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
        bPrepared=true;Enter(100,TEXT("COLD_F3_ARRIVAL"));UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS f3_start=1 production_timings=1"));return;
    }
    if(Stage==125)
    {
        if(Time-StageAt>3)
        {
            if(bRequireColdVFX && (!bColdVFXSeen || Count<AMCIceEventVFX>(GetWorld()) || (bCapture && ColdBurstPeak==0)))
            {Fail(TEXT("Cold presentation was not visible or survived cancellation"));return;}
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_PASS role=authority zones=45,40,35,30,25,20 overlap=10 series=3 cooldown=12 blocker_native=1 cone_native=1 self_axe_native=1 dodge=1 hit=1 complete_native=1 cancel_pending=1 native_lmb_presses=%d frames=%d vfx=%d bursts_peak=%d"),ColdInputPresses,Frame,bColdVFXSeen,ColdBurstPeak);bFinished=true;FPlatformMisc::RequestExitWithStatus(false,0);
        }return;
    }
    if(Stage==121)
    {
        ColdPrimaryKey(false);Capture();if(Time-StageAt<2) return;
        if(bRequireColdVFX && Count<AMCIceEventVFX>(GetWorld())) {Fail(TEXT("Completed cold presentation survived its finishing grace"));return;}
        Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent);Ice=First<AMCIceEvent>(GetWorld());RefreshHero();Enter(124,TEXT("COLD_PENDING_CANCEL"));return;
    }
    if(!IsValid(Ice) || Ice->bFailed || !RefreshHero() || !Hero->Status->IsAlive()) {Fail(TEXT("Cold gameplay lost its live event or player"));return;}
    FrameCamera();Capture();
    if(Stage<122) {ColdObserve();if(bFinished) return;ColdParkPeers();}
    if(Stage==100) {Warm();if(Ice->Stage==EMCIceEventStage::Active) {Unsafe();Enter(101,TEXT("COLD_METER_RISE"));}return;}
    if(Stage==101) {Unsafe();FreezePeak=FMath::Max(FreezePeak,Ice->FreezeAmount(Hero));if(Time-StageAt>2.5) {if(FreezePeak<.15) {Fail(TEXT("Cold did not raise the freeze meter"));return;}ThawStart=Ice->FreezeAmount(Hero);Warm();Enter(102,TEXT("COLD_WARMING"));}return;}
    if(Stage==102) {Warm();if(Time-StageAt>2) {if(Ice->FreezeAmount(Hero)>ThawStart-.1) {Fail(TEXT("Warmth did not reduce the meter"));return;}UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS cold=%.3f thaw=%.3f"),FreezePeak,Ice->FreezeAmount(Hero));Enter(103,TEXT("COLD_HAZARDS"));}return;}
    if(Stage==124)
    {
        Warm();if(!Ice->bNovaWarning || Ice->Strikes.IsEmpty()) return;
        Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);ColdPrimaryKey(false);
        if(bRequireColdVFX && Count<AMCIceEventVFX>(GetWorld())) {Fail(TEXT("F3 cancellation retained cold presentation"));return;}
        if(Count<AMCIceEvent>(GetWorld()) || Hero->HasFrozenLegs()) {Fail(TEXT("F3 cancellation retained pending cold gameplay"));return;}
        for(TActorIterator<AMCLocomotionSurface> It(GetWorld());It;++It) if(It->Priority==150 && !It->IsActorBeingDestroyed()) {Fail(TEXT("F3 cancellation retained slippery ice"));return;}
        GS->DirectorState.CurrentTitle=TEXT("MC_ICE_COLD_DONE");GS->ForceNetUpdate();Ice=nullptr;Enter(125,TEXT("COLD_CLEANUP_DONE"));return;
    }
    if(Hero->HasFrozenLegs() && Stage!=106)
    {
        CaseIndex=Stage==120?120:103;InitialHits=Hero->ConfirmedHitCount;InitialSwings=Hero->ValidatedSwingCount;
        if(!Hero->CanWork() || Hero->GetCharacterMovement()->GetMaxSpeed()!=0) {Fail(TEXT("Natural cone freeze did not preserve tool actions while rooting feet"));return;}
        bColdNova=true;Enter(106,TEXT("COLD_SELF_AXE_RESCUE"));
    }
    if(Stage==106)
    {
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);ColdPrimaryKey(true);
        if(!Hero->HasFrozenLegs())
        {
            ColdPrimaryKey(false);
            if(Hero->ConfirmedHitCount<=InitialHits || Hero->ValidatedSwingCount<=InitialSwings || Hero->GetCharacterMovement()->GetMaxSpeed()<=0) {Fail(TEXT("Feet cleared without native axe contacts and restored mobility"));return;}
            bColdRescue=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS cone_feet=1 hands_free=1 self_native_hits=%d mobility_restored=1"),Hero->ConfirmedHitCount-InitialHits);Enter(CaseIndex,CaseIndex==120?TEXT("COLD_CORE_COMBAT"):TEXT("COLD_HAZARDS"));
        }return;
    }
    if(!Hero->CanWork() && Stage!=111) {ColdPrimaryKey(false);return;}
    if(Stage==104)
    {
        const auto* Crystal=Ice->ZoneCrystals.FindByPredicate([this](const FMCIceZoneCrystal& C){return C.ZoneIndex==ColdCrystalZone && C.Health>0;});
        if(!Crystal)
        {
            ColdPrimaryKey(false);if(Hero->ConfirmedHitCount<=InitialHits || !Ice->IsSafePoint(Tongue->GetActorTransform().TransformPosition(StrikeAnchor))) {Fail(TEXT("Crystal cleared without native pickaxe hits and restored warming"));return;}
            bColdBlocker=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS blocker_zone=%d warming_resumed=1 native_hits=%d"),ColdCrystalZone,Hero->ConfirmedHitCount-InitialHits);Enter(CaseIndex,CaseIndex==120?TEXT("COLD_CORE_COMBAT"):TEXT("COLD_HAZARDS"));return;
        }
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Crystal->Anchor);
        Place(Center+FVector(-150,0,0),FVector::ForwardVector);Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);ColdPrimaryKey(true);return;
    }
    if(Stage==105)
    {
        FVector Point;if(ColdWarmPoint(Point) && Ice->IsInsideNova(Point)) Place(Point);
        else {const FVector Center=Tongue->GetActorTransform().TransformPosition(Ice->CandyAnchor),Direction=Tongue->GetActorTransform().TransformVectorNoScale(Ice->NovaDirection).GetSafeNormal2D();Place(Center+Direction*600);}
        if(!Ice->bNovaWarning && Time>StrikeAt+.25 && !Hero->HasFrozenLegs()) Enter(103,TEXT("COLD_HAZARDS"));return;
    }
    if(Stage==110 || Stage==111)
    {
        for(const auto& Strike:Ice->Strikes) if(Strike.Id==StrikeId && !Strike.Anchor.Equals(StrikeAnchor,.01)) {Fail(TEXT("The warned icicle followed player movement"));return;}
        if(Stage==111 && Time<StrikeAt) Place(Tongue->GetActorTransform().TransformPosition(StrikeAnchor));
        if(Time>StrikeAt+.15)
        {
            if(Stage==110) {if(Hero->Status->State.Health<InitialHealth-.1) {Fail(TEXT("Leaving the fixed mark did not dodge the icicle"));return;}bColdDodge=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS fixed_dodge=1 id=%d"),StrikeId);}
            else {if(Hero->Status->State.Health>InitialHealth-Ice->IcicleDamage+.5) {Fail(TEXT("A stationary marked player did not take real icicle damage"));return;}bColdHit=true;UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS stationary_hit=%.1f id=%d"),InitialHealth-Hero->Status->State.Health,StrikeId);}
            Enter(103,TEXT("COLD_HAZARDS"));
        }return;
    }
    if(!Ice->ZoneCrystals.IsEmpty())
    {
        ColdPrimaryKey(false);CaseIndex=Stage==120?120:103;ColdCrystalZone=Ice->ZoneCrystals[0].ZoneIndex;InitialHits=Hero->ConfirmedHitCount;StrikeAnchor=Ice->ZoneCrystals[0].Anchor;
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Ice->ZoneCrystals[0].Anchor);
        const bool bOwnCurrent=ColdCrystalZone==Ice->CircleIndex;
        const bool bOwnerLive=bOwnCurrent || (Ice->bNextCircle && ColdCrystalZone==Ice->CircleIndex+1);
        const bool bOtherLive=bOwnCurrent?Ice->bNextCircle:true;
        const int32 OtherIndex=bOwnCurrent?Ice->CircleIndex+1:Ice->CircleIndex;
        const FVector OtherAnchor=bOwnCurrent?Ice->NextSafeAnchor:Ice->SafeAnchor;
        const float OtherRadius=bOwnCurrent?Ice->NextSafeRadius():Ice->SafeRadius();
        FHitResult OtherFloor;
        const bool bOtherCovers=bOtherLive && !Ice->IsCircleBlocked(OtherIndex)
            && Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(OtherAnchor),OtherFloor)
            && FVector::DistSquaredXY(Center,OtherFloor.ImpactPoint)<=FMath::Square(OtherRadius)
            && FMath::Abs(Center.Z-OtherFloor.ImpactPoint.Z)<150;
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_BLOCKER_OBSERVE owner=%d current=%d next=%d owner_blocked=%d union_warm=%d other_covers=%d current_radius=%.1f next_radius=%.1f separation=%.1f crystal=%s current_center=%s next_center=%s"),
            ColdCrystalZone,Ice->CircleIndex,Ice->bNextCircle,Ice->IsCircleBlocked(ColdCrystalZone),Ice->IsSafePoint(Center),bOtherCovers,Ice->SafeRadius(),Ice->NextSafeRadius(),
            FVector::DistXY(Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor),Tongue->GetActorTransform().TransformPosition(Ice->NextSafeAnchor)),
            *Center.ToString(),*Tongue->GetActorTransform().TransformPosition(Ice->SafeAnchor).ToString(),*Tongue->GetActorTransform().TransformPosition(Ice->NextSafeAnchor).ToString());
        if(!bOwnerLive || !Ice->IsCircleBlocked(ColdCrystalZone) || (Ice->IsSafePoint(Center) && !bOtherCovers))
        {Fail(TEXT("A live crystal did not disable its owning zone or unexplained warmth remained"));return;}
        Enter(104,TEXT("COLD_BLOCKED_ZONE_AXE"));return;
    }
    if(Stage==120)
    {
        if(Ice->IsComplete())
        {
            ColdPrimaryKey(false);if(Ice->CandyHealth!=0 || !Ice->Players.IsEmpty() || !Ice->Strikes.IsEmpty() || !Ice->ZoneCrystals.IsEmpty() || Ice->bNovaWarning || Hero->HasFrozenLegs()) {Fail(TEXT("Native core completion retained cold gameplay"));return;}
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS native_core_complete=1 max_health=%.1f"),Ice->MaxCandyHealth);
            if(bRequireColdVFX)
            {
                Ice->Destroy();Ice=nullptr;
                auto* VFX=First<AMCIceEventVFX>(GetWorld());
                if(!VFX || !VFX->IsFinishingPresentation()) {Fail(TEXT("Immediate event removal killed the success presentation"));return;}
                UE_LOG(LogTemp,Display,TEXT("MC_ICE_COLD_CHECK PASS success_vfx_survived_immediate_remove=1"));
            }
            Enter(121,TEXT("COLD_CORE_COMPLETE"));return;
        }
        if(Ice->FreezeAmount(Hero)>.45f) bResting=true;else if(Ice->FreezeAmount(Hero)<.025f) bResting=false;
        const FVector Center=Tongue->GetActorTransform().TransformPosition(Ice->CandyAnchor),Approach=Center+FVector(-270,0,0);
        bool Danger=false;for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-Time<1.6 && FVector::DistSquaredXY(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Approach)<FMath::Square(Ice->IcicleRadius+90)) Danger=true;
        if(bResting || Danger) {ColdPrimaryKey(false);FVector Point;if(ColdWarmPoint(Point)) Place(Point);}
        else {Place(Approach,FVector::ForwardVector);Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);ColdPrimaryKey(true);}return;
    }
    if(Stage==103)
    {
        ColdPrimaryKey(false);
        if(!bColdNova && Ice->bNovaWarning) {StrikeAt=Ice->NovaImpactAt;Enter(105,TEXT("COLD_CONE_WARNING"));return;}
        if(!bColdDodge || !bColdHit) for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-Time>.5 && (!bColdDodge || Strike.Id%3==0) && FVector::DistSquaredXY(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Hero->GetActorLocation())<FMath::Square(Ice->IcicleRadius+80))
        {
            StrikeId=Strike.Id;StrikeAnchor=Strike.Anchor;StrikeAt=Strike.ImpactAt;InitialHealth=Hero->Status->State.Health;
            if(!bColdDodge) {FVector Escape;if(!ColdWarmPoint(Escape)) continue;Place(Escape);Enter(110,TEXT("COLD_FIXED_ICICLE_DODGE"));}
            else Enter(111,TEXT("COLD_STATIONARY_ICICLE_HIT"));return;
        }
        FVector Point;if(ColdWarmPoint(Point)) Place(Point);
        if(bColdDodge && bColdHit && bColdBlocker && bColdNova && bColdRescue && bColdOverlap && bColdMinimum && bColdSeries)
        {InitialHits=Hero->ConfirmedHitCount;InitialSwings=Hero->ValidatedSwingCount;Enter(120,TEXT("COLD_CORE_COMBAT"));}
    }
}

void UMCIceEventSmoke::Tick(float Dt)
{
    if(bFinished) return;
    if(bColdRework) {TickColdRework(Dt);return;}
    if(FPlatformTime::Seconds()-StartedAt>(bCapture?900:240)) {Fail(TEXT("Runtime smoke timeout"));return;}
    if(GetWorld()->GetNetMode()==NM_Client) {TickClient();return;}
#if WITH_EDITOR
    if(bCapture && GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Time=GetWorld()->GetTimeSeconds();
    if(!bPrepared)
    {
        if(!Mode || !GS || !RefreshHero() || Time<3 || GS->PlayerArray.Num()<ExpectedPlayers) return;
        if(!Mode->CanUseDevPanel(Host)) {Fail(TEXT("Host F3 authority guard rejected host"));return;}
        if(ExpectedPlayers>1)
            for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
                if(It->Get()!=Host && Mode->CanUseDevPanel(It->Get())) {Fail(TEXT("F3 host guard allowed remote player"));return;}
        if(auto* Plan=Mode->FirstDayPlan.LoadSynchronous())
            for(int32 I=0;I<Plan->Steps.Num();++I)
                if(Plan->Steps[I].Step!=EMCDayStep::Complete && Plan->Steps[I].Step!=EMCDayStep::DiscardBrushes) SavedSteps.Add(I);
        bPrepared=true;Enter(1,TEXT("F3_EVENTS"));StartF3Case();return;
    }
    if(Stage==1)
    {
        if(Time-StageAt<.3) return;
        if(!CheckF3Case()) {Fail(ActionName(Actions[CaseIndex]));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_F3_CHECK PASS action=%s"),ActionName(Actions[CaseIndex]));
        if(Actions[CaseIndex]==EMCDevAction::NutEncounter) {Mode->ExecuteDevAction(Host,EMCDevAction::StopNutEncounter);if(Count<AMCNutRainEvent>(GetWorld())) {Fail(TEXT("Nut stop retained event actor"));return;}}
        if(Actions[CaseIndex]==EMCDevAction::IceEvent) {Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);if(Count<AMCIceEvent>(GetWorld())) {Fail(TEXT("Ice stop retained event actor"));return;}}
        Mode->ExecuteDevAction(Host,EMCDevAction::BossRemove);Mode->ExecuteDevAction(Host,EMCDevAction::BossPhase3Remove);Mode->ExecuteDevAction(Host,EMCDevAction::MimicRemove);
        if(++CaseIndex<Actions.Num()) {StartF3Case();return;}
        Enter(2,TEXT("F3_SAVED_STEPS"));return;
    }
    if(Stage==2)
    {
        if(StepCase<SavedSteps.Num())
        {
            const int32 Index=SavedSteps[StepCase++];Mode->ExecuteDevAction(Host,EMCDevAction::StartStep,Index);
            if(!Mode->DayDirector || GS->StepIndex!=Index || !GS->bDevManualEvents) {Fail(TEXT("Saved F3 stage did not start"));return;}
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_F3_CHECK PASS action=StartStep index=%d"),Index);return;
        }
        if(bCapture)
        {
            IFileManager::Get().MakeDirectory(*CaptureFolder,true);
            FFileHelper::SaveStringToFile(TEXT("frame,server_time,stage\n"),*(CaptureFolder/TEXT("FrameTimes.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            Host->ToggleDevPanel();bPanelOpen=true;
            GetWorld()->GetWorldSettings()->SetTimeDilation(.20f);
        }
        Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent);Ice=First<AMCIceEvent>(GetWorld());RefreshHero();
        if(!Ice || Ice->bFailed) {Fail(TEXT("Winter production start failed"));return;}
        FrameCamera();Enter(10,TEXT("F3_START_ARRIVAL"));return;
    }
    if(bCapture && Stage!=23) FrameCamera();
    Capture();
    if(Stage==23)
    {
        if(Time-StageAt>3)
        {
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_SMOKE_PASS role=authority f3_events=%d saved_steps=%d freeze=1 dodge=1 damage=1 transfer=1 death=1 respawn=1 real_swings=1 reset=1 frames=%d"),Actions.Num(),SavedSteps.Num(),Frame);
            bFinished=true;FPlatformMisc::RequestExitWithStatus(false,0);
        }
        return;
    }
    if(!Ice || Ice->bFailed) {Fail(TEXT("Winter ended unexpectedly"));return;}
    bSawNextCircle|=Ice->bNextCircle;bSawTransfer|=Ice->CircleIndex>0;
    ParkOtherPlayers();
    if(Stage==10)
    {
        Warm();
        if(bPanelOpen && Time-StageAt>1) {Host->ToggleDevPanel();bPanelOpen=false;}
        if(Ice->Stage==EMCIceEventStage::Active) {Enter(11,TEXT("FREEZE_OUTSIDE"));InitialHealth=Hero->Status->State.Health;Unsafe();}return;
    }
    if(Stage==11)
    {
        if(!Unsafe()) {Fail(TEXT("No supported cold floor outside warm circle"));return;}
        FreezePeak=FMath::Max(FreezePeak,Ice->FreezeAmount(Hero));
        if(Time-StageAt>=2.7)
        {
            if(FreezePeak<.15f) {Fail(TEXT("Cold floor did not raise freeze bar"));return;}
            ThawStart=Ice->FreezeAmount(Hero);Warm();Enter(12,TEXT("THAW_INSIDE"));
        }return;
    }
    if(Stage==12)
    {
        Warm();
        if(Time-StageAt>=2)
        {
            if(Ice->FreezeAmount(Hero)>ThawStart-.1f) {Fail(TEXT("Warm circle did not reduce freeze bar"));return;}
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS freeze_peak=%.3f thaw=%.3f"),FreezePeak,Ice->FreezeAmount(Hero));
            Enter(13,TEXT("ICICLE_WARNING_DODGE"));
        }return;
    }
    if(Stage==13 || Stage==15)
    {
        Warm();
        for(const auto& Strike:Ice->Strikes)
        {
            const FVector World=Tongue->GetActorTransform().TransformPosition(Strike.Anchor);
            if(!Strike.bImpacted && Strike.ImpactAt-Time>.7 && FVector::DistSquared2D(World,Hero->GetActorLocation())<FMath::Square(Ice->IcicleRadius+80))
            {
                StrikeId=Strike.Id;StrikeAnchor=Strike.Anchor;StrikeAt=Strike.ImpactAt;InitialHealth=Hero->Status->State.Health;
                if(Stage==13)
                {
                    bool Escaped=false;
                    for(int32 I=0;I<16;++I)
                    {
                        const float A=I*UE_TWO_PI/16;FHitResult Floor;
                        const FVector Away=World+FVector(FMath::Cos(A),FMath::Sin(A),0)*(Ice->IcicleRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius()+180);
                        if(Tongue->InteriorSurfacePoint(Away,60,Floor) && Place(Floor.ImpactPoint)) {Escaped=true;break;}
                    }
                    if(!Escaped) {Fail(TEXT("No supported icicle escape point"));return;}
                    Enter(14,TEXT("ICICLE_LOCKED_DODGE"));
                }
                else {Place(World);Enter(16,TEXT("ICICLE_REAL_IMPACT"));}
                return;
            }
        }
        return;
    }
    if(Stage==14 || Stage==16)
    {
        for(const auto& Strike:Ice->Strikes) if(Strike.Id==StrikeId && !Strike.Anchor.Equals(StrikeAnchor,.01)) {Fail(TEXT("Warning target followed moving hero"));return;}
        if(Stage==16 && Time<StrikeAt) Place(Tongue->GetActorTransform().TransformPosition(StrikeAnchor));
        if(Time>StrikeAt+.25)
        {
            if(Stage==14)
            {
                if(Hero->Status->State.Health<InitialHealth-.1) {Fail(TEXT("Icicle damaged hero outside locked target"));return;}
                UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS fixed_target_dodge id=%d"),StrikeId);Enter(15,TEXT("ICICLE_WARNING_HIT"));
            }
            else
            {
                if(Hero->Status->State.Health>InitialHealth-Ice->IcicleDamage+.5) {Fail(TEXT("Real icicle failed to damage target"));return;}
                UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS strike_damage=%.1f"),InitialHealth-Hero->Status->State.Health);
                Enter(17,TEXT("CIRCLE_TRANSFER"));
            }
        }return;
    }
    if(Stage==17)
    {
        Warm();
        if(bSawNextCircle && bSawTransfer && Time-StageAt>3)
        {
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS circle_preview_transfer index=%d"),Ice->CircleIndex);
            Enter(18,TEXT("FREEZE_LETHAL"));Unsafe();
        }return;
    }
    if(Stage==18)
    {
        FreezePeak=FMath::Max(FreezePeak,Ice->FreezeAmount(Hero));
        if(Hero->Status->IsAlive()) {Unsafe();return;}
        if(FreezePeak<.99f) {Fail(TEXT("Player died before freeze bar filled"));return;}
        UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS freeze_death freeze=%.3f"),FreezePeak);
        Enter(19,TEXT("NORMAL_RESPAWN"));return;
    }
    if(Stage==19)
    {
        auto* New=Host?Cast<AMCToothCharacter>(Host->GetPawn()):nullptr;
        if(New && New!=Hero && New->Status->IsAlive())
        {
            Hero=New;Warm();UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS normal_respawn"));
            // A normal F3 restart provides an undamaged player and complete production candy health.
            Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent);Ice=First<AMCIceEvent>(GetWorld());RefreshHero();
            FrameCamera();Enter(20,TEXT("PICKAXE_ARRIVAL"));
        }
        else if(Time-StageAt>15) Fail(TEXT("Normal death flow did not respawn player"));
        return;
    }
    if(Stage==20)
    {
        Warm();if(Ice->Stage==EMCIceEventStage::Active)
        {
            Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);InitialSwings=Hero->ValidatedSwingCount;InitialHits=Hero->ConfirmedHitCount;
            Enter(21,TEXT("PICKAXE_COMBAT"));
        }return;
    }
    if(Stage==21)
    {
        if(!Hero->Status->IsAlive()) {Fail(TEXT("Combat scaffold failed to dodge or warm"));return;}
        if(Ice->IsComplete())
        {
            Hero->SetPrimaryInputHeld(false);
            if(Ice->CandyHealth!=0 || Hero->ValidatedSwingCount<=InitialSwings || Hero->ConfirmedHitCount<=InitialHits) {Fail(TEXT("Candy completed without real tool contacts"));return;}
            if(!Ice->Players.IsEmpty() || !Ice->Strikes.IsEmpty() || Ice->Body->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) {Fail(TEXT("Completed winter retained gameplay"));return;}
            UE_LOG(LogTemp,Display,TEXT("MC_ICE_CHECK PASS real_pickaxe_complete swings=%d hits=%d max_health=%.1f"),Hero->ValidatedSwingCount-InitialSwings,Hero->ConfirmedHitCount-InitialHits,Ice->MaxCandyHealth);
            Enter(22,TEXT("WINTER_COMPLETE"));return;
        }
        const float Freeze=Ice->FreezeAmount(Hero);
        if(Freeze>.45f) bResting=true; else if(Freeze<.025f) bResting=false;
        FVector Point=Tongue->GetActorTransform().TransformPosition(Ice->CandyAnchor)+FVector(-115,0,0);
        FHitResult Floor;
        // The mint is a vertical disc; approach its thin X side on the real tongue floor.
        bool Danger=false;
        for(const auto& Strike:Ice->Strikes) if(!Strike.bImpacted && Strike.ImpactAt-Time<2.1)
            if(FVector::DistSquared2D(Tongue->GetActorTransform().TransformPosition(Strike.Anchor),Point)<FMath::Square(Ice->IcicleRadius+100)) Danger=true;
        if(bResting || Danger || !Hero->CanWork()) {Hero->SetPrimaryInputHeld(false);Warm();}
        else if(Tongue->SurfacePoint(Point,Floor))
        {
            Place(Floor.ImpactPoint,Ice->GetActorLocation()-Point);Hero->SetPrimaryInputHeld(true);
        }
        else {Fail(TEXT("Candy has no supported tool approach"));return;}
        if(Time-StageAt>90) Fail(TEXT("Real pickaxe swings did not finish mint within bounded combat"));
        return;
    }
    if(Stage==22)
    {
        if(Time-StageAt<2) return;
        Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);
        if(Count<AMCIceEvent>(GetWorld())) {Fail(TEXT("F3 stop left completed winter actor"));return;}
        Mode->ExecuteDevAction(Host,EMCDevAction::RestartDay);RefreshHero();
        if(Count<AMCIceEvent>(GetWorld()) || Count<AMCNutRainEvent>(GetWorld())) {Fail(TEXT("Full restart retained key events"));return;}
        // Stop the autonomous sequence while peers receive the completion sentinel.
        Mode->ExecuteDevAction(Host,EMCDevAction::IceEvent);Mode->ExecuteDevAction(Host,EMCDevAction::StopIceEvent);RefreshHero();
        GS->DirectorState.CurrentTitle=TEXT("MC_ICE_SMOKE_DONE");GS->ForceNetUpdate();
        Ice=nullptr;Enter(23,TEXT("STOP_RESET_CLEAN"));return;
    }
}
