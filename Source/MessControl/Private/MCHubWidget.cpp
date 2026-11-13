#include "MCHubWidget.h"
#include "MCUnifiedHubWidget.h"
#include "MCGameplayUIFactory.h"
#include "CoreGlobals.h"
#include "Fonts/FontMeasure.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

#include "MCHubGameState.h"
#include "MCHubGrillStation.h"
#include "MCHubReadyZone.h"
#include "MCHubBasketball.h"
#include "MCHubFishingPresentation.h"
#include "MCFishingRodComponent.h"
#include "MCToothCharacter.h"
#include "MCHubTypes.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCHubEconomyComponent.h"
#include "MCRaidLoadoutWidget.h"
#include "MCNativeCommerceUICopy.h"
#include "MCGameplayHUD.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "MCSteamSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/TextRenderComponent.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Misc/ScopeExit.h"
#include "Rendering/DrawElements.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "MCHub"

int32 UMCHubCurrencyIcon::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    const FVector2D Size=Geometry.GetLocalSize();const float Unit=FMath::Min(Size.X,Size.Y);
    auto Point=[&](double X,double Y) {return FVector2D(Size.X*.5+(X-.5)*Unit,Size.Y*.5+(Y-.5)*Unit);};
    const FLinearColor Color=Tint*Style.GetColorAndOpacityTint();
    auto Stroke=[&](const TArray<FVector2D>& Points,float Width) {
        FSlateDrawElement::MakeLines(Elements,Base+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
    };
    auto Circle=[&](double X,double Y,double RX,double RY) {
        TArray<FVector2D> Points;for(int32 I=0;I<=32;++I) {const double A=I*2.*PI/32.;Points.Add(Point(X+RX*FMath::Cos(A),Y+RY*FMath::Sin(A)));}Stroke(Points,LineWidth);
    };
    if(bFish) {
        // Rounded body, small eye and a clear fork-like tail remain legible at 28 px.
        Circle(.57,.5,.28,.21);Stroke({Point(.31,.49),Point(.1,.27),Point(.1,.73),Point(.31,.51)},LineWidth);
        Circle(.69,.45,.018,.018);Stroke({Point(.49,.34),Point(.43,.24),Point(.62,.3)},LineWidth*.8f);
    } else {
        Circle(.5,.5,.37,.37);Circle(.5,.5,.26,.26);
        Stroke({Point(.5,.35),Point(.5,.65)},LineWidth);Stroke({Point(.43,.39),Point(.5,.35)},LineWidth);
    }
    return Base+1;
}

namespace MCHubPresentation
{
    constexpr float FooterHeight=40.f;

    UCanvasPanel* AuthoredCompactHUD(const UWidgetTree* Tree)
    {
        return Tree?Tree->FindWidget<UCanvasPanel>(TEXT("HubCompactHUD")):nullptr;
    }

