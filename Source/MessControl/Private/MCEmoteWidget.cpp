#include "MCEmoteWidget.h"
#include "MCUIDataLibrary.h"
#include "MCUIVisualStyle.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "InputKeyEventArgs.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MCEmoteUI"
namespace MCEmotePrivate
{
    float LayoutScale(const FGeometry& Geometry) {const FVector2D Area=Geometry.GetLocalSize();return FMath::Min(1.f,FMath::Min(float(Area.X)/1000.f,float(Area.Y)/700.f));}
    FVector2D WheelCenter(const FGeometry& Geometry) {return Geometry.GetLocalSize()*.5f+FVector2D(-165,-10)*LayoutScale(Geometry);}
    const FLinearColor PlayerColors[]={FLinearColor(.24f,.65f,1),FLinearColor(1,.27f,.31f),FLinearColor(.26f,.91f,.71f),FLinearColor(.75f,.45f,1)};
    UTextBlock* Label(UWidgetTree* Tree,const FText& Text,int32 Size,FName Name=NAME_None)
    {
        auto* Result=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Result->SetText(Text);Result->SetAutoWrapText(true);
        if (const auto* Owner=Tree->GetTypedOuter<UMCGameplayUIWidget>()) if(const auto* Style=Owner->GetResolvedTheme()) {
            Result->SetFont(Style->MakeGameplayFont(Size));Result->SetColorAndOpacity(Style->Palette.Ink);
        }
        Result->SetJustification(ETextJustify::Center);return Result;
    }
}

int32 UMCEmoteWheelWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const FVector2D Center=Geometry.GetLocalSize()*.5f;
    const float Scale=FMath::Min(float(Geometry.GetLocalSize().X),float(Geometry.GetLocalSize().Y))/428.f;
    const FSlateBrush* White=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
    const auto Transform=Geometry.GetAccumulatedRenderTransform();
    auto Strip=[&](float Inner,float Outer,float Start,float End,FLinearColor InnerColor,FLinearColor OuterColor,int32 PaintLayer)
    {
        constexpr int32 Steps=72;
        TArray<FSlateVertex> Vertices;Vertices.Reserve((Steps+1)*2);
        TArray<SlateIndex> Indices;Indices.Reserve(Steps*6);
        const FColor Inside=(InnerColor*Style.GetColorAndOpacityTint()).ToFColorSRGB();
        const FColor Outside=(OuterColor*Style.GetColorAndOpacityTint()).ToFColorSRGB();
        for(int32 Step=0;Step<=Steps;++Step) {
            const float Angle=FMath::DegreesToRadians(FMath::Lerp(Start,End,float(Step)/Steps));
            const FVector2D Direction(FMath::Cos(Angle),FMath::Sin(Angle));
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Center+Direction*Inner*Scale),FVector2f(.5f,.5f),Inside));
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Center+Direction*Outer*Scale),FVector2f(.5f,.5f),Outside));
            if(Step<Steps) {
                const SlateIndex Base=static_cast<SlateIndex>(Step*2);
                Indices.Append({Base,static_cast<SlateIndex>(Base+1),static_cast<SlateIndex>(Base+2),
                    static_cast<SlateIndex>(Base+1),static_cast<SlateIndex>(Base+3),static_cast<SlateIndex>(Base+2)});
            }
        }
        FSlateDrawElement::MakeCustomVerts(Elements,PaintLayer,Resource,Vertices,Indices,nullptr,0,0);
    };
    for(int32 Index=0;Index<3;++Index) {
        const float Start=-148.f+Index*120,End=Start+116.f;
        const FLinearColor Fill=Index==SelectedSector?SelectedTint:NormalTint;
        Strip(65,210,Start,End,BorderTint,BorderTint,Layer);
        Strip(67.5f,207.5f,Start+.6f,End-.6f,Fill,Fill,Layer+1);
        Strip(210,211,Start,End,BorderTint,BorderTint.CopyWithNewOpacity(0),Layer+1);
        Strip(64,65,Start,End,BorderTint.CopyWithNewOpacity(0),BorderTint,Layer+1);
    }
    return Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer+2,Style,ParentEnabled);
}

