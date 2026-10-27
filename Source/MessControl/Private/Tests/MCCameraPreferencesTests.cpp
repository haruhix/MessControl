#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MCGameUserSettings.h"
#include "MCHubGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCOrbitSpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
struct FCameraPreferenceWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FCameraPreferenceWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        World->SetGameState(World->SpawnActor<AMCHubGameState>());
    }
    ~FCameraPreferenceWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};

struct FToolModeCameraWorld : FCameraPreferenceWorld
{
    AMCToothCharacter* Hero=nullptr;
    APlayerController* PC=nullptr;
    UMCOrbitSpringArmComponent* Arm=nullptr;
    FToolModeCameraWorld()
    {
        World->GetGameState<AMCHubGameState>()->Phase=EMCHubPhase::Gathering;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        PC=World->SpawnActor<APlayerController>();
        // A real local player is required by both camera ownership and spray aim.
        PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));PC->Possess(Hero);PC->SetViewTarget(Hero);
        Hero->Status->Initialize(100);Hero->GetCharacterMovement()->DisableMovement();
        Arm=CastChecked<UMCOrbitSpringArmComponent>(Hero->CameraBoom);
        Hero->SetCameraOrbitView(0,-30,900);
    }
    bool Ready() const
    {
        return Hero->HasActorBegunPlay() && Hero->Inventory->HasBegunPlay()
            && Hero->IsLocallyControlled() && Hero->CanWork() && Hero->Inventory->ShouldPresentTool();
    }
    void View(float Dt)
    {
        // GetPlayerViewPoint only accepts a camera cache stamped after time 0.
        // Advance the isolated clock as well as the manually driven camera.
        World->TimeSeconds+=Dt;
        Hero->UpdateMouthCamera(Dt);Arm->TickComponent(Dt,LEVELTICK_All,nullptr);
        PC->PlayerCameraManager->UpdateCamera(Dt);
    }
    void Advance(float Seconds,float Step)
    {
        for(float Remaining=Seconds;Remaining>SMALL_NUMBER;)
        {
            const float Dt=FMath::Min(Step,Remaining);View(Dt);Remaining-=Dt;
        }
    }
    FVector EyeWithoutToolOffset() const
    {
        return Hero->GetCameraFocusLocation()
            -FRotator(FMath::Min(Hero->CameraOrbitPitch,0.f),Hero->CameraOrbitYaw,0).Vector()*Hero->CameraOrbitDistance;
    }
    FVector WantedEye() const
    {
        const FRotator View(Hero->CameraOrbitPitch,Hero->CameraOrbitYaw,0);
        return EyeWithoutToolOffset()+View.RotateVector(FVector::RightVector)*Hero->SprayCameraOffset.Y
            +FVector::UpVector*Hero->SprayCameraOffset.Z;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCameraPreferenceHubTest,"MessControl.Camera.OptionalSmoothingAndHubCollision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCameraPreferenceHubTest::RunTest(const FString&)
{
    auto* Settings=UMCGameUserSettings::Get();
    if(!TestNotNull(TEXT("The configured local settings class owns camera preferences"),Settings)) return false;
    const bool SavedSmoothing=Settings->GetCameraSmoothingEnabled();
    ON_SCOPE_EXIT { Settings->SetCameraSmoothingEnabled(SavedSmoothing); };
    Settings->SetCameraSmoothingEnabled(false); // In-memory only: no user ini or microphone changes.
    FCameraPreferenceWorld Fixture;
    auto* Hero=Fixture.World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
    auto* PC=Fixture.World->SpawnActor<APlayerController>();PC->SetAsLocalPlayerController();PC->Possess(Hero);PC->SetViewTarget(Hero);
    Hero->GetCharacterMovement()->DisableMovement();
    auto* Arm=CastChecked<UMCOrbitSpringArmComponent>(Hero->CameraBoom);
    const auto View=[&](float Dt) {Hero->UpdateMouthCamera(Dt);Arm->TickComponent(Dt,LEVELTICK_All,nullptr);};
    View(1.f/60);
    TestTrue(TEXT("Hub enters the ordinary free-orbit camera"),Hero->bManualCameraOrbit);
    Hero->SetCameraOrbitView(0,-30,900);View(1.f/60);
    Hero->SetActorLocation(FVector(100,150,2050));
    Hero->ApplyCameraOrbitInput(FVector2D(100,0));Hero->ZoomCamera(-1);View(1.f/120);
    const auto WantedEye=[&]() {return Hero->GetCameraFocusLocation()
        -FRotator(FMath::Min(Hero->CameraOrbitPitch,0.f),Hero->CameraOrbitYaw,0).Vector()*Hero->CameraOrbitDistance;};
    TestTrue(TEXT("With smoothing off, follow and zoom reach the selected eye in the same update"),
        Hero->Camera->GetComponentLocation().Equals(WantedEye(),.1f));
    TestTrue(TEXT("With smoothing off, mouse rotation has no interpolation delay"),
        FMath::IsNearlyEqual(float(Hero->Camera->GetComponentRotation().Yaw),Hero->CameraOrbitYaw,.01f));
    TestTrue(TEXT("An authored spring-arm lag cannot bypass the preference"),!Arm->bEnableCameraLag && !Arm->bEnableCameraRotationLag);

    Settings->SetCameraSmoothingEnabled(true);
    Hero->SetActorLocation(Hero->GetActorLocation()+FVector(120,60,0));
    Hero->ApplyCameraOrbitInput(FVector2D(100,0));Hero->ZoomCamera(-1);View(1.f/120);
    TestFalse(TEXT("Enabling smoothing immediately interpolates the follow/zoom path"),Hero->Camera->GetComponentLocation().Equals(WantedEye(),1.f));
    TestFalse(TEXT("Enabling smoothing immediately interpolates rotation"),
        FMath::IsNearlyEqual(float(Hero->Camera->GetComponentRotation().Yaw),Hero->CameraOrbitYaw,.01f));
    Settings->SetCameraSmoothingEnabled(false);View(1.f/120);
    TestTrue(TEXT("Disabling smoothing takes effect without a map or pawn restart"),Hero->Camera->GetComponentLocation().Equals(WantedEye(),.1f));

    const FVector Origin=Hero->GetActorLocation()+FVector(0,0,30);
    const FVector Ray=(WantedEye()-Origin).GetSafeNormal();
    auto* Wall=Fixture.World->SpawnActor<AActor>();auto* Collider=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Collider);Wall->AddInstanceComponent(Collider);Collider->SetBoxExtent(FVector(25,500,500));
    Collider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Collider->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collider->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);Collider->RegisterComponent();
    Wall->SetActorLocationAndRotation(Origin+Ray*450,Ray.Rotation());View(1.f/120);
    TestTrue(TEXT("Hub uses the arena's Camera-channel sphere sweep"),Arm->bDoCollisionTest && Arm->ProbeChannel==ECC_Camera && Arm->IsCollisionFixApplied());
    TestTrue(TEXT("The Hub eye retracts before a Camera-only wall"),FVector::Distance(Origin,Hero->Camera->GetComponentLocation())<430);
    Wall->SetActorLocation(Origin+Ray*220);View(1.f/120);
    TestTrue(TEXT("A newly closer obstacle always retracts immediately"),FVector::Distance(Origin,Hero->Camera->GetComponentLocation())<210);
    Collider->SetCollisionEnabled(ECollisionEnabled::NoCollision);View(1.f/120);
    TestTrue(TEXT("Disabled smoothing also removes outward recovery delay"),Hero->Camera->GetComponentLocation().Equals(WantedEye(),.1f));

    Settings->SetCameraSmoothingEnabled(true);Collider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Wall->SetActorLocation(Origin+Ray*450);View(1.f/120);
    const float Blocked=float(FVector::Distance(Origin,Hero->Camera->GetComponentLocation()));
    TestTrue(TEXT("Enabled smoothing never interpolates through a new wall"),Blocked<430);
    Collider->SetCollisionEnabled(ECollisionEnabled::NoCollision);View(1.f/120);
    const float Released=float(FVector::Distance(Origin,Hero->Camera->GetComponentLocation()));
    const float FullDistance=float(FVector::Distance(Origin,WantedEye()));
    TestTrue(TEXT("Opt-in smoothing eases only the safe outward recovery"),Released>Blocked && Released<Blocked+(FullDistance-Blocked)*.2f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolCameraBlendTimingTest,"MessControl.Camera.ToolModeBlend.FrameRateIndependentEntryAndExit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolCameraBlendTimingTest::RunTest(const FString&)
{
    auto* Settings=UMCGameUserSettings::Get();
    if(!TestNotNull(TEXT("Camera preferences are available"),Settings)) return false;
    const bool SavedSmoothing=Settings->GetCameraSmoothingEnabled();
    ON_SCOPE_EXIT {Settings->SetCameraSmoothingEnabled(SavedSmoothing);};
    Settings->SetCameraSmoothingEnabled(false);
    FVector ReferenceEntry=FVector::ZeroVector,ReferenceExit=FVector::ZeroVector;
    for(const int32 FPS:{30,60,120})
    {
        FToolModeCameraWorld W;
        if(!TestTrue(TEXT("Tool-mode fixture has begun play and a live local owner"),W.Ready())) return false;
        const float Dt=1.f/FPS;W.View(Dt);
        const FVector BaseEye=W.Hero->Camera->GetComponentLocation();
        const FVector FullShoulderEye=BaseEye+FVector(0,140,75);
        W.Hero->Inventory->ServerSelect(EMCToolSlot::Spray);
        if(!TestEqual(TEXT("Spray is selected through the ordinary authority path"),W.Hero->Inventory->Selected,EMCToolSlot::Spray)) return false;
        W.View(Dt);
        const FVector FirstEye=W.Hero->Camera->GetComponentLocation();
        TestTrue(FString::Printf(TEXT("%d FPS: entry moves the actual camera partially, without a snap"),FPS),
            FVector::Distance(BaseEye,FirstEye)>1 && FVector::Distance(BaseEye,FirstEye)<FVector(0,140,75).Size()*.5);
        W.Advance(.35f-Dt,Dt);
        const FVector EntryEye=W.Hero->Camera->GetComponentLocation();
        TestTrue(FString::Printf(TEXT("%d FPS: entry reaches the shoulder framing within 350 ms"),FPS),
            EntryEye.Equals(FullShoulderEye,3.f));
        W.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);W.View(Dt);
        const FVector ExitFirstEye=W.Hero->Camera->GetComponentLocation();
        TestTrue(FString::Printf(TEXT("%d FPS: exit eases back from the current visible eye"),FPS),
            FVector::Distance(BaseEye,ExitFirstEye)>1 && FVector::Distance(BaseEye,ExitFirstEye)<FVector::Distance(BaseEye,EntryEye));
        W.Advance(.35f-Dt,Dt);
        const FVector ExitEye=W.Hero->Camera->GetComponentLocation();
        TestTrue(FString::Printf(TEXT("%d FPS: exit restores ordinary framing within 350 ms"),FPS),ExitEye.Equals(BaseEye,3.f));
        if(FPS==30) {ReferenceEntry=EntryEye;ReferenceExit=ExitEye;}
        else
        {
            TestTrue(TEXT("Equal-age entry has the same visible framing at every frame rate"),EntryEye.Equals(ReferenceEntry,.01f));
            TestTrue(TEXT("Equal-age exit has the same visible framing at every frame rate"),ExitEye.Equals(ReferenceExit,.01f));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolCameraRetargetTest,"MessControl.Camera.ToolModeBlend.RetargetKeepsOrbitAndAimResponsive",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolCameraRetargetTest::RunTest(const FString&)
{
    auto* Settings=UMCGameUserSettings::Get();
    if(!TestNotNull(TEXT("Camera preferences are available"),Settings)) return false;
    const bool SavedSmoothing=Settings->GetCameraSmoothingEnabled();
    ON_SCOPE_EXIT {Settings->SetCameraSmoothingEnabled(SavedSmoothing);};
    Settings->SetCameraSmoothingEnabled(false);
    FToolModeCameraWorld W;
    if(!TestTrue(TEXT("Retarget fixture has initialized tool ownership"),W.Ready())) return false;
    W.View(1.f/120);W.Hero->Inventory->ServerSelect(EMCToolSlot::Spray);W.Advance(.05f,1.f/120);
    const FVector BeforeExit=W.Hero->Camera->GetComponentLocation();
    W.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);W.View(1.f/120);
    const FVector AfterExit=W.Hero->Camera->GetComponentLocation();
    W.Hero->Inventory->ServerSelect(EMCToolSlot::Spray);W.View(1.f/120);
    TestTrue(TEXT("Re-entering reverses the current partial exit without resetting to either endpoint"),
        FVector::Distance(AfterExit,W.Hero->Camera->GetComponentLocation())<25
        && FVector::Distance(BeforeExit,AfterExit)<25 && W.Hero->SprayCameraOffset.Y>0 && W.Hero->SprayCameraOffset.Y<140);

    W.Hero->SetActorLocation(W.Hero->GetActorLocation()+FVector(300,80,50));
    W.Hero->ApplyCameraOrbitInput(FVector2D(400,0));W.Hero->ZoomCamera(-1);W.View(1.f/120);
    TestTrue(TEXT("Tool transition does not add follow delay when smoothing is off"),
        W.Hero->MouthCameraFocus.Equals(W.Hero->GetActorLocation(),.01f));
    TestTrue(TEXT("Mouse yaw reaches the visible camera in the same update"),
        FMath::IsNearlyEqual(float(W.Hero->Camera->GetComponentRotation().Yaw),W.Hero->CameraOrbitYaw,.01f));
    TestTrue(TEXT("Zoom and the shoulder basis both use the current orbit, without previous-frame lag"),
        W.Hero->Camera->GetComponentLocation().Equals(W.WantedEye(),.1f));
    FVector PlayerViewOrigin;FRotator PlayerViewRotation;
    W.PC->GetPlayerViewPoint(PlayerViewOrigin,PlayerViewRotation);
    const FVector Aim=W.Hero->Inventory->SprayAim();
    AddInfo(FString::Printf(TEXT("Spray view: clock=%.6f cache=%.6f component=%s / %s final=%s / %s aim=%s"),
        W.World->GetTimeSeconds(),W.PC->PlayerCameraManager->GetCameraCacheTime(),
        *W.Hero->Camera->GetComponentLocation().ToString(),*W.Hero->Camera->GetComponentRotation().ToString(),
        *PlayerViewOrigin.ToString(),*PlayerViewRotation.ToString(),*Aim.ToString()));
    TestTrue(TEXT("The local view has a valid camera-manager cache rather than the time-zero pawn fallback"),
        W.PC->PlayerCameraManager->GetCameraCacheTime()>0 && PlayerViewOrigin.Equals(W.Hero->Camera->GetComponentLocation(),.1f));
    TestTrue(TEXT("Spray targeting follows the final player-view ray during a partial transition"),
        FVector::DotProduct((Aim-PlayerViewOrigin).GetSafeNormal(),PlayerViewRotation.Vector())>.99999);
    TestTrue(TEXT("Spray targeting follows the visible camera centre during a partial transition"),
        FVector::DotProduct((Aim-W.Hero->Camera->GetComponentLocation()).GetSafeNormal(),
            W.Hero->Camera->GetForwardVector())>.99999);

    for(int32 Index=0;Index<40;++Index)
    {
        W.Hero->Inventory->ServerSelect(Index%2?EMCToolSlot::Brush:EMCToolSlot::Spray);W.View(1.f/120);
        const FVector Offset=W.Hero->Camera->GetComponentLocation()-W.EyeWithoutToolOffset();
        const FVector Right=W.Hero->Camera->GetRightVector();
        TestTrue(TEXT("Rapid tool changes never overshoot the framing endpoints"),
            FVector::DotProduct(Offset,Right)>=-.01 && FVector::DotProduct(Offset,Right)<=140.01
            && Offset.Z>=-.01 && Offset.Z<=75.01);
    }
    W.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);W.Advance(1.f,1.f/120);
    TestTrue(TEXT("The completed exit has no residual shoulder drift"),
        W.Hero->Camera->GetComponentLocation().Equals(W.EyeWithoutToolOffset(),.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolCameraBlendCollisionTest,"MessControl.Camera.ToolModeBlend.CollisionRetractsImmediately",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolCameraBlendCollisionTest::RunTest(const FString&)
{
    auto* Settings=UMCGameUserSettings::Get();
    if(!TestNotNull(TEXT("Camera preferences are available"),Settings)) return false;
    const bool SavedSmoothing=Settings->GetCameraSmoothingEnabled();
    ON_SCOPE_EXIT {Settings->SetCameraSmoothingEnabled(SavedSmoothing);};
    Settings->SetCameraSmoothingEnabled(false);
    FToolModeCameraWorld W;
    if(!TestTrue(TEXT("Collision fixture has initialized tool ownership"),W.Ready())) return false;
    W.View(1.f/120);W.Hero->Inventory->ServerSelect(EMCToolSlot::Spray);W.View(1.f/120);
    const FVector Origin=W.Hero->GetActorLocation()+FVector(0,0,30);
    const FVector Ray=(W.Hero->Camera->GetComponentLocation()-Origin).GetSafeNormal();
    auto* Wall=W.World->SpawnActor<AActor>();auto* Collider=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Collider);Wall->AddInstanceComponent(Collider);Collider->SetBoxExtent(FVector(25,500,500));
    Collider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Collider->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collider->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);Collider->RegisterComponent();
    Wall->SetActorLocationAndRotation(Origin+Ray*450,Ray.Rotation());W.View(1.f/120);
    const float FirstBlocked=float(FVector::Distance(Origin,W.Hero->Camera->GetComponentLocation()));
    TestTrue(TEXT("A camera-only obstacle retracts the visible eye during the tool blend"),
        W.Arm->IsCollisionFixApplied() && FirstBlocked<430);
    Wall->SetActorLocation(Origin+Ray*220);W.View(1.f/120);
    const float CloserBlocked=float(FVector::Distance(Origin,W.Hero->Camera->GetComponentLocation()));
    TestTrue(TEXT("An even closer obstacle retracts on this update without tool-transition delay"),
        CloserBlocked<210 && CloserBlocked<FirstBlocked);
    TestTrue(TEXT("Collision does not stop or complete the independent tool transition"),
        W.Hero->SprayCameraOffset.Y>0 && W.Hero->SprayCameraOffset.Y<140);
    Collider->SetCollisionEnabled(ECollisionEnabled::NoCollision);W.View(1.f/120);
    TestTrue(TEXT("Removing the obstacle restores the current blended framing immediately with smoothing off"),
        W.Hero->Camera->GetComponentLocation().Equals(W.WantedEye(),.1f));
    return true;
}
#endif
