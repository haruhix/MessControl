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
        PS->QueueChoicesThroughLevel(2);PC->RefreshLevelPerkChoices();Advance(14);
    } else if(R.Stage==14 && Now-R.At>.2) {
        if(!PC->IsRewardMenuOpen()) {Finish(false,TEXT("reward panel"));return;}
        UIKey(EKeys::Gamepad_FaceButton_Top);Advance(15);
    } else if(R.Stage==15 && Now-R.At>.2) {
        if(!PS->bRogueRerollSpent || !PS->LevelUpOffer.PerkIDs.IsValidIndex(1)) {Finish(false,TEXT("Y reroll"));return;}
        R.Chosen=PS->LevelUpOffer.PerkIDs[1];R.StackBefore=PS->Perks->GetStacks(R.Chosen);
        UIKey(EKeys::Gamepad_DPad_Right);UIKey(EKeys::Gamepad_FaceButton_Bottom);Advance(16);
    } else if(R.Stage==16 && Now-R.At>.25) {
        const bool Pass=PS->PersonalChoicesTaken==1 && PS->PendingLevelChoices==0 && PS->Perks->GetStacks(R.Chosen)>R.StackBefore && !PC->IsRewardMenuOpen();
        Finish(Pass,TEXT("movement, camera, dead zone, tools, RT, LT, booster, pause, emotes, reward reroll and choice"));
    }
}
#endif
