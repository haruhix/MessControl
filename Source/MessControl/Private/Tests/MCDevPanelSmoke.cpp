#include "MCValidationSubsystem.h"
#include "MCPlayerController.h"
#include "MCDevPanelWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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
        // Ordinary food now spoils without creating a lesion. Wait for the real
        // accelerated egg and its replication before resetting the event.
        if (DevStage==3 && T>17 && (DevSeen&2)) { Start(EMCDayStep::CoffeeWaves); ++DevStage; }
        if (DevStage==4 && T>20) { PC->RequestDevAction(EMCDevAction::CoffeeDirt); PC->RequestDevAction(EMCDevAction::SwimCoffee); ++DevStage; }
        if (DevStage==5 && T>28) { PC->RequestDevAction(EMCDevAction::StopCoffee); Start(EMCDayStep::StuckFood); ++DevStage; }
        if (DevStage==6 && T>32) { PC->RequestDevAction(EMCDevAction::KillSelf); ++DevStage; }
        if (DevStage==7 && T>39) { PC->RequestDevAction(EMCDevAction::RestartDay); PC->ToggleDevPanel(); ++DevStage; }
    }
    int32 Food=0,Stuck=0; bool Flood=false,SpoiledEgg=false;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->IsDisposed() && !It->bBrushTool) {
        ++Food; if (It->Phase==EMCFoodPhase::Stuck) ++Stuck;
        SpoiledEgg|=It->Batch==2 && It->ItemName==TEXT("Egg") && It->bSpoiled;
    }
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) Flood|=It->IsActive();
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It)
        if(It->GetPhase()==EMCCoffeePhase::Holding && It->WaterSettings.HoldSeconds==600 && !It->Jet->IsVisible() && !It->DrainRibbon->IsVisible()) DevSeen|=128;
    if (GS->bDevManualEvents && Food>0) DevSeen|=1;
    if (SpoiledEgg) DevSeen|=2;
    if (GS->bDevManualEvents && Flood) DevSeen|=4;
    Hero=Cast<AMCToothCharacter>(PC->GetPawn());
    if (Hero && Hero->Status->State.CoffeeLeft>0) DevSeen|=8;
    if (Hero && Hero->GetCharacterMovement()->IsSwimming() && Hero->AnimationSwim>.8f) DevSeen|=256;
    if (!(DevSeen&512) && T>20 && T<28 && GS->bDevManualEvents)
    {
        TArray<AMCMouthSurface*> Dirt;
        for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (!It->bUlcer && !It->IsClean()) Dirt.Add(*It);
        const int32 ExpectedDirt=GS->DayPlan?FMath::Clamp(GS->DayPlan->SurfacePatches,1,24):0;
        bool Interior=Dirt.Num()==ExpectedDirt && Dirt.Num()>1,Separated=true;
        double MinimumSeparation=DBL_MAX;
        for (int32 I=0;I<Dirt.Num();++I)
        {
            auto* Patch=Dirt[I]; auto* Tongue=Patch->GetTongue();
            const auto* Section=Patch->Liquid?Patch->Liquid->GetProcMeshSection(0):nullptr;
            const int32 Count=Section?Section->ProcVertexBuffer.Num():0;
            const int32 Width=FMath::RoundToInt(FMath::Sqrt(float(Count)));
            Interior&=Tongue && Count>0 && Width*Width==Count;
            if (Tongue && Count>0 && Width*Width==Count)
            {
                // Inspect the actual conformed liquid corners and center, rather than only its actor origin.
                const int32 Samples[]={0,Width-1,Count-Width,Count-1,Count/2};
                for (int32 Sample:Samples)
                {
                    const FVector P=Patch->Liquid->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[Sample].Position);
                    FHitResult Floor;
                    Interior&=Tongue->SurfacePoint(P,Floor) && Floor.ImpactNormal.Z>.65f && FVector::Dist(P,Floor.ImpactPoint)<3;
                }
            }
            for (int32 J=0;J<I;++J)
            {
                const double Distance=FVector::Dist2D(Patch->GetActorLocation(),Dirt[J]->GetActorLocation());
                MinimumSeparation=FMath::Min(MinimumSeparation,Distance);
                Separated&=Distance>=FMath::Min(Patch->LiquidHalfSize,Dirt[J]->LiquidHalfSize)*1.6+90;
            }
        }
        if (Interior && Separated)
        {
            DevSeen|=512;
            UE_LOG(LogTemp,Display,TEXT("MC_DIRT_LAYOUT PASS net=%d patches=%d interior=1 separated=1 min_distance=%.1f"),int32(GetWorld()->GetNetMode()),Dirt.Num(),MinimumSeparation);
        }
        else if (Age>=NextLog)
        {
            UE_LOG(LogTemp,Display,TEXT("MC_DIRT_LAYOUT pending net=%d patches=%d expected=%d interior=%d separated=%d"),int32(GetWorld()->GetNetMode()),Dirt.Num(),ExpectedDirt,Interior,Separated);
        }
    }
    if (Stuck>0) DevSeen|=16;
    // Authored arena sockets may differ from the configured ideal count during blockout work.
    if (T>32 && GS->ArenaTeeth.Num()>0 && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()-1) DevSeen|=32;
    if (T>39 && !GS->bDevManualEvents && GS->ArenaTeeth.Num()>0 && GS->AvailableArenaTeeth()==GS->ArenaTeeth.Num()) DevSeen|=64;
    if (Age>=NextLog) { NextLog+=5; UE_LOG(LogTemp,Display,TEXT("MC_DEV_PANEL net=%d seen=%d stage=%d t=%.1f"),int32(GetWorld()->GetNetMode()),DevSeen,DevStage,T); }
    // Let clients verify the last replicated reset before closing the host connection.
    if (T>(Host?48:44) || Age>95)
    {
        const bool Pass=DevSeen==1023 && (Host || bDevClientGuard);
        UE_LOG(LogTemp,Display,TEXT("MC_VALIDATION_%s DEV_PANEL net=%d seen=%d clientGuard=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),int32(GetWorld()->GetNetMode()),DevSeen,bDevClientGuard);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    }
#endif
}
