#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationLibrary.h"
#include "MCNutVFXCatalog.h"
#include "Sound/SoundBase.h"

#include <initializer_list>

namespace
{
TMap<FName,FMCVFXSourceContract>& Definitions()
{
    static TMap<FName,FMCVFXSourceContract> Result=[]
    {
        TMap<FName,FMCVFXSourceContract> Sources;
        auto Add=[&](const TCHAR* ID,const TCHAR* Label,const TCHAR* Category,const TCHAR* Owner,
            const FSoftObjectPath& Library,std::initializer_list<const TCHAR*> Events)->FMCVFXSourceContract&
        {
            FMCVFXSourceContract C;C.SourceID=FName(ID);C.Label=FText::FromString(Label);C.Category=FName(Category);
            C.SourceOwner=Owner;C.Library=Library;
            for(const auto* Event:Events)C.Events.Add(FName(Event));
            C.Description=TEXT("Реальный источник представления. Доступность сценария и редактируемого профиля проверяется отдельно; запись контракта не означает завершённую миграцию.");
            return Sources.Add(C.SourceID,MoveTemp(C));
        };
        const auto World=MCVFXSourceContracts::WorldLibraryPath(),Tool=MCVFXSourceContracts::ToolLibraryPath();
        const auto Hub=MCVFXSourceContracts::HubLibraryPath(),UI=MCVFXSourceContracts::InterfaceLibraryPath();
        Add(TEXT("Tool.Knife"),TEXT("Нож · взмах и попадание"),TEXT("Tools"),TEXT("AMCToothCharacter / UMCInventoryComponent / MCMeleeCombo"),Tool,{TEXT("Swing"),TEXT("Hit"),TEXT("FoodCut")});
        Add(TEXT("Tool.Pickaxe"),TEXT("Кирка · взмах и попадание"),TEXT("Tools"),TEXT("AMCToothCharacter / UMCInventoryComponent / MCMeleeCombo"),Tool,{TEXT("Swing"),TEXT("Hit")});
        Add(TEXT("Tool.Brush"),TEXT("Щётка · контакт"),TEXT("Cleaning"),TEXT("UMCBrushContactComponent / AMCToothCharacter"),Tool,{TEXT("Contact")});
        Add(TEXT("Tool.Spray"),TEXT("Распылитель · обработка"),TEXT("Tools"),TEXT("UMCInventoryComponent"),Tool,{TEXT("Start"),TEXT("WaterCare"),TEXT("Contact"),TEXT("End")});
        Add(TEXT("Tool.Watergun"),TEXT("Водяной пистолет"),TEXT("Tools"),TEXT("UMCInventoryComponent / AMCReactionVFX"),Tool,{TEXT("Shot"),TEXT("Impact")});
        Add(TEXT("Character.Movement"),TEXT("Зубик · движение и физическое восстановление"),TEXT("Character"),TEXT("AMCToothCharacter / UMCToothPhysicsComponent"),Tool,{TEXT("Step"),TEXT("Land"),TEXT("Jump"),TEXT("Fall"),TEXT("StandUp"),TEXT("Pull")});
        Add(TEXT("Character.FreezeDeath"),TEXT("Зубик · ледяная смерть"),TEXT("Character"),TEXT("AMCToothCharacter::OnRep_FreezeDeath / AMCFreezeDeathVFX"),World,{TEXT("Hold"),TEXT("Shatter")});
        auto& DeathSpirit=Add(TEXT("Character.DeathSpirit"),TEXT("Зубик · дух и возрождение"),TEXT("Character"),
            TEXT("UMCDeathSpiritComponent / AMCArenaTooth / AMCGameMode::ProcessRespawns"),World,{TEXT("Rise"),TEXT("Merge"),TEXT("Release")});
        DeathSpirit.Description=TEXT("Rise получает место гибели и точку подъёма. Merge начинается в конце принятого притягивания. Дух и коронка белеют, коронка сжимается к общей точке контакта. Release запускает вспышку у выхода из зуба при смене персонажа. Дополнительные Niagara и звук читаются из общей библиотеки. Изолированный сценарий Composer пока недоступен. Проверка полного респавна проходит в L_Mouth.");
        Add(TEXT("Character.Hurt"),TEXT("Зубик · полученный удар"),TEXT("Character"),TEXT("UMCCombatFeedbackComponent / AMCToothCharacter"),Tool,{TEXT("Hit")});
        Add(TEXT("Enemy.Generic.Hurt"),TEXT("Противник · полученный удар"),TEXT("Enemies"),TEXT("UMCCombatFeedbackComponent"),World,{TEXT("Hit")});
        Add(TEXT("Boss.Generic.Hurt"),TEXT("Босс · полученный удар"),TEXT("Bosses"),TEXT("UMCCombatFeedbackComponent / AMCBossCharacter"),World,{TEXT("Hit")});
        Add(TEXT("Enemy.Nut.Hurt"),TEXT("Орех · полученный удар"),TEXT("Enemies"),TEXT("UMCCombatFeedbackComponent / AMCNutEnemy"),World,{TEXT("Hit")});
        Add(TEXT("Boss.NutKnight.Hurt"),TEXT("Рыцарь · полученный удар"),TEXT("Bosses"),TEXT("UMCCombatFeedbackComponent / AMCNutBoss"),World,{TEXT("Hit")});
        const auto* ChainAction=MCNutVFX::Find(MCNutVFX::ChainSource());
        FMCVFXSourceContract Chain;
        Chain.SourceID=ChainAction->ID;Chain.Label=ChainAction->Label;Chain.Events=ChainAction->Events;
        Chain.Category=TEXT("Boss");Chain.SourceOwner=TEXT("AMCNutBoss / KnightChainPresentation");
        Chain.Library=UMCVFXPresentationLibrary::MouthLibraryPath();
        Chain.Description=TEXT("Замах, вылет цепи, захват зубика или целого ореха, возврат, удар по рыцарю и восстановление. Положение рук и цели берётся из настоящего действия.");
        for(FName Event:Chain.Events) {
            const auto Sound=MCNutVFX::ChainDefaultSound(Event);
            if(Sound.IsNull())continue;
            auto& Audio=Chain.OriginalAudio.Add(Event);
            Audio.ID=FName(*(Event.ToString()+TEXT(".Original")));
            Audio.Label=FText::FromString(TEXT("Исходный звук цепи"));
            Audio.Resource=Sound.ToSoftObjectPath();Audio.bSuppressionWired=true;
        }
        Sources.Add(Chain.SourceID,MoveTemp(Chain));
        Add(TEXT("Boss.NutMage.Hurt"),TEXT("Маг · полученный удар"),TEXT("Bosses"),TEXT("UMCCombatFeedbackComponent / AMCNutBoss"),World,{TEXT("Hit")});
        Add(TEXT("EditorOnly.CleanStain"),TEXT("Уборка · очищенное пятно"),TEXT("Cleaning"),TEXT("AMCMouthSurface / FMCCoffeeWipe"),World,{TEXT("Contact"),TEXT("Complete"),TEXT("Surface")});
        Add(TEXT("EditorOnly.CleanArenaTooth"),TEXT("Уход · очистка большого зуба"),TEXT("ToothCare"),TEXT("UMCToothCareFeedbackComponent / AMCArenaTooth"),World,{TEXT("Contact"),TEXT("Complete")});
        Add(TEXT("EditorOnly.RepairArenaTooth"),TEXT("Уход · восстановление большого зуба"),TEXT("ToothCare"),TEXT("UMCToothCareFeedbackComponent / UMCToothStatusComponent"),World,{TEXT("Contact"),TEXT("Complete")});
        auto& Piano=Add(TEXT("World.ArenaTooth.Piano"),TEXT("Большой зуб · фортепианный отклик"),TEXT("World"),TEXT("AMCArenaTooth::NotifyPianoLanding / OnRep_Piano"),World,{TEXT("Hit")});
        Piano.Description=TEXT("Родное физическое нажатие и нота; исходных частиц нет. Hit запускает добавленные Niagara/SFX после настоящего принятого приземления. Физика, клавиша, MIDI и высота исходной ноты принадлежат механике.");
        FMCVFXOriginalSFXContract PianoAudio;PianoAudio.ID=TEXT("Piano.NativeNote");
        PianoAudio.Label=FText::FromString(TEXT("Родная нота C4–C7 · фактическую октаву выбирает клавиша; ресурс C4 представляет первый диапазон"));
        PianoAudio.Resource=FSoftObjectPath(TEXT("/Game/Audio/S_ToothPianoC4.S_ToothPianoC4"));PianoAudio.bSuppressionWired=true;
        Piano.OriginalAudio.Add(TEXT("Hit"),MoveTemp(PianoAudio));
        Add(TEXT("World.Ice"),TEXT("Ледяное событие"),TEXT("World"),TEXT("AMCIceEvent / AMCIceEventVFX"),World,{TEXT("Impact"),TEXT("FrozenFeet"),TEXT("BombWarning"),TEXT("CrystalCharge"),TEXT("CrystalTrail"),TEXT("WarmMotes"),TEXT("IcicleWarning"),TEXT("Charge"),TEXT("Wind"),TEXT("ColdThrone"),TEXT("ColdPhaseRelease"),TEXT("BombImpact"),TEXT("Arrival"),TEXT("CoreHurt"),TEXT("IcicleImpact"),TEXT("ZoneCrystalLand"),TEXT("ZoneCrystalHurt"),TEXT("ZoneCrystalBreak"),TEXT("FootFreeze"),TEXT("FootHurt"),TEXT("FootBreak"),TEXT("NovaRelease"),TEXT("Finish"),TEXT("WindSheetVortex"),TEXT("WindSheetAmbient"),TEXT("FrozenFeetShell"),TEXT("BombShell"),TEXT("CrystalShell")});
        Add(TEXT("World.Ice.Surface"),TEXT("Лёд · поверхность и безопасные зоны"),TEXT("World"),TEXT("AMCIceEvent"),World,{TEXT("Frost"),TEXT("WarmZone"),TEXT("NextZone"),TEXT("Nova")});
        Add(TEXT("World.FrostNova"),TEXT("Морозная волна"),TEXT("World"),TEXT("UMCFrostNovaVFXComponent / AMCHazardWave"),World,{TEXT("Wave"),TEXT("Burst")});
        Add(TEXT("World.MintCandy"),TEXT("Мятная конфета · волна и раскалывание"),TEXT("World"),TEXT("AMCSmallMintCandy"),World,{TEXT("Wave"),TEXT("Burst"),TEXT("Shatter")});
        Add(TEXT("World.ColdCola"),TEXT("Холодная кола"),TEXT("World"),TEXT("AMCColdColaEvent / AMCIceBlock"),World,{TEXT("Frost"),TEXT("Shatter")});
        Sources.FindChecked(TEXT("World.ColdCola")).Description+=TEXT(" Shatter — исходная редактируемая NS_IceShatter. Frost сохраняет исходный общий MPC и физический лёд; дополнительные слои независимы. Превью проверяет настоящую поломку блока, полный климат этим сценарием не подтверждается.");
        Add(TEXT("World.CoffeeFlood"),TEXT("Кофе · поток"),TEXT("World"),TEXT("AMCCoffeeFlood"),World,{TEXT("Pour"),TEXT("Flow"),TEXT("Drain")});
        Add(TEXT("World.CoffeeRiver"),TEXT("Кофе · река"),TEXT("World"),TEXT("AMCCoffeeFlood"),World,{TEXT("Flow"),TEXT("Impact"),TEXT("Accents")});
        Add(TEXT("World.CataclysmStorm"),TEXT("Катаклизм · шторм"),TEXT("World"),TEXT("AMCCoffeeFlood / AMCCataclysmEvent"),World,{TEXT("Wave"),TEXT("Spray"),TEXT("Impact")});
        Add(TEXT("World.Cataclysm.Jelly"),TEXT("Катаклизм · желе"),TEXT("World"),TEXT("AMCCataclysmJelly"),World,{TEXT("Trail"),TEXT("Impact")});
        Add(TEXT("World.Cataclysm.Raft"),TEXT("Катаклизм · плот"),TEXT("World"),TEXT("AMCCataclysmRaft"),World,{TEXT("Cue"),TEXT("Impact")});
        Add(TEXT("World.Cataclysm.Debris"),TEXT("Катаклизм · обломки"),TEXT("World"),TEXT("AMCCataclysmDebris"),World,{TEXT("Impact")});
        Add(TEXT("World.FogBrawl"),TEXT("Туман · поиск и удары"),TEXT("World"),TEXT("UMCFogBrawlPresentationComponent / AMCFogBrawlEvent"),World,{TEXT("Smoke"),TEXT("Search"),TEXT("Beacon"),TEXT("Windup"),TEXT("Impact"),TEXT("Final")});
        Add(TEXT("World.Throat.Suction"),TEXT("Глотка · всасывание"),TEXT("World"),TEXT("AMCThroat / AMCThroatVortex"),World,{TEXT("Start"),TEXT("Loop"),TEXT("Intake"),TEXT("End")});
        Add(TEXT("World.FirePatch"),TEXT("Огонь · опасный участок"),TEXT("World"),TEXT("AMCFirePatch"),World,{TEXT("Loop"),TEXT("Contact"),TEXT("Steam")});
        Add(TEXT("Food.Break"),TEXT("Еда · настоящие осколки и пыль"),TEXT("Food"),TEXT("AMCFoodActor / AMCReactionVFX"),World,{TEXT("BreakArt"),TEXT("BreakDust")});
        auto& AutoMouth=Add(TEXT("Food.AutoMouth"),TEXT("Еда · крошки в глотку / наружу"),TEXT("Food"),TEXT("AMCFoodActor::TryConsumeCutPortion / AMCGameState::FoodCutIntakePresentation"),UMCVFXPresentationLibrary::MouthLibraryPath(),{TEXT("Consume"),TEXT("Discard")});
        AutoMouth.Description=TEXT("Настоящий принятый ножом маленький кусочек превращается в редактируемые Niagara-крошки. Consume подаёт свежую еду в глотку; Discard выводит испорченную через сохранённый передний выход. У событий отдельные слоты. Начало, конечную точку, цвет и размер задаёт механика; частицы не владеют зачётом или физикой. Старый перенос сохранён за переключателем еды.");
        auto& NutThrow=Add(TEXT("Food.NutThrow"),TEXT("Орех · раскалывание при броске в босса"),TEXT("Food"),TEXT("AMCFoodActor::UpdateNutFragmentThrowImpact / AMCGameState::NutThrowPresentation"),UMCVFXPresentationLibrary::MouthLibraryPath(),{TEXT("BossImpact")});
        NutThrow.Description=TEXT("Подтверждённый бросок ореха в босса. Точку удара, нормаль и размер задаёт физический контакт. Штатный резолвер применяет урон по щиту или здоровью, после чего орех расходуется, а Niagara и SFX воспроизводят раскалывание.");
        Add(TEXT("Food.Spoiled"),TEXT("Еда · запах испорченного продукта"),TEXT("Food"),TEXT("AMCFoodActor::UpdateSmell / AMCReactionVFX"),World,{TEXT("Loop")});
        Add(TEXT("Food.Spicy"),TEXT("Перец · принятый взрыв"),TEXT("Food"),TEXT("AMCFoodActor::Detonate / AMCReactionVFX"),World,{TEXT("DetonateArt")});
        Add(TEXT("World.Vomit"),TEXT("Выброс"),TEXT("World"),TEXT("AMCVomitBurst"),World,{TEXT("Burst")});
        Add(TEXT("World.Yawn"),TEXT("Зевок · ветер"),TEXT("World"),TEXT("AMCYawn"),World,{TEXT("Wind")});
        Add(TEXT("World.Toothpick"),TEXT("Зубочистка · попадание"),TEXT("World"),TEXT("AMCToothpick"),World,{TEXT("Impact")});
        Add(TEXT("World.Calculus"),TEXT("Зубной камень · сколы"),TEXT("World"),TEXT("UMCToothCalculusComponent"),World,{TEXT("Chip"),TEXT("Break")});
        Add(TEXT("World.DamageUlcer"),TEXT("Язва · волна"),TEXT("World"),TEXT("AMCDamageUlcer"),World,{TEXT("Wave")});
        Add(TEXT("Reward.Chest"),TEXT("Сундук · прибытие и получение"),TEXT("Rewards"),TEXT("AMCRewardChest"),World,{TEXT("Arrival"),TEXT("Open"),TEXT("Claim")});
        Add(TEXT("Reward.StageBooster"),TEXT("Награда этапа"),TEXT("Rewards"),TEXT("AMCRewardChest / AMCSingleDayDirector"),World,{TEXT("Spit"),TEXT("Arrival"),TEXT("Open"),TEXT("Claim")});
        Add(TEXT("Reward.PerkPickup"),TEXT("Подбор усиления"),TEXT("Rewards"),TEXT("AMCPerkPickup"),World,{TEXT("Arrival"),TEXT("Claim")});
        Add(TEXT("Reward.TaskCoins"),TEXT("Монеты за задачу"),TEXT("Rewards"),TEXT("UMCTaskCoinPresentationComponent"),World,{TEXT("Reward")});
        Add(TEXT("Reward.TaskComplete"),TEXT("Задача · завершение"),TEXT("Rewards"),TEXT("AMCTaskActor"),World,{TEXT("Complete")});
        Add(TEXT("Hub.Fishing.Cast"),TEXT("Рыбалка · заброс и улов"),TEXT("Hub"),TEXT("UMCFishingRodComponent"),Hub,{TEXT("Cast"),TEXT("Line"),TEXT("Ripple"),TEXT("Catch")});
        Add(TEXT("EditorOnly.FishingCatch"),TEXT("Награда · улов рыбы"),TEXT("Hub"),TEXT("UMCFishingRewardWidget / MCPlayerFishingFeedback"),Hub,{TEXT("Catch")});
        auto& Grill=Add(TEXT("Hub.Grill"),TEXT("Слот-машина · рыба, прокрутка и приз"),TEXT("Hub"),
            TEXT("AMCHubGrillStation / UMCHubGrillPresentationComponent / UMCHubGrillHologramWidget"),Hub,
            {TEXT("Intake"),TEXT("Spin"),TEXT("Result"),TEXT("RareWin")});
        Grill.Description=TEXT("В ожидании проекция показывает зацикленную ленту каталога и зубика справа. Intake показывает приём рыбы. Spin и Result отображают принятый бросок на неподвижной проекции над корпусом автомата. Лента рисуется в Slate из данных реального броска.\n")
            TEXT("RareWin показывает принятый эпический или легендарный предмет. Зубики выезжают с двух сторон поверх карточки на проекции Slate. По бокам корпуса автомата вспыхивают звёзды в цвете редкости и разлетается конфетти. Празднование запускается вместе с фанфарой и выездом у получателя.\n")
            TEXT("Виджет /Game/Gameplay/UI/WBP_GrillHologram\n")
            TEXT("Материал проекции /Game/Gameplay/Hub/Economy/M_CaseHologramProjection\n")
            TEXT("Атлас реакции /Game/Gameplay/Hub/Economy/T_CaseRareToothReaction\n")
            TEXT("Вспышка и звёзды /Game/Gameplay/Hub/Economy/VFX/NS_CaseRareCardSparks\n")
            TEXT("Конфетти /Game/Gameplay/Hub/Economy/VFX/NS_CaseRareWinConfetti\n")
            TEXT("Фанфара /Game/Gameplay/Hub/Economy/SFX/SW_CaseRareFanfare\n")
            TEXT("Контекстный просмотр недоступен, пока в Composer нет изолированного сценария реального автомата с временным профилем.");
        FMCVFXOriginalSFXContract GrillFanfare;
        GrillFanfare.ID=TEXT("RareWin.Original");GrillFanfare.Label=FText::FromString(TEXT("Исходный звук · фанфара ценного приза"));
        GrillFanfare.Resource=FSoftObjectPath(TEXT("/Game/Gameplay/Hub/Economy/SFX/SW_CaseRareFanfare.SW_CaseRareFanfare"));
        GrillFanfare.bSuppressionWired=true;
        Grill.OriginalAudio.Add(TEXT("RareWin"),MoveTemp(GrillFanfare));
        Add(TEXT("Hub.ReadyZone"),TEXT("Хаб · зона готовности"),TEXT("Hub"),TEXT("AMCHubReadyZone"),Hub,{TEXT("Active"),TEXT("Ready")});
        Add(TEXT("Hub.DeliveryZone"),TEXT("Хаб · зона доставки"),TEXT("Hub"),TEXT("UMCDeliveryZoneVisualComponent"),Hub,{TEXT("Active"),TEXT("Complete")});
        Add(TEXT("Hub.Basketball"),TEXT("Хаб · баскетбол"),TEXT("Hub"),TEXT("AMCHubBasketball"),Hub,{TEXT("Contact"),TEXT("Score")});
        Add(TEXT("UI.ShiftFeedback"),TEXT("Смена · начало и итог"),TEXT("Interface"),TEXT("UMCPrototypeWidget / AMCPlayerController"),UI,{TEXT("Start"),TEXT("Win"),TEXT("Lose")});
        Add(TEXT("UI.TutorialFeedback"),TEXT("Обучение · подтверждённый итог"),TEXT("Interface"),TEXT("AMCTutorialDirector / UMCTutorialWidget"),UI,{TEXT("Complete"),TEXT("Prompt")});
        auto Palette=[&](const TCHAR* Source,const TCHAR* Event,const TCHAR* Selector)
        {
            auto& Contract=Sources.FindChecked(FName(Source));FMCVFXOriginalSFXContract Audio;
            Audio.ID=FName(*(FString(Event)+TEXT(".Original")));Audio.Label=FText::FromString(TEXT("Исходный звук · ")+FString(Selector));
            Audio.Resource=FSoftObjectPath(TEXT("/Game/Data/DA_MouthSounds.DA_MouthSounds"));Audio.PaletteEvent=FName(Selector);
            Audio.bSuppressionWired=true;Contract.OriginalAudio.Add(FName(Event),MoveTemp(Audio));
        };
        Palette(TEXT("Tool.Knife"),TEXT("Swing"),TEXT("KnifeSwing"));
        Palette(TEXT("Tool.Knife"),TEXT("Hit"),TEXT("KnifeHit"));
        Palette(TEXT("Tool.Pickaxe"),TEXT("Swing"),TEXT("PickaxeSwing"));
        Palette(TEXT("Tool.Pickaxe"),TEXT("Hit"),TEXT("PickaxeHit"));
        Palette(TEXT("Tool.Brush"),TEXT("Contact"),TEXT("Brush"));
        Palette(TEXT("Tool.Spray"),TEXT("Contact"),TEXT("Brush"));
        Palette(TEXT("Character.Movement"),TEXT("Step"),TEXT("Step"));
        Palette(TEXT("Character.Movement"),TEXT("Land"),TEXT("Jump"));
        // Saved Jump event overrides remain a landing alias, never a takeoff cue.
        Palette(TEXT("Character.Movement"),TEXT("Jump"),TEXT("Jump"));
        Palette(TEXT("Character.Movement"),TEXT("Fall"),TEXT("Fall"));
        Palette(TEXT("Character.Movement"),TEXT("StandUp"),TEXT("StandUp"));
        Palette(TEXT("Character.Movement"),TEXT("Pull"),TEXT("Pull"));
        Palette(TEXT("UI.ShiftFeedback"),TEXT("Start"),TEXT("DayStart"));
        Palette(TEXT("UI.ShiftFeedback"),TEXT("Win"),TEXT("Win"));
        Palette(TEXT("UI.ShiftFeedback"),TEXT("Lose"),TEXT("Lose"));
        Palette(TEXT("Reward.TaskComplete"),TEXT("Complete"),TEXT("Complete"));
        Sources.FindChecked(TEXT("Reward.Chest")).Description+=TEXT(" Подключены исходные 12 искр Arrival/Claim и принятое Open. Полный сценарий выбора награды в песочнице пока недоступен.");
        Sources.FindChecked(TEXT("Reward.StageBooster")).Description+=TEXT(" Spit запускается при выбросе сундука из глотки. В песочнице воспроизводятся сам выброс, полёт и приземление на язык. Открытие и выбор бустера доступны в игре.");
        for(const FName ID:{FName(TEXT("Reward.PerkPickup")),FName(TEXT("Reward.TaskComplete")),FName(TEXT("UI.ShiftFeedback"))})
            Sources.FindChecked(ID).Description+=TEXT(" Слоты подключены к принятым штатным событиям. Контекстуальный сценарий сейчас недоступен: полный поток награды/виджета в песочнице ещё не воспроизводится. Исходные физические предметы, подписи и UI остаются штатными; отдельный исходный particle effect не заявляется.");
        Sources.FindChecked(TEXT("Character.Movement")).Description+=TEXT(" Land — фактическое приземление; Jump сохранён как совместимый псевдоним этого события и исходного ключа палитры. При настроенном Land он имеет приоритет, одновременно оба события не запускаются.");
        FMCVFXOriginalSFXContract Coins;
        Coins.ID=TEXT("Reward.Original");Coins.Label=FText::FromString(TEXT("Исходный звук · монеты за задачу"));
        Coins.Resource=FSoftObjectPath(TEXT("/Game/Audio/Rewards/S_TaskCoins.S_TaskCoins"));Coins.bSuppressionWired=true;
        Sources.FindChecked(TEXT("Reward.TaskCoins")).OriginalAudio.Add(TEXT("Reward"),MoveTemp(Coins));
        auto ThroatAudio=[&](const TCHAR* Event,const TCHAR* Path){FMCVFXOriginalSFXContract A;
            A.ID=FName(*(FString(Event)+TEXT(".Original")));A.Label=FText::FromString(TEXT("Исходный звук глотки"));
            A.Resource=FSoftObjectPath(Path);A.bSuppressionWired=true;Sources.FindChecked(TEXT("World.Throat.Suction")).OriginalAudio.Add(Event,MoveTemp(A));};
        ThroatAudio(TEXT("Start"),TEXT("/Game/Audio/S_Whoosh.S_Whoosh"));
        ThroatAudio(TEXT("Loop"),TEXT("/Game/Audio/S_FoodSuctionWind.S_FoodSuctionWind"));
        FMCVFXOriginalSFXContract TutorialAudio;TutorialAudio.ID=TEXT("Complete.FoamPartyOriginal");
        TutorialAudio.Label=FText::FromString(TEXT("Родной итоговый звук FoamParty"));
        TutorialAudio.Resource=FSoftObjectPath(TEXT("/Game/Audio/S_Win.S_Win"));TutorialAudio.bSuppressionWired=true;
        Sources.FindChecked(TEXT("UI.TutorialFeedback")).OriginalAudio.Add(TEXT("Complete"),MoveTemp(TutorialAudio));
        Sources.FindChecked(TEXT("UI.TutorialFeedback")).Description+=TEXT(" Complete: исходный S_Win и добавленные SFX после настоящего FoamParty; исходная пена остаётся штатной, VFX-lane этим аудио-hook не мигрирована. Полный учебный сценарий пока недоступен.");
        return Sources;
    }();
    return Result;
}
}

