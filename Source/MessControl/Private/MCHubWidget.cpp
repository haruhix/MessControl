#include "MCHubWidget.h"

#include "MCHubGameState.h"
#include "MCHubBasketball.h"
#include "MCToothCharacter.h"
#include "MCHubTypes.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Camera/CameraActor.h"
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
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "MCHub"

namespace MCHubPresentation
{
    bool IsSelectorWorld(const UWorld* World)
    {
        return World && World->GetMapName().EndsWith(TEXT("L_HubSelection"),ESearchCase::IgnoreCase);
    }

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
        if(World) for(TActorIterator<ACameraActor> It(World);It;++It)
            if(It->ActorHasTag(TEXT("Menu3D_Camera"))) return true;
        return false;
    }

    void HideLegacyControls(UUserWidget* Widget,bool bDisableFocus=false)
    {
        if(!Widget || !Widget->WidgetTree) return;
        // The authored stage retains its visual labels and animation. Its old
        // travel/settings controls are never focused, hit-tested or invoked.
        const FName Hidden[]={TEXT("DaysBrand"),TEXT("DaysHint"),TEXT("DaysBackButton"),
            TEXT("DaysScrollHint"),TEXT("DayCounter"),TEXT("LaunchPage")};
        for(FName Name:Hidden) if(auto* Child=Widget->WidgetTree->FindWidget(Name)) Child->SetVisibility(ESlateVisibility::Collapsed);
        for(int32 Day=1;Day<=3;++Day)
            if(auto* Child=Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Day%dCavityControls"),Day))))
                Child->SetVisibility(ESlateVisibility::Hidden);
        if(bDisableFocus) Widget->WidgetTree->ForEachWidget([](UWidget* Child)
            {
                // InitIsFocusable is protected in UE 5.8. Set its reflected
                // instance value before the authored Slate buttons are built.
                if(auto* Button=Cast<UButton>(Child)) SetFlag(Button,TEXT("IsFocusable"),false);
                if(auto* UserWidget=Cast<UUserWidget>(Child)) UserWidget->SetIsFocusable(false);
            });
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
    BallHintSlot->SetPosition(FVector2D(0,-36));BallHintSlot->SetAutoSize(true);BallHintSlot->SetZOrder(3);
    BasketballReticle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HubBasketballReticle"));
    BasketballReticle->SetText(FText::AsCultureInvariant(TEXT("+")));
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
        HitArea->Configure(this,EMCHubMenuAction::SelectDay,Index+1,false);
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
    ApplyTheme();
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
        if(!bHint) Label->SetText(FText::AsCultureInvariant(TEXT("+")));
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
    SelectionTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubSelectionTitle")));
    SelectionStatus=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubSelectionStatus")));
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
    MouthHitAreas.Reset();
    for(int32 Index=1;Index<=3;++Index)
        if(auto* HitArea=Cast<UMCHubActionButton>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("HubMouthHit%d"),Index)))))
        {
            HitArea->Configure(this,EMCHubMenuAction::SelectDay,Index,false);
            MouthHitAreas.Add(HitArea);
        }
    BoundPresentationRoot=WidgetTree->RootWidget;
    LastStateSignature.Reset();
}

