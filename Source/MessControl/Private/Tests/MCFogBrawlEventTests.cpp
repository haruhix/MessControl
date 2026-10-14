#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFogBrawlEvent.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "ProceduralMeshComponent.h"

namespace MCFogBrawlTestsPrivate
{
struct FFogWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCFogBrawlEvent* Event=nullptr;
    AMCArenaTooth* Tooth=nullptr;
    TArray<AMCToothCharacter*> Crew;
    TArray<AMCArenaTooth*> Teeth;
    FFogWorld(int32 TeamSize=1,bool bReserve=true)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL); World->InitializeActorsForPlay(URL); World->BeginPlay();
        Box(FVector(0,0,-10),FVector(2500,2500,10));
        if(bReserve) for(int32 I=0;I<8;++I) {
            auto* T=Reserve(FVector(-750+(I/2)*500,I%2?500:-500,100),I+1);if(!Tooth) Tooth=T;
        }
        for(int32 I=0;I<TeamSize;++I)
        {
            const float Angle=I*2*PI/FMath::Max(1,TeamSize);
            auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(220*FMath::Cos(Angle),220*FMath::Sin(Angle),61),FRotator::ZeroRotator);
            auto* Controller=World->SpawnActor<APlayerController>();
            if(!Controller->GetPlayerState<APlayerState>()) Controller->InitPlayerState();
            Controller->Possess(Hero);
            Hero->SetActorTickEnabled(false); Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Hero->GetMesh()->SetComponentTickEnabled(false); Hero->ToothPhysics->SetComponentTickEnabled(false);
            // Keep recoil below the fall threshold while checking target allocation independently.
            Hero->ToothPhysics->Settings.FallThreshold=1400;
            Hero->Grip->SetComponentTickEnabled(false); Crew.Add(Hero);
        }
        Event=World->SpawnActor<AMCFogBrawlEvent>();
    }
    ~FFogWorld()
    {
        World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AActor* Box(FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
    }
    AMCArenaTooth* Reserve(FVector Center,int32 Id)
    {
        auto* Reserve=World->SpawnActor<AMCArenaTooth>(Center,FRotator::ZeroRotator);
        FMCArenaToothSettings Settings; Settings.InitialCalculusEveryNthTooth=0; Reserve->Initialize(Id,Settings);
        Reserve->SetActorTickEnabled(false); Reserve->Body->SetSimulatePhysics(false);
        Reserve->Body->SetBoxExtent(FVector(80,80,100)); Teeth.Add(Reserve); return Reserve;
    }
    void Clock(double Time)
    {
        World->GetWorldSettings()->MaxUndilatedFrameTime=3600;
        ++GFrameCounter; World->Tick(LEVELTICK_TimeOnly,FMath::Max(.001f,float(Time-World->GetTimeSeconds())));
    }
    void Selection()
    {
        Event->Start(); Event->SetActorTickEnabled(false);
        Clock(Event->StageEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
    void Warning()
    {
        Selection(); Clock(Event->StageEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
    void Guard(AMCToothCharacter* Hero,AMCArenaTooth* Target)
    {
        const float Side=AMCFogBrawlEvent::ToothSide(Target)==EMCFogBrawlSide::Right?-1.f:1.f;
        Hero->SetActorLocation(Target->GetActorLocation()+FVector(0,Side*220,-39));
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Hero->SetBraceInputHeld(true);
    }
    void Impact()
    {
        Clock(Event->WarningEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
    void NextWarning()
    {
        Clock(Event->StageEndsAt+.01); Event->Tick(.01f); Event->SetActorTickEnabled(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogSelectionCountTest,"MessControl.FogBrawl.PlayerCountAndBothSides",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogSelectionCountTest::RunTest(const FString&)
{
    for(int32 Players=1;Players<=4;++Players) {
        MCFogBrawlTestsPrivate::FFogWorld F(Players);F.Warning();
        TestEqual(TEXT("Exactly one distinct target per participant"),F.Event->Targets.Num(),Players);
        TestEqual(TEXT("The round records the current participant count"),F.Event->RoundPlayerCount,Players);
        TSet<AMCArenaTooth*> Unique;int32 Right=0,Left=0;
        for(const auto& T:F.Event->Targets) {Unique.Add(T.Tooth);T.Side==EMCFogBrawlSide::Right?++Right:++Left;}
        TestEqual(TEXT("Random choices never repeat a tooth"),Unique.Num(),Players);
        if(Players>1) {
            TestTrue(TEXT("Two or more players always get both sides"),Right>0 && Left>0);
            TestEqual(TEXT("The right side always attacks first"),F.Event->CurrentSide,EMCFogBrawlSide::Right);
        }
    }
    for(int32 Side=0;Side<2;++Side) {
        MCFogBrawlTestsPrivate::FFogWorld F(1,false);
        auto* Only=F.Reserve(FVector(0,Side?500:-500,100),Side?2:1);F.Warning();
        TestTrue(TEXT("Solo can select a tooth on either side without inventing a second target"),F.Event->Targets.Num()==1 && F.Event->ActiveTarget==Only);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogSelectionScanTest,"MessControl.FogBrawl.AllAvailableTeethScanAndAccelerate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogSelectionScanTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F(4);F.Teeth[3]->ConsumeForRespawn();F.Selection();
    TestEqual(TEXT("Consumed teeth are excluded; every available reserve participates"),F.Event->SelectionTeeth.Num(),7);
    TestEqual(TEXT("Selection is a distinct stage before random targets are announced"),F.Event->Stage,EMCFogBrawlStage::Selection);
    TestTrue(TEXT("No target is prematurely locked during the scan"),F.Event->Targets.IsEmpty() && F.Event->ActiveTarget==nullptr);
    const double End=F.Event->StageEndsAt,Start=End-F.Event->SelectionSeconds;
    TSet<AMCArenaTooth*> Seen;int32 Early=0,Late=0;
    for(int32 I=0;I<200;++I) {
        F.Clock(Start+F.Event->SelectionSeconds*I/200.);
        if(auto* Tooth=F.Event->SelectionTooth()) Seen.Add(Tooth);
        if(I==20) Early=F.Event->SelectionStep();
        if(I==40) Early=F.Event->SelectionStep()-Early;
        if(I==160) Late=F.Event->SelectionStep();
        if(I==180) Late=F.Event->SelectionStep()-Late;
    }
    TestEqual(TEXT("The moving glow actually visits all seven eligible teeth"),Seen.Num(),7);
    TestTrue(TEXT("The same time window contains more switches at the end"),Late>Early);
    F.Clock(End+.01);F.Event->Tick(.01f);
    TestEqual(TEXT("The scan ends with four exact choices"),F.Event->Targets.Num(),4);
    TestTrue(TEXT("The first impact is scheduled 3.5 seconds after roulette selection"),FMath::IsNearlyEqual(F.Event->WarningRemaining(),3.5f,.001f));
    F.Clock(F.Event->WarningEndsAt-1.51);TestFalse(TEXT("Final reveal never starts before 1.5 s"),F.Event->IsFinalWarning());
    F.Clock(F.Event->WarningEndsAt-1.49);TestTrue(TEXT("The final 1.5 s still opens the fog"),F.Event->IsFinalWarning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogOrderedWavesTest,"MessControl.FogBrawl.RightThenLeftOnlySelectedTeeth",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogOrderedWavesTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F(4);F.Event->TotalStrikes=2;F.Warning();
    TMap<AMCArenaTooth*,EMCFogBrawlSide> Planned;
    for(const auto& T:F.Event->Targets) Planned.Add(T.Tooth,T.Side);
    F.Impact();
    TestEqual(TEXT("The right impact immediately counts as one strike"),F.Event->StrikesResolved,1);
    for(auto* Tooth:F.Teeth) {
        const bool Hit=Planned.Contains(Tooth) && Planned[Tooth]==EMCFogBrawlSide::Right;
        TestTrue(TEXT("Only selected right teeth are damaged by the first impact"),FMath::IsNearlyEqual(Tooth->State.Health,Hit?200.f/3.f:100.f,.01f));
    }
    F.NextWarning();TestEqual(TEXT("The next warning is exclusively left"),F.Event->CurrentSide,EMCFogBrawlSide::Left);
    TestTrue(TEXT("Left also gets a complete 3.5-second warning window"),FMath::IsNearlyEqual(F.Event->WarningRemaining(),3.5f,.001f));
    F.Impact();
    TestEqual(TEXT("Right and left are two separate strikes"),F.Event->StrikesResolved,2);
    for(auto* Tooth:F.Teeth) TestTrue(TEXT("The round hits each selected tooth once and leaves other reserves intact"),
        FMath::IsNearlyEqual(Tooth->State.Health,Planned.Contains(Tooth)?200.f/3.f:100.f,.01f));
    F.Event->Tick(.1f);TestEqual(TEXT("Recovery cannot repeat a resolved strike"),F.Event->StrikesResolved,2);
    F.NextWarning();TestTrue(TEXT("Both configured strikes finish the event"),F.Event->IsComplete());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogFiveStrikesTest,"MessControl.FogBrawl.ExactlyFiveIndividualStrikes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogFiveStrikesTest::RunTest(const FString&)
{
    for(int32 Players=1;Players<=4;++Players) {
        MCFogBrawlTestsPrivate::FFogWorld F(Players);F.Warning();
        TestEqual(TEXT("The production default is five individual strikes"),F.Event->TotalStrikes,5);
        for(int32 Strike=1;Strike<=5;++Strike) {
            if(!TestEqual(TEXT("Every scheduled strike has a warning"),F.Event->Stage,EMCFogBrawlStage::Warning)) return false;
            TestEqual(TEXT("Every selection retains one distinct target per participant"),F.Event->Targets.Num(),Players);
            if(Players>1) {
                TestEqual(TEXT("The normal five-strike sequence alternates right then left"),F.Event->CurrentSide,
                    Strike%2?EMCFogBrawlSide::Right:EMCFogBrawlSide::Left);
                if(Strike==5) {
                    TestEqual(TEXT("The final single attack covers the whole player quota"),F.Event->CurrentTargetCount(),Players);
                    TestFalse(TEXT("The final selection cannot announce a sixth strike"),F.Event->Targets.ContainsByPredicate(
                        [&](const FMCFogBrawlTarget& T){return T.Side!=F.Event->CurrentSide;}));
                }
            }
            F.Impact();TestEqual(TEXT("Each side impact increments the total exactly once"),F.Event->StrikesResolved,Strike);
            F.NextWarning();
            if(Strike<5) {
                TestFalse(TEXT("The event does not finish before its fifth strike"),F.Event->IsComplete());
                if(F.Event->Stage==EMCFogBrawlStage::Selection) {
                    F.Clock(F.Event->StageEndsAt+.01);F.Event->Tick(.01f);F.Event->SetActorTickEnabled(false);
                }
            }
        }
        TestTrue(TEXT("The fifth individual strike completes the event"),F.Event->IsComplete());
        TestTrue(TEXT("Completion clears all target and selection cues"),F.Event->Targets.IsEmpty() && F.Event->SelectionTeeth.IsEmpty());
        const double LastImpact=F.Event->LastImpactAt;
        F.Clock(F.World->GetTimeSeconds()+100);F.Event->Tick(.1f);
        TestEqual(TEXT("There is no sixth strike after completion"),F.Event->StrikesResolved,5);
        TestEqual(TEXT("Completion never produces a later impact"),F.Event->LastImpactAt,LastImpact);
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F(4);F.Teeth[1]->ConsumeForRespawn();F.Event->TotalStrikes=1;F.Warning();
        TestEqual(TEXT("The final wave uses the other side when only it can fit the full team"),F.Event->CurrentSide,EMCFogBrawlSide::Left);
        TestEqual(TEXT("Fallback still gives one target to every participant"),F.Event->CurrentTargetCount(),4);
        F.Impact();F.NextWarning();TestTrue(TEXT("Fallback also ends after its single remaining strike"),F.Event->IsComplete());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogIndependentDefenceTest,"MessControl.FogBrawl.IndependentDefenceAndSharedMarks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogIndependentDefenceTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F(4);F.Warning();
    // Two neighbouring right targets give overlapping guard areas in a valid four-player plan.
    F.Event->Targets.Reset();
    for(int32 Index:{0,2,1,3}) {
        FMCFogBrawlTarget T;T.Tooth=F.Teeth[Index];T.Side=AMCFogBrawlEvent::ToothSide(T.Tooth);F.Event->Targets.Add(T);
    }
    F.Event->CurrentSide=EMCFogBrawlSide::Right;F.Event->ActiveTarget=F.Teeth[1];
    auto* Hero=F.Crew[0];F.Guard(Hero,F.Teeth[1]);
    TestTrue(TEXT("Ordinary held RMB guards the nearby right target"),F.Event->IsGuardingTooth(Hero,F.Teeth[1]));
    auto* Marker=F.Crew[1];F.Guard(Marker,F.Teeth[0]);Marker->SetBraceInputHeld(false);
    TestTrue(TEXT("A player may share the waiting left target before that side attacks"),F.Event->MarkTarget(Marker,F.Teeth[0]));
    TestTrue(TEXT("The beacon belongs to the selected tooth"),F.Event->Targets[0].bMarked);
    TestFalse(TEXT("Repeating a marker cannot restart it"),F.Event->MarkTarget(Marker,F.Teeth[0]));
    TestFalse(TEXT("Unselected/stale teeth cannot be marked"),F.Event->MarkTarget(Marker,F.Teeth[7]));
    F.Impact();
    TestEqual(TEXT("The protected tooth retains full health"),F.Teeth[1]->State.Health,100.f);
    TestTrue(TEXT("The second unguarded right tooth still takes damage"),F.Teeth[3]->State.Health<100);
    TestEqual(TEXT("One guard takes its target's share of the team load"),Hero->Status->State.Health,85.f);
    TestEqual(TEXT("The future left target remains untouched"),F.Teeth[0]->State.Health,100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogReplacementTest,"MessControl.FogBrawl.ReplacementKeepsCountSideAndDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogReplacementTest::RunTest(const FString&)
{
    MCFogBrawlTestsPrivate::FFogWorld F(3);F.Warning();
    auto* Old=F.Event->ActiveTarget.Get();const auto Side=AMCFogBrawlEvent::ToothSide(Old);
    const double Deadline=F.Event->WarningEndsAt;Old->ConsumeForRespawn();F.Clock(Deadline-.1);F.Event->Tick(.01f);
    TestEqual(TEXT("Replacement preserves one target per participant"),F.Event->Targets.Num(),3);
    TestTrue(TEXT("The consumed tooth is no longer a target"),!F.Event->Targets.ContainsByPredicate([&](const FMCFogBrawlTarget& T){return T.Tooth==Old;}));
    TestTrue(TEXT("A current-side replacement receives a fresh 3.5-second preparation"),FMath::IsNearlyEqual(F.Event->WarningRemaining(),3.5f,.001f));
    TestEqual(TEXT("Replacement retains the correct side"),AMCFogBrawlEvent::ToothSide(F.Event->ActiveTarget),Side);
    TestEqual(TEXT("Replacing a reserve never spends an impact"),F.Event->StrikesResolved,0);
    F.Event->Stop();TestTrue(TEXT("Cancellation clears the whole selection and plan"),F.Event->Targets.IsEmpty() && F.Event->SelectionTeeth.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFogFailureAuthorityTest,"MessControl.FogBrawl.ShortageAuthorityAndCleanup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFogFailureAuthorityTest::RunTest(const FString&)
{
    {
        MCFogBrawlTestsPrivate::FFogWorld F(2,false);F.Reserve(FVector(0,-500,100),1);F.Reserve(FVector(500,-500,100),3);F.Warning();
        TestTrue(TEXT("A two-player round cannot silently choose both teeth on the same side"),F.Event->bFailed && !F.Event->IsActive());
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F(4,false);F.Reserve(FVector(0,500,100),2);F.Reserve(FVector(0,-500,100),1);F.Warning();
        TestTrue(TEXT("A shortage cannot silently reduce the player quota"),F.Event->bFailed);
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F;F.Event->SetRole(ROLE_SimulatedProxy);F.Event->Start();
        TestEqual(TEXT("A client cannot start the authoritative selection"),F.Event->Stage,EMCFogBrawlStage::Idle);
        F.Event->Stage=EMCFogBrawlStage::Selection;F.Event->StageEndsAt=0;F.Event->Tick(10);
        TestTrue(TEXT("A client cannot choose or damage targets"),F.Event->Targets.IsEmpty() && F.Teeth[0]->State.Health==100);
        F.Event->SetRole(ROLE_Authority);F.Event->Stop();
    }
    {
        MCFogBrawlTestsPrivate::FFogWorld F;F.Selection();auto* State=F.World->SpawnActor<AMCGameState>();F.World->SetGameState(State);State->Phase=EMCShiftPhase::Lost;
        F.Event->Tick(.01f);TestFalse(TEXT("A terminal shift stops even during the all-teeth scan"),F.Event->IsActive());
        TestTrue(TEXT("Terminal cleanup empties local cues"),F.Event->Pulse->GetNumSections()==0 && F.Event->Targets.IsEmpty() && F.Event->SelectionTeeth.IsEmpty());
    }
    return true;
}
#endif