void UMCEmoteWidget::EnsureWheelPresentation()
{
    if(!WidgetTree) return;
    auto* Canvas=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("EmoteRoot")));
    if(!Canvas) return;
    // NativePaint on a UUserWidget runs AFTER all of its children. A backdrop
    // there darkens every button and a wheel there covers its own text labels.
    // Explicit child ordering works for both saved Phase1 trees and new trees.
    auto* Viewport=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("EmoteViewport")));
    if(!Viewport) {
        UWidget* Content=WidgetTree->RootWidget;
        Viewport=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("EmoteViewport"));WidgetTree->RootWidget=Viewport;
        auto* ContentSlot=Viewport->AddChildToCanvas(Content);ContentSlot->SetAnchors(FAnchors(0,0,1,1));ContentSlot->SetOffsets(FMargin(0));ContentSlot->SetZOrder(0);
    }
    if(!WidgetTree->FindWidget(TEXT("EmoteBackdrop"))) {
        auto* Backdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("EmoteBackdrop"));Backdrop->SetPadding(FMargin(0));Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* BackdropSlot=Viewport->AddChildToCanvas(Backdrop);BackdropSlot->SetAnchors(FAnchors(0,0,1,1));BackdropSlot->SetOffsets(FMargin(0));BackdropSlot->SetZOrder(-10);
    }
    Wheel=Cast<UMCEmoteWheelWidget>(WidgetTree->FindWidget(TEXT("EmoteWheel")));
    if(!Wheel) {
        Wheel=WidgetTree->ConstructWidget<UMCEmoteWheelWidget>(UMCEmoteWheelWidget::StaticClass(),TEXT("EmoteWheel"));Wheel->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* WheelSlot=Canvas->AddChildToCanvas(Wheel);WheelSlot->SetAnchors(FAnchors(.5f,.5f));WheelSlot->SetAlignment(FVector2D(.5f,.5f));WheelSlot->SetPosition(FVector2D(-165,-10));WheelSlot->SetSize(FVector2D(428,428));WheelSlot->SetZOrder(-1);
    }
}
UMCEmoteButton::UMCEmoteButton() {InitIsFocusable(false);}
void UMCEmoteButton::Configure(AMCPlayerController* Player,FName Emote)
{
    Controller=Player;Id=Emote;OnClicked.AddUniqueDynamic(this,&UMCEmoteButton::Clicked);
}
void UMCEmoteButton::ConfigureAlarm(AMCPlayerController* Player,EMCPlayerAlarm Value)
{
    Configure(Player,NAME_None);Alarm=Value;
}
void UMCEmoteButton::ConfigureColor(AMCPlayerController* Player,FLinearColor Value)
{
    Configure(Player,NAME_None);bColorChoice=true;PlayerColor=Value;
}
void UMCEmoteButton::Clicked()
{
    if(!Controller) return;
    if(bColorChoice) {Controller->ServerSetPlayerColor(PlayerColor);return;}
    if(Alarm!=EMCPlayerAlarm::None) Controller->ServerSendAlarm(Alarm);
    else if(auto* Hero=Cast<AMCToothCharacter>(Controller->GetPawn());Hero && Hero->Expression) Hero->Expression->ServerPlayEmote(Id);
    Controller->ToggleEmotes();
}

