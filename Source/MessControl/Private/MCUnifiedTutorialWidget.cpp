#include "MCUnifiedTutorialWidget.h"

#include "MCGameplayUIFactory.h"
#include "MCUniversalUIWidget.h"
#include "MCUIDataLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Rendering/SlateRenderer.h"
#include "InputCoreTypes.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "MCUnifiedTutorial"

namespace MCUnifiedTutorialPrivate
{
    FLinearColor Color(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
    struct FLabel {const TCHAR* Name;int32 Size;bool bInk=false;};
    const FLabel Labels[]={
        {TEXT("TutorialStageTitle"),26,true},{TEXT("TutorialDialogue"),20},{TEXT("TutorialAction"),22},
        {TEXT("TutorialOwnProgress"),18},{TEXT("TutorialTeamProgress"),18},{TEXT("TutorialTimer"),20},
        {TEXT("TutorialReadyLabel"),22},{TEXT("TutorialMenuLabel"),22},
        {TEXT("TutorialPortraitStar"),28},{TEXT("TutorialPortraitName"),14}};
    struct FLesson {EMCTutorialStage Stage;const TCHAR* Keys[3];const TCHAR* Fallbacks[3];};
    // Designer copy references the actual EnterStage keys/defaults. No actor or progression code is called.
    const FLesson Lessons[]={
        {EMCTutorialStage::Loading,{TEXT("LoadingTitle"),TEXT("LoadingFairy"),TEXT("LoadingHint")},{TEXT("ДЕНЬ 0 · СБОР КОМАНДЫ"),TEXT("Подождём, пока все появятся во рту."),TEXT("Ожидаем загрузку участников. Можно осмотреться.")}},
        {EMCTutorialStage::Intro,{TEXT("IntroTitle"),TEXT("IntroFairy"),TEXT("IntroHint")},{TEXT("ДЕНЬ 0 · ПЕРВАЯ СМЕНА"),TEXT("Йоу! Я Зубная фея. Обычно забираю зубы, но сегодня попробуем их сохранить. Впереди семь дней. Сначала покажу, как здесь работать."),TEXT("WASD — движение, Space — прыжок. Инструменты уже с тобой: 1 — щётка, 2 — кирка, 3 — нож, 4 — спрей.")}},
        {EMCTutorialStage::ToothpickPull,{TEXT("ToothpickPullTitle"),TEXT("ToothpickPullFairy"),TEXT("ToothpickPullHint")},{TEXT("05 · ЗУБОЧИСТКА В ЯЗЫКЕ"),TEXT("Зубочистка ранила язык — от язвы идёт волна боли. Сначала вытащите её вместе."),TEXT("Подойди к центру языка и удерживай E у зубочистки, пока она не выйдет. Space — перепрыгнуть волну.")}},
        {EMCTutorialStage::ToothpickBreak,{TEXT("ToothpickBreakTitle"),TEXT("ToothpickBreakFairy"),TEXT("ToothpickBreakHint")},{TEXT("06 · СЛОМАТЬ ЗУБОЧИСТКУ"),TEXT("Зубочистка извлечена. Теперь разбейте её киркой — после этого можно лечить рану."),TEXT("2 — кирка. ЛКМ или F — удар по извлечённой зубочистке.")}},
        {EMCTutorialStage::ToothpickHeal,{TEXT("ToothpickHealTitle"),TEXT("ToothpickHealFairy"),TEXT("ToothpickHealHint")},{TEXT("07 · ВЫЛЕЧИТЬ РАНУ"),TEXT("Осталась язва. Залечите её спреем; на время лечения волны боли остановятся."),TEXT("4 — спрей. Удерживай ЛКМ у язвы до 100%. При отпускании прогресс сохраняется.")}},
        {EMCTutorialStage::BrushTooth,{TEXT("BrushTitle"),TEXT("BrushFairy"),TEXT("BrushHint")},{TEXT("01 · ОСВОЕНИЕ ЩЁТКИ"),TEXT("Щётка всегда в первом слоте. Выбери её и очисти свой подсвеченный зуб. Каждый пробует сам — я подожду!"),TEXT("1 — щётка. Держи ЛКМ у своего подсвеченного зуба до полной очистки. Без таймера.")}},
        {EMCTutorialStage::FoodCut,{TEXT("CutTitle"),TEXT("CutFairy"),TEXT("CutHint")},{TEXT("02 · РАЗБИТЬ ЕДУ"),TEXT("Разбей подсвеченную еду ножом. Из неё выпадут кусочки: их можно собирать в стопку через E и по одному бросать в глотку через Q."),TEXT("3 — нож. ЛКМ или F — удары по подсвеченной еде до полного разрушения.")}},
        {EMCTutorialStage::FreshSort,{TEXT("FreshTitle"),TEXT("FreshFairy"),TEXT("FreshHint")},{TEXT("03 · СВЕЖЕЕ — В ГЛОТКУ"),TEXT("Собери свой кусочек через E. Наведи камеру на открытую глотку наверху: прицел раскроется. Попади едой в лунку!"),TEXT("E у еды — собирать в стопку. Q — бросить верхний кусочек; удерживай Q для серии. Засчитывается попадание в глотку.")}},
        {EMCTutorialStage::SpoiledSort,{TEXT("SpoiledTitle"),TEXT("SpoiledFairy"),TEXT("SpoiledHint")},{TEXT("04 · ИСПОРЧЕННОЕ — ЗА БОРТ"),TEXT("Этот кусочек испортился. Возьми его через E и выброси за край рта — он не должен попасть в глотку."),TEXT("E у еды — собрать. Наведи камеру за край рта и нажми Q. Задача выполнится, когда испорченная еда покинет арену.")}},
        {EMCTutorialStage::BreakfastRain,{TEXT("BreakfastTitle"),TEXT("BreakfastFairy"),TEXT("BreakfastHint")},{TEXT("05 · УЧЕБНЫЙ ЗАВТРАК"),TEXT("Теперь общий заказ: брокколи, яйцо и бекон. Работайте вместе!"),TEXT("Разбивайте еду, собирайте кусочки через E и бросайте через Q в открытую глотку.")}},
        {EMCTutorialStage::BreakfastCleanup,{TEXT("BreakfastCleanupTitle"),TEXT("BreakfastCleanupFairy"),TEXT("BreakfastCleanupHint")},{TEXT("ЗАВТРАК · УБОРКА КОМАНДОЙ"),TEXT("Двадцать секунд на уборку. Если не успеете — помогу закончить. Здоровьем здесь не рискуем."),TEXT("3 — нож; E у кусочков — сбор. Наведи камеру на глотку и бросай через Q; удержание Q — серия. Это общая задача.")}},
        {EMCTutorialStage::CoffeeWarning,{TEXT("CoffeeWarningTitle"),TEXT("CoffeeWarningFairy"),TEXT("CoffeeWarningHint")},{TEXT("06 · ОСТОРОЖНО, КОФЕ!"),TEXT("Сейчас придёт цунами снаружи рта! Уходи в сторону, прячься за зубом или удерживайся за него."),TEXT("WASD + Shift — бежать; держи ПКМ рядом со стеной, зубом или едой — удерживаться. Ошибаться безопасно.")}},
        {EMCTutorialStage::CoffeeWaves,{TEXT("CoffeeTitle"),TEXT("CoffeeFairy"),TEXT("CoffeeHint")},{TEXT("КОФЕ · ЦУНАМИ"),TEXT("Держись или убегай из потока! Когда вода уйдёт, переключимся на щётку."),TEXT("WASD + Shift — бежать. ПКМ у стены, зуба или еды — удержаться. Переживи потоп без штрафов.")}},
        {EMCTutorialStage::CoffeeCleanup,{TEXT("TongueTitle"),TEXT("TongueFairy"),TEXT("TongueHint")},{TEXT("07 · ЧИСТКА ЯЗЫКА"),TEXT("Щётка работает и на языке. Очисти своё маленькое подсвеченное пятно."),TEXT("1 — щётка. Держи ЛКМ у своего пятна на языке до полной очистки. C — почистить себя.")}},
        {EMCTutorialStage::Calculus,{TEXT("CalculusTitle"),TEXT("CalculusFairy"),TEXT("CalculusHint")},{TEXT("08 · ЗУБНОЙ КАМЕНЬ"),TEXT("Это камень — щёткой такой не взять. Разбей свой нарост киркой."),TEXT("2 — кирка. Подойди к своему подсвеченному зубу; ЛКМ или F — удар. Удали нарост полностью.")}},
        {EMCTutorialStage::FoamParty,{TEXT("PartyTitle"),TEXT("PartyFairy"),TEXT("PartyHint")},{TEXT("09 · ПЕННАЯ ВЕЧЕРИНКА"),TEXT("Стажировка пройдена! Чистить умеете, еду доставлять умеете, мусор отличаете. Немного пены за ваш счёт!"),TEXT("Пятнадцать секунд праздника. Можно свободно побегать вместе.")}},
        {EMCTutorialStage::Complete,{TEXT("CompleteTitle"),TEXT("CompleteFairy"),TEXT("CompleteHint")},{TEXT("ОБУЧЕНИЕ ПРОЙДЕНО"),TEXT("Он просыпается — скоро настоящий завтрак. Когда вся команда готова, начинаем день 1."),TEXT("Нажми «Готов к дню 1». Переход начнётся после готовности всей команды.")}},
    };
    struct FShortCopy {EMCTutorialStage Stage;int32 Field;const TCHAR* Key;const TCHAR* Fallback;};
    const FShortCopy ShortCopy[]={
        {EMCTutorialStage::Intro,0,TEXT("ShortIntroTitle"),TEXT("ОБУЧЕНИЕ · ПЕРВАЯ СМЕНА")},
        {EMCTutorialStage::Intro,1,TEXT("ShortIntroFairy"),TEXT("Я Зубная фея. Впереди один насыщенный день! Сначала потренируемся с едой, щёткой, киркой и спреем.")},
        {EMCTutorialStage::CoffeeCleanup,0,TEXT("ShortTongueTitle"),TEXT("03 · ЧИСТКА ЯЗЫКА")},
        {EMCTutorialStage::CoffeeCleanup,1,TEXT("ShortTongueFairy"),TEXT("Еда разбита. Теперь очисти подсвеченное пятно щёткой — и перейдём к общей задаче.")},
        {EMCTutorialStage::ToothpickPull,0,TEXT("ShortToothpickPullTitle"),TEXT("04 · ЗУБОЧИСТКА В ЯЗЫКЕ")},
        {EMCTutorialStage::ToothpickBreak,0,TEXT("ShortToothpickBreakTitle"),TEXT("05 · СЛОМАТЬ ЗУБОЧИСТКУ")},
        {EMCTutorialStage::ToothpickHeal,0,TEXT("ShortToothpickHealTitle"),TEXT("06 · ВЫЛЕЧИТЬ РАНУ")},
        {EMCTutorialStage::Complete,1,TEXT("ShortCompleteFairy"),TEXT("Обучение пройдено! Переходим на основную карту. Награды ждут в сундуках за выполненные этапы.")},
        {EMCTutorialStage::Complete,2,TEXT("ShortCompleteHint"),TEXT("После готовности команды загрузится основная карта.")},
    };
    const FLesson* FindLesson(EMCTutorialStage Stage) {for(const auto& Row:Lessons) if(Row.Stage==Stage) return &Row;return nullptr;}
    bool EnsureScrollableContent(UWidgetTree* Tree)
    {
        if(!Tree) return false;
        auto* Column=Tree->FindWidget<UVerticalBox>(TEXT("TutorialV2Content"));
        auto* Header=Tree->FindWidget<UMCUniversalUIWidget>(TEXT("TutorialV2Header"));
        auto* Instruction=Tree->FindWidget<UMCUniversalUIWidget>(TEXT("TutorialV2ActionShape"));
        auto* Scroll=Tree->FindWidget<UScrollBox>(TEXT("TutorialBodyScroll"));
        auto* Body=Tree->FindWidget<UVerticalBox>(TEXT("TutorialV2Body"));
        auto* Actions=Tree->FindWidget<UWrapBox>(TEXT("TutorialV2Actions"));
        if(!Column || !Header || !Instruction || !Scroll || !Body || !Actions) return false;
        auto* Content=Tree->FindWidget<UVerticalBox>(TEXT("TutorialV2ScrollableContent"));
        auto* Viewport=Tree->FindWidget<USizeBox>(TEXT("TutorialV2ContentViewport"));
        const bool Fixed=Viewport && Scroll->GetParent()==Viewport && Viewport->GetParent()==Column && Header->GetParent()==Column && Instruction->GetParent()==Column && Actions->GetParent()==Column && Column->GetChildrenCount()==4;
        if(Fixed) return true;
        const bool FormerWholeScroll=Content && Viewport && Content->GetParent()==Scroll && Scroll->GetParent()==Viewport && Viewport->GetParent()==Column && Column->GetChildrenCount()==2 && Content->GetChildrenCount()==3 && Header->GetParent()==Content && Instruction->GetParent()==Content && Body->GetParent()==Content && Actions->GetParent()==Column;
        const bool Original=!Content && !Viewport && !Tree->FindWidget(TEXT("TutorialV2ScrollableContent")) && !Tree->FindWidget(TEXT("TutorialV2ContentViewport")) && Header->GetParent()==Column && Instruction->GetParent()==Column && Scroll->GetParent()==Column && Actions->GetParent()==Column && Body->GetParent()==Scroll && Column->GetChildrenCount()==4;
        // Upgrade only the two known native arrangements, retaining authored forms, text and actions.
        if(!FormerWholeScroll && !Original) return false;
        const auto* HeaderCell=Cast<UVerticalBoxSlot>(Header->Slot);const auto* InstructionCell=Cast<UVerticalBoxSlot>(Instruction->Slot);const auto* ActionsCell=Cast<UVerticalBoxSlot>(Actions->Slot);
        const FMargin HeaderInset=HeaderCell?HeaderCell->GetPadding():FMargin(0,0,0,10),InstructionInset=InstructionCell?InstructionCell->GetPadding():FMargin(0,0,0,10),ActionsInset=ActionsCell?ActionsCell->GetPadding():FMargin(0,12,0,0);
        Header->RemoveFromParent();Instruction->RemoveFromParent();Actions->RemoveFromParent();
        if(FormerWholeScroll) Viewport->RemoveFromParent();
        else {Scroll->RemoveFromParent();Viewport=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TutorialV2ContentViewport"));Viewport->SetContent(Scroll);}
        Column->AddChildToVerticalBox(Header)->SetPadding(HeaderInset);Column->AddChildToVerticalBox(Instruction)->SetPadding(InstructionInset);
        Column->AddChildToVerticalBox(Viewport);Column->AddChildToVerticalBox(Actions)->SetPadding(ActionsInset);
        Scroll->SetAllowOverscroll(false);Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);return true;
    }
}
UMCUnifiedTutorialWidget::UMCUnifiedTutorialWidget(const FObjectInitializer& Initializer):Super(Initializer)
{
    bUseNativeLayout=false;bOverridePreviewUIVersion=true;PreviewUIVersion=EMCUIVersion::New;SetIsFocusable(true);
}
UMCUniversalUIWidget* UMCUnifiedTutorialWidget::MakeShape(FName Name,FLinearColor Fill,FMargin InPadding)
{
    auto* Shape=WidgetTree->ConstructWidget<UMCUniversalUIWidget>(UMCUniversalUIWidget::StaticClass(),Name);
    Shape->bUseNativeLayout=false;Shape->bUseThemeShape=false;Shape->bCustomColors=true;Shape->CustomFill=Fill;
    Shape->CustomOutline=MCUnifiedTutorialPrivate::Color(TEXT("FFF9F8"));Shape->CustomShadow=MCUnifiedTutorialPrivate::Color(TEXT("1E266A"));
    Shape->ContentPadding=InPadding;Shape->CornerRadius=20;Shape->OutlineWidth=3;Shape->ShadowOffset=FVector2D(8,9);
    Shape->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Shape->Initialize();Shape->BuildPresentationTree();Shape->ApplyTheme();return Shape;
}
UTextBlock* UMCUnifiedTutorialWidget::MakeLabel(FName Name,int32 Size,FLinearColor InLabelColor)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);Label->SetFont(GetResolvedTheme()->MakeFont(Size,true));
    Label->SetColorAndOpacity(InLabelColor);Label->SetAutoWrapText(true);Label->SetWrapTextAt(880);Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
    Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);Label->SetVisibility(ESlateVisibility::HitTestInvisible);return Label;
}
void UMCUnifiedTutorialWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;if(!Theme) Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    if(WidgetTree->RootWidget) {MCUnifiedTutorialPrivate::EnsureScrollableContent(WidgetTree);Super::BuildPresentationTree();BindViewActions();return;}
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    const auto White=MCUnifiedTutorialPrivate::Color(TEXT("FFF9F8")),Ink=MCUnifiedTutorialPrivate::Color(TEXT("1E266A"));
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TutorialRoot"));WidgetTree->RootWidget=Root;Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Safe=WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),TEXT("TutorialV2SafeZone"));auto* SafeSlot=Root->AddChildToCanvas(Safe);SafeSlot->SetAnchors(FAnchors(0,0,1,1));SafeSlot->SetOffsets(FMargin(0));
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("TutorialV2Canvas"));Safe->SetContent(Canvas);Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TutorialCardSize"));Bounds->SetWidthOverride(MaximumCardWidth);Bounds->SetMaxDesiredHeight(MaximumCardHeight);Bounds->SetMinDesiredHeight(220);
    auto* FrameCanvasCell=Canvas->AddChildToCanvas(Bounds);FrameCanvasCell->SetAnchors(FAnchors(.5,1));FrameCanvasCell->SetAlignment(FVector2D(.5,1));FrameCanvasCell->SetPosition(FVector2D(0,-BottomInset));FrameCanvasCell->SetAutoSize(true);
    auto* CompatCard=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TutorialCard"));CompatCard->SetBrush(FSlateNoResource());CompatCard->SetPadding(FMargin(0));Bounds->SetContent(CompatCard);
    auto* Frame=MakeShape(TEXT("TutorialV2Frame"),MCUnifiedTutorialPrivate::Color(TEXT("4C76AE")),FMargin(20,16));CompatCard->SetContent(Frame);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TutorialV2Content"));Frame->SetContentForSlot(TEXT("Content"),Column);
    auto* Header=MakeShape(TEXT("TutorialV2Header"),White,FMargin(13,8));Header->CustomOutline=Ink;Header->ShadowOffset=FVector2D(3,4);Header->ApplyTheme();
    auto* Title=MakeLabel(TEXT("TutorialStageTitle"),26,Ink);Header->SetContentForSlot(TEXT("Content"),Title);Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,10));
    // Header and current instruction stay visible; only the story/status body may scroll.
    auto* Instruction=MakeShape(TEXT("TutorialV2ActionShape"),MCUnifiedTutorialPrivate::Color(TEXT("355C70")),FMargin(14,10));Instruction->ShadowOffset=FVector2D(3,4);Instruction->ApplyTheme();
    Instruction->SetContentForSlot(TEXT("Content"),MakeLabel(TEXT("TutorialAction"),22,White));Column->AddChildToVerticalBox(Instruction)->SetPadding(FMargin(0,0,0,10));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("TutorialBodyScroll"));Scroll->SetAllowOverscroll(false);Scroll->SetScrollbarThickness(FVector2D(5,5));
    Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll);Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Body=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TutorialV2Body"));Scroll->AddChild(Body);
    auto* Story=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("TutorialV2Story"));Body->AddChildToVerticalBox(Story)->SetPadding(FMargin(0,0,0,10));
    auto* PortraitBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TutorialV2PortraitSize"));PortraitBounds->SetWidthOverride(100);PortraitBounds->SetHeightOverride(100);
    Story->AddChildToHorizontalBox(PortraitBounds)->SetPadding(FMargin(0,0,16,0));
    auto* CompatPortrait=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TutorialPortraitBorder"));CompatPortrait->SetBrush(FSlateNoResource());CompatPortrait->SetPadding(FMargin(0));PortraitBounds->SetContent(CompatPortrait);
    auto* PortraitFrame=MakeShape(TEXT("TutorialV2PortraitFrame"),MCUnifiedTutorialPrivate::Color(TEXT("355C70")),FMargin(5));PortraitFrame->ShadowOffset=FVector2D(3,4);PortraitFrame->ApplyTheme();CompatPortrait->SetContent(PortraitFrame);
    auto* PortraitLayers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("TutorialV2PortraitLayers"));PortraitFrame->SetContentForSlot(TEXT("Content"),PortraitLayers);
    auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("TutorialV2PortraitFit"));Fit->SetStretch(EStretch::ScaleToFit);PortraitLayers->AddChildToOverlay(Fit);
    Fit->SetContent(WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("TutorialPortrait")));
    auto* Fallback=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("TutorialPortraitFallback"));auto* FallbackSlot=PortraitLayers->AddChildToOverlay(Fallback);FallbackSlot->SetHorizontalAlignment(HAlign_Center);FallbackSlot->SetVerticalAlignment(VAlign_Center);
    auto* Star=MakeLabel(TEXT("TutorialPortraitStar"),28,White);Star->SetText(FText::AsCultureInvariant(TEXT("✦")));Star->SetJustification(ETextJustify::Center);Fallback->AddChildToVerticalBox(Star);
    auto* Fairy=MakeLabel(TEXT("TutorialPortraitName"),14,White);Fairy->SetJustification(ETextJustify::Center);Fallback->AddChildToVerticalBox(Fairy);
    auto* DialogueView=MakeLabel(TEXT("TutorialDialogue"),20,White);auto* DialogueSlot=Story->AddChildToHorizontalBox(DialogueView);DialogueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));DialogueSlot->SetVerticalAlignment(VAlign_Center);
    auto* Status=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("TutorialV2Status"));Status->SetExplicitWrapSize(true);Status->SetWrapSize(900);Status->SetInnerSlotPadding(FVector2D(18,8));Body->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,0,0,8));
    for(const TCHAR* Name:{TEXT("TutorialOwnProgress"),TEXT("TutorialTeamProgress"),TEXT("TutorialTimer")}) {
        auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(FString(Name)+TEXT("V2Width"))));Width->SetMaxDesiredWidth(400);
        Width->SetContent(MakeLabel(FName(Name),Name==FString(TEXT("TutorialTimer"))?20:18,White));Status->AddChildToWrapBox(Width);
    }
    auto* Track=MakeShape(TEXT("TutorialV2ProgressFrame"),MCUnifiedTutorialPrivate::Color(TEXT("355C70")),FMargin(3));Track->CornerRadius=8;Track->OutlineWidth=2;Track->ShadowOffset=FVector2D::ZeroVector;Track->ApplyTheme();
    auto* ProgressView=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("TutorialProgress"));ProgressView->SetBorderPadding(FVector2D::ZeroVector);Track->SetContentForSlot(TEXT("Content"),ProgressView);
    auto* TrackHeight=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TutorialV2ProgressHeight"));TrackHeight->SetHeightOverride(20);TrackHeight->SetContent(Track);Body->AddChildToVerticalBox(TrackHeight);
    auto* Actions=WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("TutorialV2Actions"));Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(900);Actions->SetHorizontalAlignment(HAlign_Center);Actions->SetInnerSlotPadding(FVector2D(16,10));Column->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,12,0,0));
    for(const TCHAR* Name:{TEXT("TutorialReadyButton"),TEXT("TutorialMenuButton")}) {
        const bool Ready=FString(Name)==TEXT("TutorialReadyButton");const FString Prefix(Name);
        auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Prefix+TEXT("V2Width"))));Width->SetMaxDesiredWidth(430);
        auto* Shape=MakeShape(FName(*(Prefix+TEXT("V2Shape"))),MCUnifiedTutorialPrivate::Color(Ready?TEXT("26A661"):TEXT("AE4D78")),FMargin(0));Shape->CornerRadius=16;Shape->ShadowOffset=FVector2D(4,5);Shape->ApplyTheme();Width->SetContent(Shape);
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));Button->SetContent(MakeLabel(Ready?TEXT("TutorialReadyLabel"):TEXT("TutorialMenuLabel"),22,White));Shape->SetContentForSlot(TEXT("Content"),Button);Actions->AddChildToWrapBox(Width);
    }
    MCUnifiedTutorialPrivate::EnsureScrollableContent(WidgetTree);
    Super::BuildPresentationTree(); // Reuse named control binding; runtime actions forward to the original owner.
    BindViewActions();
    ApplyTheme();UpdateLayout();SyncFrames();
}
void UMCUnifiedTutorialWidget::ApplyTheme()
{
    // Capture saved Designer sizes before the legacy owner applies its old fixed sizes.
    if(WidgetTree) for(const auto& Spec:MCUnifiedTutorialPrivate::Labels) if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Spec.Name))
        AuthoredFontSizes.Add(FName(Spec.Name),FMath::Max(8,FMath::RoundToInt(Label->GetFont().Size)));
    Super::ApplyTheme();if(!WidgetTree || !GetResolvedTheme()) return;
    for(const TCHAR* Name:{TEXT("TutorialCard"),TEXT("TutorialPortraitBorder")}) if(auto* Wrapper=WidgetTree->FindWidget<UBorder>(Name)) {Wrapper->SetBrush(FSlateNoResource());Wrapper->SetPadding(FMargin(0));}
    int32 Index=0;bool FontsChanged=PreparedFonts.Num()!=UE_ARRAY_COUNT(MCUnifiedTutorialPrivate::Labels);
    for(const auto& Spec:MCUnifiedTutorialPrivate::Labels) {
        if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Spec.Name)) {
            const auto Font=GetResolvedTheme()->MakeFont(AuthoredFontSizes.FindRef(FName(Spec.Name)),true);FontsChanged|=!PreparedFonts.IsValidIndex(Index) || !(PreparedFonts[Index]==Font);
            Label->SetFont(Font);Label->SetColorAndOpacity(MCUnifiedTutorialPrivate::Color(Spec.bInk?TEXT("1E266A"):TEXT("FFF9F8")));
        }++Index;
    }
    for(const TCHAR* Name:{TEXT("TutorialReadyButton"),TEXT("TutorialMenuButton")}) if(auto* Button=WidgetTree->FindWidget<UButton>(Name)) {
        FButtonStyle Style;Style.SetNormal(FSlateNoResource());Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.12f),14.f));Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.12f),14.f));Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.24f),14.f));Style.SetNormalPadding(FMargin(16,10));Style.SetPressedPadding(FMargin(16,10));Button->SetStyle(Style);
    }
    if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(TEXT("TutorialProgress"))) {
        FProgressBarStyle Style;Style.SetBackgroundImage(FSlateRoundedBoxBrush(MCUnifiedTutorialPrivate::Color(TEXT("355C70")),6.f));Style.SetFillImage(FSlateRoundedBoxBrush(MCUnifiedTutorialPrivate::Color(TEXT("AE4D78")),6.f));Style.BackgroundImage.ImageSize=FVector2D(120,14);Style.FillImage.ImageSize=FVector2D(120,14);Bar->SetWidgetStyle(Style);Bar->SetFillColorAndOpacity(FLinearColor::White);
    }
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    if(FontsChanged || Culture!=PreparedCulture) bPrepared=false;SyncFrames();UpdateLayout();if(!bPreparing) PreparePresentation();
    if(!IsDesignTime()) {
        if(auto* Source=Backend.Get()) Present(Source->GetUnifiedPresentationSnapshot());
        else if(auto* CompatView=WidgetTree->FindWidget<UBorder>(TEXT("TutorialCard"))) CompatView->SetVisibility(ESlateVisibility::Collapsed);
    }
}
void UMCUnifiedTutorialWidget::UpdateLayout()
{
    if(!WidgetTree) return;const FVector2D View=GetPaintSpaceGeometry().GetLocalSize();if(View.X<=1 || View.Y<=1) return;LastViewport=View;
    const float Width=FMath::Max(1.f,FMath::Min(MaximumCardWidth,float(View.X)-64.f));
    const float Inset=FMath::Min(BottomInset,float(View.Y)*.28f),Height=FMath::Min(MaximumCardHeight,FMath::Max(180.f,float(View.Y)-Inset-40.f));
    if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("TutorialCardSize"))) {
        Bounds->SetWidthOverride(Width);Bounds->SetMaxDesiredHeight(Height);Bounds->SetMinDesiredHeight(FMath::Min(220.f,Height));
        if(auto* LayoutCell=Cast<UCanvasPanelSlot>(Bounds->Slot)) LayoutCell->SetPosition(FVector2D(0,-Inset));
    }
    const float Inner=Width-40;
    auto Wrap=[&](const TCHAR* Name,float At) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) {Label->SetAutoWrapText(false);Label->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Label->SetWrapTextAt(FMath::Max(1.f,At));}};
    Wrap(TEXT("TutorialStageTitle"),Inner-26);Wrap(TEXT("TutorialAction"),Inner-28);Wrap(TEXT("TutorialDialogue"),Inner-132);
    for(const TCHAR* Name:{TEXT("TutorialOwnProgress"),TEXT("TutorialTeamProgress"),TEXT("TutorialTimer")}) {
        const float At=Name==FString(TEXT("TutorialTimer"))?140.f:(Inner-18)*.5f;
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(FName(*(FString(Name)+TEXT("V2Width"))))) Bounds->SetMaxDesiredWidth(At);Wrap(Name,At);
    }
    for(const TCHAR* Name:{TEXT("TutorialV2Status"),TEXT("TutorialV2Actions")}) if(auto* Row=WidgetTree->FindWidget<UWrapBox>(Name)) Row->SetWrapSize(Inner);
    for(const TCHAR* Name:{TEXT("TutorialReadyButton"),TEXT("TutorialMenuButton")}) {
        const bool Ready=FString(Name)==TEXT("TutorialReadyButton");const float At=FMath::Max(180.f,(Inner-16)*.5f);
        if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(FName(*(FString(Name)+TEXT("V2Width"))))) Bounds->SetMaxDesiredWidth(FMath::Min(At,Inner));
        // Auto-wrap feeds the previous narrow allocation back into desired
        // width. Explicit wrapping at the available maximum lets short
        // localized button labels grow their own forms first.
        if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Ready?TEXT("TutorialReadyLabel"):TEXT("TutorialMenuLabel"))) Label->SetAutoWrapText(false);
        Wrap(Ready?TEXT("TutorialReadyLabel"):TEXT("TutorialMenuLabel"),FMath::Min(At,Inner)-32);
    }
    auto* ContentViewport=WidgetTree->FindWidget<USizeBox>(TEXT("TutorialV2ContentViewport"));
    auto* ContentScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("TutorialBodyScroll"));
    auto* Actions=WidgetTree->FindWidget<UWrapBox>(TEXT("TutorialV2Actions"));
    if(ContentViewport && ContentScroll && Actions) {
        const auto* Frame=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("TutorialV2Frame"));
        const FMargin FrameInset=Frame?Frame->ContentPadding:FMargin(20,16);
        const float FontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
        auto Measure=[&](UWidget* Widget) {
            if(!Widget || !Widget->IsVisible()) return 0.f;
            const auto Slate=Widget->TakeWidget();Slate->MarkPrepassAsDirty();Slate->SlatePrepass(FontScale);
            float Value=FMath::CeilToFloat(Widget->GetDesiredSize().Y);
            if(const auto* Cell=Cast<UVerticalBoxSlot>(Widget->Slot)) Value+=Cell->GetPadding().Top+Cell->GetPadding().Bottom;
            return Value;
        };
        const float FixedHeight=Measure(Actions)+Measure(WidgetTree->FindWidget(TEXT("TutorialV2Header")))+Measure(WidgetTree->FindWidget(TEXT("TutorialV2ActionShape")))+FrameInset.Top+FrameInset.Bottom;
        const float RequiredHeight=FixedHeight+64.f;
        const float ContentInset=FMath::Min(Inset,FMath::Max(0.f,float(View.Y)-40.f-RequiredHeight));
        const float AvailableHeight=FMath::Max(1.f,float(View.Y)-ContentInset-40.f);
        const float CardHeight=FMath::Min(AvailableHeight,FMath::Max(Height,RequiredHeight));
        const float ContentHeight=FMath::Max(1.f,CardHeight-FixedHeight);
        ContentViewport->ClearMinDesiredHeight();ContentViewport->ClearMaxDesiredHeight();ContentViewport->SetHeightOverride(ContentHeight);
        if(auto* CardBounds=WidgetTree->FindWidget<USizeBox>(TEXT("TutorialCardSize"))) {
            CardBounds->SetHeightOverride(CardHeight);CardBounds->ClearMinDesiredHeight();CardBounds->ClearMaxDesiredHeight();
            if(auto* CardCell=Cast<UCanvasPanelSlot>(CardBounds->Slot)) CardCell->SetPosition(FVector2D(0,-ContentInset));
        }
        // Wrapping is explicit above, so no header or action can take space from this scroll viewport.
        ContentScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    }
}
void UMCUnifiedTutorialWidget::SyncFrames()
{
    if(!WidgetTree) return;bool Any=false;
    for(const TCHAR* Name:{TEXT("TutorialReadyButton"),TEXT("TutorialMenuButton")}) {
        const auto* Button=WidgetTree->FindWidget<UButton>(Name);const bool Visible=Button && Button->IsVisible();Any|=Visible;
        if(auto* Bounds=WidgetTree->FindWidget(FName(*(FString(Name)+TEXT("V2Width"))))) Bounds->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* Actions=WidgetTree->FindWidget(TEXT("TutorialV2Actions"))) Actions->SetVisibility(Any?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    const auto* ProgressView=WidgetTree->FindWidget<UProgressBar>(TEXT("TutorialProgress"));
    if(auto* Frame=WidgetTree->FindWidget(TEXT("TutorialV2ProgressHeight"))) Frame->SetVisibility(ProgressView && ProgressView->IsVisible()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}
FText UMCUnifiedTutorialWidget::FixtureCopy(FName Key,const FText& Fallback) const {return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,Fallback,PreviewCulture);}
void UMCUnifiedTutorialWidget::PreparePresentation()
{
    if(bPreparing) return;UDataTable* CopyTable=Backend.IsValid()?Backend->GetResolvedTranslationTable():GetResolvedTranslationTable();
    if(bPrepared && PreparedCopyTable.Get()==CopyTable) return;bPrepared=false;TGuardValue<bool> Guard(bPreparing,true);BuildPresentationTree();
    if(!WidgetTree || !GetResolvedTheme() || !FSlateApplication::IsInitialized()) return;const auto Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer) return;
    PreparedFonts.Reset();PreparedCulture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    TArray<FText> Warm;
    for(const auto& Lesson:MCUnifiedTutorialPrivate::Lessons) for(int32 I=0;I<3;++I) Warm.Add(UMCUIDataLibrary::ResolveText(CopyTable,FName(*(FString(TEXT("UI.Tutorial.Copy."))+Lesson.Keys[I])),FText::FromString(Lesson.Fallbacks[I]),PreparedCulture));
    for(const auto& ShortEntry:MCUnifiedTutorialPrivate::ShortCopy) Warm.Add(UMCUIDataLibrary::ResolveText(CopyTable,FName(*(FString(TEXT("UI.Tutorial.Copy."))+ShortEntry.Key)),FText::FromString(ShortEntry.Fallback),PreparedCulture));
    if(auto* Source=Backend.Get()) {
        const auto Snapshot=Source->GetUnifiedPresentationSnapshot();
        for(const FText& Value:{Snapshot.StageTitleText,Snapshot.DialogueText,Snapshot.ActionText,Snapshot.OwnProgressText,
            Snapshot.TeamProgressText,Snapshot.TimerText,Snapshot.ReadyLabelText,Snapshot.MenuLabelText,Snapshot.PortraitNameText}) Warm.Add(Value);
    }
    const FText AllCopy=FText::Join(FText::AsCultureInvariant(TEXT(" ")),Warm);bool Ready=true;
    PreparedFontScale=FMath::Max(.001f,GetPaintSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
    const auto Measure=Renderer->GetFontMeasureService();
    for(const auto& Spec:MCUnifiedTutorialPrivate::Labels) {
        auto* Label=WidgetTree->FindWidget<UTextBlock>(Spec.Name);Ready&=Label && Label->GetFont().HasValidFont();
        PreparedFonts.Add(Label?Label->GetFont():GetResolvedTheme()->MakeFont(Spec.Size,true));
        if(Label) {Measure->Measure(Label->GetText(),Label->GetFont(),PreparedFontScale);Measure->Measure(AllCopy,Label->GetFont(),PreparedFontScale);Measure->Measure(FText::AsCultureInvariant(TEXT("0123456789 / %✓")),Label->GetFont(),PreparedFontScale);}
    }
    Ready&=WidgetTree->FindWidget<UBorder>(TEXT("TutorialCard")) && WidgetTree->FindWidget<UButton>(TEXT("TutorialReadyButton")) && WidgetTree->FindWidget<UButton>(TEXT("TutorialMenuButton")) && WidgetTree->FindWidget<UProgressBar>(TEXT("TutorialProgress"));
    // Bind the inherited, reflected portrait before readiness even while no director is visible.
    if(FairyPortrait) if(auto* PortraitView=WidgetTree->FindWidget<UImage>(TEXT("TutorialPortrait")))
        if(PortraitView->GetBrush().GetResourceObject()!=FairyPortrait.Get()) PortraitView->SetBrushFromTexture(FairyPortrait,true);
    PreparedCopyTable=CopyTable;bPrepared=Ready;
}
void UMCUnifiedTutorialWidget::BindViewActions()
{
    if(!WidgetTree) return;
    if(auto* ReadyView=WidgetTree->FindWidget<UButton>(TEXT("TutorialReadyButton"))) {
        ReadyView->OnClicked.RemoveAll(this);ReadyView->OnClicked.AddUniqueDynamic(this,&UMCUnifiedTutorialWidget::ForwardReady);
    }
    if(auto* MenuView=WidgetTree->FindWidget<UButton>(TEXT("TutorialMenuButton"))) {
        MenuView->OnClicked.RemoveAll(this);MenuView->OnClicked.AddUniqueDynamic(this,&UMCUnifiedTutorialWidget::ForwardMenu);
    }
}
void UMCUnifiedTutorialWidget::SetBackend(UMCTutorialWidget* Source)
{
    UMCTutorialWidget* Candidate=Source && Source!=this && Source->GetWorld()==GetWorld() && Source->GetOwningPlayer()==GetOwningPlayer()?Source:nullptr;
    if(Backend.Get()!=Candidate) bPrepared=false;Backend=Candidate;
    if(!IsDesignTime()) {PreparePresentation();RefreshFromBackend();}
}
void UMCUnifiedTutorialWidget::Present(const FMCTutorialPresentationSnapshot& Snapshot)
{
    const bool NewContent=!LastSnapshot.StageTitleText.EqualTo(Snapshot.StageTitleText) || !LastSnapshot.DialogueText.EqualTo(Snapshot.DialogueText) || !LastSnapshot.ActionText.EqualTo(Snapshot.ActionText);
    LastSnapshot=Snapshot;if(!WidgetTree) return;
    if(auto* CompatView=WidgetTree->FindWidget<UBorder>(TEXT("TutorialCard"))) CompatView->SetVisibility(bPrepared && Snapshot.bVisible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(!bPrepared || !Snapshot.bVisible) return;
    auto Set=[&](const TCHAR* Name,const FText& Value) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) if(!Label->GetText().EqualTo(Value)) Label->SetText(Value);};
    Set(TEXT("TutorialStageTitle"),Snapshot.StageTitleText);Set(TEXT("TutorialDialogue"),Snapshot.DialogueText);Set(TEXT("TutorialAction"),Snapshot.ActionText);
    Set(TEXT("TutorialOwnProgress"),Snapshot.OwnProgressText);Set(TEXT("TutorialTeamProgress"),Snapshot.TeamProgressText);Set(TEXT("TutorialTimer"),Snapshot.TimerText);
    Set(TEXT("TutorialReadyLabel"),Snapshot.ReadyLabelText);Set(TEXT("TutorialMenuLabel"),Snapshot.MenuLabelText);Set(TEXT("TutorialPortraitName"),Snapshot.PortraitNameText);
    if(auto* TimerView=WidgetTree->FindWidget<UTextBlock>(TEXT("TutorialTimer"))) TimerView->SetVisibility(Snapshot.bTimerVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* ProgressView=WidgetTree->FindWidget<UProgressBar>(TEXT("TutorialProgress"))) {ProgressView->SetPercent(Snapshot.ProgressFraction);ProgressView->SetVisibility(Snapshot.bProgressVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
    if(auto* ReadyView=WidgetTree->FindWidget<UButton>(TEXT("TutorialReadyButton"))) {ReadyView->SetIsEnabled(Snapshot.bReadyEnabled);ReadyView->SetVisibility(Snapshot.bReadyVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    if(auto* MenuView=WidgetTree->FindWidget<UButton>(TEXT("TutorialMenuButton"))) {MenuView->SetIsEnabled(Snapshot.bMenuEnabled);MenuView->SetVisibility(Snapshot.bMenuVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    UTexture2D* PortraitTexture=Snapshot.PortraitTexture.Get();
    if(auto* PortraitView=WidgetTree->FindWidget<UImage>(TEXT("TutorialPortrait"))) {
        if(PortraitView->GetBrush().GetResourceObject()!=PortraitTexture) PortraitView->SetBrushFromTexture(PortraitTexture,true);
        PortraitView->SetVisibility(PortraitTexture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* FallbackView=WidgetTree->FindWidget(TEXT("TutorialPortraitFallback"))) FallbackView->SetVisibility(PortraitTexture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    SyncFrames();UpdateLayout();
    if(NewContent) if(auto* ContentScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("TutorialBodyScroll"))) ContentScroll->ScrollToStart();
}
void UMCUnifiedTutorialWidget::RefreshFromBackend()
{
    if(IsDesignTime()) return;auto* Source=Backend.Get();
    if(!Source || Source->GetWorld()!=GetWorld() || Source->GetOwningPlayer()!=GetOwningPlayer()) {Present(FMCTutorialPresentationSnapshot());return;}
    PreparePresentation();Present(Source->GetUnifiedPresentationSnapshot());
}
void UMCUnifiedTutorialWidget::ForwardReady()
{
    if(IsDesignTime()) return;if(auto* Source=Backend.Get()) Source->TryActivateUnifiedPresentationButton(true);
}
void UMCUnifiedTutorialWidget::ForwardMenu()
{
    if(IsDesignTime()) return;if(auto* Source=Backend.Get()) Source->TryActivateUnifiedPresentationButton(false);
}
UWidget* UMCUnifiedTutorialWidget::GetDefaultFocusTarget() const
{
    if(!WidgetTree || !LastSnapshot.bRequiresMenuInput) return nullptr;
    return WidgetTree->FindWidget(LastSnapshot.bReadyVisible && LastSnapshot.bReadyEnabled?TEXT("TutorialReadyButton"):TEXT("TutorialMenuButton"));
}
bool UMCUnifiedTutorialWidget::RouteSourceKey(const FKeyEvent& Event) {return Backend.IsValid() && NativeOnKeyDown(GetPaintSpaceGeometry(),Event).IsEventHandled();}
FReply UMCUnifiedTutorialWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    const bool bIsNavigationKey=Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Tab || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right || Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom || Key==EKeys::Virtual_Gamepad_Accept.GetVirtualKey() || Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right;
    if(Backend.IsValid() && bIsNavigationKey) return NativeOnKeyDown(Geometry,Event);
    return UMCGameplayUIWidget::NativeOnPreviewKeyDown(Geometry,Event);
}
FReply UMCUnifiedTutorialWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(!IsDesignTime()) if(auto* Source=Backend.Get()) {
        const auto Snapshot=Source->GetUnifiedPresentationSnapshot();const FKey Key=Event.GetKey();
        if(!Event.IsRepeat() && Snapshot.bVisible && (Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right)) {Source->RequestUnifiedPresentationBack();return FReply::Handled();}
        if(!Event.IsRepeat() && Snapshot.bRequiresMenuInput && WidgetTree) {
            auto* ReadyView=WidgetTree->FindWidget<UButton>(TEXT("TutorialReadyButton"));auto* MenuView=WidgetTree->FindWidget<UButton>(TEXT("TutorialMenuButton"));
            if(Key==EKeys::Left || Key==EKeys::Right || Key==EKeys::Tab || Key==EKeys::Gamepad_DPad_Left || Key==EKeys::Gamepad_DPad_Right) {
                const bool ReadyFocused=ReadyView && (ReadyView->HasUserFocus(GetOwningPlayer()) || ReadyView->HasFocusedDescendants());
                auto* Target=ReadyFocused || !Snapshot.bReadyEnabled?MenuView:ReadyView;if(Target && GetOwningPlayer()) Target->SetUserFocus(GetOwningPlayer());return FReply::Handled();
            }
            if(Key==EKeys::Enter || Key==EKeys::Gamepad_FaceButton_Bottom || Key==EKeys::Virtual_Gamepad_Accept.GetVirtualKey()) {
                const bool MenuFocused=MenuView && (MenuView->HasUserFocus(GetOwningPlayer()) || MenuView->HasFocusedDescendants());Source->TryActivateUnifiedPresentationButton(!MenuFocused);return FReply::Handled();
            }
        }
    }
    return UMCGameplayUIWidget::NativeOnKeyDown(Geometry,Event);
}
void UMCUnifiedTutorialWidget::NativeConstruct()
{
    UMCGameplayUIWidget::NativeConstruct(); // Do not start a second tutorial presenter polling the director.
    PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();
}
void UMCUnifiedTutorialWidget::NativeDestruct() {Backend.Reset();UMCGameplayUIWidget::NativeDestruct();}
void UMCUnifiedTutorialWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    UMCGameplayUIWidget::NativeTick(Geometry,DeltaSeconds);if(!LastViewport.Equals(Geometry.GetLocalSize(),.5f)) UpdateLayout();
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    if(Culture!=PreparedCulture || !FMath::IsNearlyEqual(PreparedFontScale,Geometry.GetAccumulatedLayoutTransform().GetScale(),.001f)) bPrepared=false;PreparePresentation();
    if(!IsDesignTime()) {ViewRefreshElapsed+=DeltaSeconds;if(ViewRefreshElapsed>=.1f) {ViewRefreshElapsed=0;RefreshFromBackend();}}
}
void UMCUnifiedTutorialWidget::OnLocalizationChanged() {UMCGameplayUIWidget::OnLocalizationChanged();bPrepared=false;PreparePresentation();if(!IsDesignTime()) RefreshFromBackend();}
void UMCUnifiedTutorialWidget::ApplyEditorPreview(float Time)
{
    // The native fixture API is guarded here: a runtime preview call cannot enable bEditorFixture or fake progress.
    if(!IsDesignTime()) {RefreshFromBackend();return;}
    PreviewTime=FMath::IsFinite(Time)?FMath::Max(0.f,Time):0.f;
    const bool Complete=PreviewLesson==EMCTutorialStage::Complete || PreviewState==TEXT("Complete") || PreviewState==TEXT("FullComplete") || PreviewState==TEXT("ShortComplete") || PreviewState==TEXT("Ready") || PreviewScenario==EMCGameplayUIPreviewScenario::Victory;
    const EMCTutorialStage FixtureStage=Complete?EMCTutorialStage::Complete:PreviewLesson;
    const auto* Lesson=MCUnifiedTutorialPrivate::FindLesson(FixtureStage);if(!Lesson) {if(WidgetTree) if(auto* CardView=WidgetTree->FindWidget<UBorder>(TEXT("TutorialCard"))) CardView->SetVisibility(ESlateVisibility::Collapsed);return;}
    const bool Long=PreviewState==TEXT("LongText") || PreviewScenario==EMCGameplayUIPreviewScenario::LongText;
    FText LessonTexts[3];for(int32 I=0;I<3;++I) LessonTexts[I]=FixtureCopy(FName(*(FString(TEXT("UI.Tutorial.Copy."))+Lesson->Keys[I])),FText::FromString(Lesson->Fallbacks[I]));
    if(bPreviewShortFlow || PreviewState==TEXT("ShortComplete")) for(const auto& Override:MCUnifiedTutorialPrivate::ShortCopy) if(Override.Stage==FixtureStage)
        LessonTexts[Override.Field]=FixtureCopy(FName(*(FString(TEXT("UI.Tutorial.Copy."))+Override.Key)),FText::FromString(Override.Fallback));
    auto Extend=[&](FText Value) {return Long?FText::Format(FText::AsCultureInvariant(TEXT("{0} · {0} · {0}")),Value):Value;};
    TGuardValue<FText> TitleGuard(PreviewTitle,PreviewTitle.IsEmpty()?Extend(LessonTexts[0]):PreviewTitle);
    TGuardValue<FText> DialogueGuard(PreviewDialogue,PreviewDialogue.IsEmpty()?Extend(LessonTexts[1]):PreviewDialogue);
    TGuardValue<FText> InstructionGuard(PreviewInstruction,PreviewInstruction.IsEmpty()?Extend(LessonTexts[2]):PreviewInstruction);
    TGuardValue<FName> State(PreviewState,Complete && PreviewState!=TEXT("Empty") && PreviewState!=TEXT("Ready") && PreviewState!=TEXT("ShortComplete")?FName(TEXT("FullComplete")):PreviewState);
    Super::ApplyEditorPreview(Time);
    if(!Complete) {
        auto Set=[&](const TCHAR* Name,const FText& Text) {if(auto* Label=WidgetTree->FindWidget<UTextBlock>(Name)) Label->SetText(Text);};
        auto Format=[&](FName Key,const FText& Fallback,const FFormatNamedArguments& Arguments) {return UMCUIDataLibrary::FormatTextForCulture(FixtureCopy(Key,Fallback),Arguments,PreviewCulture);};
        const bool Personal=FMCTutorialProgressRules::RequiredActions(PreviewLesson)!=0;
        const bool Shared=FMCTutorialProgressRules::IsSharedStage(PreviewLesson);
        if(!Personal) Set(TEXT("TutorialOwnProgress"),FixtureCopy(PreviewLesson==EMCTutorialStage::Loading?TEXT("UI.Tutorial.LoadedWaiting"):Shared?TEXT("UI.Tutorial.SharedTask"):TEXT("UI.Tutorial.WorkTogether"),
            PreviewLesson==EMCTutorialStage::Loading?LOCTEXT("Loaded","Ты на арене. Ждём загрузку команды"):Shared?LOCTEXT("Shared","Одна общая задача для всей команды"):LOCTEXT("Together","Работайте вместе")));
        Set(TEXT("TutorialTeamProgress"),Shared?Format(TEXT("UI.Tutorial.SharedProgress"),LOCTEXT("SharedProgress","Общая задача: {Current}/{Total}"),{{TEXT("Current"),PreviewCurrent},{TEXT("Total"),PreviewRequired}}):
            Format(PreviewLesson==EMCTutorialStage::Loading?TEXT("UI.Tutorial.TeamLoaded"):Personal?TEXT("UI.Tutorial.TeamComplete"):TEXT("UI.Tutorial.ParticipantCount"),
                PreviewLesson==EMCTutorialStage::Loading?LOCTEXT("TeamLoaded","На арене: {Current}/{Total}"):Personal?LOCTEXT("TeamComplete","Команда: {Current}/{Total}"):LOCTEXT("Participants","Участников: {Count}"),{{TEXT("Current"),2},{TEXT("Total"),4},{TEXT("Count"),4}}));
        if(auto* Bar=WidgetTree->FindWidget<UProgressBar>(TEXT("TutorialProgress"))) Bar->SetVisibility(Personal || Shared?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* TimerView=WidgetTree->FindWidget<UTextBlock>(TEXT("TutorialTimer"))) {
            const bool Intro=PreviewLesson==EMCTutorialStage::Intro;TimerView->SetVisibility(Intro?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            if(Intro) TimerView->SetText(Format(TEXT("UI.Tutorial.StageSeconds"),LOCTEXT("Seconds","{Seconds} с"),{{TEXT("Seconds"),bPreviewShortFlow?3:10}}));
        }
    }
    SyncFrames();UpdateLayout();
    // Changing fixtures starts at the visible title/instruction; animation/progress ticks retain scroll position.
    auto CopyNow=[&](const TCHAR* Name) {const auto* Caption=WidgetTree->FindWidget<UTextBlock>(Name);return Caption?Caption->GetText():FText::GetEmpty();};
    const FText TitleNow=CopyNow(TEXT("TutorialStageTitle")),DialogueNow=CopyNow(TEXT("TutorialDialogue")),ActionNow=CopyNow(TEXT("TutorialAction"));
    const bool Changed=!LastSnapshot.StageTitleText.EqualTo(TitleNow) || !LastSnapshot.DialogueText.EqualTo(DialogueNow) || !LastSnapshot.ActionText.EqualTo(ActionNow);
    if(Changed) if(auto* ContentScroll=WidgetTree->FindWidget<UScrollBox>(TEXT("TutorialBodyScroll"))) ContentScroll->ScrollToStart();
    LastSnapshot.StageTitleText=TitleNow;LastSnapshot.DialogueText=DialogueNow;LastSnapshot.ActionText=ActionNow;
}

#undef LOCTEXT_NAMESPACE
