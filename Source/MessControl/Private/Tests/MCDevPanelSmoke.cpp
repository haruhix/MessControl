#include "MCValidationSubsystem.h"
#include "MCPlayerController.h"
#include "MCDevPanelWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

void UMCValidationSubsystem::TickDevPanel(float Dt)
{
#if !UE_BUILD_SHIPPING
    Age+=Dt;
    auto* GS=GetWorld()->GetGameState<AMCGameState>();
    auto* PC=Cast<AMCPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!GS || !PC) return;
    const bool Host=GetWorld()->GetNetMode()!=NM_Client;
    int32 Expected=1; FParse::Value(FCommandLine::Get(),TEXT("MCExpectedPlayers="),Expected);
    auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn());
    if (Host && DevStartedAt<0 && GS->PlayerArray.Num()>=Expected && Hero && Age>3)
        DevStartedAt=GS->GetServerWorldTimeSeconds()+3;
    if (!Host && DevStartedAt<0 && GS->bDevManualEvents) DevStartedAt=GS->DayStartedAt;
    const double T=DevStartedAt>=0?GS->GetServerWorldTimeSeconds()-DevStartedAt:-1;
    if (!Host && !bDevClientGuard && GS->bDevManualEvents)
    {
        const float HP=GS->MouthHealth;
        PC->RequestDevAction(EMCDevAction::RestoreMouth);
        bDevClientGuard=!PC->CanUseDevPanel() && GS->MouthHealth==HP;
    }
    auto Start=[&](EMCDayStep Step)
    {
        auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
        const auto* Plan=Mode->FirstDayPlan.LoadSynchronous();
        PC->RequestDevAction(EMCDevAction::StartStep,Plan->Steps.IndexOfByPredicate([Step](const FMCDayStepSettings& S){return S.Step==Step;}));
    };
    if (Host && T>=0)
    {
        if (DevStage==0) { Start(EMCDayStep::BreakfastRain); PC->ToggleDevPanel(); ++DevStage; }
        if (DevStage==1 && T>3)
        {
            if (FParse::Param(FCommandLine::Get(),TEXT("MCDevPanelCapture")))
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Artifacts/DevPanel.png"),true,false);
            ++DevStage;
        }
        if (DevStage==2 && T>6) { PC->RequestDevAction(EMCDevAction::Infection); ++DevStage; }
        if (DevStage==3 && T>11) { Start(EMCDayStep::CoffeeWaves); ++DevStage; }
        if (DevStage==4 && T>13) { PC->RequestDevAction(EMCDevAction::CoffeeDirt); ++DevStage; }
        if (DevStage==5 && T>18) { PC->RequestDevAction(EMCDevAction::StopCoffee); Start(EMCDayStep::StuckFood); ++DevStage; }
        if (DevStage==6 && T>22) { PC->RequestDevAction(EMCDevAction::KillSelf); ++DevStage; }
        if (DevStage==7 && T>29) { PC->RequestDevAction(EMCDevAction::RestartDay); PC->ToggleDevPanel(); ++DevStage; }
    }
    int32 Food=0,Ulcers=0,Stuck=0; bool Flood=false;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed() && !It->bBrushTool) { ++Food; if (It->Phase==EMCFoodPhase::Stuck) ++Stuck; }
    for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->bUlcer) ++Ulcers;
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) Flood|=It->IsActive();
    if (GS->bDevManualEvents && Food>0) DevSeen|=1;
    if (Ulcers>0) DevSeen|=2;
    if (GS->bDevManualEvents && Flood) DevSeen|=4;
    Hero=Cast<AMCToothCharacter>(PC->GetPawn());
    if (Hero && Hero->Status->State.CoffeeLeft>0) DevSeen|=8;
    if (Stuck>0) DevSeen|=16;
    // Authored arena sockets may differ from the configured ideal count during blockout work.
    if (T>22 && GS->ArenaTeeth.Num()>0 && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()-1) DevSeen|=32;
    if (T>29 && !GS->bDevManualEvents && GS->ArenaTeeth.Num()>0 && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()) DevSeen|=64;
    if (Age>=NextLog) { NextLog+=5; UE_LOG(LogTemp,Display,TEXT("MC_DEV_PANEL net=%d seen=%d stage=%d t=%.1f"),int32(GetWorld()->GetNetMode()),DevSeen,DevStage,T); }
    // Let clients verify the last replicated reset before closing the host connection.
    if (T>(Host?38:33) || Age>85)
    {
        const bool Pass=DevSeen==127 && (Host || bDevClientGuard);
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s DEV_PANEL net=%d seen=%d clientGuard=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,bDevClientGuard);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
