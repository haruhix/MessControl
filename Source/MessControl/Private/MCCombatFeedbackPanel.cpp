#include "MCCombatFeedbackPanel.h"

#include "MCCombatFeedbackStation.h"
#include "MCToothCharacter.h"
#include "MCVFXLabBossStation.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using FControllerAction=TFunction<void(AMCCombatFeedbackController&)>;

struct FScenarioEntry
{
    EMCVFXLabBossKind Kind;
    const TCHAR* Title;
    const TCHAR* Group;
};

const FScenarioEntry BossScenarios[]={
    {EMCVFXLabBossKind::TankMelee,TEXT("Ближний удар"),TEXT("Танк")},
    {EMCVFXLabBossKind::TankCharge,TEXT("Рывок"),nullptr},
    {EMCVFXLabBossKind::TankJump,TEXT("Прыжок"),nullptr},
    {EMCVFXLabBossKind::TankRoll,TEXT("Крутилка"),nullptr},
    {EMCVFXLabBossKind::TankEntrance,TEXT("Появление"),nullptr},
    {EMCVFXLabBossKind::TankShieldHit,TEXT("Удар по щиту"),nullptr},
    {EMCVFXLabBossKind::TankShieldBreakOverflow,TEXT("Разрушение щита"),nullptr},
    {EMCVFXLabBossKind::TankDeath,TEXT("Смерть"),nullptr},
    {EMCVFXLabBossKind::MageMelee,TEXT("Ближний удар"),TEXT("Маг")},
    {EMCVFXLabBossKind::MageFireball,TEXT("Огненный шар"),nullptr},
    {EMCVFXLabBossKind::MageSummon,TEXT("Призыв"),nullptr},
    {EMCVFXLabBossKind::MageNutRain,TEXT("Ореховый дождь"),nullptr},
    {EMCVFXLabBossKind::MageTeleport,TEXT("Телепорт"),nullptr},
    {EMCVFXLabBossKind::MageHurt,TEXT("Получение урона"),nullptr},
    {EMCVFXLabBossKind::MageDeath,TEXT("Смерть"),nullptr},
    {EMCVFXLabBossKind::MageEntrance,TEXT("Появление"),nullptr},
    {EMCVFXLabBossKind::ZombiePunchLeft,TEXT("Левый удар"),TEXT("Зомби")},
    {EMCVFXLabBossKind::ZombiePunchRight,TEXT("Правый удар"),nullptr},
    {EMCVFXLabBossKind::ZombieKick,TEXT("Удар ногой"),nullptr},
    {EMCVFXLabBossKind::ZombieHurt,TEXT("Получение урона"),nullptr},
    {EMCVFXLabBossKind::ZombieDeath,TEXT("Смерть"),nullptr},
    {EMCVFXLabBossKind::ZombieRoar,TEXT("Рёв"),nullptr},
    {EMCVFXLabBossKind::Phase3PunchRight,TEXT("Правый удар"),TEXT("Босс фазы 3")},
    {EMCVFXLabBossKind::Phase3AreaAttack,TEXT("Атака по площади"),nullptr}
};
static_assert(UE_ARRAY_COUNT(BossScenarios)==uint8(EMCVFXLabBossKind::Phase3AreaAttack)+1,
    "Add new boss diagnostic scenarios to the combat feedback menu.");

class SMCCombatFeedbackPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMCCombatFeedbackPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AMCCombatFeedbackController>,Controller)
    SLATE_END_ARGS()

    void Construct(const FArguments& Arguments)
    {
        Controller=Arguments._Controller;
        const auto WeakController=Controller;
        ChildSlot
        [
            SNew(SDPIScaler).DPIScale(this,&SMCCombatFeedbackPanel::InverseViewportScale)
            [
                SNew(SOverlay)
                +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [
                    SNew(SBox).WidthOverride(this,&SMCCombatFeedbackPanel::PanelWidth)
                        .HeightOverride(this,&SMCCombatFeedbackPanel::PanelHeight)
                    [
                        SNew(SBorder).Padding(14).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                            .BorderBackgroundColor(FLinearColor(.025f,.035f,.05f,.97f))
                        [
                            SNew(SVerticalBox)
                            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                            [
                                SNew(SHorizontalBox)
                                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                                [Heading(TEXT("Импакт: OLD / NEW"),20)]
                                +SHorizontalBox::Slot().AutoWidth()
                                [Action(TEXT("Закрыть · F3 / Esc"),[](auto& PC){PC.CloseTestPanel();})]
                            ]
                            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                            [SNew(STextBlock).Text(FText::FromString(TEXT("Каждое действие запускается в обеих дорожках. Мир продолжает работать.")))
                                .AutoWrapText(true).ColorAndOpacity(FLinearColor(.7f,.83f,.95f))]
                            +SVerticalBox::Slot().FillHeight(1)
                            [
                                SNew(SHorizontalBox)
                                +SHorizontalBox::Slot().FillWidth(.34f).Padding(0,0,10,0)
                                [BuildScenarioList()]
                                +SHorizontalBox::Slot().FillWidth(.66f)
                                [
                                    SNew(SScrollBox).ScrollBarAlwaysVisible(true)
                                    +SScrollBox::Slot()
                                    [
                                        SNew(SVerticalBox)
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
                                        [SNew(STextBlock).Text_Lambda([WeakController]{
                                            const auto* PC=WeakController.Get();
                                            return FText::FromString(PC?PC->GetScenarioTitle():TEXT("Стенд недоступен"));})
                                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),16)).AutoWrapText(true)
                                            .ColorAndOpacity(FLinearColor::White)]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [Action(TEXT("Атака / кейс босса"),[](auto& PC){PC.RunSelectedCase();})]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [Heading(TEXT("Игрок → монстр"))]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [OutgoingAmounts()]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [
                                            SNew(SHorizontalBox)
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("Сильный удар · 50"),[](auto& PC){PC.StrikeOutgoing(50.f);})]
                                            +SHorizontalBox::Slot().FillWidth(1)
                                            [Action(TEXT("Добить монстра"),[](auto& PC){PC.FinishEnemy();})]
                                        ]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [Heading(TEXT("Монстр → игрок"))]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [
                                            SNew(SHorizontalBox)
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("Урон · 15"),[](auto& PC){PC.StrikeIncoming(15.f);})]
                                            +SHorizontalBox::Slot().FillWidth(1)
                                            [Action(TEXT("Урон · 40"),[](auto& PC){PC.StrikeIncoming(40.f);})]
                                        ]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [Action(TEXT("Добить игрока"),[](auto& PC){PC.FinishPlayer();})]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [Heading(TEXT("Щит танка"))]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [
                                            SNew(SHorizontalBox)
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("Удар по щиту · 75"),[](auto& PC){PC.StrikeShield(75.f);},true)]
                                            +SHorizontalBox::Slot().FillWidth(1)
                                            [Action(TEXT("Пробить щит · 600"),[](auto& PC){PC.StrikeShield(600.f);},true)]
                                        ]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [
                                            SNew(SHorizontalBox)
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("Пауза / продолжить цикл"),[](auto& PC){PC.ToggleRepeating();},false,true)]
                                            +SHorizontalBox::Slot().FillWidth(1)
                                            [Action(TEXT("Сбросить стенд · R"),[](auto& PC){PC.ResetStations();})]
                                        ]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [Heading(TEXT("Камера"))]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                                        [
                                            SNew(SHorizontalBox)
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("OLD · 1"),[](auto& PC){PC.ViewLegacy();})]
                                            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
                                            [Action(TEXT("NEW · 2"),[](auto& PC){PC.ViewCurrent();})]
                                            +SHorizontalBox::Slot().FillWidth(1)
                                            [Action(TEXT("Общий · 0"),[](auto& PC){PC.ViewOverview();})]
                                        ]
                                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                                        [Heading(TEXT("Фактическое состояние"))]
                                        +SVerticalBox::Slot().AutoHeight()
                                        [SNew(STextBlock).Text_Lambda([WeakController]{
                                            const auto* PC=WeakController.Get();
                                            return FText::FromString(PC?PC->GetComparisonStatusText():TEXT("PIE завершён"));})
                                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).AutoWrapText(true)
                                            .ColorAndOpacity(FLinearColor(.75f,.86f,.92f))]
                                    ]
                                ]
                            ]
                        ]
                    ]
                ]
            ]
        ];
    }

    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
    {
        return HandleCloseKey(Event);
    }
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
    {
        return HandleCloseKey(Event);
    }