void MCVFXSourceContracts::Register(FMCVFXSourceContract Contract)
{if(!Contract.SourceID.IsNone()&&!Contract.Events.IsEmpty())Definitions().Add(Contract.SourceID,MoveTemp(Contract));}
const FMCVFXSourceContract* MCVFXSourceContracts::Find(FName SourceID){return Definitions().Find(SourceID);}
TArray<FMCVFXSourceContract> MCVFXSourceContracts::GetAll()
{TArray<FMCVFXSourceContract> Out;Definitions().GenerateValueArray(Out);Out.Sort([](const auto& A,const auto& B){return A.SourceID.LexicalLess(B.SourceID);});return Out;}
FMCVFXOriginalAudioStatus MCVFXSourceContracts::GetOriginalAudioStatus(FName SourceID,FName Event)
{
    FMCVFXOriginalAudioStatus Result;
    if(IsOriginalSFXConfirmedSilent(SourceID,Event,&Result.Evidence)) {
        Result.Knowledge=EMCVFXOriginalAudioKnowledge::ConfirmedAbsent;return Result;
    }
    const auto* Definition=Find(SourceID);const auto* Audio=Definition?Definition->OriginalAudio.Find(Event):nullptr;
    if(Audio&&!Audio->Resource.IsNull()) {
        Result.Knowledge=EMCVFXOriginalAudioKnowledge::DeclaredSource;Result.bIndependentNativeGate=Audio->bSuppressionWired;
        Result.Evidence=Audio->bSuppressionWired
            ?TEXT("Указан исходный источник звука и его отдельное управление. Сохранённое назначение и слышимое воспроизведение проверяются отдельно.")
            :TEXT("Исходный источник звука указан, но его независимое отключение ещё не подключено. Можно добавить свои звуковые слои.");
    } else Result.Evidence=TEXT("Исходный звук этого события пока не проверен полностью. Его отсутствие не подтверждено. Можно добавить свои звуковые слои; замена и отключение оригинала пока недоступны.");
    return Result;
}
bool MCVFXSourceContracts::CanSuppressOriginalSFX(FName SourceID,FName Event)
{
    const auto Status=GetOriginalAudioStatus(SourceID,Event);
    return Status.Knowledge==EMCVFXOriginalAudioKnowledge::ConfirmedAbsent
        ||(Status.Knowledge==EMCVFXOriginalAudioKnowledge::DeclaredSource&&Status.bIndependentNativeGate);
}
bool MCVFXSourceContracts::IsOriginalSFXConfirmedSilent(FName SourceID,FName Event,FString* Evidence)
{
    if(Evidence)Evidence->Reset();
    if(SourceID==MCNutVFX::ChainSource()) {
        const auto* Chain=MCNutVFX::Find(SourceID);
        const bool bSilent=Chain && Chain->Events.Contains(Event) && MCNutVFX::ChainDefaultSound(Event).IsNull();
        if(bSilent && Evidence)*Evidence=TEXT("У принятого события цепи нет исходного звукового слоя.");
        return bSilent;
    }
    // Exact native branches, not a source-wide declaration. Knight Slash/Slam,
    // Fireball and donor Niagara audio renderers are deliberately unaudited.
    const bool bConfirmed=(SourceID==TEXT("Boss.NutKnight.Melee")&&Event==TEXT("Tell"))
        ||(SourceID==TEXT("Boss.NutMage.Melee")&&(Event==TEXT("Tell")||Event==TEXT("Slash")));
    if(bConfirmed&&Evidence)*Evidence=TEXT("2026-11-01: MCNutCombatEffect::Present MeleeTell/MageMeleeSlash; native sound callsite audit plus complete saved notify/dependency audit of 11 selected clips (SavedNutAudioAudit, 06:33:07Z). No original sound source in these exact branches; other events remain unknown.");
    return bConfirmed;
}
FSoftObjectPath MCVFXSourceContracts::WorldLibraryPath(){return FSoftObjectPath(TEXT("/Game/Gameplay/VFX/World/DA_WorldPresentation.DA_WorldPresentation"));}
FSoftObjectPath MCVFXSourceContracts::ToolLibraryPath(){return FSoftObjectPath(TEXT("/Game/Gameplay/VFX/Tools/DA_ToolPresentation.DA_ToolPresentation"));}
FSoftObjectPath MCVFXSourceContracts::HubLibraryPath(){return FSoftObjectPath(TEXT("/Game/Gameplay/VFX/Hub/DA_HubPresentation.DA_HubPresentation"));}
FSoftObjectPath MCVFXSourceContracts::InterfaceLibraryPath(){return FSoftObjectPath(TEXT("/Game/Gameplay/VFX/UI/DA_InterfacePresentation.DA_InterfacePresentation"));}