    const UMCFishingRodComponent* ActiveFishingMeter(const APlayerController* PC,const AMCHubGameState* Hub)
    {
        if(!PC || PC->bShowMouseCursor || !Hub || Hub->Phase!=EMCHubPhase::Gathering) return nullptr;
        const auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn());
        const auto* Rod=Hero?Hero->FishingRod.Get():nullptr;
        return Rod && (Rod->IsChargingCast() || Rod->IsCatchChallengeActive())?Rod:nullptr;
    }

    // The selector can be an always-loaded sublevel of Hub. Its saved actors,
    // rather than the persistent map name, are the presentation contract.
    bool HasSavedStage(const UWorld* World)
    {
        uint8 Days=0;
        bool bCamera=false;
        if(World) for(TActorIterator<AActor> It(World);It;++It)
        {
            bCamera|=It->ActorHasTag(TEXT("Menu3D_Camera")) && It->FindComponentByClass<UCameraComponent>()!=nullptr;
            for(int32 Day=1;Day<=7;++Day)
                if(It->ActorHasTag(FName(*FString::Printf(TEXT("Menu3D_DAY%02d"),Day)))) Days|=uint8(1u<<(Day-1));
        }
        return bCamera && Days==0x7f;
    }

    // Blueprint Construct/ShowPage may select their old front-end camera.
    // Constructing this hidden cosmetic widget must never change Hub gameplay.
    struct FPreservePlayerView
    {
        APlayerController* Player;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<AActor> Target;
        FRotator Rotation;
        bool bAutoManage;
        explicit FPreservePlayerView(APlayerController* InPlayer)
            :Player(InPlayer),Pawn(InPlayer->GetPawn()),Target(InPlayer->GetViewTarget()),
             Rotation(InPlayer->GetControlRotation()),bAutoManage(InPlayer->bAutoManageActiveCameraTarget) {}
        ~FPreservePlayerView()
        {
            if(!IsValid(Player)) return;
            Player->bAutoManageActiveCameraTarget=bAutoManage;
            AActor* Restore=Target.IsValid()?Target.Get():Pawn.Get();
            if(IsValid(Restore) && Player->GetViewTarget()!=Restore) Player->SetViewTarget(Restore);
            if(!Player->GetControlRotation().Equals(Rotation)) Player->SetControlRotation(Rotation);
        }
    };

    // The embedded Blueprint still runs its old Construct/ShowPage keyboard
    // focus nodes. Keep those temporary requests out of the real Hub/loading
    // input, including a deferred focus reply when Slate has no path yet.
    struct FPreservePlayerFocus
    {
        TWeakObjectPtr<ULocalPlayer> LocalPlayer;
        TMap<uint32,TWeakPtr<SWidget>> PreviousFocus;
        FReply PreviousOperations=FReply::Unhandled();
        bool bCapturedOperations=false;
        explicit FPreservePlayerFocus(APlayerController* Player)
            :LocalPlayer(Player?Player->GetLocalPlayer():nullptr)
        {
            auto* Local=LocalPlayer.Get();
            if(!Local || !FSlateApplication::IsInitialized()) return;
            PreviousOperations=Local->GetSlateOperations();
            bCapturedOperations=true;
            auto& Slate=FSlateApplication::Get();
            const int32 KeyboardUser=Slate.GetUserIndexForKeyboard();
            if(KeyboardUser>=0) PreviousFocus.Add(uint32(KeyboardUser),Slate.GetUserFocusedWidget(KeyboardUser));
            if(const auto User=Local->GetSlateUser())
                PreviousFocus.Add(uint32(User->GetUserIndex()),Slate.GetUserFocusedWidget(User->GetUserIndex()));
        }
        ~FPreservePlayerFocus()
        {
            if(FSlateApplication::IsInitialized()) {
                auto& Slate=FSlateApplication::Get();
                for(const auto& Entry:PreviousFocus) {
                    const auto Focus=Entry.Value.Pin();
                    if(Slate.GetUserFocusedWidget(Entry.Key)==Focus) continue;
                    if(Focus.IsValid() && Focus->SupportsKeyboardFocus() && Focus->IsEnabled() && Focus->GetVisibility().IsVisible())
                        Slate.SetUserFocus(Entry.Key,Focus,EFocusCause::SetDirectly);
                    else Slate.ClearUserFocus(Entry.Key,EFocusCause::SetDirectly);
                }
            }
            if(bCapturedOperations) if(auto* Local=LocalPlayer.Get()) Local->GetSlateOperations()=PreviousOperations;
        }
    };

    bool SetNumber(UObject* Object,FName Name,double Value)
    {
        if(!Object) return false;
        auto* Property=FindFProperty<FNumericProperty>(Object->GetClass(),Name);
        if(!Property) return false;
        void* Address=Property->ContainerPtrToValuePtr<void>(Object);
        if(Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address,Value);
        else if(Property->IsInteger()) Property->SetIntPropertyValue(Address,int64(Value));
        else return false;
        return true;
    }

    bool SetFlag(UObject* Object,FName Name,bool Value)
    {
        auto* Property=Object?FindFProperty<FBoolProperty>(Object->GetClass(),Name):nullptr;
        if(!Property) return false;
        Property->SetPropertyValue_InContainer(Object,Value);return true;
    }

    void SetLegacyButtonFocusable(UButton* Button,bool bFocusable)
    {
        if(!Button) return;
        // The reflection flag prepares the known Construct targets before
        // Slate exists. Afterwards these visual-only legacy buttons are
        // disabled through UMG's public API, which updates the live Slate tree
        // and prevents navigation without touching protected SButton state.
        SetFlag(Button,TEXT("IsFocusable"),bFocusable);
        Button->SetIsEnabled(bFocusable);
    }

    bool Call(UObject* Object,FName Name)
    {
        auto* Function=Object?Object->FindFunction(Name):nullptr;
        if(!Function || Function->NumParms!=0) return false;
        Object->ProcessEvent(Function,nullptr);return true;
    }

    bool CallInt(UObject* Object,FName Name,FName Parameter,int32 Value)
    {
        auto* Function=Object?Object->FindFunction(Name):nullptr;
        auto* Property=Function?FindFProperty<FIntProperty>(Function,Parameter):nullptr;
        if(!Function || Function->NumParms!=1 || !Property || !Property->HasAnyPropertyFlags(CPF_Parm)) return false;
        FStructOnScope Parameters(Function);
        Property->SetPropertyValue_InContainer(Parameters.GetStructMemory(),Value);
        Object->ProcessEvent(Function,Parameters.GetStructMemory());return true;
    }

    bool DisableAutonomousTick(UUserWidget* Widget)
    {
        auto* Property=Widget?FindFProperty<FEnumProperty>(Widget->GetClass(),TEXT("TickFrequency")):nullptr;
        if(!Property || !Property->GetUnderlyingProperty()) return false;
        Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Widget),int64(EWidgetTickFrequency::Never));
        Widget->UpdateCanTick();
        return Widget->GetDesiredTickFrequency()==EWidgetTickFrequency::Never;
    }

    bool HasStageActors(const UUserWidget* Widget,int32 Day)
    {
        const FName Name(*FString::Printf(TEXT("Day%dActors"),Day));
        const auto* Property=Widget?FindFProperty<FArrayProperty>(Widget->GetClass(),Name):nullptr;
        if(!Property || !CastField<FObjectPropertyBase>(Property->Inner)) return false;
        FScriptArrayHelper Values(Property,Property->ContainerPtrToValuePtr<void>(Widget));
        const auto* ObjectProperty=CastFieldChecked<FObjectPropertyBase>(Property->Inner);
        for(int32 Index=0;Index<Values.Num();++Index)
            if(IsValid(ObjectProperty->GetObjectPropertyValue(Values.GetRawPtr(Index)))) return true;
        return false;
    }

    bool HasCamera(const UWorld* World)
    {
        if(World) for(TActorIterator<AActor> It(World);It;++It)
            if(It->ActorHasTag(TEXT("Menu3D_Camera")) && It->FindComponentByClass<UCameraComponent>()) return true;
        return false;
    }

    void HideLegacyControls(UUserWidget* Widget,bool bDisableFocus=false)
    {
        if(!Widget || !Widget->WidgetTree) return;
        // The authored stage retains its visual labels and animation. Its old
        // travel/settings controls are never focused, hit-tested or invoked.
        const FName Hidden[]={TEXT("DaysBrand"),TEXT("DaysHint"),TEXT("DaysBackButton"),TEXT("DaysHeader"),TEXT("DaysFooter"),
            TEXT("DaysScrollHint"),TEXT("DayCounter"),TEXT("LaunchPage")};
        for(FName Name:Hidden) if(auto* Child=Widget->WidgetTree->FindWidget(Name)) Child->SetVisibility(ESlateVisibility::Collapsed);
        for(int32 Day=1;Day<=3;++Day)
            if(auto* Child=Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Day%dCavityControls"),Day))))
                Child->SetVisibility(ESlateVisibility::Hidden);
        if(bDisableFocus) {
            // Keep row geometry for the authored stage without its disabled-button tint.
            for(int32 Day=1;Day<=7;++Day)
                if(auto* Button=Widget->WidgetTree->FindWidget<UButton>(FName(*FString::Printf(TEXT("Day%dButton"),Day)))) {
                    FButtonStyle Style=Button->GetStyle();
                    Style.SetDisabled(FSlateNoResource());
                    Button->SetStyle(Style);
                }
            Widget->WidgetTree->ForEachWidget([](UWidget* Child)
            {
                // InitIsFocusable is protected in UE 5.8. Set its reflected
                // instance value before the authored Slate buttons are built.
                if(auto* Button=Cast<UButton>(Child)) SetLegacyButtonFocusable(Button,false);
                if(auto* UserWidget=Cast<UUserWidget>(Child)) UserWidget->SetIsFocusable(false);
            });
        }
        Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    void SetText(UTextBlock* Widget,const FText& Text)
    {
        if(Widget && !Widget->GetText().EqualTo(Text)) Widget->SetText(Text);
    }
}

UMCHubActionButton::UMCHubActionButton() {InitIsFocusable(true);}
void UMCHubActionButton::Configure(UMCHubWidget* InOwner,EMCHubMenuAction InAction,int32 InDay,bool bFocusable)
{
    HubOwner=InOwner;Action=InAction;Day=InDay;
    InitIsFocusable(bFocusable);
    OnClicked.AddUniqueDynamic(this,&ThisClass::Clicked);
}
void UMCHubActionButton::Clicked()
{
    if(GetIsEnabled()) if(auto* Owner=HubOwner.Get()) Owner->HandleAction(Action,Day);
}

UMCHubWidget::UMCHubWidget()
{
    SetIsFocusable(true);
    SelectionPresentationClass=TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C")));
    RaidLoadoutClass=TSoftClassPtr<UMCRaidLoadoutWidget>(FSoftObjectPath(
        TEXT("/Game/Gameplay/UI/V2/WBP_UIRaidLoadout.WBP_UIRaidLoadout_C")));
}

AMCHubGameState* UMCHubWidget::State() const
{
    if(BoundState.IsValid()) return BoundState.Get();
    return GetWorld()?GetWorld()->GetGameState<AMCHubGameState>():nullptr;
}
bool UMCHubWidget::IsLocalHost() const
{
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* PS=PC?PC->GetPlayerState<AMCPlayerState>():nullptr;
    return PC && PC->IsLocalController() && PS && PS->bSessionHost;
}
void UMCHubWidget::Init(AMCHubGameState* InState)
{
    if(BoundState.Get()!=InState)
    {
        if(auto* Previous=BoundState.Get()) Previous->OnHubStateChanged.RemoveDynamic(this,&ThisClass::RefreshFromState);
        BoundState=InState;
        LastStateSignature.Reset();
    }
    if(InState) InState->OnHubStateChanged.AddUniqueDynamic(this,&ThisClass::RefreshFromState);
    BuildPresentationTree();RefreshFromState();
}

UTextBlock* UMCHubWidget::AddLabel(UVerticalBox* Parent,FName Name,int32 Size,bool bBold)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
    Label->SetAutoWrapText(true);
    Parent->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,3));
    BindStyle(Name,EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,Size,bBold);
    return Label;
}
UMCHubActionButton* UMCHubWidget::AddAction(UVerticalBox* Parent,FName Name,EMCHubMenuAction Action,int32 Day)
{
    auto* Button=MakeAction(Name,Action,Day);
    Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,4));
    return Button;
}
UMCHubActionButton* UMCHubWidget::MakeAction(FName Name,EMCHubMenuAction Action,int32 Day)
{
    auto* Button=WidgetTree->ConstructWidget<UMCHubActionButton>(UMCHubActionButton::StaticClass(),Name);
    Button->Configure(this,Action,Day);
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("%sLabel"),*Name.ToString())));
    Label->SetJustification(ETextJustify::Center);Label->SetAutoWrapText(true);
    Label->SetMinDesiredWidth(240.f);
    auto* ContentSlot=CastChecked<UButtonSlot>(Button->AddChild(Label));
    ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);
    return Button;
}
void UMCHubWidget::BuildPresentationTree()
{
    if(!WidgetTree || (BoundPresentationRoot.IsValid() && BoundPresentationRoot.Get()==WidgetTree->RootWidget)) return;
    if(WidgetTree->RootWidget)
    {
        // The Designer owns hierarchy, dimensions and style. Bind controls by
        // their stable names instead of replacing the authored root.
        bAuthoredPresentation=true;
        BindPresentationTree();
        EnsureRaidLoadoutPresentation();
        return;
    }
    bAuthoredPresentation=false;
    // The authored mouths and their labels share a 1600 x 900 camera contract.
    // Fit our controls to that same frame so small windows keep every action.
    auto* ViewportScale=WidgetTree->ConstructWidget<UScaleBox>();ViewportScale->SetStretch(EStretch::ScaleToFit);
    auto* DesignFrame=WidgetTree->ConstructWidget<USizeBox>();DesignFrame->SetWidthOverride(1600);DesignFrame->SetHeightOverride(900);
    ViewportScale->AddChild(DesignFrame);WidgetTree->RootWidget=ViewportScale;
    RootCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HubCanvas"));
    DesignFrame->AddChild(RootCanvas);
    PresentationHost=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("HubMouthPresentation"));
    auto* StageSlot=RootCanvas->AddChildToCanvas(PresentationHost);
    StageSlot->SetAnchors(FAnchors(0,0,1,1));StageSlot->SetOffsets(FMargin(0));
    PresentationHost->SetVisibility(ESlateVisibility::HitTestInvisible);

    ReadinessPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubReadinessPanel"));
    ReadinessPanel->SetPadding(FMargin(16,12));
    auto* ReadySize=WidgetTree->ConstructWidget<USizeBox>();ReadySize->SetWidthOverride(350.f);ReadySize->AddChild(ReadinessPanel);
    auto* ReadySlot=RootCanvas->AddChildToCanvas(ReadySize);ReadySlot->SetAutoSize(true);ReadySlot->SetPosition(FVector2D(24,24));ReadySlot->SetZOrder(1);
    auto* ReadyBody=WidgetTree->ConstructWidget<UVerticalBox>();ReadinessPanel->AddChild(ReadyBody);
    ReadinessTitle=AddLabel(ReadyBody,TEXT("HubReadinessTitle"),24,true);
    ReadinessCount=AddLabel(ReadyBody,TEXT("HubReadinessCount"),22,true);
    PlayerNames=AddLabel(ReadyBody,TEXT("HubPlayerNames"),18);
    ReadinessHint=AddLabel(ReadyBody,TEXT("HubReadinessHint"),18);
    OpenButton=AddAction(ReadyBody,TEXT("HubOpenSelection"),EMCHubMenuAction::OpenSelection);
    BasketballHint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubBasketballHint"));
    BasketballHint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20));
    BasketballHint->SetJustification(ETextJustify::Center);
    BasketballHint->SetShadowColorAndOpacity(FLinearColor::Black);BasketballHint->SetShadowOffset(FVector2D(1,1));
    auto* BallHintSlot=RootCanvas->AddChildToCanvas(BasketballHint);
    BallHintSlot->SetAnchors(FAnchors(.5f,1.f));BallHintSlot->SetAlignment(FVector2D(.5f,1.f));
    BallHintSlot->SetPosition(FVector2D(0,-116));BallHintSlot->SetAutoSize(true);BallHintSlot->SetZOrder(3);
    BasketballReticle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubBasketballReticle"));
    BasketballReticle->SetText(FText::GetEmpty());
    BasketballReticle->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),24));
    BasketballReticle->SetShadowColorAndOpacity(FLinearColor::Black);BasketballReticle->SetShadowOffset(FVector2D(1,1));
    auto* BallAimSlot=RootCanvas->AddChildToCanvas(BasketballReticle);
    BallAimSlot->SetAnchors(FAnchors(.5f,.5f));BallAimSlot->SetAlignment(FVector2D(.5f,.5f));
    BallAimSlot->SetAutoSize(true);BallAimSlot->SetZOrder(3);
    BasketballHint->SetVisibility(ESlateVisibility::Collapsed);BasketballReticle->SetVisibility(ESlateVisibility::Collapsed);
    BindStyle(TEXT("HubBasketballHint"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,20,true);
    BindStyle(TEXT("HubBasketballReticle"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,24,true);

    // Three invisible hit regions follow the real mouth row geometry. The
    // original labels remain visible; there is no second seven-day catalogue.
    for(int32 Index=0;Index<3;++Index)
    {
        auto* HitArea=WidgetTree->ConstructWidget<UMCHubActionButton>(UMCHubActionButton::StaticClass(),FName(*FString::Printf(TEXT("HubMouthHit%d"),Index+1)));
        HitArea->Configure(this,EMCHubMenuAction::EnterDestination,Index+1,false);
        FButtonStyle Transparent;
        Transparent.SetNormal(FSlateNoResource());Transparent.SetHovered(FSlateNoResource());
        Transparent.SetPressed(FSlateNoResource());Transparent.SetDisabled(FSlateNoResource());
        HitArea->SetStyle(Transparent);HitArea->SetVisibility(ESlateVisibility::Collapsed);
        RootCanvas->AddChildToCanvas(HitArea)->SetZOrder(1);
        MouthHitAreas.Add(HitArea);
    }
    SelectionPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubSelectionFooter"));
    SelectionPanel->SetBrush(FSlateNoResource());SelectionPanel->SetPadding(FMargin(8,4));
    auto* SelectionSize=WidgetTree->ConstructWidget<USizeBox>();SelectionSize->SetWidthOverride(1504.f);SelectionSize->AddChild(SelectionPanel);
    auto* SelectSlot=RootCanvas->AddChildToCanvas(SelectionSize);SelectSlot->SetAnchors(FAnchors(.5,1));SelectSlot->SetAlignment(FVector2D(.5,1));SelectSlot->SetPosition(FVector2D(0,-8));SelectSlot->SetAutoSize(true);SelectSlot->SetZOrder(2);
    auto* FooterBody=WidgetTree->ConstructWidget<UHorizontalBox>();SelectionPanel->AddChild(FooterBody);
    auto* Summary=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* SummarySlot=FooterBody->AddChildToHorizontalBox(Summary);SummarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));SummarySlot->SetVerticalAlignment(VAlign_Center);SummarySlot->SetPadding(FMargin(0,0,20,0));
    SelectionTitle=AddLabel(Summary,TEXT("HubSelectionTitle"),20,true);
    SelectionStatus=AddLabel(Summary,TEXT("HubSelectionStatus"),18);
    SelectionStatus->SetWrapTextAt(800.f);
    LaunchButton=MakeAction(TEXT("HubLaunch"),EMCHubMenuAction::Launch);
    ReturnButton=MakeAction(TEXT("HubReturn"),EMCHubMenuAction::Return);
    for(auto* Button:{LaunchButton.Get(),ReturnButton.Get()})
    {
        auto* ButtonSlot=FooterBody->AddChildToHorizontalBox(Button);ButtonSlot->SetVerticalAlignment(VAlign_Center);ButtonSlot->SetPadding(FMargin(8,0));
    }
    BindStyle(TEXT("HubReadinessPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
    // Saved defaults intentionally follow the shared palette. Removing an explicit
    // binding in Designer lets an artist give that control an independent style.
    BindStyle(TEXT("HubOpenSelection"),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
    BindStyle(TEXT("HubLaunch"),EMCUIColorToken::Accent,EMCUIStyleTarget::Button);
    BindStyle(TEXT("HubReturn"),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
    for(const TCHAR* Name:{TEXT("HubOpenSelectionLabel"),TEXT("HubLaunchLabel"),TEXT("HubReturnLabel")})
        BindStyle(Name,EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,18,true);
    BindPresentationTree();
    EnsureRaidLoadoutPresentation();
    ApplyTheme();
}

void UMCHubWidget::EnsureRaidLoadoutPresentation()
{
    auto* Player=GetOwningPlayer();
    if(RaidLoadoutView || IsDesignTime() || !GetWorld() || !GetWorld()->IsGameWorld()
        || !Player || !Player->IsLocalController() || !WidgetTree || !WidgetTree->RootWidget) return;
    UClass* Class=RaidLoadoutClass.LoadSynchronous();
    if(!Class) Class=UMCRaidLoadoutWidget::StaticClass();
    RaidLoadoutView=CreateWidget<UMCRaidLoadoutWidget>(Player,Class);
    if(!RaidLoadoutView) return;
    RaidLoadoutView->SetBackend(this);
    auto* Viewport=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("HubRaidViewport"));
    Viewport->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* StageSlot=Viewport->AddChildToOverlay(WidgetTree->RootWidget);
    StageSlot->SetHorizontalAlignment(HAlign_Fill);StageSlot->SetVerticalAlignment(VAlign_Fill);
    auto* RaidSlot=Viewport->AddChildToOverlay(RaidLoadoutView);
    RaidSlot->SetHorizontalAlignment(HAlign_Fill);RaidSlot->SetVerticalAlignment(VAlign_Fill);
    WidgetTree->RootWidget=Viewport;
    BoundPresentationRoot=Viewport;
}

void UMCHubWidget::BindPresentationTree()
{
    if(!WidgetTree || BoundPresentationRoot.Get()==WidgetTree->RootWidget) return;
    if(BoundPresentationRoot.Get()!=WidgetTree->RootWidget) ResetSelectionPresentation();
    RootCanvas=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HubCanvas")));
    PresentationHost=Cast<UOverlay>(WidgetTree->FindWidget(TEXT("HubMouthPresentation")));
    ReadinessPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubReadinessPanel")));
    SelectionPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubSelectionFooter")));
    ReadinessTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadinessTitle")));
    ReadinessCount=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadinessCount")));
    PlayerNames=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubPlayerNames")));
    ReadinessHint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadinessHint")));
    EnsureGatheringGuidance();
    // Older saved Hub trees predate basketball. Bind artist-authored controls
    // when present; add just the two missing controls without replacing layout.
    auto BallLabel=[this](FName Name,int32 FontSize,bool bHint)->UTextBlock*
    {
        if(auto* Existing=WidgetTree->FindWidget(Name)) return Cast<UTextBlock>(Existing);
        if(!RootCanvas) return nullptr;
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        if(const auto* ThemeData=GetResolvedTheme()) Label->SetFont(ThemeData->MakeGameplayFont(FontSize,true));
        Label->SetJustification(ETextJustify::Center);
        Label->SetShadowColorAndOpacity(FLinearColor::Black);Label->SetShadowOffset(FVector2D(1,1));
        if(!bHint) Label->SetText(FText::GetEmpty());
        auto* LabelSlot=RootCanvas->AddChildToCanvas(Label);
        LabelSlot->SetAnchors(FAnchors(.5f,bHint?1.f:.5f));
        LabelSlot->SetAlignment(FVector2D(.5f,bHint?1.f:.5f));
        LabelSlot->SetPosition(FVector2D(0,bHint?-36.f:0.f));
        LabelSlot->SetAutoSize(true);LabelSlot->SetZOrder(3);
        Label->SetVisibility(ESlateVisibility::Collapsed);
        BindStyle(Name,EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,FontSize,true);
        return Label;
    };
    BasketballHint=BallLabel(TEXT("HubBasketballHint"),20,true);
    BasketballReticle=BallLabel(TEXT("HubBasketballReticle"),24,false);
    // The source snapshot paints the same bracket reticle in Current and New UI.
    if(BasketballReticle) BasketballReticle->SetText(FText::GetEmpty());
    FishingPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubFishingPanel")));
    FishingCount=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubFishingCount")));
    if(!FishingCount && RootCanvas) {
        if(!FishingPanel) {
            FishingPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubFishingPanel"));
            FishingPanel->SetPadding(FMargin(16,10));FishingPanel->SetBrushColor(FLinearColor(.02f,.1f,.16f,.94f));
            auto* CountSlot=RootCanvas->AddChildToCanvas(FishingPanel);
            CountSlot->SetAnchors(FAnchors(1,0));CountSlot->SetAlignment(FVector2D(1,0));
            CountSlot->SetPosition(FVector2D(-24,24));CountSlot->SetAutoSize(true);CountSlot->SetZOrder(4);
            BindStyle(TEXT("HubFishingPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
        }
        FishingCount=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubFishingCount"));
        FishingCount->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),22));
        FishingCount->SetColorAndOpacity(FSlateColor(FLinearColor::White));FishingPanel->SetContent(FishingCount);
        BindStyle(TEXT("HubFishingCount"),EMCUIColorToken::OnMarine,EMCUIStyleTarget::Text,22,true);
    }
    SelectionTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubSelectionTitle")));
    SelectionStatus=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubSelectionStatus")));
    if(SelectionTitle) SelectionTitle->SetVisibility(ESlateVisibility::Collapsed);
    if(SelectionPanel) {
        SelectionPanel->SetBrush(FSlateNoResource());
        if(auto* Bounds=Cast<USizeBox>(SelectionPanel->GetParent())) Bounds->ClearWidthOverride();
    }
    OpenButton=Cast<UMCHubActionButton>(WidgetTree->FindWidget(TEXT("HubOpenSelection")));
    LaunchButton=Cast<UMCHubActionButton>(WidgetTree->FindWidget(TEXT("HubLaunch")));
    ReturnButton=Cast<UMCHubActionButton>(WidgetTree->FindWidget(TEXT("HubReturn")));
    if(OpenButton) OpenButton->Configure(this,EMCHubMenuAction::OpenSelection);
    if(LaunchButton) LaunchButton->Configure(this,EMCHubMenuAction::Launch);
    if(ReturnButton) ReturnButton->Configure(this,EMCHubMenuAction::Return);
    // Named labels may be nested inside arbitrary Designer content. Keep the
    // original direct TextBlock content compatible with an older authored tree.
    auto Label=[this](const TCHAR* Name,UMCHubActionButton* Button)
    {
        auto* Text=Cast<UTextBlock>(WidgetTree->FindWidget(FName(Name)));
        return Text?Text:Button?Cast<UTextBlock>(Button->GetContent()):nullptr;
    };
    OpenButtonLabel=Label(TEXT("HubOpenSelectionLabel"),OpenButton);
    LaunchButtonLabel=Label(TEXT("HubLaunchLabel"),LaunchButton);
    ReturnButtonLabel=Label(TEXT("HubReturnLabel"),ReturnButton);
    const bool AuthoredCompact=MCHubPresentation::AuthoredCompactHUD(WidgetTree)!=nullptr;
    if(!AuthoredCompact && ReadinessPanel && BottomBar) if(auto* Canvas=Cast<UCanvasPanel>(BottomBar->GetParent())) {
        ReadinessPanel->RemoveFromParent();
        auto* Cell=Canvas->AddChildToCanvas(ReadinessPanel);
        Cell->SetAnchors(FAnchors(.5f,1.f));Cell->SetAlignment(FVector2D(.5f,1.f));
        Cell->SetAutoSize(true);Cell->SetPosition(FVector2D(0,-MCHubPresentation::FooterHeight-10.f));Cell->SetZOrder(4);
    }
    for(UTextBlock* Text:{ReadinessTitle.Get(),ReadinessCount.Get(),PlayerNames.Get()})
        if(Text) Text->SetVisibility(ESlateVisibility::Collapsed);
    if(!AuthoredCompact && ReadinessHint) {
        ReadinessHint->SetAutoWrapText(false);ReadinessHint->SetWrapTextAt(300.f);
        ReadinessHint->SetJustification(ETextJustify::Center);
        if(auto* Cell=Cast<UVerticalBoxSlot>(ReadinessHint->Slot)) Cell->SetPadding(FMargin(0));
    }
    if(!AuthoredCompact && OpenButtonLabel) {
        OpenButtonLabel->SetAutoWrapText(false);OpenButtonLabel->SetMinDesiredWidth(0.f);
    }
    if(!AuthoredCompact && OpenButton) if(auto* Cell=Cast<UVerticalBoxSlot>(OpenButton->Slot)) Cell->SetPadding(FMargin(0));
    MouthHitAreas.Reset();
    for(int32 Index=1;Index<=3;++Index)
        if(auto* HitArea=Cast<UMCHubActionButton>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HubMouthHit%d"),Index)))))
        {
            // Saved Hub trees still contain the older select-only binding.
            // Upgrade the named mouth controls without rebuilding their layout.
            HitArea->Configure(this,EMCHubMenuAction::EnterDestination,Index,false);
            MouthHitAreas.Add(HitArea);
        }
    BoundPresentationRoot=WidgetTree->RootWidget;
    LastStateSignature.Reset();
}

void UMCHubWidget::EnsureGatheringGuidance()
{
    if(!WidgetTree) return;
    InviteButton=Cast<UMCHubActionButton>(WidgetTree->FindWidget(TEXT("HubInviteFriends")));
    InviteHint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubInviteHint")));
    auto* ReadyBody=ReadinessHint?Cast<UVerticalBox>(ReadinessHint->GetParent()):nullptr;
    if(ReadyBody)
    {
        if(!InviteButton) {
            InviteButton=AddAction(ReadyBody,TEXT("HubInviteFriends"),EMCHubMenuAction::InviteFriends);
            BindStyle(TEXT("HubInviteFriends"),EMCUIColorToken::Button,EMCUIStyleTarget::Button);
            BindStyle(TEXT("HubInviteFriendsLabel"),EMCUIColorToken::ButtonText,EMCUIStyleTarget::Text,18,true);
        }
        if(!InviteHint) InviteHint=AddLabel(ReadyBody,TEXT("HubInviteHint"),15);
    }
    if(InviteButton) InviteButton->Configure(this,EMCHubMenuAction::InviteFriends);
    InviteButtonLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubInviteFriendsLabel")));
    ReadyWaypoint=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubReadyWaypoint")));
    if(!ReadyWaypoint && RootCanvas)
    {
        ReadyWaypoint=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubReadyWaypoint"));
        ReadyWaypoint->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.93f),10.f));
        ReadyWaypoint->SetPadding(FMargin(9,4));
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();ReadyWaypoint->AddChild(Body);
        ReadyWaypointArrow=AddLabel(Body,TEXT("HubReadyWaypointArrow"),16,true);
        ReadyWaypointArrow->SetJustification(ETextJustify::Center);
        ReadyWaypointArrow->SetText(FText::AsCultureInvariant(TEXT("↓")));
        ReadyWaypointLabel=AddLabel(Body,TEXT("HubReadyWaypointLabel"),14,true);
        ReadyWaypointLabel->SetJustification(ETextJustify::Center);
        ReadyWaypointLabel->SetAutoWrapText(false);
        for(auto* Label:{ReadyWaypointArrow.Get(),ReadyWaypointLabel.Get()})
            if(auto* Cell=Cast<UVerticalBoxSlot>(Label->Slot)) Cell->SetPadding(FMargin(0));
        auto* WaypointSlot=RootCanvas->AddChildToCanvas(ReadyWaypoint);
        WaypointSlot->SetAutoSize(true);WaypointSlot->SetAlignment(FVector2D(.5f,.5f));WaypointSlot->SetZOrder(2);
        BindStyle(TEXT("HubReadyWaypoint"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
        BindStyle(TEXT("HubReadyWaypointArrow"),EMCUIColorToken::Accent,EMCUIStyleTarget::Text,16,true);
    }
    ReadyWaypointArrow=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadyWaypointArrow")));
    ReadyWaypointLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadyWaypointLabel")));
    if(ReadyWaypoint) {
        const bool AuthoredCompact=MCHubPresentation::AuthoredCompactHUD(WidgetTree)!=nullptr;
        if(!AuthoredCompact) ReadyWaypoint->SetPadding(FMargin(9,4));
        if(!AuthoredCompact && ReadyWaypointArrow && ReadyWaypointLabel && !Cast<UHorizontalBox>(ReadyWaypoint->GetContent())) {
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HubReadyWaypointRow"));
            ReadyWaypointArrow->RemoveFromParent();ReadyWaypointLabel->RemoveFromParent();
            ReadyWaypoint->SetContent(Row);
            Row->AddChildToHorizontalBox(ReadyWaypointArrow)->SetVerticalAlignment(VAlign_Center);
            auto* TextSlot=Row->AddChildToHorizontalBox(ReadyWaypointLabel);
            TextSlot->SetVerticalAlignment(VAlign_Center);TextSlot->SetPadding(FMargin(7,0,0,0));
        }
        if(!AuthoredCompact && ReadyWaypointLabel) {
            ReadyWaypointLabel->SetAutoWrapText(false);ReadyWaypointLabel->SetWrapTextAt(0.f);
            ReadyWaypointLabel->SetMinDesiredWidth(0.f);
        }
        ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);
    }
    EnsureBottomBar();
}

