#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCOrbitSpringArmComponent.h"
#include "MCPlayerCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBlueprintCameraDefaults,"MessControl.Camera.BlueprintDefaultsReachPlayerView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBlueprintCameraDefaults::RunTest(const FString&)
{
    const auto* Mode=GetDefault<AMCGameMode>();
    UClass* PlayerClass=Mode->DefaultPawnClass;
    if(!TestTrue(TEXT("The game spawns the editable player Blueprint"),PlayerClass && PlayerClass->GetPathName()==TEXT("/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"))) return false;
    auto* Defaults=CastChecked<AMCToothCharacter>(PlayerClass->GetDefaultObject());
    auto* DefaultCamera=Defaults->Camera.Get();
    const FPostProcessSettings Saved=DefaultCamera->PostProcessSettings;
    const float SavedWeight=DefaultCamera->PostProcessBlendWeight;
    // Exercise a Blueprint camera override independently of the C++ camera defaults.
    DefaultCamera->PostProcessSettings.bOverride_BloomIntensity=true;
    DefaultCamera->PostProcessSettings.BloomIntensity=2.375f;
    DefaultCamera->PostProcessBlendWeight=.65f;

    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Hero=World->SpawnActor<AMCToothCharacter>(PlayerClass,FVector(0,0,2000),FRotator::ZeroRotator);
    DefaultCamera->PostProcessSettings=Saved;DefaultCamera->PostProcessBlendWeight=SavedWeight;
    if(TestNotNull(TEXT("Blueprint pawn spawns with its native gameplay components"),Hero)) {
        TestNotNull(TEXT("Movement uses the current custom component"),Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()));
        TestNotNull(TEXT("Collision-aware camera arm is retained"),Cast<UMCOrbitSpringArmComponent>(Hero->CameraBoom));
        TestNotNull(TEXT("Player autofocus camera is retained"),Cast<UMCPlayerCameraComponent>(Hero->Camera));
        FMinimalViewInfo View;Hero->Camera->GetCameraView(1.f/60,View);
        TestEqual(TEXT("Blueprint post-process blend weight reaches the player view"),View.PostProcessBlendWeight,.65f);
        TestTrue(TEXT("The Blueprint bloom override stays enabled"),View.PostProcessSettings.bOverride_BloomIntensity);
        TestEqual(TEXT("Autofocus preserves unrelated Blueprint post-process settings"),View.PostProcessSettings.BloomIntensity,2.375f);
    }
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
