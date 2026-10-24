#include "MCHubWidget.h"

#include "MCHubGameState.h"
#include "MCHubReadyZone.h"
#include "MCHubBasketball.h"
#include "MCFishingRodComponent.h"
#include "MCToothCharacter.h"
#include "MCHubTypes.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCHubEconomyComponent.h"
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
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"
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
        if(World) for(TActorIterator<AActor> It(World);It;++It)
            if(It->ActorHasTag(TEXT("Menu3D_Camera")) && It->FindComponentByClass<UCameraComponent>()) return true;
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
    BallHintSlot->SetPosition(FVector2D(0,-116));BallHintSlot->SetAutoSize(true);BallHintSlot->SetZOrder(3);
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
        ReadyWaypoint->SetPadding(FMargin(12,6));
        auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();ReadyWaypoint->AddChild(Body);
        ReadyWaypointArrow=AddLabel(Body,TEXT("HubReadyWaypointArrow"),24,true);
        ReadyWaypointArrow->SetJustification(ETextJustify::Center);
        ReadyWaypointArrow->SetText(FText::AsCultureInvariant(TEXT("↓")));
        ReadyWaypointLabel=AddLabel(Body,TEXT("HubReadyWaypointLabel"),18,true);
        ReadyWaypointLabel->SetJustification(ETextJustify::Center);
        ReadyWaypointLabel->SetMinDesiredWidth(174.f);ReadyWaypointLabel->SetWrapTextAt(174.f);
        auto* WaypointSlot=RootCanvas->AddChildToCanvas(ReadyWaypoint);
        WaypointSlot->SetAutoSize(true);WaypointSlot->SetAlignment(FVector2D(.5f,.5f));WaypointSlot->SetZOrder(2);
        BindStyle(TEXT("HubReadyWaypoint"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);
        BindStyle(TEXT("HubReadyWaypointArrow"),EMCUIColorToken::Accent,EMCUIStyleTarget::Text,24,true);
    }
    ReadyWaypointArrow=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadyWaypointArrow")));
    ReadyWaypointLabel=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubReadyWaypointLabel")));
    if(ReadyWaypoint) ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);
    EnsureBottomBar();
}