private:
    TSharedRef<SWidget> Heading(const TCHAR* Title,const int32 Size=13) const
    {
        return SNew(STextBlock).Text(FText::FromString(Title))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size)).ColorAndOpacity(FLinearColor::White);
    }

    TSharedRef<SWidget> Action(const TCHAR* Title,FControllerAction Callback,const bool bShieldOnly=false,const bool bBasicLoopOnly=false) const
    {
        const auto WeakController=Controller;
        return SNew(SButton).ContentPadding(FMargin(8,6))
            .IsEnabled_Lambda([WeakController,bShieldOnly,bBasicLoopOnly]{
                const auto* PC=WeakController.Get();
                return PC && (!bShieldOnly || PC->CanShieldTest()) && (!bBasicLoopOnly ||
                    (PC->GetSelectedScenario()==0 && (!PC->GetPawn() || !PC->GetPawn()->IsA(AMCToothCharacter::StaticClass()))));})
            .ToolTipText(bBasicLoopOnly?FText::FromString(TEXT("Цикл работает для обычного монстра при общей камере.")):FText::GetEmpty())
            .OnClicked_Lambda([WeakController,Callback=MoveTemp(Callback)]{
                if (auto* PC=WeakController.Get()) Callback(*PC);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Title)).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12))];
    }

    TSharedRef<SWidget> Scenario(const int32 Index,const TCHAR* Title) const
    {
        const auto WeakController=Controller;
        return SNew(SButton).ContentPadding(FMargin(8,6))
            .IsEnabled_Lambda([WeakController]{return WeakController.IsValid();})
            .ButtonColorAndOpacity_Lambda([WeakController,Index]{
                const auto* PC=WeakController.Get();return FSlateColor(PC && PC->GetSelectedScenario()==Index
                    ?FLinearColor(.32f,.37f,.42f):FLinearColor(.07f,.10f,.14f));})
            .OnClicked_Lambda([WeakController,Index]{
                if (auto* PC=WeakController.Get()) PC->SelectScenario(Index);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Title)).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12)).ColorAndOpacity(FLinearColor::White)];
    }

    TSharedRef<SWidget> BuildScenarioList() const
    {
        const TSharedRef<SVerticalBox> List=SNew(SVerticalBox);
        List->AddSlot().AutoHeight().Padding(0,0,0,7)[Heading(TEXT("Сценарии"))];
        List->AddSlot().AutoHeight().Padding(0,0,0,4)[Scenario(0,TEXT("Обычный монстр"))];
        for (const FScenarioEntry& Entry:BossScenarios)
        {
            if (Entry.Group) List->AddSlot().AutoHeight().Padding(0,10,0,5)[Heading(Entry.Group)];
            List->AddSlot().AutoHeight().Padding(0,0,0,4)[Scenario(1+int32(uint8(Entry.Kind)),Entry.Title)];
        }
        return SNew(SScrollBox).ScrollBarAlwaysVisible(true)+SScrollBox::Slot()[List];
    }

    TSharedRef<SWidget> OutgoingAmounts() const
    {
        const TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox);
        for (const float Amount:{10.f,25.f,50.f,100.f})
        {
            const FString Title=FString::Printf(TEXT("%.0f"),Amount);
            Row->AddSlot().FillWidth(1).Padding(0,0,4,0)[Action(*Title,[Amount](auto& PC){PC.StrikeOutgoing(Amount);})];
        }
        return Row;
    }

    FReply HandleCloseKey(const FKeyEvent& Event) const
    {
        if (Event.GetKey()!=EKeys::F3 && Event.GetKey()!=EKeys::Escape) return FReply::Unhandled();
        if (auto* PC=Controller.Get()) PC->CloseTestPanel();
        return FReply::Handled();
    }

    FVector2D ViewportSize() const
    {
        FVector2D Size(960,680);
        if (const auto* PC=Controller.Get()) if (const auto* World=PC->GetWorld())
            if (const auto* Viewport=World->GetGameViewport()) Viewport->GetViewportSize(Size);
        return Size;
    }
    float InverseViewportScale() const
    {
        if (const auto* PC=Controller.Get()) if (const auto* World=PC->GetWorld())
            if (const auto* Viewport=World->GetGameViewport())
                return 1.f/FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(Viewport));
        return 1.f;
    }
    FOptionalSize PanelWidth() const {return FOptionalSize(FMath::Clamp(float(ViewportSize().X)-24.f,320.f,960.f));}
    FOptionalSize PanelHeight() const {return FOptionalSize(FMath::Clamp(float(ViewportSize().Y)-24.f,180.f,680.f));}

    TWeakObjectPtr<AMCCombatFeedbackController> Controller;
};
}

TSharedRef<SWidget> MCBuildCombatFeedbackPanel(TWeakObjectPtr<AMCCombatFeedbackController> Controller)
{
    return SNew(SMCCombatFeedbackPanel).Controller(Controller);
}

FString MCCombatFeedbackScenarioTitle(int32 Index)
{
    if(Index==0) return TEXT("Обычный монстр: входящий и исходящий урон");
    const TCHAR* Group=TEXT("Босс");
    for(const FScenarioEntry& Entry:BossScenarios)
    {
        if(Entry.Group) Group=Entry.Group;
        if(Index==1+int32(uint8(Entry.Kind))) return FString::Printf(TEXT("%s: %s"),Group,Entry.Title);
    }
    return TEXT("Неизвестный сценарий");
}