void UMCHubWidget::EnsureBottomBar()
{
    if(!WidgetTree || !RootCanvas) return;
    constexpr float Height=MCHubPresentation::FooterHeight;
    const FMargin FooterOffsets(0,-Height,0,Height);
    const FMargin RowOffsets(12,-Height+3,12,Height-6);
    auto* CompactCanvas=MCHubPresentation::AuthoredCompactHUD(WidgetTree);
    auto* ViewportCanvas=WidgetTree->FindWidget<UCanvasPanel>(TEXT("HubViewportCanvas"));
    if(!ViewportCanvas) {
        // The stage keeps its authored 1600x900 fit, while the HUD uses the full viewport.
        auto* LegacyRoot=Cast<UScaleBox>(WidgetTree->RootWidget);
        auto* DesignFrame=LegacyRoot?Cast<USizeBox>(LegacyRoot->GetContent()):nullptr;
        if(DesignFrame && DesignFrame->GetContent()==RootCanvas && FMath::IsNearlyEqual(DesignFrame->GetWidthOverride(),1600.f)
            && FMath::IsNearlyEqual(DesignFrame->GetHeightOverride(),900.f)) {
            ViewportCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HubViewportCanvas"));
            ViewportCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            WidgetTree->RootWidget=ViewportCanvas;
            auto* LegacySlot=ViewportCanvas->AddChildToCanvas(LegacyRoot);
            LegacySlot->SetAnchors(FAnchors(0,0,1,1));LegacySlot->SetOffsets(FMargin(0));LegacySlot->SetZOrder(0);
        }
    }
    UCanvasPanel* FooterCanvas=CompactCanvas?CompactCanvas:
        ViewportCanvas && WidgetTree->RootWidget==ViewportCanvas?ViewportCanvas:RootCanvas.Get();
    auto* Backdrop=WidgetTree->FindWidget<UBorder>(TEXT("HubFooterBackdrop"));
    if(!Backdrop) Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubFooterBackdrop"));
    if(!CompactCanvas) Backdrop->SetPadding(FMargin(0));
    Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);

    BottomBar=WidgetTree->FindWidget<UHorizontalBox>(TEXT("HubBottomBar"));
    if(!BottomBar) {
        BottomBar=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HubBottomBar"));
        for(const bool bFish:{false,true}) {
            auto* Card=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),bFish?TEXT("HubFishCard"):TEXT("HubWalletCard"));
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),bFish?TEXT("HubFishRow"):TEXT("HubWalletRow"));
            Card->SetContent(Row);
            auto* Icon=WidgetTree->ConstructWidget<UMCHubCurrencyIcon>(UMCHubCurrencyIcon::StaticClass(),bFish?TEXT("HubFishIcon"):TEXT("HubCoinIcon"));
            Icon->bFish=bFish;
            auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetContent(Icon);
            Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
            auto* Count=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),bFish?TEXT("HubBottomFishCount"):TEXT("HubWalletCount"));
            Count->SetAutoWrapText(false);
            Row->AddChildToHorizontalBox(Count)->SetVerticalAlignment(VAlign_Center);
            BottomBar->AddChildToHorizontalBox(Card);
        }
        BottomBar->AddChildToHorizontalBox(WidgetTree->ConstructWidget<UHorizontalBox>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    for(UWidget* Item:{static_cast<UWidget*>(Backdrop),static_cast<UWidget*>(BottomBar.Get())}) {
        if(CompactCanvas && Item->GetParent()) continue;
        if(Item->GetParent()!=FooterCanvas) {Item->RemoveFromParent();FooterCanvas->AddChildToCanvas(Item);}
        if(auto* Cell=Cast<UCanvasPanelSlot>(Item->Slot)) {
            Cell->SetAnchors(FAnchors(0,1,1,1));Cell->SetAutoSize(false);
            Cell->SetOffsets(Item==Backdrop?FooterOffsets:RowOffsets);Cell->SetZOrder(Item==Backdrop?4:5);
        }
    }
    if(!CompactCanvas && ReadyWaypoint && ReadyWaypoint->GetParent()!=FooterCanvas) {
        ReadyWaypoint->RemoveFromParent();
        auto* Cell=FooterCanvas->AddChildToCanvas(ReadyWaypoint);
        Cell->SetAutoSize(true);Cell->SetAlignment(FVector2D(.5f,.5f));Cell->SetZOrder(2);
    }
    if(auto* Profile=WidgetTree->FindWidget(TEXT("HubProfileCard"))) Profile->SetVisibility(ESlateVisibility::Collapsed);
    auto* Team=WidgetTree->FindWidget<UMCHubActionButton>(TEXT("HubPartyButton"));
    if(!Team) {
        Team=MakeAction(TEXT("HubPartyButton"),EMCHubMenuAction::ToggleTeam,0);
        BottomBar->AddChildToHorizontalBox(Team)->SetVerticalAlignment(VAlign_Center);
    }
    Team->Configure(this,EMCHubMenuAction::ToggleTeam,0,false);
    WalletCount=WidgetTree->FindWidget<UTextBlock>(TEXT("HubWalletCount"));
    BottomFishCount=WidgetTree->FindWidget<UTextBlock>(TEXT("HubBottomFishCount"));
    PartyHint=WidgetTree->FindWidget<UTextBlock>(TEXT("HubPartyButtonLabel"));
    CoinIcon=WidgetTree->FindWidget<UMCHubCurrencyIcon>(TEXT("HubCoinIcon"));
    FishIcon=WidgetTree->FindWidget<UMCHubCurrencyIcon>(TEXT("HubFishIcon"));
    if(CompactCanvas) return;
    if(PartyHint) {PartyHint->SetAutoWrapText(false);PartyHint->SetMinDesiredWidth(0.f);}
    for(const FName Name:{FName(TEXT("HubWalletCard")),FName(TEXT("HubFishCard"))})
        if(auto* Card=WidgetTree->FindWidget<UBorder>(Name)) {
            Card->SetPadding(FMargin(10,2));Card->SetVerticalAlignment(VAlign_Center);
            Card->SetVisibility(ESlateVisibility::HitTestInvisible);
            if(auto* Cell=Cast<UHorizontalBoxSlot>(Card->Slot)) {
                Cell->SetVerticalAlignment(VAlign_Center);Cell->SetPadding(FMargin(0,0,6,0));
            }
        }
    for(auto* Icon:{CoinIcon.Get(),FishIcon.Get()})
        if(Icon) if(auto* Box=Cast<USizeBox>(Icon->GetParent())) {
            Box->SetWidthOverride(18.f);Box->SetHeightOverride(18.f);
        }
    for(auto* Count:{WalletCount.Get(),BottomFishCount.Get()})
        if(Count) if(auto* Cell=Cast<UHorizontalBoxSlot>(Count->Slot)) {
            Cell->SetVerticalAlignment(VAlign_Center);Cell->SetPadding(FMargin(5,0,0,0));
        }
}
void UMCHubWidget::RefreshBottomBar()
{
    const auto* Hub=State();
    const bool Preview=IsDesignTime();
    const bool Gathering=Preview?PreviewState!=TEXT("Selecting") && PreviewState!=TEXT("Selection") && PreviewState!=TEXT("Travelling")
        :Hub && Hub->Phase==EMCHubPhase::Gathering;
    if(BottomBar) BottomBar->SetVisibility(Gathering?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Backdrop=WidgetTree?Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubFooterBackdrop"))):nullptr)
        Backdrop->SetVisibility(Gathering?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    // The social bar replaces the floating fish counter and duplicate roster.
    // Keep their authored widgets so designers can recover/reposition them.
    if(FishingPanel) FishingPanel->SetVisibility(ESlateVisibility::Collapsed);
    if(InviteButton) InviteButton->SetVisibility(ESlateVisibility::Collapsed);
    if(InviteHint) InviteHint->SetVisibility(ESlateVisibility::Collapsed);
    if(PlayerNames) PlayerNames->SetVisibility(ESlateVisibility::Collapsed);
    const auto* Identity=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    const int32 Coins=Preview?125:Identity && Identity->Economy?Identity->Economy->Coins:0;
    const int32 Fish=Preview?3:Identity?Identity->FishCaught:0;
    MCHubPresentation::SetText(WalletCount,UMCUIDataLibrary::NumberForCulture(Coins,Preview?PreviewCulture:FString()));
    MCHubPresentation::SetText(BottomFishCount,UMCUIDataLibrary::NumberForCulture(Fish,Preview?PreviewCulture:FString()));
    if(WalletCount) WalletCount->SetToolTipText(Localized(TEXT("hub.bottom.coins"),LOCTEXT("BottomCoins","Монеты")));
    if(BottomFishCount) BottomFishCount->SetToolTipText(Localized(TEXT("hub.bottom.fish"),LOCTEXT("BottomFish","Рыбные монеты для гриля")));
    const auto* FooterTheme=Preview?GetResolvedTheme():UMCGameplayUIFactory::GetThemeForVersion(UMCGameplayUIFactory::GetPresentationVersion());
    const auto& BottomPalette=FooterTheme->Palette;
    if(CoinIcon) CoinIcon->Tint=bUseSoftBottomBar?FMath::Lerp(BottomPalette.Warning,BottomPalette.Ink,.2f):BottomPalette.Warning;
    if(FishIcon) FishIcon->Tint=bUseSoftBottomBar?FMath::Lerp(BottomPalette.Mint,BottomPalette.Ink,.4f):BottomPalette.Mint;
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const FText Key=FText::AsCultureInvariant(PC && PC->IsUsingGamepad()?TEXT("[View]"):TEXT("[Tab]"));
    MCHubPresentation::SetText(PartyHint,Format(TEXT("hub.bottom.party"),LOCTEXT("BottomParty","Команда {Count}/4 · {Key}"),
        {{TEXT("Count"),Preview?4:Hub?Hub->GetParticipantCount():0},{TEXT("Key"),Key}}));
    if(bUseNativeLayout && BasketballHint) if(auto* HintSlot=Cast<UCanvasPanelSlot>(BasketballHint->Slot)) HintSlot->SetPosition(FVector2D(0,-116));
}

void UMCHubWidget::UpdateReadyWaypoint()
{
    if(!ReadyWaypoint || !ReadyWaypoint->GetParent() || IsDesignTime()) return;
    const auto* Hub=State();
    auto* PC=GetOwningPlayer();auto* Pawn=GetOwningPlayerPawn();
    const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0.;
    if(!Hub || Hub->Phase!=EMCHubPhase::Gathering || !PC || PC->IsMoveInputIgnored() || !Pawn) {
        ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);return;
    }
    if(Now<NextReadyWaypointAt) return;
    NextReadyWaypointAt=Now+.05;
    const AMCHubReadyZone* Target=nullptr;double Nearest=MAX_dbl;
    for(TActorIterator<AMCHubReadyZone> It(GetWorld());It;++It)
    {
        if(!It->bEnabled) continue;
        const double Distance=FVector::DistSquared(Pawn->GetActorLocation(),It->GetActorLocation());
        if(Distance<Nearest) {Nearest=Distance;Target=*It;}
    }
    if(!Target || Target->ContainsPawn(Pawn)) {ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);return;}
    int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);
    const FGeometry& CanvasGeometry=ReadyWaypoint->GetParent()->GetCachedGeometry();
    const FVector2D Size=CanvasGeometry.GetLocalSize();
    if(Width<=0 || Height<=0 || Size.X<300.f || Size.Y<180.f) {ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);return;}
    const FVector Anchor=Target->Label?Target->Label->GetComponentLocation():Target->GetActorLocation();
    FVector2D Pixel,Point;
    const bool Projected=PC->ProjectWorldLocationToScreen(Anchor,Pixel,false);
    if(Projected && Pixel.X>=0 && Pixel.X<=Width && Pixel.Y>=0 && Pixel.Y<=Height) {
        ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);return;
    }
    const int32 Metres=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Pawn->GetActorLocation(),Target->GetActorLocation())/100.));
    const FText Label=Format(TEXT("hub.ready.waypoint"),LOCTEXT("ReadyWaypoint","READY · {Metres} м"),{{TEXT("Metres"),Metres}});
    const bool Measure=!ReadyWaypoint->IsVisible() || (ReadyWaypointLabel && !ReadyWaypointLabel->GetText().EqualTo(Label));
    MCHubPresentation::SetText(ReadyWaypointLabel,Label);
    ReadyWaypoint->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(Measure) ReadyWaypoint->ForceLayoutPrepass();
    const FVector2D Center=Size*.5f;
    const FVector2D HalfSize=ReadyWaypoint->GetDesiredSize()*.5f;
    const double SideInset=FMath::Min(FMath::Max(74.,HalfSize.X+12.),Center.X-1.);
    const double TopInset=FMath::Min(FMath::Max(35.,HalfSize.Y+12.),Center.Y-1.);
    const double BottomInset=FMath::Min(MCHubPresentation::FooterHeight+FMath::Max(33.,HalfSize.Y+12.),Center.Y-1.);
    const FVector2D Min(SideInset,TopInset),Max(Size.X-SideInset,Size.Y-BottomInset);
    if(Projected) {
        Point=FVector2D(Pixel.X*Size.X/Width,Pixel.Y*Size.Y/Height);
    }
    else {
        FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
        const FVector Offset=Anchor-Eye;
        Point=FVector2D(FVector::DotProduct(Offset,View.RotateVector(FVector::RightVector)),
            -FVector::DotProduct(Offset,View.RotateVector(FVector::UpVector)));
        if(FVector::DotProduct(Offset,View.Vector())<0.) Point.Y=FMath::Max(FMath::Abs(Point.Y),1.);
        Point=Center+Point.GetSafeNormal()*Size.Size();
    }
    FVector2D Direction=(Point-Center).GetSafeNormal();
    if(Direction.IsNearlyZero()) Direction=FVector2D(0,1);
    const double X=FMath::Abs(Direction.X)>UE_SMALL_NUMBER?(Direction.X>0?Max.X-Center.X:Center.X-Min.X)/FMath::Abs(Direction.X):MAX_dbl;
    const double Y=FMath::Abs(Direction.Y)>UE_SMALL_NUMBER?(Direction.Y>0?Max.Y-Center.Y:Center.Y-Min.Y)/FMath::Abs(Direction.Y):MAX_dbl;
    const FVector2D Position=Center+Direction*FMath::Min(X,Y);
    if(auto* Layout=Cast<UCanvasPanelSlot>(ReadyWaypoint->Slot)) Layout->SetPosition(Position);
    if(ReadyWaypointArrow) ReadyWaypointArrow->SetRenderTransformAngle(float(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X))-90.));
}