void UMCHubWidget::EnsureBottomBar()
{
    if(!WidgetTree || !RootCanvas) return;
    auto* ViewportCanvas=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HubViewportCanvas")));
    if(!ViewportCanvas && !WidgetTree->FindWidget(TEXT("HubViewportCanvas"))) {
        // Keep the mouth stage's authored 1600x900 fit intact. Only the social
        // footer belongs to the full viewport, including ultrawide side space.
        auto* LegacyRoot=Cast<UScaleBox>(WidgetTree->RootWidget);
        auto* DesignFrame=LegacyRoot?Cast<USizeBox>(LegacyRoot->GetContent()):nullptr;
        if(DesignFrame && DesignFrame->GetContent()==RootCanvas && FMath::IsNearlyEqual(DesignFrame->GetWidthOverride(),1600.f)
            && FMath::IsNearlyEqual(DesignFrame->GetHeightOverride(),900.f)) {
            ViewportCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("HubViewportCanvas"));
            WidgetTree->RootWidget=ViewportCanvas;
            auto* LegacySlot=ViewportCanvas->AddChildToCanvas(LegacyRoot);
            LegacySlot->SetAnchors(FAnchors(0,0,1,1));LegacySlot->SetOffsets(FMargin(0));LegacySlot->SetZOrder(0);
        }
    }
    UCanvasPanel* FooterCanvas=ViewportCanvas && WidgetTree->RootWidget==ViewportCanvas?ViewportCanvas:RootCanvas.Get();
    // One full-width footer joins the currency and profile groups. Its own
    // named branch is additive: existing artist widgets keep their parents.
    auto* Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubFooterBackdrop")));
    if(!Backdrop) {
        Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HubFooterBackdrop"));
        Backdrop->SetPadding(FMargin(0));Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* FooterSlot=FooterCanvas->AddChildToCanvas(Backdrop);
        FooterSlot->SetAnchors(FAnchors(0,1,1,1));FooterSlot->SetOffsets(FMargin(0,-88,0,88));FooterSlot->SetZOrder(4);
    }
    BottomBar=Cast<UHorizontalBox>(WidgetTree->FindWidget(TEXT("HubBottomBar")));
    if(!BottomBar) {
        BottomBar=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HubBottomBar"));
        auto* BarSlot=FooterCanvas->AddChildToCanvas(BottomBar);
        BarSlot->SetAnchors(FAnchors(0,1,1,1));BarSlot->SetOffsets(FMargin(24,-76,24,64));BarSlot->SetZOrder(5);
        auto Label=[&](FName Name,int32 Size,EMCUIColorToken Token)->UTextBlock* {
            auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
            Text->SetAutoWrapText(false);BindStyle(Name,Token,EMCUIStyleTarget::Text,Size,true);return Text;
        };
        auto Card=[&](FName Name,UWidget* Content)->UBorder* {
            auto* Frame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
            Frame->SetPadding(FMargin(18,12));Frame->SetContent(Content);
            Frame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,BottomBarCornerRadius));
            return Frame;
        };
        auto Currency=[&](FName RowName,FName IconName,FName TextName,bool Fish)->UHorizontalBox* {
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),RowName);
            auto* Icon=WidgetTree->ConstructWidget<UMCHubCurrencyIcon>(UMCHubCurrencyIcon::StaticClass(),IconName);Icon->bFish=Fish;
            auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(30);IconSize->SetHeightOverride(30);IconSize->SetContent(Icon);
            Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
            auto* TextSlot=Row->AddChildToHorizontalBox(Label(TextName,22,EMCUIColorToken::Ink));TextSlot->SetVerticalAlignment(VAlign_Center);TextSlot->SetPadding(FMargin(9,0,0,0));
            return Row;
        };
        auto* Wallet=Card(TEXT("HubWalletCard"),Currency(TEXT("HubWalletRow"),TEXT("HubCoinIcon"),TEXT("HubWalletCount"),false));
        BottomBar->AddChildToHorizontalBox(Wallet)->SetPadding(FMargin(0,0,10,0));
        auto* Fish=Card(TEXT("HubFishCard"),Currency(TEXT("HubFishRow"),TEXT("HubFishIcon"),TEXT("HubBottomFishCount"),true));
        BottomBar->AddChildToHorizontalBox(Fish)->SetPadding(FMargin(0,0,10,0));
        BottomBar->AddChildToHorizontalBox(WidgetTree->ConstructWidget<UHorizontalBox>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Profile=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("HubProfileRow"));
        auto* Portrait=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("HubProfilePortrait"));
        Portrait->bTooth=true;Portrait->bHappy=true;
        auto* PortraitSize=WidgetTree->ConstructWidget<USizeBox>();PortraitSize->SetWidthOverride(40);PortraitSize->SetHeightOverride(44);PortraitSize->SetContent(Portrait);
        Profile->AddChildToHorizontalBox(PortraitSize)->SetPadding(FMargin(0,0,12,0));
        auto* Name=Label(TEXT("HubProfileName"),20,EMCUIColorToken::Ink);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetClipping(EWidgetClipping::ClipToBounds);
        auto* NameSize=WidgetTree->ConstructWidget<USizeBox>();NameSize->SetWidthOverride(190);NameSize->SetContent(Name);
        auto* NameSlot=Profile->AddChildToHorizontalBox(NameSize);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(0,0,12,0));
        Profile->AddChildToHorizontalBox(Label(TEXT("HubPartyHint"),21,EMCUIColorToken::Ink))->SetVerticalAlignment(VAlign_Center);
        BottomBar->AddChildToHorizontalBox(Card(TEXT("HubProfileCard"),Profile));
    }
    WalletCount=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubWalletCount")));
    BottomFishCount=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubBottomFishCount")));
    ProfileName=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubProfileName")));
    PartyHint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HubPartyHint")));
    ProfilePortrait=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("HubProfilePortrait")));
    CoinIcon=Cast<UMCHubCurrencyIcon>(WidgetTree->FindWidget(TEXT("HubCoinIcon")));
    FishIcon=Cast<UMCHubCurrencyIcon>(WidgetTree->FindWidget(TEXT("HubFishIcon")));
    if(FooterCanvas!=RootCanvas) {
        for(UWidget* Item:{static_cast<UWidget*>(Backdrop),static_cast<UWidget*>(BottomBar.Get())})
            if(Item && Item->GetParent()==RootCanvas) {
                Item->RemoveFromParent();auto* FooterSlot=FooterCanvas->AddChildToCanvas(Item);
                FooterSlot->SetAnchors(FAnchors(0,1,1,1));FooterSlot->SetAutoSize(false);
                FooterSlot->SetOffsets(Item==Backdrop?FMargin(0,-88,0,88):FMargin(24,-76,24,64));FooterSlot->SetZOrder(Item==Backdrop?4:5);
            }
    }
    if(bUseNativeLayout) if(auto* RowSlot=Cast<UCanvasPanelSlot>(BottomBar->Slot)) {
        RowSlot->SetAnchors(FAnchors(0,1,1,1));RowSlot->SetOffsets(FMargin(24,-76,24,64));RowSlot->SetZOrder(5);
    }
}
void UMCHubWidget::RefreshBottomBar()
{
    const auto* Hub=State();
    const bool Preview=IsDesignTime();
    const bool Gathering=Preview?PreviewState!=TEXT("Selecting") && PreviewState!=TEXT("Selection") && PreviewState!=TEXT("Travelling")
        :Hub && Hub->Phase==EMCHubPhase::Gathering;
    if(BottomBar) BottomBar->SetVisibility(Gathering?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
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
    if(BottomFishCount) BottomFishCount->SetToolTipText(Localized(TEXT("hub.bottom.fish"),LOCTEXT("BottomFish","Рыба для гриля")));
    const auto& BottomPalette=GetResolvedTheme()->Palette;
    if(CoinIcon) CoinIcon->Tint=bUseSoftBottomBar?FMath::Lerp(BottomPalette.Warning,BottomPalette.Ink,.2f):BottomPalette.Warning;
    if(FishIcon) FishIcon->Tint=bUseSoftBottomBar?FMath::Lerp(BottomPalette.Mint,BottomPalette.Ink,.4f):BottomPalette.Mint;
    const FText Name=Preview?FText::AsCultureInvariant(TEXT("Player 1")):Identity?FText::AsCultureInvariant(Identity->GetPlayerName()):FText::GetEmpty();
    MCHubPresentation::SetText(ProfileName,Name);
    if(ProfileName) ProfileName->SetToolTipText(Name);
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const FText Key=FText::AsCultureInvariant(PC && PC->IsUsingGamepad()?TEXT("[View]"):TEXT("[Tab]"));
    MCHubPresentation::SetText(PartyHint,Format(TEXT("hub.bottom.party"),LOCTEXT("BottomParty","Команда {Count}/4 · {Key}"),
        {{TEXT("Count"),Preview?4:Hub?Hub->GetParticipantCount():0},{TEXT("Key"),Key}}));
    if(ProfilePortrait) {
        const auto& Palette=GetResolvedTheme()->Palette;
        ProfilePortrait->Tint=Preview?Palette.Accent:Identity?Identity->PlayerColor:Palette.Accent;
        ProfilePortrait->InkTint=Palette.Ink;ProfilePortrait->EmptyTint=Palette.Muted;
        ProfilePortrait->HostTint=Palette.Warning;ProfilePortrait->bHost=Preview || (Identity && Identity->bSessionHost);
        ProfilePortrait->InvalidateLayoutAndVolatility();
    }
    if(bUseNativeLayout && BasketballHint) if(auto* HintSlot=Cast<UCanvasPanelSlot>(BasketballHint->Slot)) HintSlot->SetPosition(FVector2D(0,-116));
}

void UMCHubWidget::UpdateReadyWaypoint()
{
    if(!ReadyWaypoint || !RootCanvas || IsDesignTime()) return;
    const auto* Hub=State();
    auto* PC=GetOwningPlayer();auto* Pawn=GetOwningPlayerPawn();
    const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0.;
    if(!Hub || Hub->Phase!=EMCHubPhase::Gathering || !PC || !Pawn) {
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
    const FVector2D Size=RootCanvas->GetCachedGeometry().GetLocalSize();
    if(Width<=0 || Height<=0 || Size.X<300.f || Size.Y<180.f) {ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);return;}
    const FVector Anchor=Target->GetActorLocation()+FVector(0,0,70.f);
    FVector2D Pixel,Point;
    const bool Projected=PC->ProjectWorldLocationToScreen(Anchor,Pixel,false);
    const FVector2D Center=Size*.5f;
    const FVector2D Min(110.f,70.f),Max(Size.X-110.f,Size.Y-70.f);
    if(Projected) {
        FVector2D Absolute;
        USlateBlueprintLibrary::ScreenToWidgetAbsolute(this,Pixel,Absolute,true);
        Point=RootCanvas->GetCachedGeometry().AbsoluteToLocal(Absolute);
    }
    else {
        FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
        const FVector Offset=Anchor-Eye;
        Point=FVector2D(FVector::DotProduct(Offset,View.RotateVector(FVector::RightVector)),
            -FVector::DotProduct(Offset,View.RotateVector(FVector::UpVector)));
        if(FVector::DotProduct(Offset,View.Vector())<0.) Point.Y=FMath::Max(FMath::Abs(Point.Y),1.);
        Point=Center+Point.GetSafeNormal()*Size.Size();
    }
    const bool InFrame=Projected && Point.X>=Min.X && Point.X<=Max.X && Point.Y>=Min.Y+85.f && Point.Y<=Max.Y;
    FVector2D Position,Direction;
    if(InFrame) {Position=Point-FVector2D(0,85.f);Direction=FVector2D(0,1);}
    else {
        Direction=(Point-Center).GetSafeNormal();
        if(Direction.IsNearlyZero()) Direction=FVector2D(0,1);
        const double X=FMath::Abs(Direction.X)>UE_SMALL_NUMBER?(Size.X*.5f-110.f)/FMath::Abs(Direction.X):MAX_dbl;
        const double Y=FMath::Abs(Direction.Y)>UE_SMALL_NUMBER?(Size.Y*.5f-70.f)/FMath::Abs(Direction.Y):MAX_dbl;
        Position=Center+Direction*FMath::Min(X,Y);
    }
    Position.X=FMath::Clamp(Position.X,Min.X,Max.X);Position.Y=FMath::Clamp(Position.Y,Min.Y,Max.Y);
    if(auto* Layout=Cast<UCanvasPanelSlot>(ReadyWaypoint->Slot)) Layout->SetPosition(Position);
    if(ReadyWaypointArrow) ReadyWaypointArrow->SetRenderTransformAngle(float(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X))-90.));
    const int32 Metres=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Pawn->GetActorLocation(),Target->GetActorLocation())/100.));
    MCHubPresentation::SetText(ReadyWaypointLabel,Format(TEXT("hub.ready.waypoint"),LOCTEXT("ReadyWaypoint","READY · {Metres} м"),{{TEXT("Metres"),Metres}}));
    ReadyWaypoint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMCHubWidget::ApplyTheme()
{
    Super::ApplyTheme();
    if(bUseSoftBottomBar && WidgetTree) {
        const auto& Palette=GetResolvedTheme()->Palette;
        FLinearColor FooterColor=Palette.Cream;FooterColor.A=.95f;
        if(auto* Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HubFooterBackdrop")))) {
            Backdrop->SetBrush(FSlateRoundedBoxBrush(FooterColor,0.f));Backdrop->SetBrushColor(FLinearColor::White);
        }
        for(FName Name:{FName(TEXT("HubWalletCard")),FName(TEXT("HubFishCard")),FName(TEXT("HubProfileCard"))})
            if(auto* Card=Cast<UBorder>(WidgetTree->FindWidget(Name))) {
                FLinearColor Color=Name==TEXT("HubProfileCard")?Palette.SurfaceAlt:Palette.Surface;
                Color.A=FMath::Clamp(BottomBarOpacity,0.f,1.f);
                Card->SetBrush(FSlateRoundedBoxBrush(Color,FMath::Min(24.f,BottomBarCornerRadius)));Card->SetBrushColor(FLinearColor::White);
            }
        for(FName Name:{FName(TEXT("HubWalletCount")),FName(TEXT("HubBottomFishCount")),FName(TEXT("HubProfileName")),FName(TEXT("HubPartyHint"))})
            if(auto* Text=Cast<UTextBlock>(WidgetTree->FindWidget(Name))) Text->SetColorAndOpacity(Palette.Ink);
    }
    if(bAuthoredPresentation) return;
    const auto* ThemeData=GetResolvedTheme();if(!ThemeData) return;
    TArray<UMCHubActionButton*> Buttons={OpenButton,LaunchButton,ReturnButton,InviteButton};
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
    const auto* PC=GetOwningPlayer<AMCPlayerController>();
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* Identity=GetOwningPlayer()?GetOwningPlayer()->GetPlayerState<AMCPlayerState>():nullptr;
    if(FishingPanel) FishingPanel->SetVisibility(Hub && Hub->Phase==EMCHubPhase::Gathering?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    MCHubPresentation::SetText(FishingCount,Format(TEXT("hub.fishing.count"),LOCTEXT("FishingCount","Рыба: {Count}"),{{TEXT("Count"),Identity?Identity->FishCaught:0}}));
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
    const bool ShowBall=Hub && Hub->Phase==EMCHubPhase::Gathering && Hero && Hero->CanWork()
        && (BallHeld || AMCHubBasketball::PickupCandidate(Hero));
    const bool ShowFishing=Hub && Hub->Phase==EMCHubPhase::Gathering && Hero && Hero->FishingRod && Hero->FishingRod->IsPresented();
    if(BasketballHint) BasketballHint->SetVisibility(ShowBall || ShowFishing?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BasketballReticle) BasketballReticle->SetVisibility(ShowBall && HeldBall && HeldBall->IsAiming()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(BasketballHint) BasketballHint->SetText(BallHeld
        ? LOCTEXT("BasketballAimThrow","ПКМ — прицел   ·   ЛКМ / Q — бросить   ·   E — отпустить\nГеймпад: LB — прицел   ·   RT / Y — бросить   ·   X — отпустить")
        : LOCTEXT("BasketballPickup","E — подобрать мяч   |   Геймпад: X — подобрать"));
    if(BasketballHint && ShowFishing && !ShowBall) BasketballHint->SetText(Hero->FishingRod->bLineOut || Hero->FishingRod->CanCastLine()
        ? LOCTEXT("FishingPondControls","ПКМ — поза рыбалки   ·   ЛКМ — заброс в пруд   ·   удерживать E — подмотка\nХодьба — закончить рыбалку   ·   5 — убрать удочку   ·   1–4 — инструменты")
        : LOCTEXT("FishingPondRequired","Для заброса подойди к пруду и повернись к воде\n5 — убрать удочку   ·   1–4 — инструменты"));
    if(BasketballHint && ShowFishing && !ShowBall && Hero->FishingRod->bLineOut && !Hero->FishingRod->bReeling)
        BasketballHint->SetText(LOCTEXT("FishingWaiting","Ждём поклёвку…\nРыба вытянется автоматически   ·   ходьба — закончить рыбалку"));
    if(BasketballHint && ShowFishing && !ShowBall && Hero->FishingRod->bFishHooked)
        BasketballHint->SetText(LOCTEXT("FishingHooked","Рыба на крючке! Вытягиваем…\nПосле подбора рыба добавится к улову"));
    FString Signature=FString::Printf(TEXT("%p:%d:%d:%d:%d:%d:%d:%d:%s"),Hub,Hub?int32(Hub->Phase):-1,
        Hub?Hub->SceneGeneration:0,Hub?Hub->SelectedDay:0,IsLocalHost(),IsSceneReady(),bPresentationFailed,bCanInvite,Hub?*Hub->Notice.ToString():TEXT(""));
    if(Hub) for(const auto& Player:Hub->Players)
        Signature+=FString::Printf(TEXT("|%p:%d:%d:%s"),Player.Player.Get(),Player.bReady,Player.bLoaded,
            IsValid(Player.Player)?*Player.Player->GetPlayerName():TEXT(""));
    Signature+=GetWorld() && GetWorld()->GetRealTimeSeconds()<InteractionFeedbackUntil?TEXT("|Feedback"):TEXT("|Guidance");
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
    MCHubPresentation::SetText(ReadinessHint,GetWorld() && GetWorld()->GetRealTimeSeconds()<InteractionFeedbackUntil?InteractionFeedback:
        bTravelling?Localized(TEXT("hub.loading.players"),LOCTEXT("TransitionWaiting","Ждём загрузку игроков…")):
        Localized(bSelecting?TEXT("hub.team_view"):TEXT("hub.ready.flow"),
            bSelecting?LOCTEXT("TeamViewHint","Вся команда видит выбранный хостом день."):
                LOCTEXT("GatherFlowHint","Чтобы начать игру, всей командой встаньте в подсвеченную READY-зону. Затем хост нажимает Enter и выбирает локацию.")));
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
    MCHubPresentation::SetText(ReadinessHint,Localized(TEXT("hub.ready.flow"),LOCTEXT("GatherFlowHint","Чтобы начать игру, всей командой встаньте в подсвеченную READY-зону. Затем хост нажимает Enter и выбирает локацию.")));
    MCHubPresentation::SetText(InviteButtonLabel,Localized(TEXT("hub.invite"),LOCTEXT("InviteFriends","ПРИГЛАСИТЬ ДРУЗЕЙ")));
    MCHubPresentation::SetText(InviteHint,Localized(TEXT("hub.invite.steam"),LOCTEXT("InviteSteam","ESC — меню и приглашения · Shift+Tab — друзья Steam")));
    if(InviteButton) InviteButton->SetVisibility(Selecting?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    if(ReadyWaypoint) ReadyWaypoint->SetVisibility(ESlateVisibility::Collapsed);
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
    RefreshBottomBar();
    ApplyTheme();
}

bool UMCHubWidget::EnsureSelectionPresentation()
{
    if(SelectionPresentation) return true;
    if(bPresentationFailed || IsDesignTime() || !PresentationHost || !GetOwningPlayer() || !MCHubPresentation::HasSavedStage(GetWorld())) return false;
    MCHubPresentation::FPreservePlayerView PreserveView(GetOwningPlayer());
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