void UMCEmoteWidget::NativeOnInitialized() {Super::NativeOnInitialized();SetIsFocusable(true);}
void UMCEmoteWidget::NativeConstruct()
{
    Super::NativeConstruct();if(!IsPreviewing()) Refresh();
}
void UMCEmoteWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {
        Items=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("EmoteItems")));
        Hint=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EmoteHint")));
        CategoryTitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EmoteCategoryTitle")));
        ListPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("EmoteListPanel")));
        Footer=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EmoteFooter")));
        ColorHeading=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EmoteColorHeading")));
        SectorLabels.Reset();ColorButtons.Reset();ColorLabels.Reset();
        for(int32 Index=0;Index<3;++Index) SectorLabels.Add(Cast<UTextBlock>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("EmoteSector%d"),Index)))));
        for(int32 Index=0;Index<4;++Index) {
            ColorButtons.Add(Cast<UMCEmoteButton>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("EmoteColor%d"),Index)))));
            ColorLabels.Add(Cast<UTextBlock>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("EmoteColorLabel%d"),Index)))));
        }
        EnsureWheelPresentation();
        return;
    }
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("EmoteScale"));Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);WidgetTree->RootWidget=Scale;
    auto* DesignSize=WidgetTree->ConstructWidget<USizeBox>();DesignSize->SetWidthOverride(1000);DesignSize->SetHeightOverride(700);Scale->SetContent(DesignSize);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("EmoteRoot"));DesignSize->SetContent(Root);
    ListPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("EmoteListPanel"));ListPanel->SetPadding(FMargin(20));
    auto* Panel=Root->AddChildToCanvas(ListPanel);Panel->SetAnchors(FAnchors(.5f,.5f));Panel->SetAlignment(FVector2D(0,.5f));Panel->SetPosition(FVector2D(80,-10));Panel->SetSize(FVector2D(360,470));
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("EmoteListContent"));ListPanel->SetContent(Box);
    CategoryTitle=MCEmotePrivate::Label(WidgetTree,FText::GetEmpty(),25,TEXT("EmoteCategoryTitle"));
    auto* Header=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("EmoteHeader"));Header->SetPadding(FMargin(12,10));Header->SetContent(CategoryTitle);Box->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,12));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("EmoteListScroll"));Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Items=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("EmoteItems"));Scroll->AddChild(Items);
    Hint=MCEmotePrivate::Label(WidgetTree,FText::GetEmpty(),13,TEXT("EmoteHint"));Box->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,12,0,0));
    auto CanvasText=[&](FName Name,FVector2D Position,FVector2D Size,int32 FontSize) {
        auto* Text=MCEmotePrivate::Label(WidgetTree,FText::GetEmpty(),FontSize,Name);
        auto* TextSlot=Root->AddChildToCanvas(Text);TextSlot->SetAnchors(FAnchors(.5f,.5f));TextSlot->SetAlignment(FVector2D(.5f,.5f));TextSlot->SetPosition(Position);TextSlot->SetSize(Size);return Text;
    };
    for(int32 Index=0;Index<3;++Index) {
        const float Mid=FMath::DegreesToRadians(-90.f+Index*120);
        SectorLabels.Add(CanvasText(FName(*FString::Printf(TEXT("EmoteSector%d"),Index)),FVector2D(-165,-10)+FVector2D(FMath::Cos(Mid),FMath::Sin(Mid))*136.f,FVector2D(190,48),16));
    }
    auto* Center=CanvasText(TEXT("EmoteCenterKey"),FVector2D(-165,-10),FVector2D(54,40),24);Center->SetText(FText::AsCultureInvariant(TEXT("T")));
    ColorHeading=CanvasText(TEXT("EmoteColorHeading"),FVector2D(-165,208),FVector2D(390,25),12);
    Footer=CanvasText(TEXT("EmoteFooter"),FVector2D(-40,295),FVector2D(800,54),12);
    auto* Colors=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("EmoteColors"));
    auto* ColorSlot=Root->AddChildToCanvas(Colors);ColorSlot->SetAnchors(FAnchors(.5f,.5f));ColorSlot->SetAlignment(FVector2D(.5f,0));ColorSlot->SetPosition(FVector2D(-165,230));ColorSlot->SetSize(FVector2D(390,40));
    for(int32 Index=0;Index<4;++Index) {
        auto* Button=WidgetTree->ConstructWidget<UMCEmoteButton>(UMCEmoteButton::StaticClass(),FName(*FString::Printf(TEXT("EmoteColor%d"),Index)));
        Button->ConfigureColor(nullptr,MCEmotePrivate::PlayerColors[Index]);
        auto* Text=MCEmotePrivate::Label(WidgetTree,FText::GetEmpty(),12,FName(*FString::Printf(TEXT("EmoteColorLabel%d"),Index)));Button->SetContent(Text);
        auto* ButtonSlot=Colors->AddChildToHorizontalBox(Button);ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ButtonSlot->SetPadding(FMargin(3));
        ColorButtons.Add(Button);ColorLabels.Add(Text);
    }
    EnsureWheelPresentation();ApplyTheme();
}
FText UMCEmoteWidget::Copy(FName Key,const FText& Fallback) const
{
    const auto* Style=GetResolvedTheme();UDataTable* Table=Style?Style->LocalizationTable.LoadSynchronous():nullptr;
    if(!Table) Table=UMCUIDataLibrary::GetDefaultLocalizationTable();
    return UMCUIDataLibrary::ResolveText(Table,Key,Fallback,IsPreviewing()?PreviewCulture:FString());
}
FText UMCEmoteWidget::CategoryCopy(int32 Index) const
{
    if(Index==1) return Copy(TEXT("UI.Emote.Category.Gesture"),LOCTEXT("Gesture","Жесты"));
    if(Index==2) return Copy(TEXT("UI.Emote.Category.Signal"),LOCTEXT("Signal","Сигналы"));
    return Copy(TEXT("UI.Emote.Category.Face"),LOCTEXT("Face","Мимика"));
}
void UMCEmoteWidget::SelectCategory(int32 Value) {Category=FMath::Clamp(Value,0,2);KeyboardChoice=0;Refresh();InvalidateLayoutAndVolatility();}
void UMCEmoteWidget::Refresh()
{
    BuildPresentationTree();if(!Items) return;Items->ClearChildren();Buttons.Reset();
    if(CategoryTitle) CategoryTitle->SetText(CategoryCopy(Category));
    auto* Player=IsPreviewing()?nullptr:Cast<AMCPlayerController>(GetOwningPlayer());
    for(int32 Index=0;Index<ColorButtons.Num();++Index) if(ColorButtons[Index]) ColorButtons[Index]->ConfigureColor(Player,MCEmotePrivate::PlayerColors[Index]);
    auto Add=[&](FName Id,const FText& Label,EMCPlayerAlarm Alarm) {
        auto* Button=WidgetTree->ConstructWidget<UMCEmoteButton>(UMCEmoteButton::StaticClass(),FName(*FString::Printf(TEXT("EmoteItem%d"),Buttons.Num())));
        if(Alarm==EMCPlayerAlarm::None) Button->Configure(Player,Id);else Button->ConfigureAlarm(Player,Alarm);
        Button->SetContent(MCEmotePrivate::Label(WidgetTree,Label,17));Items->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5));Buttons.Add(Button);
    };
    if(Category==2) {
        Add(NAME_None,Copy(TEXT("UI.Emote.Signal.Attention"),LOCTEXT("Attention","!  Внимание")),EMCPlayerAlarm::Attention);
        Add(NAME_None,Copy(TEXT("UI.Emote.Signal.Help"),LOCTEXT("Help","+  Нужна помощь")),EMCPlayerAlarm::Help);
    } else {
        const UMCEmoteLibrary* Library=LoadedPreviewLibrary;
        if(!IsPreviewing()) {const auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();Library=Hero && Hero->Expression?Hero->Expression->Library.Get():nullptr;}
        if(Library) for(const FMCEmoteEntry& Entry:Library->Entries) {
            const bool FaceOnly=Entry.bFaceOnly || !Entry.Animation;
            if(FaceOnly==(Category==0)) Add(Entry.Id,Copy(FName(*(TEXT("UI.Emote.Entry.")+Entry.Id.ToString()+TEXT(".Label"))),Entry.Label),EMCPlayerAlarm::None);
        }
    }
    KeyboardChoice=FMath::Clamp(KeyboardChoice,0,FMath::Max(0,Buttons.Num()-1));RefreshAvailability();
}
void UMCEmoteWidget::RefreshAvailability()
{
    auto* Hero=IsPreviewing()?nullptr:GetOwningPlayerPawn<AMCToothCharacter>();
    for(int32 Index=0;Index<Buttons.Num();++Index) {
        auto* Button=Buttons[Index].Get();bool Enabled=IsPreviewing()?bPreviewActionsAvailable:Hero && Hero->Status && Hero->Status->IsAlive();
        if(!IsPreviewing() && Button->Alarm==EMCPlayerAlarm::None) {
            const auto* Library=Hero && Hero->Expression?Hero->Expression->Library.Get():nullptr;
            const auto* Entry=Library?Library->Entries.FindByPredicate([&](const FMCEmoteEntry& Value){return Value.Id==Button->Id;}):nullptr;
            Enabled=Entry && Hero->Expression->CanPlay(*Entry) && Hero->Expression->EmoteAlpha()<.01f;
        }
        if(IsPreviewing() && (PreviewScenario==EMCGameplayUIPreviewScenario::Disabled || PreviewState==TEXT("Disabled"))) Enabled=false;
        Button->SetIsEnabled(Enabled);
    }
    if(Hint) Hint->SetText(Category==2?Copy(TEXT("UI.Emote.SignalHint"),LOCTEXT("SignalHint","Сигнал видят все игроки над твоим персонажем.")):
        Category==1?Copy(TEXT("UI.Emote.GestureHint"),LOCTEXT("GestureHint","Жесты — стоя на земле. Движение, работа и боль прерывают жест.")):
        Copy(TEXT("UI.Emote.FaceHint"),LOCTEXT("FaceHint","Выбери выражение лица.")));
    ApplyTheme();
}
void UMCEmoteWidget::ApplyTheme()
{
    const auto* Style=GetResolvedTheme();if(!Style) return;const auto& P=Style->Palette;
    if(Wheel) {Wheel->SelectedTint=P.Marine;Wheel->NormalTint=P.Surface;Wheel->BorderTint=P.Border;Wheel->SelectedSector=Category;Wheel->InvalidateLayoutAndVolatility();}
    if(WidgetTree) if(auto* Backdrop=Cast<UBorder>(WidgetTree->FindWidget(TEXT("EmoteBackdrop")))) {Backdrop->SetBrush(FSlateRoundedBoxBrush(P.Backdrop,0));Backdrop->SetBrushColor(FLinearColor::White);}
    BindStyle(TEXT("EmoteListPanel"),EMCUIColorToken::Marine,EMCUIStyleTarget::Background);BindStyle(TEXT("EmoteHeader"),EMCUIColorToken::Cream,EMCUIStyleTarget::Background);
    auto Text=[&](UTextBlock* Label,int32 Size,FLinearColor Color) {if(Label) {Label->SetFont(Style->MakeGameplayFont(Size));Label->SetColorAndOpacity(Color);}};
    Text(CategoryTitle,25,WidgetTree && WidgetTree->FindWidget(TEXT("EmoteHeader"))?P.Ink:P.OnMarine);Text(Hint,15,P.OnMarine);Text(Footer,15,P.OnMarine);Text(ColorHeading,16,P.OnMarine);
    const FString Culture=IsPreviewing()?PreviewCulture:FString();
    for(int32 Index=0;Index<SectorLabels.Num();++Index) if(SectorLabels[Index]) {
        SectorLabels[Index]->SetText(UMCUIDataLibrary::FormatTextForCulture(Copy(TEXT("UI.Emote.CategoryShortcut"),LOCTEXT("CategoryShortcut","{Shortcut}  {Category}")),
            FFormatNamedArguments{{TEXT("Shortcut"),Index+1},{TEXT("Category"),CategoryCopy(Index)}},Culture));
        Text(SectorLabels[Index],18,Index==Category?P.OnMarine:P.Ink);
        SectorLabels[Index]->SetWrapTextAt(190);SectorLabels[Index]->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
    }
    if(ColorHeading) ColorHeading->SetText(Copy(TEXT("UI.Emote.PlayerColor"),LOCTEXT("PlayerColor","Цвет игрока")));
    if(Footer) Footer->SetText(Copy(TEXT("UI.Emote.Controls"),LOCTEXT("Controls","1 / 2 / 3 — раздел    ↑ / ↓ — выбор    Enter — отправить    T / Esc — закрыть")));
    const FText ColorNames[]={Copy(TEXT("UI.Emote.Color.Blue"),LOCTEXT("Blue","Синий")),Copy(TEXT("UI.Emote.Color.Coral"),LOCTEXT("Coral","Коралл")),
        Copy(TEXT("UI.Emote.Color.Mint"),LOCTEXT("Mint","Мята")),Copy(TEXT("UI.Emote.Color.Lilac"),LOCTEXT("Lilac","Сирень"))};
    for(int32 Index=0;Index<Buttons.Num();++Index) if(auto* Button=Buttons[Index].Get()) {
        const bool Selected=Index==KeyboardChoice;MCUIVisualStyle::Button(Button,Style,Selected?P.Mint:P.Button);
        auto ButtonStyle=Button->GetStyle();ButtonStyle.SetNormalPadding(FMargin(12,10));ButtonStyle.SetPressedPadding(FMargin(12,10));
        if(Selected) ButtonStyle.SetNormal(MCUIVisualStyle::Surface(Button,ButtonStyle.Normal,Style,P.Mint,MCUIVisualStyle::SizeFor(Button),Style->CornerRadius,false,0,false,true));
        Button->SetStyle(ButtonStyle);
        if(auto* Label=Cast<UTextBlock>(Button->GetContent())) {
            Text(Label,20,P.ButtonText);
            const float ArrangedItemsWidth=Items?Items->GetPaintSpaceGeometry().GetLocalSize().X:0.f;
            const float WrapWidth=ArrangedItemsWidth>1.f?FMath::Min(292.f,FMath::Max(64.f,ArrangedItemsWidth-24.f)):292.f;
            Label->SetAutoWrapText(false);Label->SetWrapTextAt(WrapWidth);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        }
        if(auto* ContentSlot=Cast<UButtonSlot>(Button->GetContentSlot())) {ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);ContentSlot->SetPadding(FMargin(0));}
    }
    for(int32 Index=0;Index<ColorButtons.Num();++Index) {
        if(ColorButtons[Index]) {
            MCUIVisualStyle::Button(ColorButtons[Index],Style,FMath::Lerp(P.Surface,MCEmotePrivate::PlayerColors[Index],.32f));
            auto ButtonStyle=ColorButtons[Index]->GetStyle();ButtonStyle.SetNormalPadding(FMargin(7,4));ButtonStyle.SetPressedPadding(FMargin(7,4));ColorButtons[Index]->SetStyle(ButtonStyle);
            if(auto* ContentSlot=Cast<UButtonSlot>(ColorButtons[Index]->GetContentSlot())) {ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Center);ContentSlot->SetPadding(FMargin(0));}
        }
        if(ColorLabels.IsValidIndex(Index) && ColorLabels[Index]) {ColorLabels[Index]->SetText(ColorNames[Index]);Text(ColorLabels[Index],14,P.Ink);ColorLabels[Index]->SetWrapTextAt(76);ColorLabels[Index]->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
    }
    if(WidgetTree) Text(Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("EmoteCenterKey"))),24,P.OnMarine);
    Super::ApplyTheme();
    // Update the named slots too: the authored Phase1 tree has the original
    // tightly packed color/footer positions and is intentionally preserved.
    if(ColorHeading) if(auto* PresentationSlot=Cast<UCanvasPanelSlot>(ColorHeading->Slot)) {PresentationSlot->SetPosition(FVector2D(-165,222));PresentationSlot->SetSize(FVector2D(390,28));}
    if(Footer) {
        Footer->SetWrapTextAt(900);Footer->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        if(auto* PresentationSlot=Cast<UCanvasPanelSlot>(Footer->Slot)) {PresentationSlot->SetPosition(FVector2D(-40,318));PresentationSlot->SetSize(FVector2D(900,48));}
    }
    if(WidgetTree) {
        if(auto* Colors=WidgetTree->FindWidget(TEXT("EmoteColors"))) if(auto* PresentationSlot=Cast<UCanvasPanelSlot>(Colors->Slot)) {PresentationSlot->SetPosition(FVector2D(-165,244));PresentationSlot->SetSize(FVector2D(390,44));}
        if(CategoryTitle) {CategoryTitle->SetAutoWrapText(false);CategoryTitle->SetWrapTextAt(296);CategoryTitle->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
        if(Hint) {Hint->SetAutoWrapText(false);Hint->SetWrapTextAt(320);Hint->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);}
        auto* Column=ListPanel?Cast<UVerticalBox>(ListPanel->GetContent()):nullptr;
        auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("EmoteListScroll")));
        if(Column && Scroll && Items) {
            Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(5,5));
            const float GeometryScale=GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale();
            const float LayoutScale=FMath::IsFinite(GeometryScale) && GeometryScale>0.f?GeometryScale:1.f;
            const auto ColumnSlate=Column->TakeWidget();ColumnSlate->MarkPrepassAsDirty();ColumnSlate->SlatePrepass(LayoutScale);
            const auto ItemsSlate=Items->TakeWidget();ItemsSlate->MarkPrepassAsDirty();ItemsSlate->SlatePrepass(LayoutScale);
            float Height=ListPanel->GetPadding().GetDesiredSize().Y+8.f;
            for(UWidget* Child:Column->GetAllChildren()) {
                const auto* ChildSlot=Cast<UVerticalBoxSlot>(Child->Slot);
                Height+=(Child==Scroll?Items->GetDesiredSize().Y:Child->GetDesiredSize().Y)+(ChildSlot?ChildSlot->GetPadding().GetDesiredSize().Y:0.f);
            }
            if(auto* PresentationSlot=Cast<UCanvasPanelSlot>(ListPanel->Slot)) PresentationSlot->SetSize(FVector2D(360,FMath::Clamp(FMath::CeilToFloat(Height),280.f,470.f)));
            Scroll->SetScrollBarVisibility(Height>470.f?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        }
    }
    InvalidateLayoutAndVolatility();
}
void UMCEmoteWidget::ApplyEditorPreview(float Time)
{
    BuildPresentationTree();bEditorFixture=true;Category=FMath::Clamp(PreviewCategory,0,2);
    if(PreviewState==TEXT("Signals")) Category=2;else if(PreviewState==TEXT("Gestures")) Category=1;
    LoadedPreviewLibrary=PreviewLibrary.LoadSynchronous();Refresh();
    SetRenderOpacity(Time<=0?1.f:FMath::Clamp(Time*6.f,0.f,1.f));
}
void UMCEmoteWidget::ResetEditorPreview() {bEditorFixture=false;SetRenderOpacity(1);}
void UMCEmoteWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);if(!IsVisible() || IsPreviewing()) return;
    RefreshElapsed+=Dt;if(RefreshElapsed>=.1f) {RefreshElapsed=0;RefreshAvailability();}
}
FReply UMCEmoteWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (IsPreviewing()) return Super::NativeOnKeyDown(Geometry,Event);
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Tab)
    {
        if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()))
        {
            PC->ToggleEmotes();
            PC->InputKey(FInputKeyEventArgs(nullptr,Event.GetInputDeviceId(),Key,IE_Pressed,Event.GetEventTimestamp()));
            PC->ShowScoreboard();
        }
        return FReply::Handled();
    }
    if(Key==EKeys::T || Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right || Key==EKeys::Gamepad_Special_Right)
    {
        if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleEmotes();return FReply::Handled();
    }
    if(Key==EKeys::One || Key==EKeys::Two || Key==EKeys::Three)
    {SelectCategory(Key==EKeys::One?0:Key==EKeys::Two?1:2);return FReply::Handled();}
    if(Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right)
    {SelectCategory((Category+(Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left?2:1))%3);return FReply::Handled();}
    if(Key==EKeys::Up || Key==EKeys::Down || Key==EKeys::Gamepad_DPad_Up || Key==EKeys::Gamepad_DPad_Down)
    {
        if(!Buttons.IsEmpty()) KeyboardChoice=(KeyboardChoice+Buttons.Num()+(Key==EKeys::Up || Key==EKeys::Gamepad_DPad_Up?-1:1))%Buttons.Num();
        RefreshAvailability();if(Buttons.IsValidIndex(KeyboardChoice)) if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("EmoteListScroll")))) Scroll->ScrollWidgetIntoView(Buttons[KeyboardChoice],true,EDescendantScrollDestination::IntoView,8);return FReply::Handled();
    }
    if(Key==EKeys::Enter || Key==EKeys::SpaceBar || Key==EKeys::Gamepad_FaceButton_Bottom)
    {
        if(Buttons.IsValidIndex(KeyboardChoice) && Buttons[KeyboardChoice]->GetIsEnabled()) Buttons[KeyboardChoice]->OnClicked.Broadcast();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}
FReply UMCEmoteWidget::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        const FVector2D Delta=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())-MCEmotePrivate::WheelCenter(Geometry);
        const float Scale=MCEmotePrivate::LayoutScale(Geometry);
        if(Delta.Size()>=65*Scale && Delta.Size()<=210*Scale)
        {
            float Angle=FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X))+150;
            if(Angle<0) Angle+=360;if(Angle>=360) Angle-=360;
            SelectCategory(FMath::Min(2,FMath::FloorToInt(Angle/120)));return FReply::Handled();
        }
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}

int32 UMCEmoteWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    return Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
}
#undef LOCTEXT_NAMESPACE
