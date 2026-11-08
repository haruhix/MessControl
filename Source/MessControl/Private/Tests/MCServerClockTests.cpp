#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCServerClockLoadingRecovery,
    "MessControl.Networking.Foundation.NativeClockRecoversAfterLoadingSkew",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCServerClockLoadingRecovery::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);auto* Package=World->GetOutermost();
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
    State->SetRole(ROLE_SimulatedProxy);
    auto* Timestamp=FindFProperty<FDoubleProperty>(AGameStateBase::StaticClass(),TEXT("ReplicatedWorldTimeSecondsDouble"));
    auto* Notification=State->FindFunction(TEXT("OnRep_ReplicatedWorldTimeSecondsDouble"));
    if(TestNotNull(TEXT("The test drives the real native replicated timestamp"),Timestamp)
        && TestNotNull(TEXT("The test invokes the real native clock RepNotify"),Notification)) {
        const auto Sample=[&](double Offset) {
            Timestamp->SetPropertyValue_InContainer(State,World->GetTimeSeconds()+Offset);
            State->ProcessEvent(Notification,nullptr);
        };
        // Seed real native history, then model a loading stall changing the
        // server/local delta by three seconds. Do not copy the clock algorithm.
        for(int32 I=0;I<100;++I) Sample(12.);
        TestTrue(TEXT("The original steady estimate was established"),FMath::Abs(State->GetServerWorldTimeSeconds()-World->GetTimeSeconds()-12.)<.001);
        State->Tick(.016f);
        for(int32 I=0;I<30;++I) {Sample(15.);State->Tick(.016f);}
        TestTrue(TEXT("Recent native samples recover the server clock without retaining seconds of loading bias"),
            FMath::Abs(State->GetServerWorldTimeSeconds()-World->GetTimeSeconds()-15.)<.2);
        for(int32 I=0;I<20;++I) {Sample(15.+(I%2?.02:-.02));State->Tick(.016f);}
        TestTrue(TEXT("Native smoothing still rejects small alternating timestamp noise"),
            FMath::Abs(State->GetServerWorldTimeSeconds()-World->GetTimeSeconds()-15.)<.05);
        TestTrue(TEXT("The closed-mouth client keeps clock maintenance enabled"),State->IsActorTickEnabled());
    }
    State->SetRole(ROLE_Authority);World->EndPlay(EEndPlayReason::Quit);
    GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    if(IsValid(Package) && Package->GetName().StartsWith(TEXT("/Temp/"))) Package->SetDirtyFlag(false);
    return true;
}
#endif