void UMCHubWidget::ApplyTheme()
{
    Super::ApplyTheme();
    if(bAuthoredPresentation) return;
    const auto* ThemeData=GetResolvedTheme();if(!ThemeData) return;
    TArray<UMCHubActionButton*> Buttons={OpenButton,LaunchButton,ReturnButton};
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
    BuildPresentationTree();if(!ReadinessTitle) return;
    auto* Hub=State();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* HeldBall=Hero?AMCHubBasketball::HeldBy(Hero):nullptr;
    const bool BallHeld=HeldBall!=nullptr;
    const bool ShowBall=Hub && Hub->Phase==EMCHubPhase::Gathering && Hero && Hero->CanWork()
        && (BallHeld || AMCHubBasketball::PickupCandidate(Hero));
    if(BasketballHint) BasketballHint->SetVisibility(ShowBall?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BasketballReticle) BasketballReticle->SetVisibility(ShowBall && HeldBall && HeldBall->IsAiming()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BasketballHint) BasketballHint->SetText(BallHeld
        ? LOCTEXT("BasketballAimThrow","ПКМ — прицел   ·   ЛКМ / Q — бросить   ·   E — отпустить\nГеймпад: LB — прицел   ·   RT / Y — бросить   ·   X — отпустить")
        : LOCTEXT("BasketballPickup","E — подобрать мяч   |   Геймпад: X — подобрать"));
    FString Signature=FString::Printf(TEXT("%p:%d:%d:%d:%d:%d:%s"),Hub,Hub?int32(Hub->Phase):-1,
        Hub?Hub->SelectedDay:0,IsLocalHost(),IsSceneReady(),bPresentationFailed,Hub?*Hub->Notice.ToString():TEXT(""));
    if(Hub) for(const auto& Player:Hub->Players)
        Signature+=FString::Printf(TEXT("|%p:%d:%d:%s"),Player.Player.Get(),Player.bReady,Player.bLoaded,
            IsValid(Player.Player)?*Player.Player->GetPlayerName():TEXT(""));
    if(Signature==LastStateSignature) return;
    LastStateSignature=MoveTemp(Signature);
    const bool bSelecting=Hub && (Hub->Phase==EMCHubPhase::Selecting
        || (Hub->Phase==EMCHubPhase::Travelling && MCHubPresentation::IsSelectorWorld(GetWorld())));
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
    MCHubPresentation::SetText(ReadinessHint,bTravelling?Localized(TEXT("hub.loading.players"),LOCTEXT("TransitionWaiting","Ждём загрузку игроков…")):
        Localized(bSelecting?TEXT("hub.team_view"):TEXT("hub.ready.enter"),
            bSelecting?LOCTEXT("TeamViewHint","Вся команда видит выбранный хостом день."):LOCTEXT("GatherHint","Соберитесь в зоне готовности, чтобы выбрать день.")));
    if(OpenButton)
    {
        OpenButton->SetVisibility(Hub && Hub->Phase==EMCHubPhase::Gathering && bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        OpenButton->SetIsEnabled(Hub && Hub->AreAllReady());
    }
    MCHubPresentation::SetText(OpenButtonLabel,Localized(TEXT("hub.open_selection"),LOCTEXT("OpenSelection","ENTER · ВЫБРАТЬ ДЕНЬ")));
    if(SelectionPanel) SelectionPanel->SetVisibility(bSelecting?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ReadinessPanel) ReadinessPanel->SetVisibility(bSelecting?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    if(PresentationHost) PresentationHost->SetVisibility(bSelecting?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(bSelecting)
    {
        EnsureSelectionPresentation();
        const auto* Destination=Hub->FindDestination(Hub->SelectedDay);
        const FText Name=Destination?Localized(Destination->DisplayNameKey,Destination->DisplayName):FText::AsNumber(Hub->SelectedDay);
        const int32 Loaded=Hub->Players.FilterByPredicate([](const FMCHubPlayerReadiness& Player){return Player.bLoaded;}).Num();
        const FText Loading=Format(TEXT("hub.loading.count"),LOCTEXT("FooterLoaded","ЗАГРУЗИЛИСЬ · {Ready} / {Total}"),{{TEXT("Ready"),Loaded},{TEXT("Total"),Total}});
        MCHubPresentation::SetText(SelectionTitle,FText::Join(FText::AsCultureInvariant(TEXT(" · ")),Name,Loading));
        if(LaunchButton)
        {
            LaunchButton->SetVisibility(bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            LaunchButton->SetIsEnabled(bHost && !bTravelling && Destination && Destination->bEnabled && Destination->HasValidMapPath() && Hub->AreAllLoaded() && IsSceneReady());
        }
        MCHubPresentation::SetText(LaunchButtonLabel,Localized(TEXT("hub.selection.start"),LOCTEXT("Launch","ENTER · НАЧАТЬ")));
        if(ReturnButton) {ReturnButton->SetVisibility(bHost?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReturnButton->SetIsEnabled(!bTravelling);}
        MCHubPresentation::SetText(ReturnButtonLabel,Localized(TEXT("hub.selection.cancel"),LOCTEXT("Return","ESC · ВЕРНУТЬСЯ В ХАБ")));
        FText Status;
        if(bTravelling) Status=Localized(TEXT("hub.loading"),LOCTEXT("Travelling","ЗАГРУЖАЕМ ДЕНЬ…"));
        else if(bPresentationFailed) Status=Localized(TEXT("hub.presentation.unavailable"),LOCTEXT("PresentationUnavailable","Не удалось открыть сцену выбора. Вернитесь в хаб."));
        else if(!Hub->AreAllLoaded()) Status=Localized(TEXT("hub.loading.players"),LOCTEXT("WaitingForLoad","Ждём загрузку остальных игроков…"));
        else if(!Destination || !Destination->bEnabled || !Destination->HasValidMapPath()) Status=Localized(TEXT("hub.selection.unavailable"),LOCTEXT("DayUnavailable","Этот день ещё не подключён."));
        else Status=Localized(bHost?TEXT("hub.selection.hint"):TEXT("hub.guest.hint"),bHost?LOCTEXT("HostSelectHint","Выберите день. Запуск — Enter, возврат — Esc."):LOCTEXT("WaitingForHost","Хост выбирает день. ESC — меню."));
        MCHubPresentation::SetText(SelectionStatus,Hub->Notice.IsEmpty()?Status:Hub->Notice);
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
    const bool Waiting=PreviewScenario==EMCGameplayUIPreviewScenario::Waiting;
    const bool Travelling=PreviewState==TEXT("Travelling");
    const bool Guest=PreviewState==TEXT("Guest");
    const int32 Ready=Waiting?3:4;
    if(ReadinessPanel) ReadinessPanel->SetVisibility(Selecting?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    if(SelectionPanel) SelectionPanel->SetVisibility(Selecting?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // Preview only the widget presentation. It never creates gameplay state,
    // starts travel, or runs the actor-driving Blueprint mouth stage.
    if(PresentationHost) PresentationHost->SetVisibility(ESlateVisibility::Collapsed);
    for(const auto& HitArea:MouthHitAreas) if(HitArea) HitArea->SetVisibility(ESlateVisibility::Collapsed);
    MCHubPresentation::SetText(ReadinessTitle,Localized(TEXT("hub.title"),LOCTEXT("Title","СБОР КОМАНДЫ")));
    MCHubPresentation::SetText(ReadinessCount,Format(TEXT("hub.ready.count"),LOCTEXT("ReadyCount","ГОТОВЫ · {Ready} / {Total}"),{{TEXT("Ready"),Ready},{TEXT("Total"),4}}));
    MCHubPresentation::SetText(PlayerNames,FText::AsCultureInvariant(Waiting?TEXT("✓ Player 1\n✓ Player 2\n✓ Player 3\n… Player 4"):TEXT("✓ Player 1\n✓ Player 2\n✓ Player 3\n✓ Player 4")));
    MCHubPresentation::SetText(ReadinessHint,Localized(TEXT("hub.ready.enter"),LOCTEXT("GatherHint","Соберитесь в зоне готовности, чтобы выбрать день.")));
    MCHubPresentation::SetText(OpenButtonLabel,Localized(TEXT("hub.open_selection"),LOCTEXT("OpenSelection","ENTER · ВЫБРАТЬ ДЕНЬ")));
    MCHubPresentation::SetText(LaunchButtonLabel,Localized(TEXT("hub.selection.start"),LOCTEXT("Launch","ENTER · НАЧАТЬ")));
    MCHubPresentation::SetText(ReturnButtonLabel,Localized(TEXT("hub.selection.cancel"),LOCTEXT("Return","ESC · ВЕРНУТЬСЯ В ХАБ")));
    MCHubPresentation::SetText(SelectionTitle,Format(TEXT("hub.loading.count"),LOCTEXT("FooterLoaded","ЗАГРУЗИЛИСЬ · {Ready} / {Total}"),{{TEXT("Ready"),Ready},{TEXT("Total"),4}}));
    const FText Hint=Travelling?Localized(TEXT("hub.loading"),LOCTEXT("Travelling","ЗАГРУЖАЕМ ДЕНЬ…")):
        Waiting?Localized(TEXT("hub.loading.players"),LOCTEXT("WaitingForLoad","Ждём загрузку остальных игроков…")):
        Localized(Guest?TEXT("hub.guest.hint"):TEXT("hub.selection.hint"),Guest?LOCTEXT("WaitingForHost","Хост выбирает день. ESC — меню."):LOCTEXT("HostSelectHint","Выберите день. Запуск — Enter, возврат — Esc."));
    MCHubPresentation::SetText(SelectionStatus,Hint);
    if(OpenButton) {OpenButton->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);OpenButton->SetIsEnabled(!Waiting);}
    if(LaunchButton) {LaunchButton->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);LaunchButton->SetIsEnabled(!Waiting && !Travelling);}
    if(ReturnButton) {ReturnButton->SetVisibility(Guest?ESlateVisibility::Collapsed:ESlateVisibility::Visible);ReturnButton->SetIsEnabled(!Travelling);}
    ApplyTheme();
}

bool UMCHubWidget::EnsureSelectionPresentation()
{
    if(SelectionPresentation) return true;
    if(bPresentationFailed || IsDesignTime() || !PresentationHost || !GetOwningPlayer() || !MCHubPresentation::IsSelectorWorld(GetWorld())) return false;
    UClass* Class=SelectionPresentationClass.LoadSynchronous();
    if(!Class || !Class->IsChildOf(UUserWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) {bPresentationFailed=true;return false;}
    SelectionPresentation=CreateWidget<UUserWidget>(GetOwningPlayer(),Class);
    if(!SelectionPresentation) {bPresentationFailed=true;return false;}
    SelectionPresentation->SetIsFocusable(false);
    if(!MCHubPresentation::DisableAutonomousTick(SelectionPresentation)) {ResetSelectionPresentation();bPresentationFailed=true;return false;}
    MCHubPresentation::HideLegacyControls(SelectionPresentation,true);
    auto* PresentationSlot=PresentationHost->AddChildToOverlay(SelectionPresentation);PresentationSlot->SetHorizontalAlignment(HAlign_Fill);PresentationSlot->SetVerticalAlignment(VAlign_Fill);
    SelectionPresentation->TakeWidget(); // Blueprint Construct finishes before our authoritative setup.
    SelectionPresentation->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(!MCHubPresentation::Call(SelectionPresentation,TEXT("FindStageActors"))
        || !MCHubPresentation::CallInt(SelectionPresentation,TEXT("ShowPage"),TEXT("Index"),1))
    {ResetSelectionPresentation();bPresentationFailed=true;return false;}
    MCHubPresentation::HideLegacyControls(SelectionPresentation,true);
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
    if(!Hub || Hub->Phase==EMCHubPhase::Gathering || !MCHubPresentation::IsSelectorWorld(GetWorld()) || !EnsureSelectionPresentation()) return;
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
    bPresentationReady=bValid && MCHubPresentation::HasStageActors(SelectionPresentation,Day)
        && MCHubPresentation::HasCamera(GetWorld());
    if(!bValid) bPresentationFailed=true;
}
void UMCHubWidget::TickPresentation(float DeltaSeconds)
{
    if(DeltaSeconds>=0.f) AdvanceSelectionPresentation();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* PC=GetOwningPlayer();
    const auto* Hub=State();
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

int32 UMCHubWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    auto* PC=GetOwningPlayer();
    const auto* Ball=Hero?AMCHubBasketball::HeldBy(Hero):nullptr;
    if(!Ball || !PC || !Ball->IsAiming() || BasketballTrajectory.Num()<2) return Top;
    int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);
    if(Width<=0 || Height<=0) return Top;
    const FVector2D Size=Geometry.GetLocalSize();
    const float Scale=FMath::Clamp(float(Size.X/1600.),.5f,2.f);
    const float Dash=18.f*Scale,Gap=13.f*Scale;
    auto Project=[&](const FVector& Point,FVector2D& Local) {
        FVector2D Pixel;
        if(!PC->ProjectWorldLocationToScreen(Point,Pixel,true)) return false;
        Local=FVector2D(Pixel.X*Size.X/Width,Pixel.Y*Size.Y/Height);return true;
    };
    auto Stroke=[&](const TArray<FVector2D>& Points) {
        FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(0,0,0,.35f),true,4.f*Scale);
        FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(1,1,1,.96f),true,2.5f*Scale);
    };
    double Walked=0;
    for(int32 I=1;I<BasketballTrajectory.Num();++I) {
        FVector2D A,B;
        if(!Project(BasketballTrajectory[I-1],A) || !Project(BasketballTrajectory[I],B)) {Walked=0;continue;}
        const float Length=(B-A).Size();
        if(!FMath::IsFinite(Length) || Length<.01f || Length>10000.f*Scale) continue;
        const FVector2D Direction=(B-A)/Length;
        float Along=0;
        while(Along<Length) {
            const float Phase=FMath::Fmod(float(Walked+Along),Dash+Gap);
            const bool Visible=Phase<Dash;
            const float Count=FMath::Min(Length-Along,(Visible?Dash:Dash+Gap)-Phase);
            if(Count<.001f) {Along+=.001f;continue;}
            if(Visible) Stroke({A+Direction*Along,A+Direction*(Along+Count)});
            Along+=Count;
        }
        Walked+=Length;
    }
    FVector2D End;
    if(Project(BasketballTrajectory.Last(),End)) {
        TArray<FVector2D> Circle;
        for(int32 I=0;I<=20;++I) {
            const float Angle=I*(2.f*PI/20.f);
            Circle.Add(End+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*5.f*Scale);
        }
        Stroke(Circle);
    }
    return Top+2;
}

void UMCHubWidget::UpdateMouthHitAreas()
{
    const auto* Hub=State();
    const bool bCanChoose=Hub && Hub->Phase==EMCHubPhase::Selecting && IsLocalHost()
        && SelectionPresentation && RootCanvas && SelectionPresentation->WidgetTree;
    if(!bCanChoose)
    {
        for(const auto& HitArea:MouthHitAreas) if(HitArea) HitArea->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    auto* Scroll=SelectionPresentation->WidgetTree->FindWidget(TEXT("DayScroll"));
    const auto* FirstDay=FindFProperty<FIntProperty>(SelectionPresentation->GetClass(),TEXT("WindowFirstDay"));
    if(!Scroll || !FirstDay) return;
    const FGeometry& CanvasGeometry=RootCanvas->GetCachedGeometry();
    const FGeometry& ClipGeometry=Scroll->GetCachedGeometry();
    if(CanvasGeometry.GetLocalSize().X<=0 || ClipGeometry.GetLocalSize().Y<=0) return;
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
        HitArea->Configure(this,EMCHubMenuAction::SelectDay,Day,false);
        if(auto* LayoutSlot=Cast<UCanvasPanelSlot>(HitArea->Slot))
        {
            const FVector2D SafeSize=Size.ComponentMax(FVector2D::ZeroVector);
            if(!LayoutSlot->GetPosition().Equals(Min,.01)) LayoutSlot->SetPosition(Min);
            if(!LayoutSlot->GetSize().Equals(SafeSize,.01)) LayoutSlot->SetSize(SafeSize);
        }
        HitArea->SetIsEnabled(Hub->FindDestination(Day)!=nullptr);
        HitArea->SetVisibility(Size.X>0 && Size.Y>0?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(const auto* Destination=Hub->FindDestination(Day))
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
    if(Hub->Phase==EMCHubPhase::Gathering) return true;
    return SelectionPresentation && SelectionPresentation->GetCachedWidget().IsValid()
        && bPresentationReady && !bPresentationFailed && MCHubPresentation::HasCamera(GetWorld());
}
UWidget* UMCHubWidget::GetFocusTarget() const
{
    const auto* Hub=State();
    if(!Hub || !IsLocalHost()) return const_cast<UMCHubWidget*>(this);
    if(Hub->Phase==EMCHubPhase::Gathering) return OpenButton && OpenButton->GetIsEnabled()?static_cast<UWidget*>(OpenButton.Get()):const_cast<UMCHubWidget*>(this);
    if(LaunchButton && LaunchButton->GetIsEnabled()) return LaunchButton.Get();
    return const_cast<UMCHubWidget*>(this);
}
void UMCHubWidget::HandleAction(EMCHubMenuAction Action,int32 Day)
{
    if(IsDesignTime() || !IsLocalHost()) return;
    auto* PC=GetOwningPlayer<AMCPlayerController>();const auto* Hub=State();
    if(!PC || !Hub || Hub->Phase==EMCHubPhase::Travelling) return;
    switch(Action)
    {
    case EMCHubMenuAction::OpenSelection:if(Hub->Phase==EMCHubPhase::Gathering && Hub->AreAllReady()) PC->RequestHubOpenSelection();break;
    case EMCHubMenuAction::SelectDay:
        if(Hub->Phase==EMCHubPhase::Selecting && Hub->FindDestination(Day))
        {
            // An unavailable day disables Start. Keep arrow/Escape navigation
            // on the root instead of leaving focus on that disabled button.
            SetUserFocus(PC);PC->RequestHubSelectDay(Day);
        }
        break;
    case EMCHubMenuAction::Launch:
        if(const auto* Destination=Hub->FindDestination(Hub->SelectedDay);Hub->Phase==EMCHubPhase::Selecting
            && Destination && Destination->bEnabled && Destination->HasValidMapPath() && Hub->AreAllLoaded() && IsSceneReady()) PC->RequestHubLaunch();
        break;
    case EMCHubMenuAction::Return:if(Hub->Phase==EMCHubPhase::Selecting) PC->RequestHubReturn();break;
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
        if(IsLocalHost() && Hub->Phase==EMCHubPhase::Selecting) HandleAction(EMCHubMenuAction::Return);
        else if(auto* PC=GetOwningPlayer<AMCPlayerController>()) PC->TogglePauseMenu();
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
    Super::NativeConstruct();Init(State());
}
void UMCHubWidget::NativeDestruct()
{
    if(auto* Hub=BoundState.Get()) Hub->OnHubStateChanged.RemoveDynamic(this,&ThisClass::RefreshFromState);
    BoundState.Reset();ResetSelectionPresentation();
    Super::NativeDestruct();
}
void UMCHubWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    // The controller advances the authored 3D stage before its mesh components.
    // NativeTick only refreshes readiness/name labels, never advances it twice.
    RefreshElapsed+=DeltaSeconds;
    if(RefreshElapsed>=.25f) {RefreshElapsed=0.f;RefreshFromState();}
    UpdateMouthHitAreas();
}
void UMCHubWidget::OnLocalizationChanged()
{
    LastStateSignature.Reset();
    RefreshFromState();
}

#undef LOCTEXT_NAMESPACE
