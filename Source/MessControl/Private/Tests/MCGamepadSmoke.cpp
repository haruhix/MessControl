#include "MCValidationSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UMCValidationSubsystem::IsTickableWhenPaused() const
{
#if !UE_BUILD_SHIPPING
    return FParse::Param(FCommandLine::Get(),TEXT("MCGamepadTest"));
#else
    return false;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include <limits>
#include "Misc/AutomationTest.h"
#include "MCToolScrollInput.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCGripComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCToolScrollTests
{
    struct FWorld
    {
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
        AMCPlayerController* Controller=nullptr;
        AMCToothCharacter* Hero=nullptr;
        FWorld()
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
            auto* State=World->SpawnActor<AMCGameState>();World->SetGameState(State);
            State->Phase=EMCShiftPhase::Working;State->bLobbyWaiting=false;
            Controller=World->SpawnActor<AMCPlayerController>();
            // A standalone controller without ULocalPlayer is intentionally
            // nonlocal in this engine, as are remote listen-server controllers.
            Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine));
            auto* Identity=World->SpawnActor<AMCPlayerState>();Controller->SetPlayerState(Identity);
            Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
            Controller->Possess(Hero);Hero->Status->Initialize(100);Hero->GetCharacterMovement()->DisableMovement();
            Hero->SetCameraOrbitView(0,-30,1000);
        }
        ~FWorld() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    };

    struct FTriggeredValue : FInputActionInstance
    {
        FTriggeredValue(const UInputAction* Action,float Amount):FInputActionInstance(Action)
        {Value=FInputActionValue(Amount);TriggerEvent=ETriggerEvent::Triggered;}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolWheelCycleTest,"MessControl.Input.Tools.WheelCyclesWithoutZoom",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolWheelCycleTest::RunTest(const FString&)
{
    MCToolScrollTests::FWorld W;MCToolScrollInput::FCursor Cursor;
    if(!TestTrue(TEXT("The local input fixture dispatched pawn and inventory BeginPlay"),W.Hero->HasActorBegunPlay() && W.Hero->Inventory->HasBegunPlay() && W.Hero->IsLocallyControlled())) return false;
    const float Distance=W.Hero->CameraOrbitDistance;
    TestTrue(TEXT("Wheel up selects the next valid tool"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    TestEqual(TEXT("Brush advances to pickaxe"),W.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    MCToolScrollInput::Handle(*W.Hero,2.f,Cursor);
    TestEqual(TEXT("Two more notches select spray"),W.Hero->Inventory->Selected,EMCToolSlot::Spray);
    MCToolScrollInput::Handle(*W.Hero,1.f,Cursor);
    TestEqual(TEXT("The last tool wraps to brush"),W.Hero->Inventory->Selected,EMCToolSlot::Brush);
    MCToolScrollInput::Handle(*W.Hero,-1.f,Cursor);
    TestEqual(TEXT("Reverse scrolling wraps back to spray"),W.Hero->Inventory->Selected,EMCToolSlot::Spray);
    TestFalse(TEXT("A full revolution is not a same-slot mode toggle"),MCToolScrollInput::Handle(*W.Hero,4.f,Cursor));
    TestEqual(TEXT("Every wheel request leaves camera distance untouched"),W.Hero->CameraOrbitDistance,Distance);
    TestFalse(TEXT("Nonfinite wheel values are rejected"),MCToolScrollInput::Handle(*W.Hero,std::numeric_limits<float>::quiet_NaN(),Cursor));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolWheelGuardTest,"MessControl.Input.Tools.WheelRespectsModalAndActionLocks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolWheelGuardTest::RunTest(const FString&)
{
    MCToolScrollTests::FWorld W;MCToolScrollInput::FCursor Cursor;
    if(!TestTrue(TEXT("The lock fixture dispatched pawn and inventory BeginPlay"),W.Hero->HasActorBegunPlay() && W.Hero->Inventory->HasBegunPlay() && W.Hero->IsLocallyControlled())) return false;
    W.Controller->bShowMouseCursor=true;
    TestFalse(TEXT("Scrolling a modal cannot select gameplay tools"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    W.Controller->bShowMouseCursor=false;W.Controller->SetIgnoreMoveInput(true);
    TestFalse(TEXT("Loading/movement input barriers block tool scrolling"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    W.Controller->SetIgnoreMoveInput(false);
    W.World->GetGameState<AMCGameState>()->TeamToolUpgrades|=1<<(uint8(EMCToolUpgrade::Chainsaw)-1);
    W.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);W.Hero->Inventory->bChainsawRunning=true;
    if(!TestTrue(TEXT("The fixture has a selected, upgraded, actively running chainsaw"),W.Hero->Inventory->IsChainsawRunning())) return false;
    TestFalse(TEXT("An active chainsaw retains the existing tool lock"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    W.Hero->Inventory->bChainsawRunning=false;W.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);Cursor.Reset();
    auto* Other=W.World->SpawnActor<AMCToothCharacter>(FVector(1000,0,10000),FRotator::ZeroRotator);
    W.Hero->Grip->GrabbedPlayer=Other;
    TestFalse(TEXT("An incompatible player grip blocks scroll selection"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    W.Hero->Grip->GrabbedPlayer=nullptr;W.Hero->Status->State.Health=0;
    TestFalse(TEXT("A dead body cannot change tools"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    TestEqual(TEXT("Rejected requests never changed the authoritative slot"),W.Hero->Inventory->Selected,EMCToolSlot::Brush);
    W.Hero->Status->Initialize(100);
    TestTrue(TEXT("Normal scrolling resumes after those locks are removed"),MCToolScrollInput::Handle(*W.Hero,1.f,Cursor));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolWheelBindingsTest,"MessControl.Input.Tools.WheelAndBracketBindingsAreSeparate",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolWheelBindingsTest::RunTest(const FString&)
{
    MCToolScrollTests::FWorld W;
    if(!TestTrue(TEXT("The binding fixture dispatched pawn and inventory BeginPlay"),W.Hero->HasActorBegunPlay() && W.Hero->Inventory->HasBegunPlay() && W.Hero->IsLocallyControlled())) return false;
    auto* Input=NewObject<UEnhancedInputComponent>(W.Hero);W.Hero->SetupPlayerInputComponent(Input);
    const auto* Wheel=FindObject<UInputAction>(W.Hero,TEXT("ToolScrollAction"));
    const auto* Brackets=FindObject<UInputAction>(W.Hero,TEXT("BracketZoomAction"));
    if(!TestNotNull(TEXT("Wheel has its own tool action"),Wheel) || !TestNotNull(TEXT("Brackets have a separate zoom action"),Brackets)) return false;
    TestTrue(TEXT("The two inputs never share an action"),Wheel!=Brackets);
    TestTrue(TEXT("Holding a bracket does not repeat a zoom step each frame"),Brackets->Triggers.ContainsByPredicate([](const auto& Trigger) {return Trigger && Trigger->IsA<UInputTriggerPressed>();}));
    const UInputMappingContext* Context=nullptr;
    for(TObjectIterator<UInputMappingContext> It;It;++It) if(It->GetOuter()==W.Hero) {Context=*It;break;}
    if(!TestNotNull(TEXT("The character owns a real Enhanced Input mapping"),Context)) return false;
    int32 Digits=0;bool WheelMapped=false,LeftMapped=false,RightMapped=false;
    for(const auto& Mapping:Context->GetMappings()) {
        if(Mapping.Key==EKeys::MouseWheelAxis) {WheelMapped=true;TestTrue(TEXT("Wheel maps to tool selection only"),Mapping.Action==Wheel);}
        if(Mapping.Key==EKeys::RightBracket) {RightMapped=true;TestTrue(TEXT("Right bracket maps to zoom"),Mapping.Action==Brackets);}
        if(Mapping.Key==EKeys::LeftBracket) {
            LeftMapped=true;TestTrue(TEXT("Left bracket shares only the bracket zoom action"),Mapping.Action==Brackets);
            TestTrue(TEXT("Left bracket reverses the zoom direction"),Mapping.Modifiers.ContainsByPredicate([](const auto& Modifier) {return Modifier && Modifier->IsA<UInputModifierNegate>();}));
        }
        if(Mapping.Key==EKeys::One || Mapping.Key==EKeys::Two || Mapping.Key==EKeys::Three || Mapping.Key==EKeys::Four) ++Digits;
    }
    TestTrue(TEXT("Wheel and both camera zoom keys are mapped"),WheelMapped && LeftMapped && RightMapped);
    TestEqual(TEXT("Four direct tool shortcuts remain available as secondary input"),Digits,4);
    const float Distance=W.Hero->CameraOrbitDistance;
    int32 WheelBindings=0,ZoomBindings=0;
    for(const auto& Binding:Input->GetActionEventBindings()) {
        if(Binding->GetAction()==Wheel && Binding->GetTriggerEvent()==ETriggerEvent::Triggered) {
            ++WheelBindings;Binding->Execute(MCToolScrollTests::FTriggeredValue(Wheel,1.f));
        }
    }
    TestEqual(TEXT("One wheel event invokes one production selection route"),WheelBindings,1);
    TestEqual(TEXT("The bound wheel action selected the next tool"),W.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    TestEqual(TEXT("The bound wheel action did not zoom"),W.Hero->CameraOrbitDistance,Distance);
    for(const auto& Binding:Input->GetActionEventBindings()) if(Binding->GetAction()==Brackets) {
        ++ZoomBindings;Binding->Execute(MCToolScrollTests::FTriggeredValue(Brackets,1.f));
    }
    TestEqual(TEXT("The bracket action has one zoom handler"),ZoomBindings,1);
    TestTrue(TEXT("Right bracket zooms in through the existing camera route"),W.Hero->CameraOrbitDistance<Distance);
    TestEqual(TEXT("Bracket zoom does not select a tool"),W.Hero->Inventory->Selected,EMCToolSlot::Pickaxe);
    return true;
}
#endif

#if !UE_BUILD_SHIPPING
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCPerkComponent.h"
#include "MCMainMenuWidget.h"
#include "MCEmoteWidget.h"
#include "MCPerkChoiceWidget.h"
#include "MCRewardChest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

// Exercise the production Enhanced Input and Slate routes without requiring a USB pad.
void MCTickGamepadValidation(UWorld* World)
{
    struct FRun {
        TWeakObjectPtr<UWorld> World;
        double Started=0,At=0;
        int32 Stage=0,StackBefore=0;
        float Yaw=0,Pitch=0;
        FVector Start=FVector::ZeroVector;
        FName Chosen;
        TWeakObjectPtr<AMCRewardChest> Chest;
        TArray<FName> ChestCards;
    };
    static FRun R;
    const double Now=FPlatformTime::Seconds();
    if(R.World.Get()!=World) { R=FRun();R.World=World;R.Started=R.At=Now; }
    auto Finish=[](bool Pass,const TCHAR* Detail) {
        UE_LOG(LogTemp,Display,TEXT("MC_GAMEPAD_%s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Detail);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    if(Now-R.Started>45) {Finish(false,TEXT("timeout"));return;}
    auto* BasePC=World->GetFirstPlayerController();
    if(!BasePC || Now-R.Started<2) return;
    auto Key=[&](FKey K,EInputEvent Event,float Value=1) {
        auto Args=FInputKeyEventArgs::CreateSimulated(K,Event,Value,1,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice());
        Args.DeltaTime=World->GetDeltaSeconds();BasePC->InputKey(Args);
    };
    auto UIKey=[](FKey K) {
        const FKeyEvent Event(K,FModifierKeysState(),0,false,0,0);
        const bool Handled=FSlateApplication::Get().ProcessKeyDownEvent(Event);
        FSlateApplication::Get().ProcessKeyUpEvent(Event);
        UE_LOG(LogTemp,Display,TEXT("MC_GAMEPAD_UI key=%s stage=%d handled=%d"),*K.ToString(),R.Stage,Handled);
    };
    auto Advance=[&](int32 Next) {R.Stage=Next;R.At=Now;UE_LOG(LogTemp,Display,TEXT("MC_GAMEPAD_STAGE %d"),Next);};
    auto Menu=[&]() -> UMCMainMenuWidget* {
        for(TObjectIterator<UMCMainMenuWidget> It;It;++It)
            if(It->GetOwningPlayer()==BasePC && It->IsInViewport()) return *It;
        return nullptr;
    };
    auto HasText=[](UMCMainMenuWidget* Widget,const TCHAR* Expected) {
        bool Found=false;
        if(Widget && Widget->GetActiveMenuPageRoot()) UWidgetTree::ForWidgetAndChildren(Widget->GetActiveMenuPageRoot(),[&](UWidget* Child) {
            for(const UWidget* Ancestor=Child;Ancestor;Ancestor=Ancestor->GetParent())
                if(!Ancestor->IsVisible()) return;
            if(const auto* Text=Cast<UTextBlock>(Child)) Found|=Text->GetText().ToString()==Expected;
        });
        return Found;
    };
    if(FParse::Param(FCommandLine::Get(),TEXT("MCGamepadMenuTest"))) {
        if(!Menu()) return;
        if(R.Stage==0) {Menu()->FocusMenu();UIKey(EKeys::Gamepad_DPad_Down);Advance(1);}
        else if(R.Stage==1 && Now-R.At>.15) {UIKey(EKeys::Gamepad_FaceButton_Bottom);Advance(2);}
        else if(R.Stage==2 && Now-R.At>.2) {
            if(!HasText(Menu(),TEXT("CREATE LOBBY"))) {Finish(false,TEXT("main menu D-pad/A"));return;}
            UIKey(EKeys::Gamepad_FaceButton_Right);Advance(3);
        } else if(R.Stage==3 && Now-R.At>.15) {
            if(!HasText(Menu(),TEXT("Tutorial"))) {Finish(false,TEXT("main menu B"));return;}
            UIKey(EKeys::Gamepad_DPad_Down);UIKey(EKeys::Gamepad_DPad_Down);UIKey(EKeys::Gamepad_DPad_Down);Advance(4);
        } else if(R.Stage==4 && Now-R.At>.15) {UIKey(EKeys::Gamepad_FaceButton_Bottom);Advance(5);}
        else if(R.Stage==5 && Now-R.At>.2) {
            if(!HasText(Menu(),TEXT("SETTINGS"))) {Finish(false,TEXT("settings navigation"));return;}
            UIKey(EKeys::Gamepad_FaceButton_Right);Finish(true,TEXT("main menu, lobby page, settings, accept, back"));
        }
        return;
    }
    auto* PC=Cast<AMCPlayerController>(BasePC);
    auto* Hero=PC?Cast<AMCToothCharacter>(PC->GetPawn()):nullptr;
    auto* PS=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    if(!Hero || !PS || !PS->Perks) return;
    if(auto* GS=World->GetGameState<AMCGameState>()) {GS->Phase=EMCShiftPhase::Intermission;GS->PhaseEndsAt=GS->GetServerWorldTimeSeconds()+300;}
    if(R.Stage<=3) {
        Key(EKeys::Gamepad_LeftY,IE_Axis,R.Stage==1?.1f:R.Stage==2?.85f:0);
        Key(EKeys::Gamepad_RightX,IE_Axis,R.Stage==1?.1f:R.Stage==2?.8f:0);
        Key(EKeys::Gamepad_RightY,IE_Axis,R.Stage==3?.8f:0);
    }
    if(R.Stage==0) {
        Hero->SetActorLocation(FVector(0,0,1800));Hero->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Hero->ApplyCameraOrbitInput(FVector2D(.01,0));
        R.Start=Hero->GetActorLocation();R.Yaw=Hero->CameraOrbitYaw;Advance(1);
    } else if(R.Stage==1 && Now-R.At>.35) {
        if(!FMath::IsNearlyEqual(R.Yaw,Hero->CameraOrbitYaw,.05f) || FVector::Dist(R.Start,Hero->GetActorLocation())>1) {Finish(false,TEXT("stick dead zone"));return;}
        Advance(2);
    } else if(R.Stage==2 && Now-R.At>.8) {
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(R.Yaw,Hero->CameraOrbitYaw))<25 || FVector::Dist(R.Start,Hero->GetActorLocation())<25 || !PC->IsUsingGamepad()) {Finish(false,TEXT("left stick movement / right stick yaw / input hints"));return;}
        R.Pitch=Hero->CameraOrbitPitch;Advance(3);
    } else if(R.Stage==3 && Now-R.At>.35) {
        if(FMath::Abs(R.Pitch-Hero->CameraOrbitPitch)<10) {Finish(false,TEXT("right stick pitch"));return;}
        Key(EKeys::Gamepad_RightY,IE_Axis,0);Hero->GetCharacterMovement()->DisableMovement();
        Key(EKeys::Gamepad_DPad_Up,IE_Pressed);Advance(4);
    } else if(R.Stage==4 && Now-R.At>.12) {
        Key(EKeys::Gamepad_DPad_Up,IE_Released,0);
        if(Hero->Inventory->Selected!=EMCToolSlot::Pickaxe) {Finish(false,TEXT("D-pad tool selection"));return;}
        Key(EKeys::Gamepad_RightTrigger,IE_Pressed);Advance(5);
    } else if(R.Stage==5 && Now-R.At>.12) {
        if(!Hero->IsPrimaryHeld()) {Finish(false,TEXT("RT hold"));return;}
        Key(EKeys::Gamepad_RightTrigger,IE_Released,0);Advance(6);
    } else if(R.Stage==6 && Now-R.At>1.2) {
        if(Hero->IsPrimaryHeld()) {Finish(false,TEXT("RT release"));return;}
        Key(EKeys::Gamepad_DPad_Right,IE_Pressed);Advance(7);
    } else if(R.Stage==7 && Now-R.At>.12) {
        Key(EKeys::Gamepad_DPad_Right,IE_Released,0);Key(EKeys::Gamepad_LeftTrigger,IE_Pressed);Advance(8);
    } else if(R.Stage==8 && Now-R.At>.2) {
        Key(EKeys::Gamepad_LeftTrigger,IE_Released,0);
        if(Hero->Inventory->Selected!=EMCToolSlot::Knife || Hero->ValidatedSwingCount==0) {Finish(false,TEXT("LT attack"));return;}
        PS->Perks->AddBoosterCharge(TEXT("Fallback_Grenade"));Key(EKeys::Gamepad_FaceButton_Right,IE_Pressed);Advance(9);
    } else if(R.Stage==9 && Now-R.At>.15) {
        Key(EKeys::Gamepad_FaceButton_Right,IE_Released,0);
        if(!PS->Perks->bGrenadeArmed) {Finish(false,TEXT("B booster"));return;}
        Key(EKeys::Gamepad_Special_Right,IE_Pressed);Advance(10);
    } else if(R.Stage==10 && Now-R.At>.2) {
        Key(EKeys::Gamepad_Special_Right,IE_Released,0);
        if(!Menu() || !PC->IsMoveInputIgnored()) {Finish(false,TEXT("Start opens pause"));return;}
        R.Yaw=Hero->CameraOrbitYaw;Key(EKeys::Gamepad_RightX,IE_Axis,1);UIKey(EKeys::Gamepad_DPad_Down);Advance(11);
    } else if(R.Stage==11 && Now-R.At>.15) {UIKey(EKeys::Gamepad_FaceButton_Bottom);Advance(12);}
    else if(R.Stage==12 && Now-R.At>.2) {
        if(!PC->EmoteWidget || !PC->EmoteWidget->IsVisible() || !FMath::IsNearlyEqual(R.Yaw,Hero->CameraOrbitYaw,.05f)) {Finish(false,TEXT("pause navigation / emotes / camera blocked in UI"));return;}
        UIKey(EKeys::Gamepad_FaceButton_Right);Advance(13);
    } else if(R.Stage==13 && Now-R.At>.2) {
        if(PC->EmoteWidget->IsVisible() || PC->IsMoveInputIgnored()) {Finish(false,TEXT("B returns to gameplay"));return;}
        // Isolate gamepad presentation/selection of an actual authoritative
        // chest. Placement/approach/lockpicking are covered by chest checks;
        // no retired XP queue may manufacture the reward panel.
        const FVector At=Hero->GetActorLocation()+FVector(150,0,0);
        auto* Chest=World->SpawnActor<AMCRewardChest>(At,FRotator::ZeroRotator);
        if(!Chest || !PS->Perks->BuildRogueOffer(EMCPerkPolarity::Positive,41,R.ChestCards)) {Finish(false,TEXT("chest fixture offer"));return;}
        Chest->InitializeReward(At,At,41,PS->Perks->GetPerkTable(),EMCRewardSelectionPolicy::ChooseOne,nullptr);
        Chest->ForceMimicForTest(false);Chest->LootIDs=R.ChestCards;Chest->OpeningPlayer=Hero;
        Hero->RewardInteraction=Chest;Chest->Stage=EMCRewardChestStage::Open;R.Chest=Chest;
        PC->ClientShowPerkChoices(Chest,R.ChestCards,EMCPerkPolarity::Positive);Advance(14);
    } else if(R.Stage==14 && Now-R.At>.2) {
        if(!PC->IsRewardMenuOpen()) {Finish(false,TEXT("reward panel"));return;}
        UIKey(EKeys::Gamepad_FaceButton_Top);Advance(15);
    } else if(R.Stage==15 && Now-R.At>.2) {
        if(!R.Chest.IsValid() || R.Chest->LootIDs!=R.ChestCards || PS->bRogueRerollSpent
            || PS->LevelUpOffer.IsValid() || !PC->IsRewardMenuOpen()) {Finish(false,TEXT("Y cannot invoke removed level reroll on chest cards"));return;}
        R.Chosen=R.ChestCards[1];R.StackBefore=PS->Perks->GetStacks(R.Chosen);
        UIKey(EKeys::Gamepad_DPad_Right);UIKey(EKeys::Gamepad_FaceButton_Bottom);Advance(16);
    } else if(R.Stage==16 && Now-R.At>.25) {
        const bool Pass=R.Chest.IsValid() && R.Chest->ClaimedMask==7 && R.Chest->Stage==EMCRewardChestStage::Exhausted
            && PS->PendingLevelChoices==0 && PS->Perks->GetStacks(R.Chosen)>R.StackBefore && !PC->IsRewardMenuOpen();
        Finish(Pass,TEXT("movement, camera, dead zone, tools, RT, LT, booster, pause, emotes, physical chest choice without XP/reroll"));
    }
}
#endif
