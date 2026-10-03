#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCThroat.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FThroatBatchWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCThroat* Throat=nullptr;
    AMCToothCharacter* First=nullptr;
    AMCToothCharacter* Second=nullptr;
    FVector Center=FVector::ZeroVector;
    FThroatBatchWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Throat=World->SpawnActor<AMCThroat>(FVector(0,0,2000),FRotator::ZeroRotator);
        Center=Throat->GetActorTransform().TransformPosition(Throat->ZoneCenter);
        First=Hero(-110);Second=Hero(110);
    }
    ~FThroatBatchWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCToothCharacter* Hero(float Y)
    {
        auto* H=World->SpawnActor<AMCToothCharacter>(Center+FVector(-Throat->ZoneRadius-200,Y,70),FRotator::ZeroRotator);
        H->GetCharacterMovement()->DisableMovement();return H;
    }
    void Step(float Seconds,float Dt=1.f/60)
    {for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}}
    AMCFoodActor* Food(FVector Position,bool Brush=false)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(.32,.32,.12);
        Row.Mass=3;Row.Fragments=3;Row.SpoilSeconds=300;
        const FTransform Transform(Position);
        auto* F=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(7);F->ConfigureItem(TEXT("ThroatStackFixture"),Row,Random,true);
        if(Brush) F->ConfigureBrush();F->FinishSpawning(Transform);F->Body->SetEnableGravity(false);return F;
    }
    TArray<AMCFoodActor*> Stack(AMCToothCharacter* H,int32 Count)
    {
        H->FoodCollection->Toggle();TArray<AMCFoodActor*> Result;
        for(int32 I=0;I<Count;++I) {
            auto* F=Food(H->GetActorLocation()+FVector(100,(I-(Count-1)*.5f)*40,-35));
            if(H->FoodCollection->Collect(F)) Result.Add(F);
        }
        return Result;
    }
    void Enter(AMCToothCharacter* H,float Y)
    {H->SetActorLocation(Center+FVector(-60,Y,70),false,nullptr,ETeleportType::TeleportPhysics);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatTwoCarriers,"MessControl.Throat.TwoCarriersShareFixedWindow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatTwoCarriers::RunTest(const FString&)
{
    for(float Dt:{1.f/30,1.f/60}) {
        FThroatBatchWorld T;
        const auto First=T.Stack(T.First,2),Second=T.Stack(T.Second,2);
        if(!TestEqual(TEXT("First carrier has two pieces"),First.Num(),2) || !TestEqual(TEXT("Second carrier has two pieces"),Second.Num(),2)) return false;
        T.Step(.5f,Dt);T.Enter(T.First,-110);T.Step(.15f,Dt);
        TestTrue(TEXT("Entering the delivery zone clears the hand without an action input"),T.First->FoodCollection->Pieces.IsEmpty() && !T.First->FoodCollection->bCollecting);
        TestEqual(TEXT("First delivered stack opens the gathering window"),T.Throat->ThroatPhase,EMCThroatPhase::Anticipation);
        TestEqual(TEXT("Nothing is consumed at delivery"),T.Throat->FoodSwallowed,0);
        const double Window=T.Throat->PhaseStartedAt;
        T.Step(1.2f,Dt);T.Enter(T.Second,110);T.Step(.15f,Dt);
        TestTrue(TEXT("Another carrier automatically joins the same batch"),T.Second->FoodCollection->Pieces.IsEmpty() && !T.Second->FoodCollection->bCollecting);
        TestEqual(TEXT("All four pieces wait together"),T.Throat->QueuedFoodCount,4);
        TestEqual(TEXT("Second delivery does not restart the three-second window"),T.Throat->PhaseStartedAt,Window);
        T.Step(FMath::Max(0.,Window+2.8-T.World->GetTimeSeconds()),Dt);
        TestEqual(TEXT("Suction cannot start before the window ends"),T.Throat->SwallowCount,0);
        T.Step(.3f,Dt);
        TestEqual(TEXT("The batch begins one swallow after three seconds"),T.Throat->SwallowCount,1);
        TestEqual(TEXT("The full visible swallow lasts two seconds by default"),T.Throat->SwallowSeconds,2.f);
        TestTrue(TEXT("Food is held without heap physics and all players stay safe"),!First[0]->Body->IsSimulatingPhysics() && !First[0]->IsDisposed() && !T.First->SwallowedBy && !T.Second->SwallowedBy && T.First->CanWork() && T.Second->CanWork());
        T.Step(1.65f,Dt);TestEqual(TEXT("Food stays visible through the suction event"),T.Throat->FoodSwallowed,0);
        T.Step(.4f,Dt);TestEqual(TEXT("All four foods commit at the end of one gulp"),T.Throat->FoodSwallowed,4);
        T.Step(1.5f,Dt);TestEqual(TEXT("Recovering cannot consume any food twice"),T.Throat->FoodSwallowed,4);
        TestEqual(TEXT("Recovery permits the next delivery cycle"),T.Throat->ThroatPhase,EMCThroatPhase::Collecting);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatLateDelivery,"MessControl.Throat.LateDeliveryQueuesNextBatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatLateDelivery::RunTest(const FString&)
{
    FThroatBatchWorld T;const auto First=T.Stack(T.First,1);const auto Late=T.Stack(T.Second,1);
    if(!TestEqual(TEXT("Both carriers have food"),First.Num()+Late.Num(),2)) return false;
    T.Step(.5f);T.Enter(T.First,-110);T.Step(3.3f);
    TestEqual(TEXT("First batch is in suction"),T.Throat->ThroatPhase,EMCThroatPhase::Swallowing);
    T.Enter(T.Second,110);T.Step(.15f);
    TestEqual(TEXT("Food arriving during suction is reserved for the next batch"),T.Throat->QueuedFoodCount,1);
    TestFalse(TEXT("A queued delivery cannot be claimed again"),T.Throat->AcceptDelivery(Late[0]));
    T.Step(2.f);
    TestEqual(TEXT("Only the original frozen batch is consumed"),T.Throat->FoodSwallowed,1);
    TestFalse(TEXT("The late ingredient remains visible and alive"),Late[0]->IsDisposed());
    TestEqual(TEXT("Late ingredients stay collisionless while the throat recovers"),Late[0]->Body->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    T.Step(1.1f);
    TestEqual(TEXT("Recovery gives late deliveries a fresh gathering window"),T.Throat->ThroatPhase,EMCThroatPhase::Anticipation);
    const double NextWindow=T.Throat->PhaseStartedAt;
    T.Step(FMath::Max(0.,NextWindow+2.8-T.World->GetTimeSeconds()));
    TestEqual(TEXT("Queued food receives the whole three-second delay"),T.Throat->SwallowCount,1);
    T.Step(.3f);TestEqual(TEXT("The next batch starts its own suction event"),T.Throat->SwallowCount,2);
    T.Step(2.1f);TestEqual(TEXT("The late ingredient is consumed exactly once in its own batch"),T.Throat->FoodSwallowed,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatPendingReset,"MessControl.Throat.PendingResetAndBrushExclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatPendingReset::RunTest(const FString&)
{
    FThroatBatchWorld T;const auto Pieces=T.Stack(T.First,1);if(Pieces.IsEmpty()) return false;
    T.Step(.5f);T.Enter(T.First,-110);T.Step(.15f);
    auto* Brush=T.Food(T.Center+FVector(0,100,60),true);T.Step(.15f);
    TestFalse(TEXT("Brushes are never delivered as ingredients"),T.Throat->ContainsFood(Brush) || T.Throat->AcceptDelivery(Brush));
    TestEqual(TEXT("Only the stack piece is reserved"),T.Throat->QueuedFoodCount,1);
    T.Throat->ResetSwallow();
    TestEqual(TEXT("Reset empties the shared gathering queue"),T.Throat->QueuedFoodCount,0);
    TestEqual(TEXT("Reset returns pending food to normal physics alive"),Pieces[0]->Phase,EMCFoodPhase::Free);
    TestTrue(TEXT("Released pending food can simulate physics again"),Pieces[0]->Body->IsSimulatingPhysics());
    TestEqual(TEXT("Reset cannot commit a meal"),T.Throat->FoodSwallowed,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThroatFinalFrameDelivery,"MessControl.Throat.CarrierJoinsAtEndOfGatheringWindow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThroatFinalFrameDelivery::RunTest(const FString&)
{
    FThroatBatchWorld T;const auto First=T.Stack(T.First,1),Second=T.Stack(T.Second,1);
    if(!TestEqual(TEXT("Both carriers hold an ingredient"),First.Num()+Second.Num(),2)) return false;
    T.Step(.5f);
    // Drive only this actor explicitly so the final two intake ticks have exact
    // times, while the world clock and real carried food continue to advance.
    T.Throat->SetActorTickEnabled(false);T.Enter(T.First,-110);T.Throat->Tick(.01f);
    const double Window=T.Throat->PhaseStartedAt;
    for(int32 I=0;I<290;++I) T.Step(.01f,.01f);
    T.Throat->Tick(.01f); // Last loose scan schedules the next one for 3.00 s.
    auto* Loose=T.Food(T.Center+FVector(0,170,60));Loose->Body->SetSimulatePhysics(false);
    for(int32 I=0;I<6;++I) T.Step(.01f,.01f);
    T.Enter(T.Second,110);T.Throat->Tick(.01f);
    TestTrue(TEXT("Second carrier enters at 2.96 seconds, before the shared deadline"),FMath::Abs(T.World->GetTimeSeconds()-Window-2.96)<.002);
    TestTrue(TEXT("The final-frame carrier is delivered without waiting for a loose-food scan"),T.Second->FoodCollection->Pieces.IsEmpty() && !T.Second->FoodCollection->bCollecting);
    TestEqual(TEXT("Both carrier ingredients join the original batch"),T.Throat->QueuedFoodCount,2);
    TestEqual(TEXT("The shared deadline stays fixed"),T.Throat->PhaseStartedAt,Window);
    TestTrue(TEXT("Loose food is still throttled independently at ten scans per second"),Loose->Phase==EMCFoodPhase::Free || Loose->Phase==EMCFoodPhase::Falling);
    for(int32 I=0;I<5;++I) T.Step(.01f,.01f);
    T.Throat->Tick(.01f);
    TestEqual(TEXT("The original batch freezes once the deadline passes"),T.Throat->SwallowCount,1);
    TestEqual(TEXT("Loose food first observed after expiry goes to the next queue"),T.Throat->QueuedFoodCount,1);
    for(int32 I=0;I<205;++I) T.Step(.01f,.01f);
    T.Throat->Tick(.01f);
    TestEqual(TEXT("Both pre-deadline carriers are consumed in the same gulp"),T.Throat->FoodSwallowed,2);
    TestTrue(TEXT("Both original foods commit, while the post-deadline loose piece stays alive"),First[0]->IsDisposed() && Second[0]->IsDisposed() && !Loose->IsDisposed());
    return true;
}
#endif
