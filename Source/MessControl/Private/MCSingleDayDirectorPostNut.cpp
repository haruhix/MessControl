#include "MCSingleDayDirector.h"
#include "MCCataclysmEvent.h"
#include "MCNutRainEvent.h"
#include "MCGameDirector.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCDayPlan.h"
#include "MCFoodActor.h"
#include "MCFoodDirectorHooks.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace MCSingleDayDirectorPrivate
{
    int32 CountDirtyAvailableArenaTeeth(UWorld* World);
    int32 CountDirtyFloorSurfaces(UWorld* World);
}

void AMCSingleDayDirector::BeginPostNutMeal()
{
    if(IsValid(NutEvent)) {NutEvent->Stop();NutEvent->Destroy();} NutEvent=nullptr;
    if(IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();} Interlude=nullptr;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if(IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    PostNutMealBatch=4000000+int32(GetUniqueID()&0x000FFFFF);
    MealRandom.Initialize((GS?GS->RunSeed:41)^0x4E55544D);
    MealPortionCount=Settings->AfterNutMealPortions.Num();
    MealServices=GetWorld()->SpawnActor<AMCDayDirector>();
    if(!MealServices) {Fail(TEXT("Не удалось начать приём пищи после орехового события."));return;}
    MealServices->SetOwner(this);MealServices->InitializeEventServices(Mechanics,GS?GS->RunSeed:41);
    InterludeEndsAt=StageEndsAt=0;
    Record(TEXT("Ореховый бой завершён: второй Relax, порции 5 → 8 → 9; без кофе и случайных опасных событий"));
    BeginPostNutPortion(0);
}

void AMCSingleDayDirector::BeginPostNutPortion(int32 Portion)
{
    if(!Settings || !Settings->AfterNutMealPortions.IsValidIndex(Portion)) {Fail(TEXT("Не найдены настройки следующей порции второго Relax."));return;}
    Stage=EMCSingleDayStage::PostNutMeal;StageEndsAt=0;
    MealPortionNumber=Portion+1;MealPortionTarget=Settings->AfterNutMealPortions[Portion];MealPortionSpawned=MealAttempts=0;
    LastMealGateState.Reset();NextMealDiagnosticAt=0;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    NextMealDropAt=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    Record(FString::Printf(TEXT("Второй Relax: порция %d/%d, целых продуктов %d"),MealPortionNumber,MealPortionCount,MealPortionTarget));
    Publish(TEXT("ПЕРЕДЫШКА ПОСЛЕ ОРЕХОВ"),FString::Printf(TEXT("Порция %d/%d: обработайте %d продуктов и отправьте фрагменты. Щёткой очищайте язык и зубы."),
        MealPortionNumber,MealPortionCount,MealPortionTarget),MealPortionTarget,MealPortionTarget);
    ForceNetUpdate();
}

void AMCSingleDayDirector::TickPostNutMeal()
{
    auto* GS=GetWorld()->GetGameState<AMCGameState>();if(!GS) return;
    const double Time=GS->GetServerWorldTimeSeconds();
    const auto CurrentFood=MCMeasureFoodPipeline(GetWorld());
    int32 Workers=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(It->GetController() && It->Status && It->Status->IsAlive()) ++Workers;
    const int32 WholeLimit=FMath::Clamp(6+2*(FMath::Max(1,Workers)-1),6,12);
    if(MealPortionSpawned<MealPortionTarget && Time>=NextMealDropAt && CurrentFood.Whole<WholeLimit
        && CurrentFood.Fragments<48 && !CurrentFood.bThroatBusy) {
        const FName Row=ChooseOpeningFood();++MealAttempts;
        if(auto* Food=MCSpawnDirectedFoodEntry(GetWorld(),Mechanics,Row,PostNutMealBatch,MealRandom,MealServices)) {
            Food->SetOwner(this);++MealPortionSpawned;ForceNetUpdate();
        }
        else if(MealAttempts>=MealPortionTarget*6) {Fail(TEXT("Не удалось подать еду второго Relax. Проверьте меню и доступную поверхность языка."));return;}
        NextMealDropAt=Time+Settings->AfterNutMealDropSeconds;
    }
    const bool Final=MealPortionNumber>=MealPortionCount;
    const auto Food=MCMeasureFoodPipeline(GetWorld(),Final?INDEX_NONE:PostNutMealBatch);
    const int32 Dirt=MCSingleDayDirectorPrivate::CountDirtyFloorSurfaces(GetWorld());
    const int32 Teeth=MCSingleDayDirectorPrivate::CountDirtyAvailableArenaTeeth(GetWorld());
    if(MealPortionSpawned>=MealPortionTarget && Food.OutstandingActors==0 && Food.UnresolvedHazards==0) {
        if(Dirt==0 && Teeth==0) {
            BeginStageReward(FName(*FString::Printf(TEXT("PostNutPortion_%d"),MealPortionNumber)),
                Final?EMCSingleDayRewardContinuation::PostNutMint:EMCSingleDayRewardContinuation::PostNutPortion);
            return;
        }
    }
    const int32 Pending=FMath::Max(0,MealPortionTarget-MealPortionSpawned);
    const int32 Left=Pending+Food.OutstandingActors+Food.UnresolvedHazards+Dirt+Teeth;
    Publish(TEXT("ПЕРЕДЫШКА ПОСЛЕ ОРЕХОВ"),FString::Printf(
        TEXT("Порция %d/%d · подано %d/%d. Еда: %d · пятна: %d · грязные зубы: %d. Q — закинуть фрагмент в глотку.%s"),
        MealPortionNumber,MealPortionCount,MealPortionSpawned,MealPortionTarget,Food.OutstandingActors,Dirt,Teeth,
        TEXT(" Очистите язык и зубы: после завершённой порции появится общий сундук с бустером.")),
        Left,FMath::Max(GS->TasksTotal,Left));
    if(Time>=NextMealDiagnosticAt) {
        NextMealDiagnosticAt=Time+10;
        UE_LOG(LogTemp,Display,TEXT("MC_POST_NUT_MEAL portion=%d/%d served=%d/%d food=%d hazards=%d floor=%d teeth=%d"),
            MealPortionNumber,MealPortionCount,MealPortionSpawned,MealPortionTarget,Food.OutstandingActors,Food.UnresolvedHazards,Dirt,Teeth);
    }
}

void AMCSingleDayDirector::BeginSmallMint()
{
    Stage=EMCSingleDayStage::PostNutMint;StageEndsAt=0;
    SmallMint=GetWorld()->SpawnActor<AMCSmallMintCandy>();
    AMCTongue* Tongue=nullptr;for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {Tongue=*It;break;}
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(!SmallMint) {Fail(TEXT("Не удалось создать маленькую мятную конфету."));return;}
    SmallMint->SetOwner(this);
    if(!SmallMint->Start(Tongue,Settings->SmallMint,(GS?GS->RunSeed:41)^0x4D494E54)) {Fail(TEXT("Не удалось разместить мятную конфету на доступном языке."));return;}
    Record(TEXT("Маленькая мятная конфета: первая холодная волна через 3–5 секунд после приземления, повторяется до разрушения"));
    Publish(TEXT("МЯТНАЯ КОНФЕТА"),TEXT("Разбейте конфету киркой. Перепрыгивайте холодные волны; киркой можно освободить свои ноги и помочь союзнику. Волна не наносит урон."),
        FMath::CeilToInt(SmallMint->Health),FMath::CeilToInt(SmallMint->MaxHealth));
    ForceNetUpdate();
}

void AMCSingleDayDirector::TickSmallMint()
{
    if(!IsValid(SmallMint) || SmallMint->HasFailed()) {Fail(TEXT("Мятная конфета исчезла или не смогла выпустить волну."));return;}
    if(SmallMint->IsComplete()) {
        SmallMint->Destroy();SmallMint=nullptr;
        Record(TEXT("Мятная конфета разбита: второй Relax завершён, начинается событие с желе"));
        BeginStageReward(TEXT("PostNutMint"),EMCSingleDayRewardContinuation::NextKeyEvent);return;
    }
    Publish(TEXT("МЯТНАЯ КОНФЕТА"),TEXT("Разбейте конфету киркой. Перепрыгивайте холодные волны; киркой можно освободить свои ноги и помочь союзнику. Волна не наносит урон."),
        FMath::CeilToInt(SmallMint->Health),FMath::CeilToInt(SmallMint->MaxHealth));
}

void AMCSingleDayDirector::BeginJellies()
{
    if(IsValid(Interlude)) {Interlude->Stop();Interlude->Destroy();} Interlude=nullptr;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) {
        if(IsValid(Mode->GameDirector)) {Mode->GameDirector->Stop();Mode->GameDirector->Destroy();}
        Mode->GameDirector=nullptr;
    }
    Stage=EMCSingleDayStage::Jellies;StageEndsAt=0;
    JellyEvent=GetWorld()->SpawnActor<AMCCataclysmEvent>();
    if(!JellyEvent) {Fail(TEXT("Не удалось создать событие с желе."));return;}
    JellyEvent->SetOwner(this);
    if(!JellyEvent->Start(Mechanics,EMCCataclysmVariant::Jellies)) {Fail(TEXT("Не удалось запустить событие с желе."));return;}
    Record(FString::Printf(TEXT("Ключевое событие %d: грязное желе; обычная поддержка приостановлена"),KeyEventIndex+1));
    Publish(JellyEvent->Title(),JellyEvent->Instruction());ForceNetUpdate();
}

void AMCSingleDayDirector::TickJellies()
{
    if(!IsValid(JellyEvent) || JellyEvent->bFailed) {Fail(TEXT("Событие с желе не смогло продолжиться."));return;}
    if(JellyEvent->IsComplete()) {
        JellyEvent->Destroy();JellyEvent=nullptr;BeginDirector();return;
    }
    const int32 Left=JellyEvent->JelliesLeft+JellyEvent->DirtLeft;
    Publish(JellyEvent->Title(),JellyEvent->Instruction(),Left,FMath::Max(Left,JellyEvent->DirtTotal+JellyEvent->JelliesLeft));
}
