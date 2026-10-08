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
#include "MCInventoryComponent.h"
#include "MCLocomotionSurface.h"
#include "MCMouthSurface.h"
#include "MCNutRainEvent.h"
#include "MCPlayerController.h"
#include "MCRewardChest.h"
#include "MCRoguelikeDirector.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
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
    return FParse::Param(FCommandLine::Get(),TEXT("MCIceEventSmoke")) && Super::ShouldCreateSubsystem(Outer);
#else
    return false;
#endif
}

void UMCIceEventSmoke::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); StartedAt=FPlatformTime::Seconds();
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
    UE_LOG(LogTemp,Error,TEXT("MC_ICE_SMOKE_FAIL role=%s stage=%d case=%d reason=%s health=%.1f freeze=%.3f candy=%.1f"),
        GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("authority"),Stage,CaseIndex,Reason,
        Hero?Hero->Status->State.Health:-1,Ice?Ice->FreezeAmount(Hero):-1,Ice?Ice->CandyHealth:-1);
    FPlatformMisc::RequestExitWithStatus(false,1);
}

void UMCIceEventSmoke::Enter(int32 Next,const TCHAR* Label)
{
    Stage=Next; StageAt=GetWorld()->GetTimeSeconds(); StageLabel=Label;
    UE_LOG(LogTemp,Display,TEXT("MC_ICE_STAGE stage=%d label=%s server_time=%.3f"),Stage,Label,StageAt);
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
    const FVector Aim=FMath::Lerp(Candy,(Player+Safe)*.5f,.4f)+FVector(0,0,FMath::Lerp(360.f,120.f,Arrival));
    const FVector Eye=Aim+FVector(-1250,-950,900);
    CaptureCamera->SetActorLocationAndRotation(Eye,(Aim-Eye).Rotation());
    CaptureCamera->GetCameraComponent()->SetFieldOfView(60);
    Host->SetViewTarget(CaptureCamera);
}

void UMCIceEventSmoke::Capture()
{
    if(!bCapture || Stage<10 || !Host || !Host->IsLocalController()) return;
    const double Time=GetWorld()->GetTimeSeconds(); if(Time<NextCaptureAt || FScreenshotRequest::IsScreenshotRequested()) return;
    // PNG readback can stall wall time. Slow only this visual probe and sample each rendered frame.
    NextCaptureAt=Time+.045;
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

void UMCIceEventSmoke::Tick(float Dt)
{
    if(bFinished) return;
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
