#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCGameMode.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCOrbitSpringArmComponent.h"
#include "MCPlayerCameraComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGroundImpactSubsystem.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPhysicalImpactCamera,"MessControl.Camera.PhysicalImpactFeedback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPhysicalImpactCamera::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
    auto* Other=World->SpawnActor<AMCToothCharacter>(FVector(600,0,2000),FRotator::ZeroRotator);
    auto* PC=World->SpawnActor<APlayerController>();
    PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));PC->Possess(Hero);
    Hero->GetCharacterMovement()->DisableMovement();Other->GetCharacterMovement()->DisableMovement();
    // Fresh standing bodies have the same brief protection as a recovered pawn.
    for(int32 I=0;I<48;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}
    const FRotator StableRotation=Hero->Camera->GetComponentRotation();
    const FVector StableLocation=Hero->Camera->GetComponentLocation();
    const auto ReadView=[Hero]() {FMinimalViewInfo V;Hero->Camera->GetCameraView(1.f/60,V);return V;};
    const float StableFOV=ReadView().FOV;

    Hero->NotifyCameraImpact(80,FVector::UpVector);
    TestTrue(TEXT("Small resting contacts leave the camera steady"),ReadView().Rotation.Equals(StableRotation));
    Hero->ToothPhysics->ApplyHit(FVector(400,0,0),Hero->GetActorLocation());
    const FMinimalViewInfo Weak=ReadView();
    TestTrue(TEXT("Accepted physical hits shake the owning player's view"),!Weak.Rotation.Equals(StableRotation,.001f));
    Hero->NotifyCameraImpact(400,FVector::ForwardVector);
    TestTrue(TEXT("Duplicate contacts do not amplify a single hit"),ReadView().Rotation.Equals(Weak.Rotation,.001f));
    Hero->NotifyCameraImpact(10000,FVector::UpVector);
    const FMinimalViewInfo Strong=ReadView();
    TestTrue(TEXT("Higher kinetic energy gives stronger feedback"),FMath::Abs(Strong.Rotation.Pitch-StableRotation.Pitch)>FMath::Abs(Weak.Rotation.Pitch-StableRotation.Pitch));
    TestTrue(TEXT("Even extreme hits stay below one degree"),FMath::Abs(Strong.Rotation.Pitch-StableRotation.Pitch)<1.f);
    TestTrue(TEXT("Weak and strong impact feedback keep the eye location fixed"),Weak.Location.Equals(StableLocation,.001f) && Strong.Location.Equals(StableLocation,.001f));
    TestEqual(TEXT("Impact feedback keeps the lens fixed"),Strong.FOV,StableFOV);
    TestTrue(TEXT("Shake never feeds into the orbit component rotation"),Hero->Camera->GetComponentRotation().Equals(StableRotation));
    FMinimalViewInfo OtherView;Other->Camera->GetCameraView(1.f/60,OtherView);
    TestTrue(TEXT("Another pawn's camera stays steady"),OtherView.Rotation.Equals(Other->Camera->GetComponentRotation()));
    for(int32 I=0;I<24;++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);}
    TestTrue(TEXT("Impact feedback settles completely after 0.28 seconds"),ReadView().Rotation.Equals(Hero->Camera->GetComponentRotation(),.001f));
    Hero->Status->Damage(1,FVector::ForwardVector);Hero->UpdateMouthCamera(0);
    TestEqual(TEXT("Damage no longer produces a zoom pulse"),Hero->Camera->FieldOfView,StableFOV);
    FHitResult Floor;Floor.ImpactNormal=FVector::UpVector;
    Hero->GetCharacterMovement()->Velocity=FVector(0,0,-650);Hero->Landed(Floor);
    TestTrue(TEXT("A physical landing also triggers feedback"),!ReadView().Rotation.Equals(Hero->Camera->GetComponentRotation(),.001f));
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHeavyPropCamera,"MessControl.Camera.HeavyPropGroundImpact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHeavyPropCamera::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
    World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    TestNotNull(TEXT("Ground impact feedback is automatically available in gameplay worlds"),World->GetSubsystem<UMCGroundImpactSubsystem>());
    auto* Near=World->SpawnActor<AMCToothCharacter>(FVector(450,0,220),FRotator::ZeroRotator);
    auto* Far=World->SpawnActor<AMCToothCharacter>(FVector(6000,0,220),FRotator::ZeroRotator);
    for(auto* Hero:{Near,Far}) {
        auto* PC=World->SpawnActor<APlayerController>();PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));PC->Possess(Hero);
        Hero->GetCharacterMovement()->DisableMovement();
    }
    const auto Box=[World](FVector Location,FVector Extent,bool Simulated,float Mass) {
        auto* Actor=World->SpawnActor<AActor>();auto* Body=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Body);Body->SetBoxExtent(Extent);Body->SetCollisionProfileName(TEXT("BlockAll"));Body->RegisterComponent();
        Actor->SetActorLocation(Location);Body->SetMassOverrideInKg(NAME_None,Mass,true);Body->SetSimulatePhysics(Simulated);
        return Body;
    };
    auto* Floor=Box(FVector::ZeroVector,FVector(5000,3000,20),false,100);
    auto* Heavy=Box(FVector(0,0,750),FVector(70),true,20);
    Heavy->SetPhysicsLinearVelocity(FVector(0,0,-500));
    float NearAngle=0,FarAngle=0,LocationOffset=0;int32 LandingFrames=0;
    for(int32 I=0;I<100;++I) {
        ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);
        for(auto* Hero:{Near,Far}) {
            FMinimalViewInfo View;Hero->Camera->GetCameraView(1.f/60,View);
            const float Angle=float(FMath::Abs(FMath::FindDeltaAngleDegrees(Hero->Camera->GetComponentRotation().Pitch,View.Rotation.Pitch)));
            if(Hero==Near) {NearAngle=FMath::Max(NearAngle,Angle);if(Angle>.01f) ++LandingFrames;}
            else FarAngle=FMath::Max(FarAngle,Angle);
            LocationOffset=FMath::Max(LocationOffset,float(FVector::Dist(View.Location,Hero->Camera->GetComponentLocation())));
        }
    }
    TestTrue(TEXT("A generic heavy prop landing shakes a nearby player without hitting them"),NearAngle>.05f && LandingFrames>4);
    TestTrue(TEXT("Players outside the landing radius remain steady"),FarAngle<.001f);
    TestTrue(TEXT("Earthquake feedback changes rotation only"),LocationOffset<.001f);
    TestEqual(TEXT("The nearby player receives no damage from environmental feedback"),Near->Status->State.Health,100.f);
    FMinimalViewInfo Settled;Near->Camera->GetCameraView(1.f/60,Settled);
    TestTrue(TEXT("A resting heavy object does not keep shaking the camera"),Settled.Rotation.Equals(Near->Camera->GetComponentRotation(),.001f));
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
