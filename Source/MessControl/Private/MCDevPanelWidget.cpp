#include "MCDevPanelWidget.h"
#include "MCTongue.h"
#include "MCPlayerController.h"
#include "MCPrototypeWidget.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlaytestSession.h"
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
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
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
    PlayerOverlayCheck=WidgetTree->ConstructWidget<UCheckBox>();
    auto* OverlayLabel=WidgetTree->ConstructWidget<UTextBlock>();
    OverlayLabel->SetText(FText::FromString(TEXT("Показывать статус игрока и подсказки управления")));
    OverlayLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
    OverlayLabel->SetColorAndOpacity(FSlateColor(DevMint));
    PlayerOverlayCheck->SetContent(OverlayLabel);
    PlayerOverlayCheck->SetToolTipText(FText::FromString(TEXT("Нижние плашки HP, взаимодействия и клавиш. Только на моём экране.")));
    PlayerOverlayCheck->OnCheckStateChanged.AddDynamic(this,&UMCDevPanelWidget::PlayerOverlayChanged);
    Main->AddChildToVerticalBox(PlayerOverlayCheck)->SetPadding(FMargin(0,2,0,12));
    AddText(Main,TEXT("AI-БОТЫ / ПРОВЕРКА ТЕМПА И СЛОЖНОСТИ"),18)->SetColorAndOpacity(FSlateColor(DevMint));
    AddText(Main,TEXT("Запуск начинает новый обычный день с текущим seed. До 4 участников: игроки + боты. Наблюдение убирает тела игроков; остановка возвращает их и перезапускает день."),12);
    auto* BotOptions=WidgetTree->ConstructWidget<UHorizontalBox>();
    Main->AddChildToVerticalBox(BotOptions)->SetPadding(FMargin(0,2,0,4));
    auto BotField=[&](const TCHAR* Title,const TArray<FString>& Options,int32 Selected)
    {
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Slot=BotOptions->AddChildToHorizontalBox(Box); Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(0,0,12,0));
        AddText(Box,Title,12);
        auto* Choice=WidgetTree->ConstructWidget<UComboBoxString>();
        for (const FString& Option:Options) Choice->AddOption(Option);
        Choice->SetSelectedIndex(Selected);
        Box->AddChildToVerticalBox(Choice);
        return Choice;
    };
    BotCount=BotField(TEXT("Число ботов"),{TEXT("1"),TEXT("2"),TEXT("3"),TEXT("4")},2);
    BotSkill=BotField(TEXT("Уровень решений"),{TEXT("Новички / novice"),TEXT("Обычные / regular"),TEXT("Опытные / skilled")},1);
    BotMode=BotField(TEXT("Режим"),{TEXT("Играть с ботами / coop"),TEXT("Наблюдать за ботами / observe")},0);
    BotSkill->SetToolTipText(FText::FromString(TEXT("Меняется скорость реакции и выбора задач. Здоровье, урон, движение и ресурсы остаются обычными. Поведение пока не откалибровано по людям.")));
    auto* BotButtons=WidgetTree->ConstructWidget<UHorizontalBox>();
    Main->AddChildToVerticalBox(BotButtons)->SetPadding(FMargin(0,3,0,2));
    auto BotButton=[&](const TCHAR* Text,const TCHAR* Hint)
    {
        auto* Button=WidgetTree->ConstructWidget<UButton>();
        Button->SetBackgroundColor(FLinearColor(.055f,.22f,.22f)); Button->SetToolTipText(FText::FromString(Hint));
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FText::FromString(Text));
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14)); Label->SetColorAndOpacity(FSlateColor(DevMint));
        auto* Content=CastChecked<UButtonSlot>(Button->AddChild(Label)); Content->SetPadding(FMargin(10,7));
        auto* Slot=BotButtons->AddChildToHorizontalBox(Button); Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Slot->SetPadding(FMargin(0,0,12,0));
        return Button;
    };
    StartBotsButton=BotButton(TEXT("ЗАПУСТИТЬ БОТОВ"),TEXT("Сброс текущего дня и запуск новой обычной игры. В ручном тесте сначала верни обычный день кнопкой ниже."));
    StopBotsButton=BotButton(TEXT("ОСТАНОВИТЬ БОТОВ"),TEXT("Сохранить CSV, удалить ботов, вернуть управление игрокам и начать обычный день заново."));
    ReportBotsButton=BotButton(TEXT("СОХРАНИТЬ СРЕЗ / CSV"),TEXT("Сохранить текущие задачи, смерти, простои, работу, движение, контакты и ошибки пути. Путь появится внизу и в Output Log."));
    StartBotsButton->OnClicked.AddDynamic(this,&UMCDevPanelWidget::StartBotsClicked);
    StopBotsButton->OnClicked.AddDynamic(this,&UMCDevPanelWidget::StopBotsClicked);
    ReportBotsButton->OnClicked.AddDynamic(this,&UMCDevPanelWidget::ReportBotsClicked);
    BotsStatus=AddText(Main,TEXT(""),12);
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
    if (const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()); PC && PC->PrototypeWidget && PlayerOverlayCheck)
        PlayerOverlayCheck->SetIsChecked(PC->PrototypeWidget->IsPlayerOverlayVisible());
    // Keep the two introduction labels; rebuild so a changed DA needs no widget edits.
    while (Steps->GetChildrenCount()>2) Steps->RemoveChildAt(2);
    while (Actions->GetChildrenCount()>2) Actions->RemoveChildAt(2);
    auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const UMCDayPlan* Plan=Mode?Mode->FirstDayPlan.LoadSynchronous():GS?GS->DayPlan.Get():nullptr;
    if (!Plan) Plan=LoadObject<UMCDayPlan>(nullptr,TEXT("/Game/Data/DA_Day01.DA_Day01"));
    if (Plan) for (int32 I=0;I<Plan->Steps.Num();++I)
    {
        const auto& Step=Plan->Steps[I]; if (Step.Step==EMCDayStep::Complete || Step.Step==EMCDayStep::DiscardBrushes) continue;
        AddAction(Steps,Step.Title.ToString(),Step.Instruction.ToString(),EMCDevAction::StartStep,I);
    }
    AddAction(Steps,TEXT("Обычный день 1 — полный перезапуск"),TEXT("Удаляет тестовые объекты, восстанавливает игроков и зубы. Возвращает обычные таймеры и переходы."),EMCDevAction::RestartDay);
    AddAction(Actions,TEXT("Active Ragdoll — мягкий (основной)"),TEXT("Основной профиль при старте и возрождении: стабилизированный корпус, физические руки и ноги, мягкие мышцы. Применить ко всем текущим игрокам."),EMCDevAction::ActiveRagdoll,1);
    AddAction(Actions,TEXT("Active Ragdoll — упругий"),TEXT("Сравни более сильные мышцы при поворотах, прыжках и ударах. Точные контакты сохраняются."),EMCDevAction::ActiveRagdoll,2);
    AddAction(Actions,TEXT("Active Ragdoll — исходный режим"),TEXT("Сравнение для отладки без перезапуска. Новые игроки и возрождения используют основной мягкий профиль."),EMCDevAction::ActiveRagdoll,0);
    AddAction(Actions,TEXT("Roguelike — сундук за задачу"),TEXT("Падение в безопасной зоне. Нажми E рядом с сундуком: 5 секунд вскрытия, затем выбор одной из трёх карточек."),EMCDevAction::RewardChest);
    AddText(Actions,TEXT("Босс · фаза 1"),19)->SetColorAndOpacity(FSlateColor(DevMint));
    AddAction(Actions,TEXT("Босс — создать Zombie для теста"),TEXT("Спавн рядом на свободном Boss NavMesh. AI выключен, обычная игра босса не создаёт."),EMCDevAction::BossPractice);
    AddAction(Actions,TEXT("Босс — интро / рёв [5 секунд]"),TEXT("Перемещает тестового Zombie в свободный центр языка. Sequencer-камера, рёв и кинополосы; управление вернётся через 5 секунд. Бой затем включается отдельно."),EMCDevAction::BossIntro);
    AddAction(Actions,TEXT("Босс — включить AI и бой"),TEXT("Создаёт тестового Zombie, если его нет. Преследование, удары руками и пинок с уроном."),EMCDevAction::BossAI);
    AddAction(Actions,TEXT("Босс — остановить AI / восстановить"),TEXT("Выключить бой, восстановить здоровье и вернуть idle."),EMCDevAction::BossStop);
    const TCHAR* BossClips[]={TEXT("Idle / дыхание"),TEXT("Шаркающая походка"),TEXT("Удар левой рукой"),TEXT("Удар правой рукой"),TEXT("Пинок"),TEXT("Получение урона"),TEXT("Падение / смерть"),TEXT("Рёв / интро")};
    for (int32 I=0;I<UE_ARRAY_COUNT(BossClips);++I)
        AddAction(Actions,FString(TEXT("Босс — анимация: "))+BossClips[I],TEXT("Изолированный просмотр bone clip. AI и игровой урон выключены; повторное нажатие начинает заново."),EMCDevAction::BossAnimation,I+1);
    AddAction(Actions,TEXT("Босс — убрать тестового Zombie"),TEXT("Удаляет только объект, созданный кнопками F3."),EMCDevAction::BossRemove);
    AddText(Actions,TEXT("Босс · фаза 3"),19)->SetColorAndOpacity(FSlateColor(DevMint));
    AddAction(Actions,TEXT("Фаза 3 — создать для просмотра"),TEXT("Спавн рядом на свободном Boss NavMesh. Начинает с idle; AI и урон выключены."),EMCDevAction::BossPhase3Spawn);
    const TCHAR* Phase3BossClips[]={TEXT("Idle / дыхание"),TEXT("Ходьба"),TEXT("Удар левой рукой"),TEXT("Удар правой рукой"),TEXT("Пинок"),TEXT("Получение урона"),TEXT("Падение / смерть"),TEXT("Рёв")};
    for (int32 I=0;I<UE_ARRAY_COUNT(Phase3BossClips);++I)
        AddAction(Actions,FString(TEXT("Фаза 3 — анимация: "))+Phase3BossClips[I],TEXT("Изолированный просмотр анимации фазы 3. AI и урон выключены; повторное нажатие начинает заново."),EMCDevAction::BossPhase3Animation,I+1);
    AddAction(Actions,TEXT("Фаза 3 — восстановить и включить AI"),TEXT("Восстановить здоровье и запустить AI и бой. Создаёт тестового босса фазы 3, если его нет."),EMCDevAction::BossPhase3Activate);
    AddAction(Actions,TEXT("Фаза 3 — остановить AI / восстановить"),TEXT("Выключить бой, восстановить здоровье и вернуть idle."),EMCDevAction::BossPhase3Deactivate);
    AddAction(Actions,TEXT("Фаза 3 — убрать тестового босса"),TEXT("Удаляет только тестового босса фазы 3, созданного кнопками F3."),EMCDevAction::BossPhase3Remove);
    AddText(Actions,TEXT("Другие механики"),19)->SetColorAndOpacity(FSlateColor(DevMint));
    AddAction(Actions,TEXT("Еда — уронить перед игроком"),TEXT("Случайный целый кусок из таблицы завтрака. Проверь удар, распад на фрагменты и хват."),EMCDevAction::DropFood);
    AddAction(Actions,TEXT("Перец — таймер и красная волна"),TEXT("Настоящий предмет из таблицы. Проглоти до детонации; от волны можно перепрыгнуть."),EMCDevAction::SpicyPepper);
    AddAction(Actions,TEXT("Холодная кола — иней и лёд"),TEXT("Напиток сверху, скользкая арена, падающий лёд. Разбивай киркой в слоте 2."),EMCDevAction::ColdCola);
    AddAction(Actions,TEXT("Рвота — испорченный заказ"),TEXT("Два куска в круге: глотка автоматически выплюнет испорченный заказ со струёй и брызгами. Пятна на языке очищаются щёткой."),EMCDevAction::VomitMeal);
    AddAction(Actions,TEXT("Руки — хват, тяга и толкание"),TEXT("Большой куб для толкания и тяги, два маленьких для подъёма над головой. Выбор по размеру на карте. Держи ЛКМ и двигайся."),EMCDevAction::GripPractice);
    AddAction(Actions,TEXT("Движение — липкий участок"),TEXT("Создать участок под ногами для проверки усилия и бега с Shift."),EMCDevAction::LocomotionGround,1);
    AddAction(Actions,TEXT("Движение — скользкий участок"),TEXT("Проверить инерцию, торможение и тягу при слабом сцеплении."),EMCDevAction::LocomotionGround,2);
    AddAction(Actions,TEXT("Движение — убрать участок"),TEXT("Вернуть исходные свойства пола."),EMCDevAction::LocomotionGround,0);
    AddAction(Actions,TEXT("Еда — ускоренная порча"),TEXT("Проверка порчи: 3 секунды вместо 180. Еда не становится язвой."),EMCDevAction::Infection);
    AddAction(Actions,TEXT("Зевание"),TEXT("Работа прерывается. Игроки автоматически цепляются за доступную поверхность языка."),EMCDevAction::Yawn);
    AddAction(Actions,TEXT("Огонь"),TEXT("Горящий участок повреждает язык и создаёт язву."),EMCDevAction::Fire);
    AddAction(Actions,TEXT("Язык — язва и волна боли"),TEXT("Язва перед игроком. Space — перепрыгнуть волну. Слот 4 + удерживать ЛКМ — лечить; отпускание сохраняет прогресс."),EMCDevAction::TongueUlcer);
    AddAction(Actions,TEXT("Язык — сильный рывок / ragdoll"),TEXT("Поджатие, резкий подъём и бросок игроков с едой. Случайные рывки отключены; эта кнопка запускает рывок вручную."),EMCDevAction::TongueJolt);
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It) if (It->Profile)
    {
        for (int32 I=0;I<It->Profile->DevMotions.Num();++I) if (const auto* P=It->Profile->DevMotions[I].Get())
            AddAction(Actions,TEXT("Язык — ")+P->Label.ToString(),TEXT("Профиль из DA_Tongue.DevMotions. Центр перед игроком, направление по взгляду корпуса."),EMCDevAction::TongueMotion,I);
        break;
    }
    AddAction(Actions,TEXT("Глаза — создать напарника"),TEXT("Наблюдай взгляд зуба на тебя, еду и опасности. Веки моргают; настройки в DA_Gaze и F1."),EMCDevAction::GazePractice);
    AddAction(Actions,TEXT("Язык — сравнить вес 4 / 28 кг"),TEXT("Два одинаковых предмета перед игроком. Тяжёлый сильнее продавливает язык; ЛКМ — хват и перетаскивание."),EMCDevAction::TongueWeight);
    AddAction(Actions,TEXT("Язык — включить / выключить продавливание"),TEXT("Для сравнения поверхности под игроками и едой. Движения от событий продолжают работать."),EMCDevAction::TongueWeightToggle);
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It) if (It->Profile)
    {
        for (int32 I=0;I<It->Profile->PressurePresets.Num();++I) if (const auto* P=It->Profile->PressurePresets[I].Get())
            AddAction(Actions,TEXT("Давление — ")+P->Label.ToString(),P->Description.ToString(),EMCDevAction::TonguePressurePreset,I);
        break;
    }
    AddAction(Actions,TEXT("Давление — применить DA_Tongue"),TEXT("Вернуть настройки основного DA без перезапуска Play. Если Default Pressure Preset назначен, используется он. Ассеты не перезаписываются."),EMCDevAction::TonguePressureReload);
    AddAction(Actions,TEXT("Давление — очистить следы"),TEXT("Сбросить историю продавливания. Текущие объекты продолжат давить на язык."),EMCDevAction::TonguePressureClear);
    AddAction(Actions,TEXT("Кофейный налёт"),TEXT("Покрывает зубы, игроков и поверхность налётом. Щётка доступна в слоте 1."),EMCDevAction::CoffeeDirt);
    AddAction(Actions,TEXT("Зубной камень — восстановить"),TEXT("Три приросших участка на доступном зубе. Ставит рядом и выбирает кирку: удерживай ЛКМ, чтобы выбивать камень в месте контакта."),EMCDevAction::CalculusPractice);
    AddAction(Actions,TEXT("Зубной камень — убрать тестовый"),TEXT("Убирает только камень, восстановленный кнопкой F3."),EMCDevAction::CalculusClear);
    AddAction(Actions,TEXT("Плавание — кофе на 10 минут"),TEXT("Наполняет рот выше языка. После наполнения уровень держится 10 минут без потока из струи и слива. WASD — плавать."),EMCDevAction::SwimCoffee);
    AddAction(Actions,TEXT("Расшатать зубы и игроков"),TEXT("Уход удержанием E; C включает уход за собой."),EMCDevAction::LooseTeeth);
    AddAction(Actions,TEXT("Повредить моего игрока: −25 HP"),TEXT("Проверка материала, реакции на урон и лечения."),EMCDevAction::DamageSelf);
    AddAction(Actions,TEXT("Толчок / ragdoll моего игрока"),TEXT("Обычный физический удар. После падения персонаж встаёт штатно."),EMCDevAction::Ragdoll);
    AddAction(Actions,TEXT("Гибель → возрождение за зуб арены"),TEXT("Убивает игрока хоста; обычное возрождение расходует конкретный большой зуб."),EMCDevAction::KillSelf);
    AddAction(Actions,TEXT("Восстановить общее здоровье рта"),TEXT("Язвы и повреждения отдельных зубов сохраняются."),EMCDevAction::RestoreMouth);
    AddAction(Actions,TEXT("Мгновенно убрать кофе — сброс теста"),TEXT("Заканчивает тест плавания, убирает воду и зацепы."),EMCDevAction::StopCoffee);
}
void UMCDevPanelWidget::PlayerOverlayChanged(bool Checked)
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()); PC && PC->PrototypeWidget)
        PC->PrototypeWidget->SetPlayerOverlayVisible(Checked);
}
void UMCDevPanelWidget::StartBotsClicked()
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()); PC && BotCount && BotSkill && BotMode)
        PC->RequestDevAction(EMCDevAction::BotsStart,MCDevBotSetup(BotCount->GetSelectedIndex()+1,BotSkill->GetSelectedIndex(),BotMode->GetSelectedIndex()==1));
}
void UMCDevPanelWidget::StopBotsClicked() { if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->RequestDevAction(EMCDevAction::BotsStop); }
void UMCDevPanelWidget::ReportBotsClicked() { if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->RequestDevAction(EMCDevAction::BotsReport); }
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
    const auto* BotSession=AMCPlaytestSession::Find(GetWorld());
    const bool BotsActive=BotSession && BotSession->IsActive();
    const bool CanControl=PC && PC->CanUseDevPanel();
    if (StartBotsButton) StartBotsButton->SetIsEnabled(CanControl && !BotsActive && !GS->bLobbyWaiting && !GS->bTutorialActive && !GS->bDevManualEvents);
    if (StopBotsButton) StopBotsButton->SetIsEnabled(CanControl && BotsActive);
    if (ReportBotsButton) ReportBotsButton->SetIsEnabled(CanControl && BotsActive);
    if (Steps) Steps->SetIsEnabled(!BotsActive);
    if (Actions) Actions->SetIsEnabled(!BotsActive);
    if (BotsStatus) BotsStatus->SetText(FText::FromString(FString::Printf(TEXT("%s  |  Seed %d  |  прототип, без калибровки по людям"),
        BotsActive?TEXT("БОТЫ ИГРАЮТ — ручные события заблокированы"):GS->bDevManualEvents?TEXT("Для ботов сначала верни обычный день"):TEXT("Боты не запущены"),GS->RunSeed)));
    FString Summary=FString::Printf(TEXT("%s  |  %s\nРот %.0f HP  /  зубы %d/%d  /  еда %d  /  язвы %d  /  кофе %s"),
        PC && PC->CanUseDevPanel()?TEXT("ХОСТ"):TEXT("КЛИЕНТ: только просмотр"),GS->bDevManualEvents?TEXT("РУЧНОЙ ТЕСТ — без дедлайна"):TEXT("ОБЫЧНЫЙ ДЕНЬ"),
        GS->MouthHealth,GS->AvailableArenaTeeth(),GS->ArenaTeeth.Num(),Food,Ulcers,Water?TEXT("активен"):TEXT("нет"));
    if (GS->ArenaTeeth.Num()<GS->RunSettings.InitialArenaTeeth)
        Summary+=FString::Printf(TEXT("\nРазметка карты: %d игровых мест для зубов, по правилам нужно %d."),GS->ArenaTeeth.Num(),GS->RunSettings.InitialArenaTeeth);
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        const FString Name=It->ActivePressurePreset?It->ActivePressurePreset->Label.ToString():TEXT("DA_Tongue");
        Summary+=FString::Printf(TEXT("\nДавление: %s%s  /  глубина до %.0f см  /  восстановление %.2f с"),
            *Name,It->PressureSettings.bEnabled?TEXT(""):TEXT(" — выключено"),It->PressureSettings.MaxDepth,It->PressureSettings.RecoverSeconds);
        break;
    }
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
