#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeFlood.h"
#include "MCCoffeeProfile.h"
#include "MCDayPlan.h"
#include "MCGameState.h"
#include "MCPlaytestBotController.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace MCPlaytestBotCoffeeTestsPrivate
{
/** Ordinary CMC, real flood time and the live AI timer. No navmesh, input
 * overrides, per-frame placement, force injection or completed objectives. */
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCPlaytestBotController* Bot=nullptr;
    AMCToothCharacter* Hero=nullptr;
    AMCCoffeeFlood* Flood=nullptr;
    UMCDayPlan* Plan=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* State=World->SpawnActor<AMCGameState>();
        State->Phase=EMCShiftPhase::Working;World->SetGameState(State);
        auto* Floor=World->SpawnActor<AActor>();
        auto* Collision=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Collision);Collision->SetBoxExtent(FVector(4500,3500,20));
        Collision->SetCollisionProfileName(TEXT("BlockAll"));Collision->RegisterComponent();
        Floor->SetActorLocation(FVector(0,0,-20));
        const auto* Defaults=GetDefault<AMCToothCharacter>();
        const float StandingZ=Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+4;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(900,0,StandingZ),FRotator(0,180,0));
        Bot=World->SpawnActor<AMCPlaytestBotController>();
        Bot->Configure(EMCPlaytestBotSkill::Skilled,0,41);Bot->Possess(Hero);
        auto* Profile=NewObject<UMCCoffeeProfile>(World);
        Profile->Settings.Inlet=FVector(-1800,0,650);
        Profile->Settings.DrainPoint=FVector(1900,0,0);
        Profile->Settings.bUseThroatActor=false;
        Profile->Settings.FillSeconds=3;Profile->Settings.DrainSeconds=.5f;
        Profile->Settings.FrontSpeed=2000;
        Plan=NewObject<UMCDayPlan>(World);
        Plan->ArenaCenter=FVector::ZeroVector;Plan->ArenaHalfSize=FVector(2000,1500,220);
        Plan->CoffeeProfile=Profile;
        Flood=World->SpawnActor<AMCCoffeeFlood>();
    }
    ~FFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Frame() {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestRiverEscapeInputTest,
    "MessControl.PlaytestBots.Coffee.RiverUsesMovementBeforeFrontAndReleasesAfterward",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestRiverEscapeInputTest::RunTest(const FString&)
{
    MCPlaytestBotCoffeeTestsPrivate::FFixture F;
    const float InitialHealth=F.Hero->Status->State.Health;
    const FVector Start=F.Hero->GetActorLocation();
    F.Flood->Start(F.Plan);
    if(!TestTrue(TEXT("The ordinary opening coffee starts as a real river"),F.Flood->IsActive() && F.Flood->bRiverFlood)) return false;
    bool SawInwardAcceleration=false,SawWet=false,SawSwimming=false,FiniteContinuous=true,UsedWork=false;
    FVector Previous=Start,BeforeFront=Start;
    for(int32 Frame=0;Frame<600 && F.Flood->IsActive();++Frame) {
        F.Frame();
        const auto* Move=F.Hero->GetCharacterMovement();
        const FVector Position=F.Hero->GetActorLocation(),Acceleration=Move->GetCurrentAcceleration();
        SawInwardAcceleration|=FVector::DotProduct(Acceleration,-F.Flood->RiverDirection)>10;
        SawWet|=F.Hero->bInCoffee;SawSwimming|=Move->IsSwimming();
        UsedWork|=F.Flood->IsActive() && (F.Hero->bBrushing || F.Hero->bHandling);
        FiniteContinuous&=!Position.ContainsNaN() && !Move->Velocity.ContainsNaN() && FVector::Dist(Position,Previous)<150;
        Previous=Position;
        if(Frame==59) BeforeFront=Position;
    }
    TestTrue(TEXT("The bot enters ordinary movement toward safer upstream tissue"),SawInwardAcceleration);
    TestTrue(TEXT("The pawn actually retreats before the distant front arrives"),
        FVector::DotProduct(BeforeFront-Start,-F.Flood->RiverDirection)>50);
    TestTrue(TEXT("The real river front reaches the pawn"),SawWet);
    TestFalse(TEXT("A shallow river does not manufacture deep-water swimming"),SawSwimming);
    TestFalse(TEXT("Survival suspends ordinary care actions"),UsedWork);
    TestTrue(TEXT("Movement remains finite and continuous without a relocation escape"),FiniteContinuous);
    TestFalse(TEXT("The flood reaches its ordinary timed completion"),F.Flood->IsActive());
    TestTrue(TEXT("The worker survives the complete river cycle"),F.Hero->Status->IsAlive());
    TestEqual(TEXT("Real river traversal preserves ordinary health"),F.Hero->Status->State.Health,InitialHealth);
    for(int32 Frame=0;Frame<20;++Frame) F.Frame();
    TestFalse(TEXT("The finished river releases coffee state"),F.Hero->bInCoffee);
    TestTrue(TEXT("No survival paddle remains held after completion"),F.Hero->PaddleInput.IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPlaytestSwimMovementInputTest,
    "MessControl.PlaytestBots.Coffee.DeepWaterUsesRealCMCStrokeInput",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPlaytestSwimMovementInputTest::RunTest(const FString&)
{
    MCPlaytestBotCoffeeTestsPrivate::FFixture F;
    const float InitialHealth=F.Hero->Status->State.Health;
    F.Flood->Start(F.Plan,4.f);
    if(!TestTrue(TEXT("The normal swim-test entry creates deep water"),F.Flood->IsActive() && !F.Flood->bRiverFlood)) return false;
    bool SawSwimming=false,SawCounterstroke=false,SawTravel=false,FiniteContinuous=true,UsedWork=false;
    FVector Previous=F.Hero->GetActorLocation(),SwimStart=Previous;
    for(int32 Frame=0;Frame<480 && F.Flood->IsActive();++Frame) {
        F.Frame();
        const auto* Move=F.Hero->GetCharacterMovement();
        const FVector Position=F.Hero->GetActorLocation();
        if(Move->IsSwimming()) {
            if(!SawSwimming) SwimStart=Position;
            SawSwimming=true;
            const FVector Flow=F.Flood->FlowAtPosition(Position,F.Hero).GetSafeNormal2D();
            SawCounterstroke|=!Flow.IsNearlyZero() && FVector::DotProduct(Move->GetCurrentAcceleration(),-Flow)>10;
            SawTravel|=FVector::Dist2D(Position,SwimStart)>30;
        }
        UsedWork|=F.Flood->IsActive() && (F.Hero->bBrushing || F.Hero->bHandling);
        FiniteContinuous&=!Position.ContainsNaN() && !Move->Velocity.ContainsNaN() && FVector::Dist(Position,Previous)<150;
        Previous=Position;
    }
    TestTrue(TEXT("Real fill and depth queries enter CMC swimming"),SawSwimming);
    TestTrue(TEXT("The AI supplies real CMC acceleration against the current"),SawCounterstroke);
    TestTrue(TEXT("Ordinary swimming actually moves the pawn"),SawTravel);
    TestFalse(TEXT("Swimming suspends ordinary care actions"),UsedWork);
    TestTrue(TEXT("Swimming stays finite and continuous"),FiniteContinuous);
    TestTrue(TEXT("The swimmer remains alive without artificial healing"),F.Hero->Status->IsAlive());
    TestEqual(TEXT("Real water traversal preserves ordinary health"),F.Hero->Status->State.Health,InitialHealth);
    for(int32 Frame=0;Frame<30;++Frame) F.Frame();
    TestFalse(TEXT("The normal water cycle completes"),F.Flood->IsActive());
    TestFalse(TEXT("Dry completion releases coffee state"),F.Hero->bInCoffee);
    TestTrue(TEXT("No ragdoll paddle remains after the real stroke ends"),F.Hero->PaddleInput.IsNearlyZero());
    return true;
}
#endif
