#include "MCDevPanelWidget.h"
#include "MCTongue.h"
#include "MCPlayerController.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCCoffeeFlood.h"
#include "Blueprint/WidgetTree.h"
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
#include "Styling/CoreStyle.h"
#include "EngineUtils.h"

namespace { const FLinearColor DevMint(.28f,.93f,.75f); }
void UMCDevActionButton::Configure(AMCPlayerController* Player,EMCDevAction Command,int32 Index)
{
    Controller=Player; Action=Command; StepIndex=Index;
    OnClicked.AddDynamic(this,&UMCDevActionButton::Clicked);
}
void UMCDevActionButton::Clicked() { if (Controller) Controller->RequestDevAction(Action,StepIndex); }
UTextBlock* UMCDevPanelWidget::AddText(UVerticalBox* Box,const FString& Text,int32 Size)
{
    auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text)); Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size));
    Label->SetColorAndOpacity(FSlateColor(FLinearColor(.93f,.95f,.91f))); Label->SetAutoWrapText(true);
    Box->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,3,0,6)); return Label;
}
void UMCDevPanelWidget::AddAction(UVerticalBox* Box,const FString& Text,const FString& Hint,EMCDevAction Action,int32 Step)
{
    auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());
    auto* Button=WidgetTree->ConstructWidget<UMCDevActionButton>(); Button->Configure(PC,Action,Step);
    Button->SetIsEnabled(PC && PC->CanUseDevPanel());
    Button->SetBackgroundColor(FLinearColor(.055f,.22f,.22f)); Button->SetToolTipText(FText::FromString(Hint));
    auto* TextBox=WidgetTree->ConstructWidget<UVerticalBox>();
    auto* Label=AddText(TextBox,Text,15); Label->SetColorAndOpacity(FSlateColor(DevMint));
    auto* ButtonSlot=CastChecked<UButtonSlot>(Button->AddChild(TextBox));
    ButtonSlot->SetHorizontalAlignment(HAlign_Fill); ButtonSlot->SetPadding(FMargin(10,4));
    Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3,0,4));
}
void UMCDevPanelWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetIsFocusable(true);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
    auto* Border=WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(.016f,.036f,.045f,.98f)); Border->SetPadding(FMargin(24,16));
    auto* CanvasSlot=Root->AddChildToCanvas(Border); CanvasSlot->SetAnchors(FAnchors(.10f,.05f,.90f,.95f)); CanvasSlot->SetOffsets(FMargin(0));
    auto* Main=WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Main);
    AddText(Main,TEXT("DEV / ТЕСТ МЕХАНИК"),25)->SetColorAndOpacity(FSlateColor(DevMint));
    AddText(Main,TEXT("F3 / Esc — закрыть. Мир продолжает работать. Команды доступны хосту."),13);
    Status=AddText(Main,TEXT(""),14);
    auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();
    Main->AddChildToVerticalBox(Columns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto Column=[&](const TCHAR* Title,const TCHAR* Hint)
    {
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
        auto* ColumnSlot=Columns->AddChildToHorizontalBox(Scroll); ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ColumnSlot->SetPadding(FMargin(0,0,18,0));
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Box);
        AddText(Box,Title,19); AddText(Box,Hint,12);
        return Box;
    };
    Steps=Column(TEXT("ЭТАПЫ ДНЯ"),TEXT("Чистый запуск: новые игроки, полное здоровье, все зубы с карты. Выбранный этап остаётся до следующей команды."));
    Actions=Column(TEXT("ДОБАВИТЬ В ТЕКУЩИЙ ТЕСТ"),TEXT("Можно сочетать эффекты. Отключают автопереходы; урон, порча, заживление и возрождения продолжаются."));
    Feedback=AddText(Main,TEXT("Наведи на кнопку для подсказки. Язвы заживают сами — убери еду и защищай поверхность."),13);
    Feedback->SetColorAndOpacity(FSlateColor(DevMint));
    auto* Close=WidgetTree->ConstructWidget<UButton>();
    auto* CloseText=WidgetTree->ConstructWidget<UTextBlock>(); CloseText->SetText(FText::FromString(TEXT("ВЕРНУТЬСЯ В ИГРУ  [F3]"))); CloseText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14));
    Close->SetContent(CloseText); Close->OnClicked.AddDynamic(this,&UMCDevPanelWidget::CloseClicked); Main->AddChildToVerticalBox(Close)->SetPadding(FMargin(0,8,0,0));
    RefreshActions();
}
void UMCDevPanelWidget::RefreshActions()
{
    if (!Steps || !Actions) return;
    // Keep the two introduction labels; rebuild so a changed DA needs no widget edits.
    while (Steps->GetChildrenCount()>2) Steps->RemoveChildAt(2);
    while (Actions->GetChildrenCount()>2) Actions->RemoveChildAt(2);
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const UMCDayPlan* Plan=Mode?Mode->FirstDayPlan.LoadSynchronous():GS?GS->DayPlan.Get():nullptr;
    if (!Plan) Plan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
    if (Plan) for (int32 I=0;I<Plan->Steps.Num();++I)
    {
        const auto& Step=Plan->Steps[I]; if (Step.Step==EMCDayStep::Complete) continue;
        AddAction(Steps,Step.Title.ToString(),Step.Instruction.ToString(),EMCDevAction::StartStep,I);
    }
    AddAction(Steps,TEXT("Обычный день 1 — полный перезапуск"),TEXT("Удаляет тестовые объекты, восстанавливает игроков и зубы. Возвращает обычные таймеры и переходы."),EMCDevAction::RestartDay);
    AddAction(Actions,TEXT("Еда — уронить перед игроком"),TEXT("Случайный целый кусок из таблицы завтрака. Проверь удар, распад на фрагменты и хват."),EMCDevAction::DropFood);
    AddAction(Actions,TEXT("Руки — хват, тяга и толкание"),TEXT("Лёгкий тренировочный куб. Подойди близко с разных сторон, держи E и двигайся. Положение кистей, сила и времена — DA_Grip."),EMCDevAction::GripPractice);
    AddAction(Actions,TEXT("Инфекция — испорченная еда + язва"),TEXT("Ускоряет порчу одного нового куска. Он создаёт настоящую язву и мешает её заживлению."),EMCDevAction::Infection);
    AddAction(Actions,TEXT("Язык — язва и волна боли"),TEXT("Язва без еды перед игроком. Наступи на неё: движение языка, красная волна и один толчок каждому. Язва заживает сама."),EMCDevAction::TongueUlcer);
    AddAction(Actions,TEXT("Язык — сильный рывок / ragdoll"),TEXT("Поджатие, резкий подъём и бросок игроков с едой. В обычном дне повторяется редко; в ручном тесте запускается этой кнопкой."),EMCDevAction::TongueJolt);
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It) if (It->Profile)
    {
        for (int32 I=0;I<It->Profile->DevMotions.Num();++I) if (const auto* P=It->Profile->DevMotions[I].Get())
            AddAction(Actions,TEXT("Язык — ")+P->Label.ToString(),TEXT("Профиль из DA_Tongue.DevMotions. Центр перед игроком, направление по взгляду корпуса."),EMCDevAction::TongueMotion,I);
        break;
    }
    AddAction(Actions,TEXT("Глаза — создать напарника"),TEXT("Наблюдай взгляд зуба на тебя, еду и опасности. Веки моргают; настройки в DA_Gaze и F1."),EMCDevAction::GazePractice);
    AddAction(Actions,TEXT("Язык — сравнить вес 4 / 28 кг"),TEXT("Два одинаковых предмета перед игроком. Тяжёлый сильнее продавливает язык; E — хват и перетаскивание."),EMCDevAction::TongueWeight);
    AddAction(Actions,TEXT("Язык — включить / выключить продавливание"),TEXT("Для сравнения поверхности под игроками и едой. Движения от событий продолжают работать."),EMCDevAction::TongueWeightToggle);
    AddAction(Actions,TEXT("Кофейный налёт + щётки"),TEXT("Покрывает зубы, игроков и поверхность налётом. Четыре контакта по 0,5 секунды."),EMCDevAction::CoffeeDirt);
    AddAction(Actions,TEXT("Сбросить щётки с неба"),TEXT("По одной щётке на игрока. Подобрать E, выбросить Q за передний край."),EMCDevAction::DropBrushes);
    AddAction(Actions,TEXT("Расшатать зубы и игроков"),TEXT("Уход удержанием E; C включает уход за собой."),EMCDevAction::LooseTeeth);
    AddAction(Actions,TEXT("Повредить моего игрока: −25 HP"),TEXT("Проверка материала, реакции на урон и лечения."),EMCDevAction::DamageSelf);
    AddAction(Actions,TEXT("Толчок / ragdoll моего игрока"),TEXT("Обычный физический удар. После падения персонаж встаёт штатно."),EMCDevAction::Ragdoll);
    AddAction(Actions,TEXT("Гибель → возрождение за зуб арены"),TEXT("Убивает игрока хоста; обычное возрождение расходует конкретный большой зуб."),EMCDevAction::KillSelf);
    AddAction(Actions,TEXT("Восстановить общее здоровье рта"),TEXT("Язвы и повреждения отдельных зубов сохраняются."),EMCDevAction::RestoreMouth);
    AddAction(Actions,TEXT("Мгновенно убрать кофе — сброс теста"),TEXT("Убирает воду и зацепы. Обычный двухсекундный слив запускается автоматически после наполнения."),EMCDevAction::StopCoffee);
}
void UMCDevPanelWidget::SetFeedback(const FText& Text) { if (Feedback) Feedback->SetText(Text); }
void UMCDevPanelWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); if (!Status || !IsVisible()) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>(); if (!GS) return;
    int32 Food=0,Ulcers=0; bool Water=false;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (!It->bBrushTool && !It->IsDisposed()) ++Food;
    for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->bUlcer) ++Ulcers;
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) Water|=It->bActive;
    const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());
    FString Summary=FString::Printf(TEXT("%s  |  %s\nРот %.0f HP  /  зубы %d/%d  /  еда %d  /  язвы %d  /  кофе %s"),
        PC && PC->CanUseDevPanel()?TEXT("ХОСТ"):TEXT("КЛИЕНТ: только просмотр"),GS->bDevManualEvents?TEXT("РУЧНОЙ ТЕСТ — без дедлайна"):TEXT("ОБЫЧНЫЙ ДЕНЬ"),
        GS->MouthHealth,GS->AvailableArenaTeeth(),GS->ArenaTeeth.Num(),Food,Ulcers,Water?TEXT("активен"):TEXT("нет"));
    if (GS->ArenaTeeth.Num()<GS->RunSettings.InitialArenaTeeth)
        Summary+=FString::Printf(TEXT("\nРазметка карты: %d игровых мест для зубов, по правилам нужно %d."),GS->ArenaTeeth.Num(),GS->RunSettings.InitialArenaTeeth);
    Status->SetText(FText::FromString(Summary));
}
void UMCDevPanelWidget::CloseClicked() { if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleDevPanel(); }
FReply UMCDevPanelWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()))
    {
        if (Event.GetKey()==EKeys::F3 || Event.GetKey()==EKeys::Escape) { PC->ToggleDevPanel(); return FReply::Handled(); }
        if (Event.GetKey()==EKeys::F1) { PC->ToggleTuning(); return FReply::Handled(); }
        if (Event.GetKey()==EKeys::F2) { PC->ToggleConnection(); return FReply::Handled(); }
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}