void UMCHubWidget::ApplyTheme()
{
    Super::ApplyTheme();
    const auto* ThemeData=GetResolvedTheme();if(!ThemeData) return;
    const auto* FooterTheme=IsDesignTime()?ThemeData:UMCGameplayUIFactory::GetThemeForVersion(UMCGameplayUIFactory::GetPresentationVersion());
    if(WidgetTree && FooterTheme) {
        const auto& Palette=FooterTheme->Palette;
        const bool AuthoredCompact=MCHubPresentation::AuthoredCompactHUD(WidgetTree)!=nullptr;
        auto Font=[&](UTextBlock* Text,int32 Fallback) {
            auto Info=FooterTheme->MakeGameplayFont(Fallback,true);
            if(AuthoredCompact) Info.Size=FMath::Max(1.f,Text->GetFont().Size);
            return Info;
        };
        FLinearColor FooterColor=Palette.Cream;FooterColor.A=.95f;
        if(auto* Backdrop=WidgetTree->FindWidget<UBorder>(TEXT("HubFooterBackdrop"))) {
            Backdrop->SetBrush(FSlateRoundedBoxBrush(FooterColor,0.f));Backdrop->SetBrushColor(FLinearColor::White);
        }
        for(FName Name:{FName(TEXT("HubWalletCard")),FName(TEXT("HubFishCard"))})
            if(auto* Card=WidgetTree->FindWidget<UBorder>(Name)) {
                FLinearColor Color=Palette.Surface;
                Color.A=FMath::Clamp(BottomBarOpacity,0.f,1.f);
                Card->SetBrush(FSlateRoundedBoxBrush(Color,FMath::Min(FooterTheme->CornerRadius,BottomBarCornerRadius),
                    Palette.Border,FooterTheme->OutlineWidth));
                Card->SetBrushColor(FLinearColor::White);
            }
        for(UTextBlock* Text:{WalletCount.Get(),BottomFishCount.Get()}) if(Text) {
            Text->SetFont(Font(Text,16));Text->SetColorAndOpacity(Palette.Ink);
        }
        for(UButton* Button:{static_cast<UButton*>(OpenButton.Get()),WidgetTree->FindWidget<UButton>(TEXT("HubPartyButton"))}) if(Button) {
            MCUIVisualStyle::Button(Button,FooterTheme);
            if(!AuthoredCompact) {
                auto Style=Button->GetStyle();Style.SetNormalPadding(FMargin(10,3));Style.SetPressedPadding(FMargin(10,3));Button->SetStyle(Style);
                if(auto* Cell=Cast<UButtonSlot>(Button->GetContentSlot())) Cell->SetPadding(FMargin(0));
            }
        }
        for(UTextBlock* Text:{OpenButtonLabel.Get(),PartyHint.Get()}) if(Text) {
            Text->SetFont(Font(Text,16));Text->SetColorAndOpacity(Palette.ButtonText);
        }
        if(ReadinessHint) {
            ReadinessHint->SetFont(Font(ReadinessHint,16));ReadinessHint->SetColorAndOpacity(Palette.OnMarine);
        }
        if(ReadyWaypoint) {
            ReadyWaypoint->SetBrush(FSlateRoundedBoxBrush(Palette.Marine,FooterTheme->CornerRadius,Palette.Border,FooterTheme->OutlineWidth));
            ReadyWaypoint->SetBrushColor(FLinearColor::White);
        }
        if(ReadyWaypointArrow) {
            ReadyWaypointArrow->SetFont(Font(ReadyWaypointArrow,16));ReadyWaypointArrow->SetColorAndOpacity(Palette.Accent);
        }
        if(ReadyWaypointLabel) {
            ReadyWaypointLabel->SetFont(Font(ReadyWaypointLabel,14));ReadyWaypointLabel->SetColorAndOpacity(Palette.OnMarine);
        }
        if(ReadinessPanel) {
            const bool HasStatus=ReadinessHint && ReadinessHint->GetVisibility()!=ESlateVisibility::Collapsed;
            if(!AuthoredCompact) ReadinessPanel->SetPadding(HasStatus?FMargin(10,4):FMargin(0));
            ReadinessPanel->SetBrush(HasStatus?FSlateBrush(FSlateRoundedBoxBrush(Palette.Marine,FooterTheme->CornerRadius,
                Palette.Border,FooterTheme->OutlineWidth)):FSlateBrush(FSlateNoResource()));
            ReadinessPanel->SetBrushColor(FLinearColor::White);
        }
    }
    if(bAuthoredPresentation) return;
    TArray<UMCHubActionButton*> Buttons={LaunchButton,ReturnButton,InviteButton};
    for(auto* Button:Buttons) if(Button)
    {
        const FLinearColor Color=Button==LaunchButton?ThemeData->Palette.Accent:ThemeData->Palette.Button;
        FButtonStyle Style=Button->GetStyle();
        Style.SetNormal(MCUIVisualStyle::Surface(Button,Style.Normal,ThemeData,Color,FVector2D(280,40),8));
        Style.SetHovered(MCUIVisualStyle::Surface(Button,Style.Hovered,ThemeData,ThemeData->Palette.ButtonHover,FVector2D(280,40),8));
        Style.SetPressed(MCUIVisualStyle::Surface(Button,Style.Pressed,ThemeData,ThemeData->Palette.ButtonPressed,FVector2D(280,40),8));
        Style.SetDisabled(MCUIVisualStyle::Surface(Button,Style.Disabled,ThemeData,ThemeData->Palette.ButtonDisabled,FVector2D(280,40),8));
        Style.SetNormalPadding(FMargin(12,8));Style.SetPressedPadding(FMargin(12,8));Button->SetStyle(Style);
        if(auto* Label=Cast<UTextBlock>(Button->GetContent()))
        {
            Label->SetFont(ThemeData->MakeFont(18,true));
            Label->SetColorAndOpacity(FSlateColor(ThemeData->Palette.ButtonText));
        }
    }
}

FText UMCHubWidget::Format(FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments) const
{
    return UMCUIDataLibrary::FormatTextForCulture(Localized(Key,Fallback),Arguments,IsDesignTime()?PreviewCulture:FString());
}
void UMCHubWidget::RefreshFromState()
{
    if(IsDesignTime()) {ApplyEditorPreview(PreviewTime);return;}
    if(bRefreshingState) return;
    TGuardValue<bool> Guard(bRefreshingState,true);
    ON_SCOPE_EXIT {ApplyUnifiedViewSuppression();};
    BuildPresentationTree();if(!ReadinessTitle) return;
    auto* Hub=State();
    if(RaidLoadoutView) RaidLoadoutView->RefreshFromHub();
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* Identity=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    MCHubPresentation::SetText(FishingCount,Format(TEXT("hub.fishing.count"),LOCTEXT("FishingCount","Рыбные монеты: {Count}"),{{TEXT("Count"),Identity?Identity->FishCaught:0}}));
    const auto* HeldBall=Hero?AMCHubBasketball::HeldBy(Hero):nullptr;
    const bool BallHeld=HeldBall!=nullptr;
    const auto* Instance=GetGameInstance();
    auto* Sessions=Instance?Instance->GetSubsystem<UMCSteamSessionSubsystem>():nullptr;
    const bool bCanInvite=Sessions && Sessions->CanUseSteam() && Sessions->HasRoom();
    RefreshBottomBar();
    MCHubPresentation::SetText(InviteButtonLabel,Localized(TEXT("hub.invite"),LOCTEXT("InviteFriends","ПРИГЛАСИТЬ ДРУЗЕЙ")));
    MCHubPresentation::SetText(InviteHint,Localized(bCanInvite?TEXT("hub.invite.steam"):TEXT("hub.invite.local"),
        bCanInvite?LOCTEXT("InviteSteam","ESC — меню и приглашения · Shift+Tab — друзья Steam"):
            LOCTEXT("InviteLocal","Локальный хаб · друзья могут подключиться по IP")));
    const auto* Rod=Hero?Hero->FishingRod.Get():nullptr;
    const bool ShowControls=Hub && Hub->Phase==EMCHubPhase::Gathering && PC && !PC->IsMoveInputIgnored();
    const bool ShowFishing=ShowControls && Rod && Rod->IsPresented();
    const bool ShowBall=ShowControls && AMCHubBasketball::LocalHintBall(Hero)!=nullptr;
    if(!ShowControls) InteractionFeedbackUntil=0;
    if(BasketballHint) BasketballHint->SetVisibility(ShowBall || ShowFishing?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BasketballReticle) BasketballReticle->SetVisibility(ShowBall && HeldBall && HeldBall->IsAiming()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    const bool Gamepad=PC && PC->IsUsingGamepad();
    if(BasketballHint && ShowBall) MCHubPresentation::SetText(BasketballHint,BallHeld
        ? Localized(Gamepad?TEXT("hub.basketball.controls.gamepad"):TEXT("hub.basketball.controls"),Gamepad
            ?LOCTEXT("BasketballAimThrowGamepad","LB — прицел · RT / Y — бросить · X — отпустить")
            :LOCTEXT("BasketballAimThrow","ЛКМ — прицел · ПКМ — бросить · E — отпустить"))
        : Localized(Gamepad?TEXT("hub.basketball.pickup.gamepad"):TEXT("hub.basketball.pickup"),Gamepad
            ?LOCTEXT("BasketballPickupGamepad","X — подобрать мяч"):LOCTEXT("BasketballPickup","E — подобрать мяч")));
    if(BasketballHint && ShowFishing && !ShowBall)
    {
        using MCHubFishingPresentation::EHint;
        auto FishingCopy=[&](EHint Hint) {return MCHubFishingPresentation::ResolveHint(GetResolvedTranslationTable(),Hint);};
        FText Copy;
        if(Rod->IsCatchChallengeActive()) Copy=FishingCopy(Gamepad?EHint::HookGamepad:EHint::Hook);
        else if(Rod->bFishHooked)
            Copy=Localized(TEXT("hub.fishing.hooked"),LOCTEXT("FishingHooked","Рыба на крючке! Вытягиваем…"));
        else if(Rod->bReeling) Copy=FishingCopy(EHint::Reel);
        else if(Rod->bLineOut) Copy=FishingCopy(EHint::Wait);
        else if(Rod->IsChargingCast() || Rod->CanCastLine()) Copy=FishingCopy(Gamepad?EHint::CastGamepad:EHint::Cast);
        else Copy=Localized(Gamepad?TEXT("hub.fishing.water_required.gamepad"):TEXT("hub.fishing.water_required"),Gamepad
            ?LOCTEXT("FishingPondRequiredGamepad","Остановись на пирсе и повернись к воде\nD-pad — инструменты")
            :LOCTEXT("FishingPondRequired","Остановись на пирсе и повернись к воде\n5 — убрать удочку · колесо — инструменты"));
        MCHubPresentation::SetText(BasketballHint,Copy);
    }
    FString Signature=FString::Printf(TEXT("%p:%d:%d:%d:%d:%d:%d:%d:%d:%s"),Hub,Hub?int32(Hub->Phase):-1,
        Hub?Hub->SceneGeneration:0,Hub?Hub->SelectedDay:0,PresentedDay,IsLocalHost(),IsSceneReady(),bPresentationFailed,bCanInvite,Hub?*Hub->Notice.ToString():TEXT(""));
    if(Hub) for(const auto& Player:Hub->Players)
        Signature+=FString::Printf(TEXT("|%p:%d:%d:%s"),Player.Player.Get(),Player.bReady,Player.bLoaded,
            IsValid(Player.Player)?*Player.Player->GetPlayerName():TEXT(""));
    bool HasFeedback=GetWorld() && GetWorld()->GetRealTimeSeconds()<InteractionFeedbackUntil;
    if(HasFeedback && (ShowFishing || ShowBall)) {
        bool AtGrill=false;
        for(TActorIterator<AMCHubGrillStation> Grill(GetWorld());Grill;++Grill) {
            if(Grill->ContainsPawn(Hero)) {AtGrill=true;break;}
        }
        if(!AtGrill) {InteractionFeedbackUntil=0;HasFeedback=false;}
    }
    const bool PartyOpen=PC && PC->IsHubPartyPanelOpen();
    Signature+=HasFeedback?TEXT("|Feedback"):TEXT("|Guidance");
    if(PartyOpen) Signature+=TEXT("|Party");
    Signature+=UMCGameplayUIFactory::GetPresentationVersion()==EMCUIVersion::New?TEXT("|NewUI"):TEXT("|CurrentUI");
    if(Signature==LastStateSignature) return;
    LastStateSignature=MoveTemp(Signature);
    const bool bSelecting=Hub && Hub->Phase!=EMCHubPhase::Gathering;
    const bool bTravelling=Hub && Hub->Phase==EMCHubPhase::Travelling;
    const bool bHost=IsLocalHost();
    const int32 Ready=Hub?Hub->GetReadyCount():0,Total=Hub?Hub->GetParticipantCount():0;
    MCHubPresentation::SetText(ReadinessTitle,Localized(bTravelling && !bSelecting?TEXT("hub.loading.selection"):TEXT("hub.title"),
        bTravelling && !bSelecting?LOCTEXT("OpeningSelection","ОТКРЫВАЕМ ВЫБОР ДНЯ…"):LOCTEXT("Title","СБОР КОМАНДЫ")));
    MCHubPresentation::SetText(ReadinessCount,Format(bSelecting?TEXT("hub.loading.count"):TEXT("hub.ready.count"),
        bSelecting?LOCTEXT("LoadedCount","ВЫБОР ДНЯ · {Ready} / {Total}"):LOCTEXT("ReadyCount","ГОТОВЫ · {Ready} / {Total}"),
        {{TEXT("Ready"),bSelecting && Hub?Hub->Players.FilterByPredicate([](const FMCHubPlayerReadiness& Player){return Player.bLoaded;}).Num():Ready},{TEXT("Total"),Total}}));
    FText Names;
    if(Hub) for(const auto& Player:Hub->Players) if(IsValid(Player.Player))
    {
        const bool bDone=bSelecting?Player.bLoaded:Player.bReady;
        const FText Line=Format(TEXT("hub.player.status"),LOCTEXT("PlayerStatus","{Mark} {Name}"),
            {{TEXT("Mark"),FText::FromString(bDone?TEXT("✓"):TEXT("…"))},{TEXT("Name"),FText::AsCultureInvariant(Player.Player->GetPlayerName())}});
        Names=Names.IsEmpty()?Line:FText::Join(FText::FromString(TEXT("\n")),Names,Line);
    }
    MCHubPresentation::SetText(PlayerNames,Names);
    const bool Gathering=Hub && !bSelecting && !PartyOpen;
    const bool AllReady=Gathering && Hub->AreAllReady();
    const bool CanOpen=AllReady && bHost && Hub->AreAllLoaded() && !HasFeedback;
    const bool LocalReady=Gathering && Identity && Hub->Players.ContainsByPredicate([Identity](const FMCHubPlayerReadiness& Player) {
        return Player.Player.Get()==Identity && Player.bReady;
    });
    FText GatheringStatus;
    if(Gathering && HasFeedback) {
        const FName MessageKey=MCNativeCommerceUICopy::FindMessageKey(GetResolvedTranslationTable(),InteractionFeedback);
        GatheringStatus=MessageKey.IsNone()?InteractionFeedback:Localized(MessageKey,InteractionFeedback);
    }
    else if(LocalReady && !AllReady) GatheringStatus=Localized(TEXT("hub.wait_all_ready"),LOCTEXT("GatheringWaitTeam","Ждём команду"));
    else if(AllReady && !bHost) GatheringStatus=Localized(TEXT("hub.wait_host"),LOCTEXT("GatheringWaitHost","Ждём хоста"));
    else if(AllReady && !Hub->AreAllLoaded()) GatheringStatus=Localized(TEXT("hub.loading.players"),LOCTEXT("WaitingForLoad","Ждём загрузку остальных игроков…"));
    MCHubPresentation::SetText(ReadinessHint,GatheringStatus);
    if(ReadinessHint) ReadinessHint->SetVisibility(GatheringStatus.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(OpenButton)
    {
        OpenButton->SetVisibility(CanOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        OpenButton->SetIsEnabled(CanOpen);
    }
    MCHubPresentation::SetText(OpenButtonLabel,Localized(TEXT("hub.open_selection"),LOCTEXT("OpenSelection","ENTER · ВЫБРАТЬ ДЕНЬ")));
    if(SelectionPanel) SelectionPanel->SetVisibility(bSelecting && !RaidLoadoutView?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ReadinessPanel) ReadinessPanel->SetVisibility(CanOpen || !GatheringStatus.IsEmpty()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(PresentationHost) PresentationHost->SetVisibility(bSelecting?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(bSelecting)
    {
        EnsureSelectionPresentation();
        const auto* Destination=Hub->FindDestination(Hub->SelectedDay);
        if(LaunchButton)
        {
            LaunchButton->SetVisibility(bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            LaunchButton->SetIsEnabled(CanLaunchSelectedDestination());
        }
        MCHubPresentation::SetText(LaunchButtonLabel,FText::AsCultureInvariant(TEXT("Enter")));
        if(ReturnButton) {ReturnButton->SetVisibility(bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReturnButton->SetIsEnabled(!bTravelling);}
        MCHubPresentation::SetText(ReturnButtonLabel,FText::AsCultureInvariant(TEXT("Esc")));
        FText Status;
        if(bTravelling) Status=Localized(TEXT("hub.loading"),LOCTEXT("Travelling","ЗАГРУЖАЕМ ДЕНЬ…"));
        else if(bPresentationFailed) Status=Localized(TEXT("hub.presentation.unavailable"),LOCTEXT("PresentationUnavailable","Не удалось открыть сцену выбора. Вернитесь в хаб."));
        else if(!Hub->AreAllLoaded()) Status=Localized(TEXT("hub.loading.players"),LOCTEXT("WaitingForLoad","Ждём загрузку остальных игроков…"));
        else if(!Destination || !Destination->bEnabled || !Destination->HasValidMapPath()) Status=Localized(TEXT("hub.selection.unavailable"),LOCTEXT("DayUnavailable","Этот день ещё не подключён."));
        else if(!bHost) Status=Localized(TEXT("hub.guest.hint"),LOCTEXT("WaitingForHost","Хост выбирает день. ESC — меню."));
        const FText& Detail=Hub->Notice.IsEmpty()?Status:Hub->Notice;
        MCHubPresentation::SetText(SelectionStatus,Detail);
        if(SelectionStatus) SelectionStatus->SetVisibility(Detail.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    UpdateMouthHitAreas();
    ApplyTheme();
}

void UMCHubWidget::ApplyEditorPreview(float Time)
{
    if(!IsDesignTime()) return;
    PreviewTime=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    BuildPresentationTree();
    const bool Selecting=PreviewState==TEXT("Selecting") || PreviewState==TEXT("Selection")
        || PreviewState==TEXT("Travelling") || PreviewScenario==EMCGameplayUIPreviewScenario::Selected;
    const bool Walking=PreviewState==TEXT("Walking");
    const bool Waiting=PreviewState==TEXT("WaitingForTeam") || PreviewScenario==EMCGameplayUIPreviewScenario::Waiting;
    const bool Travelling=PreviewState==TEXT("Travelling");
    const bool Guest=PreviewState==TEXT("Guest");
    const int32 Ready=Waiting?3:4;
    if(ReadinessPanel) ReadinessPanel->SetVisibility(Selecting || Walking?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    if(SelectionPanel) SelectionPanel->SetVisibility(Selecting?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // Preview only the widget presentation. It never creates gameplay state,
    // starts travel, or runs the actor-driving Blueprint mouth stage.
    if(PresentationHost) PresentationHost->SetVisibility(ESlateVisibility::Collapsed);
    for(const auto& HitArea:MouthHitAreas) if(HitArea) HitArea->SetVisibility(ESlateVisibility::Collapsed);
    MCHubPresentation::SetText(ReadinessTitle,Localized(TEXT("hub.title"),LOCTEXT("Title","СБОР КОМАНДЫ")));
    MCHubPresentation::SetText(ReadinessCount,Format(TEXT("hub.ready.count"),LOCTEXT("ReadyCount","ГОТОВЫ · {Ready} / {Total}"),{{TEXT("Ready"),Ready},{TEXT("Total"),4}}));
    MCHubPresentation::SetText(PlayerNames,FText::AsCultureInvariant(Waiting?TEXT("✓ Player 1\n✓ Player 2\n✓ Player 3\n… Player 4"):TEXT("✓ Player 1\n✓ Player 2\n✓ Player 3\n✓ Player 4")));
    FText GatheringHint;
    if(!Selecting && Waiting && ReadinessCount) GatheringHint=ReadinessCount->GetText();
    else if(!Selecting && Guest) GatheringHint=Localized(TEXT("hub.wait_host"),LOCTEXT("GatheringWaitHost","Ждём хоста"));
    MCHubPresentation::SetText(ReadinessHint,GatheringHint);
    if(ReadinessHint) ReadinessHint->SetVisibility(GatheringHint.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    MCHubPresentation::SetText(InviteButtonLabel,Localized(TEXT("hub.invite"),LOCTEXT("InviteFriends","ПРИГЛАСИТЬ ДРУЗЕЙ")));
    MCHubPresentation::SetText(InviteHint,Localized(TEXT("hub.invite.steam"),LOCTEXT("InviteSteam","ESC — меню и приглашения · Shift+Tab — друзья Steam")));
    if(InviteButton) InviteButton->SetVisibility(Selecting?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    if(ReadyWaypoint) ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);
    MCHubPresentation::SetText(OpenButtonLabel,Localized(TEXT("hub.open_selection"),LOCTEXT("OpenSelection","ENTER · ВЫБРАТЬ ДЕНЬ")));
    MCHubPresentation::SetText(LaunchButtonLabel,FText::AsCultureInvariant(TEXT("Enter")));
    MCHubPresentation::SetText(ReturnButtonLabel,FText::AsCultureInvariant(TEXT("Esc")));
    const FText Hint=Travelling?Localized(TEXT("hub.loading"),LOCTEXT("Travelling","ЗАГРУЖАЕМ ДЕНЬ…")):
        Waiting?Localized(TEXT("hub.loading.players"),LOCTEXT("WaitingForLoad","Ждём загрузку остальных игроков…")):
        Guest?Localized(TEXT("hub.guest.hint"),LOCTEXT("WaitingForHost","Хост выбирает день. ESC — меню.")):FText::GetEmpty();
    MCHubPresentation::SetText(SelectionStatus,Hint);
    if(SelectionStatus) SelectionStatus->SetVisibility(Hint.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(OpenButton) {
        const bool CanOpen=!Selecting && !Walking && !Waiting && !Guest;
        OpenButton->SetVisibility(CanOpen?ESlateVisibility::Visible:ESlateVisibility::Collapsed);OpenButton->SetIsEnabled(CanOpen);
    }
    if(LaunchButton) {LaunchButton->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);LaunchButton->SetIsEnabled(!Waiting && !Travelling);}
    if(ReturnButton) {ReturnButton->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);ReturnButton->SetIsEnabled(!Travelling);}
    RefreshBottomBar();
    ApplyTheme();
}

bool UMCHubWidget::EnsureSelectionPresentation()
{
    if(SelectionPresentation) return true;
    if(bPresentationFailed || IsDesignTime() || !PresentationHost || !GetOwningPlayer() || !MCHubPresentation::HasSavedStage(GetWorld())) return false;
    auto* Player=GetOwningPlayer<AMCPlayerController>();
    if(!Player || !Player->HasLocalPresentationOwner()) return false;
    MCHubPresentation::FPreservePlayerView PreserveView(Player);
    MCHubPresentation::FPreservePlayerFocus PreserveFocus(Player);
    UClass* Class=SelectionPresentationClass.LoadSynchronous();
    if(!Class || !Class->IsChildOf(UUserWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) {bPresentationFailed=true;return false;}
    SelectionPresentation=CreateWidget<UUserWidget>(GetOwningPlayer(),Class);
    if(!SelectionPresentation) {bPresentationFailed=true;return false;}
    // These are the two known targets of the saved Blueprint's Construct and
    // ShowPage(1), rather than the Hub's interactive controls. Allow their
    // synchronous setup, then remove all legacy focus and restore the real UI.
    SelectionPresentation->SetIsFocusable(true);
    if(SelectionPresentation->WidgetTree)
        for(FName Name:{FName(TEXT("PlayButton")),FName(TEXT("Day3Button"))})
            if(auto* Button=Cast<UButton>(SelectionPresentation->WidgetTree->FindWidget(Name)))
                MCHubPresentation::SetLegacyButtonFocusable(Button,true);
    if(!MCHubPresentation::DisableAutonomousTick(SelectionPresentation)) {ResetSelectionPresentation();bPresentationFailed=true;return false;}
    MCHubPresentation::HideLegacyControls(SelectionPresentation);
    auto* PresentationSlot=PresentationHost->AddChildToOverlay(SelectionPresentation);PresentationSlot->SetHorizontalAlignment(HAlign_Fill);PresentationSlot->SetVerticalAlignment(VAlign_Fill);
    SelectionPresentation->TakeWidget(); // Blueprint Construct finishes before our authoritative setup.
    SelectionPresentation->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(!MCHubPresentation::Call(SelectionPresentation,TEXT("FindStageActors"))
        || !MCHubPresentation::CallInt(SelectionPresentation,TEXT("ShowPage"),TEXT("Index"),1))
    {ResetSelectionPresentation();bPresentationFailed=true;return false;}
    MCHubPresentation::HideLegacyControls(SelectionPresentation,true);
    SelectionPresentation->SetIsFocusable(false);
    PresentedDay=INDEX_NONE;
    return true;
}
void UMCHubWidget::ResetSelectionPresentation()
{
    if(SelectionPresentation) SelectionPresentation->RemoveFromParent();
    SelectionPresentation=nullptr;PresentedDay=INDEX_NONE;bPresentationReady=false;
}
void UMCHubWidget::AdvanceSelectionPresentation()
{
    const auto* Hub=State();
    if(!Hub || !GetOwningPlayer() || !EnsureSelectionPresentation()) return;
    MCHubPresentation::FPreservePlayerView PreserveView(GetOwningPlayer());
    const int32 Day=FMath::Clamp(Hub->SelectedDay,1,7);
    if(PresentedDay!=Day)
    {
        if(!MCHubPresentation::CallInt(SelectionPresentation,TEXT("SelectDay"),TEXT("Day"),Day))
        {bPresentationFailed=true;bPresentationReady=false;return;}
        PresentedDay=Day;
    }
    // Do not trust hover/focus on a local machine to author the shared choice.
    // This mutates only a cosmetic widget, never the replicated Hub state.
    bool bValid=true;
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("PageIndex"),1);
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("SelectedDay"),Day);
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("ActiveDay"),Day);
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("WindowFirstDay"),FMath::Clamp(Day-2,1,5));
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("PendingHoverDay"),0);
    bValid&=MCHubPresentation::SetNumber(SelectionPresentation,TEXT("HoverQuiet"),2);
    bValid&=MCHubPresentation::SetFlag(SelectionPresentation,TEXT("HasPointerMotion"),false);
    bValid&=MCHubPresentation::Call(SelectionPresentation,TEXT("UpdateStage"));
    MCHubPresentation::HideLegacyControls(SelectionPresentation);
    // UpdateStage drives every authored day, including its lazy material
    // instances, while the overlay is collapsed during Gathering. Preload can
    // therefore snapshot the finished stage before the Hub cover is released.
    for(int32 StageDay=1;StageDay<=7;++StageDay)
        bValid&=MCHubPresentation::HasStageActors(SelectionPresentation,StageDay);
    bPresentationReady=bValid && MCHubPresentation::HasCamera(GetWorld());
    if(!bValid) bPresentationFailed=true;
}
void UMCHubWidget::ShowInteractionFeedback(const FText& Message,float Seconds)
{
    InteractionFeedback=Message;
    InteractionFeedbackUntil=(GetWorld()?GetWorld()->GetRealTimeSeconds():0)+FMath::Clamp(Seconds,.5f,8.f);
    LastStateSignature.Reset();RefreshFromState();
}

void UMCHubWidget::TickPresentation(float DeltaSeconds)
{
    if(DeltaSeconds>=0.f) AdvanceSelectionPresentation();
    UpdateReadyWaypoint();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* PC=GetOwningPlayer();
    const auto* Hub=State();
    if(BasketballHint && PC && PC->IsMoveInputIgnored()) BasketballHint->SetVisibility(ESlateVisibility::Collapsed);
    const auto* Ball=Hero?AMCHubBasketball::HeldBy(Hero):nullptr;
    const bool Aiming=Ball && Ball->IsAiming() && Hub && Hub->Phase==EMCHubPhase::Gathering && Hero->CanWork()
        && PC && !PC->bShowMouseCursor && !PC->IsLookInputIgnored();
    if(BasketballReticle) BasketballReticle->SetVisibility(Aiming?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(!Aiming) {
        BasketballTrajectory.Reset();NextBasketballPreviewAt=0.;return;
    }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextBasketballPreviewAt) {
        BasketballTrajectory=Ball->PredictThrowTrajectory();NextBasketballPreviewAt=Now+.05;
    }
}

void UMCHubWidget::PrimeControlsGlyphs()
{
    if(IsDesignTime() || !FSlateApplication::IsInitialized()) {bControlsGlyphsPrepared=false;return;}
    const auto Renderer=FSlateApplication::Get().GetRenderer();const auto* NewTheme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    UDataTable* Table=GetResolvedTranslationTable();
    if(!Renderer || !NewTheme || !Table) {bControlsGlyphsPrepared=false;return;}
    const FName Keys[]={TEXT("hub.basketball.controls"),TEXT("hub.basketball.controls.gamepad"),TEXT("hub.basketball.pickup"),TEXT("hub.basketball.pickup.gamepad"),
        TEXT("hub.fishing.water_required"),TEXT("hub.fishing.water_required.gamepad"),TEXT("hub.fishing.hooked")};
    TArray<FText> Warm;bool Complete=true;
    for(FName Key:Keys) {const FText Copy=UMCUIDataLibrary::ResolveText(Table,Key,FText::GetEmpty());Complete&=!Copy.IsEmpty();Warm.Add(Copy);}
    MCHubFishingPresentation::AppendHints(Warm,Table);
    const FText Corpus=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Warm);
    const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    const FSlateFontInfo SourceFont=GetControlsReticleFont(),NewFont=NewTheme->MakeFont(20,true);
    const float Scale=FMath::Max(.001f,UWidgetLayoutLibrary::GetViewportScale(this));
    if(bControlsGlyphsPrepared && Complete && PrimedControlsTable.Get()==Table && PrimedControlsCulture==Culture && PrimedControlsCorpus.EqualTo(Corpus)
        && PrimedControlsSourceFont==SourceFont && PrimedControlsNewFont==NewFont && FMath::IsNearlyEqual(Scale,PrimedControlsScale,.001f)) return;
    bControlsGlyphsPrepared=false;
    if(!Complete || !SourceFont.HasValidFont() || !NewFont.HasValidFont()) return; // Current remains available; never change scene/ACK gates for missing UI copy.
    for(int32 FontSize=18;FontSize<=20;++FontSize) Renderer->GetFontMeasureService()->Measure(Corpus,NewTheme->MakeFont(FontSize,true),Scale);
    Renderer->GetFontMeasureService()->Measure(FText::AsCultureInvariant(TEXT("+")),SourceFont,1.f);
    PrimedControlsTable=Table;PrimedControlsCulture=Culture;PrimedControlsCorpus=Corpus;PrimedControlsSourceFont=SourceFont;PrimedControlsNewFont=NewFont;PrimedControlsScale=Scale;
    bControlsGlyphsPrepared=true;
}
FSlateFontInfo UMCHubWidget::GetControlsReticleFont() const
{
    return BasketballReticle?BasketballReticle->GetFont():GetResolvedTheme()->MakeGameplayFont(24);
}
FMCHubControlsPresentation UMCHubWidget::EvaluateControlsPresentation(const FGeometry& SourcePaint,FVector2D LocalSize) const
{
    FMCHubControlsPresentation Result;Result.Reticle.Source=const_cast<UMCHubWidget*>(this);Result.Reticle.SourceFrame=GFrameCounter;Result.Reticle.LocalSize=LocalSize;
    Result.Reticle.bSourceVisible=IsVisible() && (!GetRootWidget() || GetRootWidget()->IsVisible());
    Result.Reticle.bPaintGeometryCompatible=LocalSize.X>1 && LocalSize.Y>1 && SourcePaint.GetLocalSize().Equals(LocalSize,.5);
    Result.bHintVisible=BasketballHint && BasketballHint->IsVisible();Result.Hint=BasketballHint?BasketballHint->GetText():FText::GetEmpty();
    const auto& Palette=GetResolvedTheme()->Palette;
    auto Stroke=[&](const TArray<FVector2D>& Points,float Width=2.f) {
        FMCUIReticleStroke Back;Back.Points=Points;Back.Color=Palette.Ink.CopyWithNewOpacity(.85f);Back.Width=Width+2.f;Back.Layer=1;Result.Reticle.Strokes.Add(MoveTemp(Back));
        FMCUIReticleStroke Ink;Ink.Points=Points;Ink.Color=Palette.OnMarine;Ink.Width=Width;Ink.Layer=2;Result.Reticle.Strokes.Add(MoveTemp(Ink));
    };
    auto Finalize=[&]() {
        if(BasketballReticle && BasketballReticle->IsVisible() && LocalSize.X>4 && LocalSize.Y>4) {
            const FVector2D Center=LocalSize*.5;
            for(FVector2D Axis:{FVector2D(1,0),FVector2D(-1,0),FVector2D(0,1),FVector2D(0,-1)})
                Stroke({Center+Axis*6.,Center+Axis*12.},1.8f);
        }
        // Both UI versions consume these strokes, including the center brackets.
        Result.NativeTrajectoryStrokeCount=Result.Reticle.Strokes.Num();
        Result.Reticle.MaximumLayer=2;
        return MoveTemp(Result);
    };
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    auto* PC=GetOwningPlayer();
    const auto* Ball=Hero?AMCHubBasketball::HeldBy(Hero):nullptr;
    if(!Ball || !PC || !Ball->IsAiming() || BasketballTrajectory.Num()<2) return Finalize();
    FVector ViewOrigin;FRotator ViewRotation;PC->GetPlayerViewPoint(ViewOrigin,ViewRotation);
    const FVector ViewForward=ViewRotation.Vector();
    const FGeometry& ProjectionGeometry=GetTickSpaceGeometry();
    auto Project=[&](const FVector& Point,FVector2D& Local) {
        FVector2D Pixel;
        if(!ProjectionGeometry.GetLocalSize().Equals(LocalSize,.5) || Point.ContainsNaN()
            || FVector::DotProduct(Point-ViewOrigin,ViewForward)<=1.
            || !PC->ProjectWorldLocationToScreen(Point,Pixel,false)) return false;
        USlateBlueprintLibrary::ScreenToWidgetLocal(this,ProjectionGeometry,Pixel,Local);
        const double Limit=FMath::Max(LocalSize.X,LocalSize.Y)*100.;
        return FMath::IsFinite(Local.X) && FMath::IsFinite(Local.Y) && FMath::Abs(Local.X)<Limit && FMath::Abs(Local.Y)<Limit;
    };
    auto Clip=[&](FVector2D& Start,FVector2D& End) {
        const FVector2D Delta=End-Start;
        double Enter=0.,Exit=1.;
        for(int32 Axis=0;Axis<2;++Axis) {
            const double Min=2.,Max=LocalSize[Axis]-2.;
            if(FMath::IsNearlyZero(Delta[Axis])) {
                if(Start[Axis]<Min || Start[Axis]>Max) return false;
                continue;
            }
            double A=(Min-Start[Axis])/Delta[Axis],B=(Max-Start[Axis])/Delta[Axis];
            if(A>B) Swap(A,B);
            Enter=FMath::Max(Enter,A);Exit=FMath::Min(Exit,B);
            if(Enter>=Exit) return false;
        }
        End=Start+Delta*Exit;Start+=Delta*Enter;
        return !Start.Equals(End,.01);
    };
    // Keep the existing physics prediction; only draw its arc like grenade aim.
    TArray<FVector2D> Run;
    Run.Reserve(BasketballTrajectory.Num());
    auto Flush=[&] {if(Run.Num()>1) Stroke(Run);Run.Reset();};
    FVector2D Previous;
    bool PreviousValid=Project(BasketballTrajectory[0],Previous);
    for(int32 Index=1;Index<BasketballTrajectory.Num();++Index) {
        FVector2D Current;
        const bool Valid=Project(BasketballTrajectory[Index],Current);
        if(PreviousValid && Valid) {
            FVector2D A=Previous,B=Current;
            if(Clip(A,B)) {
                if(!Run.IsEmpty() && !Run.Last().Equals(A,.1)) Flush();
                if(Run.IsEmpty()) Run.Add(A);
                Run.Add(B);
            } else Flush();
        } else Flush();
        if(Valid) Previous=Current;
        PreviousValid=Valid;
    }
    Flush();
    FVector2D End;
    if(Project(BasketballTrajectory.Last(),End) && End.X>=9. && End.X<=LocalSize.X-9. && End.Y>=9. && End.Y<=LocalSize.Y-9.)
        Stroke({End+FVector2D(0,-7),End+FVector2D(7,0),End+FVector2D(0,7),
            End+FVector2D(-7,0),End+FVector2D(0,-7)},2.5f);
    return Finalize();
}
void UMCHubWidget::CacheControlsPresentation(FVector2D LocalSize)
{
    if(ControlsSnapshot.Reticle.SourceFrame==GFrameCounter || IsDesignTime()) return;
    // Restore before the next source evaluation; failed/occluded New must leave the original controls visible.
    for(const auto& Entry:ControlsOriginalOpacity) if(auto* Widget=Entry.Key.Get()) Widget->SetRenderOpacity(Entry.Value);
    ControlsOriginalOpacity.Reset();
    ControlsPaintGeometry=GetPaintSpaceGeometry();ControlsSnapshot=EvaluateControlsPresentation(ControlsPaintGeometry,LocalSize);
    ControlsUpdated.Broadcast(ControlsSnapshot); // Source-first; no future New paint is required to commit.
}
bool UMCHubWidget::CommitUnifiedControlsView(UMCUnifiedHubWidget* View,uint64 SourceFrame)
{
    if(!bUnifiedViewActive || IsDesignTime() || UMCGameplayUIFactory::GetPresentationVersion()!=EMCUIVersion::New || !View
        || View->GetWorld()!=GetWorld() || View->GetOwningPlayer()!=GetOwningPlayer() || SourceFrame!=GFrameCounter
        || SourceFrame!=ControlsSnapshot.Reticle.SourceFrame || NativeControlsPaintedFrame==GFrameCounter
        || !View->HasControlsPresentationFrame(this,SourceFrame)) return false;
    UnifiedControlsView=View;UnifiedControlsFrame=SourceFrame;ApplyUnifiedControlsOpacity();return true;
}
void UMCHubWidget::ApplyUnifiedControlsOpacity()
{
    for(UWidget* Widget:{static_cast<UWidget*>(BasketballHint.Get()),static_cast<UWidget*>(BasketballReticle.Get())}) if(Widget) {
        if(!ControlsOriginalOpacity.Contains(Widget)) ControlsOriginalOpacity.Add(Widget,Widget->GetRenderOpacity());Widget->SetRenderOpacity(0);
    }
}
void UMCHubWidget::ClearUnifiedControlsView(const UMCUnifiedHubWidget* View)
{
    if(UnifiedControlsView.Get()!=View) return;
    UnifiedControlsView.Reset();UnifiedControlsFrame=MAX_uint64;
    for(const auto& Entry:ControlsOriginalOpacity) if(auto* Widget=Entry.Key.Get()) Widget->SetRenderOpacity(Entry.Value);
    ControlsOriginalOpacity.Reset();
}
void UMCHubWidget::ReleaseUnifiedControlsView()
{
    if(auto* View=UnifiedControlsView.Get()) View->ClearControlsPresentation(this);
    UnifiedControlsView.Reset();UnifiedControlsFrame=MAX_uint64;
    for(const auto& Entry:ControlsOriginalOpacity) if(auto* Widget=Entry.Key.Get()) Widget->SetRenderOpacity(Entry.Value);
    ControlsOriginalOpacity.Reset();
}
bool UMCHubWidget::UsesUnifiedControlsView() const
{
    return bUnifiedViewActive && UMCGameplayUIFactory::GetPresentationVersion()==EMCUIVersion::New && UnifiedControlsFrame==GFrameCounter
        && ControlsSnapshot.Reticle.SourceFrame==GFrameCounter && UnifiedControlsView.IsValid() && UnifiedControlsView->HasControlsPresentationFrame(this,UnifiedControlsFrame);
}
int32 UMCHubWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    if(const auto* Rod=MCHubPresentation::ActiveFishingMeter(PC,State()))
        Top=MCHubFishingPresentation::DrawMeter(*Rod,PC->IsUsingGamepad(),Geometry,Elements,Top,Style.GetColorAndOpacityTint());
    if(UsesUnifiedControlsView()) return Top;
    NativeControlsPaintedFrame=GFrameCounter;
    for(int32 Index=0;Index<ControlsSnapshot.NativeTrajectoryStrokeCount;++Index) {
        const auto& Stroke=ControlsSnapshot.Reticle.Strokes[Index];
        FSlateDrawElement::MakeLines(Elements,Top+Stroke.Layer,Geometry.ToPaintGeometry(),Stroke.Points,ESlateDrawEffect::None,Stroke.Color,true,Stroke.Width);
    }
    return Top+(ControlsSnapshot.NativeTrajectoryStrokeCount>0?2:0);
}

void UMCHubWidget::UpdateMouthHitAreas()
{
    const auto* Hub=State();
    const bool bCanChoose=Hub && Hub->Phase==EMCHubPhase::Selecting && IsLocalHost()
        && IsSceneReady() && SelectionPresentation && RootCanvas && SelectionPresentation->WidgetTree;
    const auto HideHitAreas=[this]()
    {
        for(const auto& HitArea:MouthHitAreas) if(HitArea) {
            HitArea->SetIsEnabled(false);HitArea->SetVisibility(ESlateVisibility::Collapsed);
        }
    };
    if(!bCanChoose)
    {
        HideHitAreas();
        return;
    }
    auto* Scroll=SelectionPresentation->WidgetTree->FindWidget(TEXT("DayScroll"));
    const auto* FirstDay=FindFProperty<FIntProperty>(SelectionPresentation->GetClass(),TEXT("WindowFirstDay"));
    if(!Scroll || !FirstDay) {HideHitAreas();return;}
    const FGeometry& CanvasGeometry=RootCanvas->GetCachedGeometry();
    const FGeometry& ClipGeometry=Scroll->GetCachedGeometry();
    if(CanvasGeometry.GetLocalSize().X<=0 || ClipGeometry.GetLocalSize().Y<=0) {HideHitAreas();return;}
    const FVector2D ClipMin=CanvasGeometry.AbsoluteToLocal(ClipGeometry.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D ClipMax=CanvasGeometry.AbsoluteToLocal(ClipGeometry.LocalToAbsolute(ClipGeometry.GetLocalSize()));
    const int32 First=FMath::Clamp(FirstDay->GetPropertyValue_InContainer(SelectionPresentation),1,5);
    for(int32 Index=0;Index<MouthHitAreas.Num();++Index)
    {
        auto* HitArea=MouthHitAreas[Index].Get();if(!HitArea) continue;
        const int32 Day=First+Index;
        auto* Row=SelectionPresentation->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Day%dSize"),Day)));
        if(!Row) {HitArea->SetVisibility(ESlateVisibility::Collapsed);continue;}
        const FGeometry& RowGeometry=Row->GetCachedGeometry();
        const FVector2D RowMin=CanvasGeometry.AbsoluteToLocal(RowGeometry.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D RowMax=CanvasGeometry.AbsoluteToLocal(RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()));
        const FVector2D Min(FMath::Max(RowMin.X,ClipMin.X),FMath::Max(RowMin.Y,ClipMin.Y));
        const FVector2D Max(FMath::Min(RowMax.X,ClipMax.X),FMath::Min(RowMax.Y,ClipMax.Y));
        const FVector2D Size=Max-Min;
        HitArea->Configure(this,EMCHubMenuAction::EnterDestination,Day,false);
        if(auto* LayoutSlot=Cast<UCanvasPanelSlot>(HitArea->Slot))
        {
            const FVector2D SafeSize=Size.ComponentMax(FVector2D::ZeroVector);
            if(!LayoutSlot->GetPosition().Equals(Min,.01)) LayoutSlot->SetPosition(Min);
            if(!LayoutSlot->GetSize().Equals(SafeSize,.01)) LayoutSlot->SetSize(SafeSize);
        }
        const auto* Destination=Hub->FindDestination(Day);
        // Future locked mouths may still be selected for preview. A selected
        // mouth is an entry action and shares the exact Start-button barrier.
        HitArea->SetIsEnabled(Destination && (Day!=Hub->SelectedDay || CanLaunchSelectedDestination()));
        HitArea->SetCursor(HitArea->GetIsEnabled()?EMouseCursor::Hand:EMouseCursor::Default);
        HitArea->SetVisibility(Size.X>0 && Size.Y>0?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(Destination)
        {
            const FText Tip=Localized(Destination->DescriptionKey,Destination->Description);
            if(!HitArea->GetToolTipText().EqualTo(Tip)) HitArea->SetToolTipText(Tip);
        }
    }
}
bool UMCHubWidget::RequiresMenuInput() const
{
    const auto* Hub=State();return Hub && Hub->Phase!=EMCHubPhase::Gathering;
}
bool UMCHubWidget::IsSceneReady() const
{
    const auto* Hub=State();
    if(!Hub || !RootCanvas || !GetCachedWidget().IsValid()) return false;
    return SelectionPresentation && SelectionPresentation->GetCachedWidget().IsValid()
        && bPresentationReady && !bPresentationFailed && MCHubPresentation::HasCamera(GetWorld());
}
bool UMCHubWidget::CanLaunchSelectedDestination() const
{
    const auto* Hub=State();
    const auto* Destination=Hub?Hub->FindDestination(Hub->SelectedDay):nullptr;
    return !IsDesignTime() && IsLocalHost() && Hub && Hub->Phase==EMCHubPhase::Selecting
        && Destination && Destination->bEnabled && Destination->HasValidMapPath() && Hub->AreAllLoaded() && Hub->AreAllReady()
        && PresentedDay==Hub->SelectedDay && IsSceneReady();
}
UWidget* UMCHubWidget::GetFocusTarget() const
{
    const auto* Hub=State();
    if(!Hub || !IsLocalHost()) return const_cast<UMCHubWidget*>(this);
    if(Hub->Phase==EMCHubPhase::Gathering) return OpenButton && OpenButton->GetIsEnabled()?static_cast<UWidget*>(OpenButton.Get()):const_cast<UMCHubWidget*>(this);
    if(RaidLoadoutView) return const_cast<UMCHubWidget*>(this);
    if(LaunchButton && LaunchButton->GetIsEnabled()) return LaunchButton.Get();
    return const_cast<UMCHubWidget*>(this);
}
void UMCHubWidget::HandleAction(EMCHubMenuAction Action,int32 Day)
{
    if(Action==EMCHubMenuAction::ToggleTeam) {
        if(!IsDesignTime()) if(auto* PC=GetOwningPlayer<AMCPlayerController>()) {
            if(PC->IsHubPartyPanelOpen()) PC->HideScoreboard();
            else PC->ShowScoreboard();
        }
        return;
    }
    if(Action==EMCHubMenuAction::InviteFriends) {
        const auto* Hub=State();
        if(!IsDesignTime() && Hub && Hub->Phase==EMCHubPhase::Gathering)
            if(auto* Instance=GetGameInstance()) if(auto* Sessions=Instance->GetSubsystem<UMCSteamSessionSubsystem>()) Sessions->InviteFriends();
        return;
    }
    if(IsDesignTime() || !IsLocalHost()) return;
    auto* PC=GetOwningPlayer<AMCPlayerController>();const auto* Hub=State();
    if(!PC || !Hub || Hub->Phase==EMCHubPhase::Travelling) return;
    switch(Action)
    {
    case EMCHubMenuAction::OpenSelection:if(Hub->Phase==EMCHubPhase::Gathering && Hub->AreAllReady() && Hub->AreAllLoaded()) PC->RequestHubOpenSelection();break;
    case EMCHubMenuAction::SelectDay:
        if(Hub->Phase==EMCHubPhase::Selecting && Hub->FindDestination(Day))
        {
            // An unavailable day disables Start. Keep arrow/Escape navigation
            // on the root instead of leaving focus on that disabled button.
            SetUserFocus(PC);PC->RequestHubSelectDay(Day);
        }
        break;
    case EMCHubMenuAction::Launch:
        if(CanLaunchSelectedDestination()) PC->RequestHubLaunch();
        break;
    case EMCHubMenuAction::EnterDestination:
        if(Hub->Phase==EMCHubPhase::Selecting && Hub->FindDestination(Day))
        {
            // Selection is authoritative. Never launch the previous day while
            // a different clicked mouth's selection request is still in flight.
            HandleAction(Day==Hub->SelectedDay?EMCHubMenuAction::Launch:EMCHubMenuAction::SelectDay,Day);
        }
        break;
    case EMCHubMenuAction::Return:if(Hub->Phase==EMCHubPhase::Selecting) PC->RequestHubReturn();break;
    default:break;
    }
}
void UMCHubWidget::SelectRelative(int32 Direction)
{
    const auto* Hub=State();if(!Hub || Hub->Phase!=EMCHubPhase::Selecting || !IsLocalHost()) return;
    if(FPlatformTime::Seconds()<NextNavigationAt) return;
    NextNavigationAt=FPlatformTime::Seconds()+.16;
    HandleAction(EMCHubMenuAction::SelectDay,FMath::Clamp(Hub->SelectedDay+Direction,1,7));
}
FReply UMCHubWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();const auto* Hub=State();
    if(!Hub) return Super::NativeOnPreviewKeyDown(Geometry,Event);
    if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right)
    {
        if(!Event.IsRepeat()) RequestUnifiedBack();
        return FReply::Handled();
    }
    if(RaidLoadoutView && Hub->Phase==EMCHubPhase::Selecting
        && (Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom))
    {
        if(RaidLoadoutView->HasUserFocusedDescendants(GetOwningPlayer())) return FReply::Unhandled();
        if(!Event.IsRepeat()) RaidLoadoutView->Confirm();
        return FReply::Handled();
    }
    if(!IsLocalHost()) return Super::NativeOnPreviewKeyDown(Geometry,Event);
    if(Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom)
    {
        if(Hub->Phase==EMCHubPhase::Gathering) HandleAction(EMCHubMenuAction::OpenSelection);
        else HandleAction(EMCHubMenuAction::Launch);
        return FReply::Handled();
    }
    if(Hub->Phase==EMCHubPhase::Selecting)
    {
        if(Key==EKeys::Up || Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Left) {SelectRelative(-1);return FReply::Handled();}
        if(Key==EKeys::Down || Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Down || Key==EKeys::Gamepad_DPad_Right) {SelectRelative(1);return FReply::Handled();}
    }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCHubWidget::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(IsLocalHost() && RequiresMenuInput() && !FMath::IsNearlyZero(Event.GetWheelDelta())) {SelectRelative(Event.GetWheelDelta()>0.f?-1:1);return FReply::Handled();}
    return Super::NativeOnMouseWheel(Geometry,Event);
}
void UMCHubWidget::NativeConstruct()
{
    Super::NativeConstruct();Init(State());PrimeControlsGlyphs();
}
void UMCHubWidget::NativeDestruct()
{
    ReleaseUnifiedControlsView();ControlsSnapshot=FMCHubControlsPresentation();
    if(auto* Hub=BoundState.Get()) Hub->OnHubStateChanged.RemoveDynamic(this,&ThisClass::RefreshFromState);
    BoundState.Reset();ResetSelectionPresentation();
    Super::NativeDestruct();
}
void UMCHubWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);PrimeControlsGlyphs();
    const bool bFishingMeter=MCHubPresentation::ActiveFishingMeter(GetOwningPlayer(),State())!=nullptr;
    if(bFishingMeter || bHadFishingMeter) Invalidate(EInvalidateWidgetReason::Paint);
    bHadFishingMeter=bFishingMeter;
    // The controller advances the authored 3D stage before its mesh components.
    // NativeTick only refreshes readiness/name labels, never advances it twice.
    RefreshElapsed+=DeltaSeconds;
    if(RefreshElapsed>=.25f) {RefreshElapsed=0.f;RefreshFromState();}
    UpdateMouthHitAreas();
    CacheControlsPresentation(Geometry.GetLocalSize());
}
void UMCHubWidget::OnLocalizationChanged()
{
    LastStateSignature.Reset();
    RefreshFromState();bControlsGlyphsPrepared=false;PrimeControlsGlyphs();
}

void UMCHubWidget::ApplyUnifiedViewSuppression()
{
    if(!bUnifiedViewActive || IsDesignTime() || !WidgetTree) return;
    if(SelectionPanel) SelectionPanel->SetVisibility(ESlateVisibility::Hidden);
}
void UMCHubWidget::SetUnifiedViewActive(bool bActive)
{
    if(IsDesignTime()) return;
    if(!bActive) ReleaseUnifiedControlsView();
    if(bUnifiedViewActive==bActive) {if(bActive) ApplyUnifiedViewSuppression();return;}
    bUnifiedViewActive=bActive;
    if(bActive) ApplyUnifiedViewSuppression();
    else {
        // Restore the current phase, not a visibility snapshot from an earlier phase.
        LastStateSignature.Reset();RefreshFromState();
    }
}
void UMCHubWidget::RequestUnifiedBack()
{
    const auto* Hub=State();if(IsDesignTime() || !Hub || Hub->Phase==EMCHubPhase::Travelling) return;
    if(RaidLoadoutView && RaidLoadoutView->CloseStock()) return;
    if(IsLocalHost() && Hub->Phase==EMCHubPhase::Selecting) HandleAction(EMCHubMenuAction::Return);
    else if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->TogglePauseMenu();
}
bool UMCHubWidget::RouteUnifiedKey(const FKeyEvent& Event)
{
    return !IsDesignTime() && NativeOnPreviewKeyDown(GetPaintSpaceGeometry(),Event).IsEventHandled();
}
bool UMCHubWidget::RouteUnifiedMouseWheel(const FPointerEvent& Event)
{
    return !IsDesignTime() && NativeOnMouseWheel(GetPaintSpaceGeometry(),Event).IsEventHandled();
}

#undef LOCTEXT_NAMESPACE
