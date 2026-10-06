#include "MCTutorialDirector.h"
#include "MCGameState.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCArenaTooth.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCCoffeeFlood.h"
#include "MCDayPlan.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "MCTutorial"

AMCTutorialDirector::AMCTutorialDirector()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true;
    SetNetUpdateFrequency(10);
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("TutorialRoot")); SetRootComponent(Root);
    GoalLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("YourTutorialTarget")); GoalLabel->SetupAttachment(Root);
    GoalLabel->SetHorizontalAlignment(EHTA_Center); GoalLabel->SetWorldSize(20);
    GoalLabel->SetTextRenderColor(FColor(115,245,255)); GoalLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GoalLabel->SetVisibility(false);
}

AMCTutorialDirector* AMCTutorialDirector::Find(UWorld* World)
{
    if (!World || !IsSafeTutorial(World)) return nullptr;
    for (TActorIterator<AMCTutorialDirector> It(World);It;++It) if (It->Stage!=EMCTutorialStage::Finished) return *It;
    return nullptr;
}

bool AMCTutorialDirector::IsSafeTutorial(UWorld* World)
{
    const auto* GS=World?World->GetGameState<AMCGameState>():nullptr;
    return GS && GS->bTutorialActive;
}

bool AMCTutorialDirector::IsTutorialTarget(const AActor* Actor)
{
    if (!IsValid(Actor)) return false;
    if (const auto* Food=Cast<AMCFoodActor>(Actor)) return Food->Batch==TutorialFoodBatch;
    if (const auto* Patch=Cast<AMCMouthSurface>(Actor)) return Patch->Batch==TutorialFoodBatch;
    const auto* Director=Find(Actor->GetWorld());
    if (Director) for (const auto& Player:Director->Players) if (Player.GoalTarget==Actor) return true;
    return false;
}

double AMCTutorialDirector::Now() const
{
    const auto* GS=GetWorld()->GetGameState();
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}

const FMCTutorialPlayerProgress* AMCTutorialDirector::GetPlayerProgress(const APlayerState* Player) const
{
    return Players.FindByPredicate([Player](const auto& Progress){ return Progress.PlayerState==Player; });
}

int32 AMCTutorialDirector::GetCompletedPlayers() const
{
    if (Stage==EMCTutorialStage::Complete)
    {
        int32 Count=0; for (const auto& Player:Players) Count+=Player.bReady; return Count;
    }
    if (Stage==EMCTutorialStage::Loading)
    {
        int32 Count=0; for (const auto& Player:Players) Count+=Player.bLoaded; return Count;
    }
    return FMCTutorialProgressRules::CompletedPlayers(Players);
}

void AMCTutorialDirector::Start(UMCDayPlan* Plan)
{
    if (!HasAuthority() || bStarted) return;
    bStarted=true; bRestored=false; Random.Initialize(7001);
    Settings=Plan?DuplicateObject<UMCDayPlan>(Plan,this):NewObject<UMCDayPlan>(this);
    Settings->Sanitize(); Settings->FloodHeight=120; Settings->FlowAcceleration=230;
    for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
    {
        FToothSnapshot Snapshot; Snapshot.Tooth=*It; Snapshot.Status=It->Status->State;
        Snapshot.GrimeMask=It->GrimeMask;
        if (It->Calculus) Snapshot.Calculus=It->Calculus->State;
        ToothSnapshots.Add(MoveTemp(Snapshot));
    }
    bool HasFrontExit=false;
    for (TActorIterator<AMCFoodDisposal> It(GetWorld());It;++It) if (It->bBrushBin) { HasFrontExit=true; break; }
    if (!HasFrontExit)
    {
        // Ordinary day one creates this marker in DayDirector::Start; an isolated tutorial needs its own equivalent.
        auto* Exit=GetWorld()->SpawnActor<AMCFoodDisposal>(Settings->ArenaCenter+FVector(-1110,0,140),FRotator::ZeroRotator);
        if (Exit)
        {
            Exit->bBrushBin=true; Exit->Volume->SetBoxExtent(FVector(70,680,240));
            Exit->Label->SetRelativeLocation(FVector(80,0,0)); Exit->ForceNetUpdate(); SpawnedActors.Add(Exit);
        }
    }
    SyncPlayers(); EnterStage(EMCTutorialStage::Loading);
}

void AMCTutorialDirector::SyncPlayers()
{
    TArray<AMCPlayerState*> Active;
    for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if (const auto* PC=It->Get()) if (auto* PS=PC->GetPlayerState<AMCPlayerState>()) Active.AddUnique(PS);
    bool Changed=false;
    for (int32 I=Players.Num()-1;I>=0;--I)
        if (!IsValid(Players[I].PlayerState) || !Active.Contains(Players[I].PlayerState))
        { Players.RemoveAt(I); Changed=true; }
    for (auto* PS:Active)
    {
        if (!GetPlayerProgress(PS))
        {
            auto& Progress=Players.AddDefaulted_GetRef(); Progress.PlayerState=PS;
            Progress.StageRequired=FMCTutorialProgressRules::RequiredActions(Stage)!=0?1:0;
            Changed=true;
        }
        if (auto* Hero=Cast<AMCToothCharacter>(PS->GetPawn()))
        {
            if (!PlayerSnapshots.Contains(PS)) PlayerSnapshots.Add(PS,Hero->Status->State);
            if (!SafePositions.Contains(PS)) SafePositions.Add(PS,Hero->GetActorLocation());
        }
    }
    if (Changed) ForceNetUpdate();
}

void AMCTutorialDirector::SetLoaded(AMCPlayerState* Player)
{
    if (!HasAuthority() || !bStarted || !IsValid(Player) || Stage==EMCTutorialStage::Finished) return;
    SyncPlayers();
    if (auto* Progress=Players.FindByPredicate([Player](const auto& P){return P.PlayerState==Player;}))
    { Progress->bLoaded=true; ForceNetUpdate(); }
}

void AMCTutorialDirector::SetReady(AMCPlayerState* Player,bool bReady)
{
    if (!HasAuthority() || !bStarted || !IsValid(Player)) return;
    if (auto* Progress=Players.FindByPredicate([Player](const auto& P){return P.PlayerState==Player;}))
        if (FMCTutorialProgressRules::SetReady(*Progress,Stage,bReady)) ForceNetUpdate();
}

bool AMCTutorialDirector::TargetIsOwn(const FMCTutorialPlayerProgress& Player,const AActor* Target) const
{
    if (!IsValid(Target)) return false;
    return Target==Player.GoalTarget || (IsTutorialTarget(Target) && Target->GetOwner()==Player.PlayerState);
}

void AMCTutorialDirector::NotifyAction(AMCToothCharacter* Worker,EMCTutorialAction Action,AActor* Target)
{
    if (!HasAuthority() || !bStarted || !IsValid(Worker) || !Worker->HasAuthority() || Stage==EMCTutorialStage::Finished) return;
    auto* Player=Worker->GetPlayerState<AMCPlayerState>();
    auto* Progress=Players.FindByPredicate([Player](const auto& P){return P.PlayerState==Player;});
    if (!Progress || !Progress->bLoaded) return;
    if (Action==EMCTutorialAction::Anchored && Stage==EMCTutorialStage::CoffeeWaves)
    { Progress->CompletedActions|=FMCTutorialProgressRules::ActionBit(Action); ForceNetUpdate(); return; }
    if (!FMCTutorialProgressRules::AcceptsAction(Stage,Action) || !TargetIsOwn(*Progress,Target)) return;
    // Confirm completed gameplay state even when a caller accidentally reports a contact rather than completion.
    if (Action==EMCTutorialAction::BrushTooth || Action==EMCTutorialAction::BrushTongue)
    {
        const auto* Status=Target->FindComponentByClass<UMCToothStatusComponent>();
        if (!Status || Status->NeedsCare(true)) return;
    }
    if (Action==EMCTutorialAction::CalculusCleared)
    {
        const auto* Tooth=Cast<AMCArenaTooth>(Target);
        if (!Tooth || !Tooth->Calculus || Tooth->Calculus->HasCalculus()) return;
    }
    if (const auto* Food=Cast<AMCFoodActor>(Target))
    {
        if (Food->Batch!=TutorialFoodBatch) return;
        if (Action==EMCTutorialAction::FoodCut && (Food->bFragment || Food->Health>0)) return;
        if (Action==EMCTutorialAction::FoodDelivered && Food->IsWrongIngredient()) return;
        if (Action==EMCTutorialAction::SpoiledDiscarded && !Food->bSpoiled) return;
        if (Action==EMCTutorialAction::TrashDiscarded && Food->FoodData.Kind!=EMCFoodKind::ForeignObject) return;
    }
    if (FMCTutorialProgressRules::CreditAction(*Progress,Stage,Action,true))
    { Worker->NotifyTaskFeedback(true,Target->GetActorLocation()); ForceNetUpdate(); }
}

void AMCTutorialDirector::NotifyIncorrectSort(AMCToothCharacter* Worker,AActor* Target)
{
    if (!HasAuthority() || !IsValid(Worker) || !IsTutorialTarget(Target) || !bStarted) return;
    const auto* Food=Cast<AMCFoodActor>(Target);
    FairyLine=Food && !Food->IsWrongIngredient()
        ?LOCTEXT("FreshWrongExit","Свежее съедобное — назад, в глотку. Предмет вернулся: попробуй ещё раз!")
        :LOCTEXT("TrashWrongExit","Испорченное и мусор — вперёд, изо рта. Попробуй другой выход!");
    FeedbackEndsAt=Now()+5; ForceNetUpdate();
}

FVector AMCTutorialDirector::LessonPosition(const FMCTutorialPlayerProgress& Player,int32 Index) const
{
    FVector Point=Settings?Settings->ArenaCenter:FVector::ZeroVector;
    if (const auto* Hero=Player.PlayerState?Player.PlayerState->GetPawn():nullptr)
        Point=Hero->GetActorLocation()+Hero->GetActorForwardVector()*125;
    else Point+=FVector(-450+Index*160,-180+Index*95,150);
    auto OutsideDeliveryZones=[this](FVector Position)
    {
        for(TActorIterator<AMCFoodDisposal> Zone(GetWorld());Zone;++Zone)
            if(Zone->ContainsDeliveryPosition(Position+FVector(0,0,35))) return false;
        return true;
    };
    for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
    {
        FHitResult Hit;
        if (It->InteriorSurfacePoint(Point,65,Hit) && OutsideDeliveryZones(Hit.ImpactPoint)) return Hit.ImpactPoint;
        FRandomStream Placement(7001+Index*71);
        for(int32 Attempt=0;Attempt<32;++Attempt)
            if (It->RandomInteriorPoint(Placement,80,100,TConstArrayView<FVector>(),Hit) && OutsideDeliveryZones(Hit.ImpactPoint)) return Hit.ImpactPoint;
    }
    return Point;
}

AMCArenaTooth* AMCTutorialDirector::AssignTooth(const FMCTutorialPlayerProgress& Player)
{
    const auto* Pawn=Player.PlayerState?Player.PlayerState->GetPawn():nullptr;
    const FVector Position=Pawn?Pawn->GetActorLocation():FVector::ZeroVector;
    AMCArenaTooth* Best=nullptr; double Distance=MAX_dbl;
    for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
    {
        if (!It->IsAvailable()) continue;
        bool Assigned=false; for (const auto& P:Players) if (P.GoalTarget==*It) Assigned=true;
        const double D=FVector::DistSquared(Position,It->GetActorLocation());
        if (Assigned || D>=Distance) continue;
        Best=*It; Distance=D;
    }
    return Best;
}

AMCMouthSurface* AMCTutorialDirector::SpawnTonguePatch(const FMCTutorialPlayerProgress& Player)
{
    const int32 Index=Players.IndexOfByPredicate([&Player](const auto& P){return P.PlayerState==Player.PlayerState;});
    const FVector Point=LessonPosition(Player,Index);
    const FTransform Pose(Point+FVector(0,0,5));
    auto* Patch=GetWorld()->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose,Player.PlayerState,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Patch) return nullptr;
    Patch->Batch=TutorialFoodBatch; Patch->bRandomizeLiquidSize=false; Patch->LiquidHalfSize=35;
    Patch->FinishSpawning(Pose); Patch->Status->ApplyCoffee(.25f); Patch->ForceNetUpdate();
    SpawnedActors.Add(Patch); return Patch;
}

AMCFoodActor* AMCTutorialDirector::SpawnFood(FName RowName,FVector Position,AMCPlayerState* OwnerPlayer,bool bFragment,bool bSpoiled)
{
    FMCFoodRow Row; Row.Label=LOCTEXT("SoftFood","МЯГКАЯ ЕДА");
    if (Settings) if (auto* Menu=Settings->Menu.LoadSynchronous())
        if (const auto* Found=Menu->FindRow<FMCFoodRow>(RowName,TEXT("Day zero"))) Row=*Found;
    Row.Health=25; Row.Resistance=EMCFoodResistance::Soft; Row.Fragments=2; Row.SpoilSeconds=600;
    Row.Kind=EMCFoodKind::Food;
    const FTransform Pose(FRotator::ZeroRotator,Position);
    auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,OwnerPlayer?static_cast<AActor*>(OwnerPlayer):this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Food) return nullptr;
    Food->ConfigureItem(RowName,Row,Random,bFragment); Food->Batch=TutorialFoodBatch; Food->bSpoiled=bSpoiled;
    Food->SpoilAt=Now()+86400; Food->FinishSpawning(Pose); Food->ForceNetUpdate();
    SpawnedActors.Add(Food); return Food;
}

void AMCTutorialDirector::EnsureLessonTargets()
{
    if (FMCTutorialProgressRules::RequiredActions(Stage)==0) return;
    for (int32 I=0;I<Players.Num();++I)
    {
        auto& Player=Players[I];
        if (!Player.bLoaded || Player.StageProgress>=Player.StageRequired) continue;
        bool Missing=!IsValid(Player.GoalTarget);
        if (auto* Food=Cast<AMCFoodActor>(Player.GoalTarget)) Missing=Food->IsDisposed() || Food->GetActorLocation().Z<-1200;
        if (auto* Patch=Cast<AMCMouthSurface>(Player.GoalTarget)) Missing=Patch->IsClean();
        if (auto* Tooth=Cast<AMCArenaTooth>(Player.GoalTarget))
        {
            if (!Tooth->IsAvailable()) Missing=true;
            else if (Stage==EMCTutorialStage::BrushTooth && !Tooth->Status->NeedsCare(true)) Tooth->Status->ApplyCoffee(.25f);
            else if (Stage==EMCTutorialStage::Calculus && Tooth->Calculus && !Tooth->Calculus->HasCalculus()) Tooth->Calculus->GrowCalculus(7001+I*71,1);
        }
        if (!Missing) continue;
        Player.GoalTarget=nullptr;
        if (Stage==EMCTutorialStage::BrushTooth || Stage==EMCTutorialStage::Calculus)
        {
            if (auto* Tooth=AssignTooth(Player))
            {
                Player.GoalTarget=Tooth;
                if (Stage==EMCTutorialStage::BrushTooth) { Tooth->Calculus->ClearCalculus(); Tooth->Status->ApplyCoffee(.25f); }
                else { Tooth->Status->ApplyCoffee(0); Tooth->Calculus->GrowCalculus(7001+I*71,1); }
            }
        }
        else if (Stage==EMCTutorialStage::CoffeeCleanup) Player.GoalTarget=SpawnTonguePatch(Player);
        else Player.GoalTarget=SpawnFood(TEXT("Broccoli"),LessonPosition(Player,I)+FVector(0,0,65),Player.PlayerState,
            Stage==EMCTutorialStage::FreshSort || Stage==EMCTutorialStage::SpoiledSort,Stage==EMCTutorialStage::SpoiledSort);
        ForceNetUpdate();
    }
}

void AMCTutorialDirector::ClearLessonTargets()
{
    for (auto& Player:Players) Player.GoalTarget=nullptr;
    TArray<AActor*> Remove;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (It->Batch==TutorialFoodBatch && !It->bBrushTool) Remove.Add(*It);
    for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->Batch==TutorialFoodBatch) Remove.Add(*It);
    for (auto* Actor:Remove) Actor->Destroy();
    SpawnedActors.RemoveAll([](const auto& Actor){return !IsValid(Actor);});
}

int32 AMCTutorialDirector::CountTutorialFood() const
{
    int32 Count=0;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (It->Batch==TutorialFoodBatch && !It->bBrushTool && !It->IsDisposed()) ++Count;
    return Count;
}

void AMCTutorialDirector::EnterStage(EMCTutorialStage NewStage)
{
    // Preserve serialized enum values while retiring the physical-tool lesson.
    if (NewStage==EMCTutorialStage::TrashSort) NewStage=EMCTutorialStage::BreakfastRain;
    // The rain's food stays for its cleanup; every other lesson owns its own examples.
    if (NewStage!=EMCTutorialStage::BreakfastCleanup) ClearLessonTargets();
    if (Flood && NewStage!=EMCTutorialStage::CoffeeWaves) Flood->Stop();
    Stage=NewStage; StageStartedAt=Now(); StageEndsAt=0; FeedbackEndsAt=0;
    TeamTasksLeft=0; TeamTasksTotal=0;
    for (auto& Player:Players)
    {
        Player.StageActions=0; Player.StageProgress=0; Player.bReady=false;
        Player.StageRequired=FMCTutorialProgressRules::RequiredActions(Stage)!=0?1:0;
    }
    switch (Stage)
    {
    case EMCTutorialStage::Loading:
        Title=LOCTEXT("LoadingTitle","ДЕНЬ 0 · СБОР КОМАНДЫ");
        FairyLine=LOCTEXT("LoadingFairy","Подождём, пока все появятся во рту.");
        Instruction=LOCTEXT("LoadingHint","Ожидаем загрузку участников. Можно осмотреться."); break;
    case EMCTutorialStage::Intro:
        Title=LOCTEXT("IntroTitle","ДЕНЬ 0 · ПЕРВАЯ СМЕНА");
        FairyLine=LOCTEXT("IntroFairy","Йоу! Я Зубная фея. Обычно забираю зубы, но сегодня попробуем их сохранить. Впереди семь дней. Сначала покажу, как здесь работать.");
        Instruction=LOCTEXT("IntroHint","WASD — движение, Space — прыжок. Инструменты уже с тобой: 1 — щётка, 2 — кирка, 3 — нож, 4 — спрей."); StageEndsAt=Now()+10; break;
    case EMCTutorialStage::BrushTooth:
        Title=LOCTEXT("BrushTitle","01 · ОСВОЕНИЕ ЩЁТКИ");
        FairyLine=LOCTEXT("BrushFairy","Щётка всегда в первом слоте. Выбери её и очисти свой подсвеченный зуб. Каждый пробует сам — я подожду!");
        Instruction=LOCTEXT("BrushHint","1 — щётка. Держи ЛКМ у своего подсвеченного зуба до полной очистки. Без таймера."); break;
    case EMCTutorialStage::FoodCut:
        Title=LOCTEXT("CutTitle","02 · МЯГКАЯ ЕДА");
        FairyLine=LOCTEXT("CutFairy","Мягкую порцию сначала разделяем ножом. Попробуй на своей еде.");
        Instruction=LOCTEXT("CutHint","3 — нож; ЛКМ или F — удар по еде. Раздели свою порцию на кусочки."); break;
    case EMCTutorialStage::FreshSort:
        Title=LOCTEXT("FreshTitle","03 · СВЕЖЕЕ — В ГЛОТКУ");
        FairyLine=LOCTEXT("FreshFairy","Съедобное отправляем назад. Собери свой кусочек и доставь его в глотку — затем дождись глотания.");
        Instruction=LOCTEXT("FreshHint","1 — режим сбора. ЛКМ у кусочка — собирать в стопку. Войди со стопкой в зону THROAT: передача автоматическая."); break;
    case EMCTutorialStage::SpoiledSort:
        Title=LOCTEXT("SpoiledTitle","04 · ИСПОРЧЕННОЕ — НА ВЫХОД");
        FairyLine=LOCTEXT("SpoiledFairy","Этот кусочек испортился. Отнеси его вперёд, к выходу у передних зубов.");
        Instruction=LOCTEXT("SpoiledHint","1 + ЛКМ — собрать. Отнеси свою испорченную еду к передней зоне EXIT. Q — бросить."); break;
    case EMCTutorialStage::BreakfastRain:
        Title=LOCTEXT("BreakfastTitle","05 · УЧЕБНЫЙ ЗАВТРАК");
        FairyLine=LOCTEXT("BreakfastFairy","Теперь общий заказ: брокколи, яйцо и бекон. Работайте вместе!");
        Instruction=LOCTEXT("BreakfastHint","Еда падает! Разделяйте, собирайте и доставляйте в глотку.");
        BreakfastSpawned=0; StageEndsAt=Now()+2; break;
    case EMCTutorialStage::BreakfastCleanup:
        Title=LOCTEXT("BreakfastCleanupTitle","ЗАВТРАК · УБОРКА КОМАНДОЙ");
        FairyLine=LOCTEXT("BreakfastCleanupFairy","Двадцать секунд на уборку. Если не успеете — помогу закончить. Здоровьем здесь не рискуем.");
        Instruction=LOCTEXT("BreakfastCleanupHint","3 — нож; 1 + ЛКМ — сбор; доставка в THROAT. Это общая задача.");
        StageEndsAt=Now()+20; TeamTasksLeft=CountTutorialFood(); TeamTasksTotal=TeamTasksLeft; break;
    case EMCTutorialStage::CoffeeWarning:
        Title=LOCTEXT("CoffeeWarningTitle","06 · ОСТОРОЖНО, КОФЕ!");
        FairyLine=LOCTEXT("CoffeeWarningFairy","Сейчас придёт цунами снаружи рта! Уходи в сторону, прячься за зубом или удерживайся за него.");
        Instruction=LOCTEXT("CoffeeWarningHint","WASD + Shift — бежать; держи ПКМ рядом со стеной, зубом или едой — удерживаться. Ошибаться безопасно."); StageEndsAt=Now()+6; break;
    case EMCTutorialStage::CoffeeWaves:
        Title=LOCTEXT("CoffeeTitle","КОФЕ · ЦУНАМИ");
        FairyLine=LOCTEXT("CoffeeFairy","Держись или убегай из потока! Когда вода уйдёт, переключимся на щётку.");
        Instruction=LOCTEXT("CoffeeHint","WASD + Shift — бежать. ПКМ у стены, зуба или еды — удержаться. Переживи потоп без штрафов.");
        Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>();
        if (Flood)
        {
            SpawnedActors.Add(Flood); Flood->Start(Settings);
            // Start already copied/remapped the authored profile. Change only this flood's replicated runtime settings.
            Flood->WaterSettings.Cycles=2;
            Flood->Waves=2; Flood->Seconds=Flood->WaterSettings.CycleSeconds()*2;
            Flood->ForceNetUpdate(); StageEndsAt=Now()+Flood->Seconds;
        }
        else StageEndsAt=Now()+1;
        break;
    case EMCTutorialStage::CoffeeCleanup:
        Title=LOCTEXT("TongueTitle","07 · ЧИСТКА ЯЗЫКА");
        FairyLine=LOCTEXT("TongueFairy","Щётка работает и на языке. Очисти своё маленькое подсвеченное пятно.");
        Instruction=LOCTEXT("TongueHint","1 — щётка. Держи ЛКМ у своего пятна на языке до полной очистки. C — почистить себя."); break;
    case EMCTutorialStage::Calculus:
        Title=LOCTEXT("CalculusTitle","08 · ЗУБНОЙ КАМЕНЬ");
        FairyLine=LOCTEXT("CalculusFairy","Это камень — щёткой такой не взять. Разбей свой нарост киркой.");
        Instruction=LOCTEXT("CalculusHint","2 — кирка. Подойди к своему подсвеченному зубу; ЛКМ или F — удар. Удали нарост полностью."); break;
    case EMCTutorialStage::FoamParty:
        Title=LOCTEXT("PartyTitle","09 · ПЕННАЯ ВЕЧЕРИНКА");
        FairyLine=LOCTEXT("PartyFairy","Стажировка пройдена! Чистить умеете, еду доставлять умеете, мусор отличаете. Немного пены за ваш счёт!");
        Instruction=LOCTEXT("PartyHint","Пятнадцать секунд праздника. Можно свободно побегать вместе."); StageEndsAt=Now()+15; break;
    case EMCTutorialStage::Complete:
        Title=LOCTEXT("CompleteTitle","ОБУЧЕНИЕ ПРОЙДЕНО");
        FairyLine=LOCTEXT("CompleteFairy","Он просыпается — скоро настоящий завтрак. Когда вся команда готова, начинаем день 1.");
        Instruction=LOCTEXT("CompleteHint","Нажми «Готов к дню 1». Переход начнётся после готовности всей команды."); break;
    default: break;
    }
    DefaultFairyLine=FairyLine; EnsureLessonTargets(); OnRep_Presentation(); ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MC_TUTORIAL_STAGE %d %s"),static_cast<int32>(Stage),*Title.ToString());
}

void AMCTutorialDirector::RescuePlayers()
{
    const FVector Center=Settings->ArenaCenter,Half=Settings->ArenaHalfSize;
    for (const auto& Player:Players)
    {
        auto* Hero=Player.PlayerState?Cast<AMCToothCharacter>(Player.PlayerState->GetPawn()):nullptr;
        if (!Hero) continue;
        const FVector P=Hero->ToothPhysics?Hero->ToothPhysics->PhysicalLocation():Hero->GetActorLocation();
        if (!P.ContainsNaN() && P.Z>Center.Z-900 && FMath::Abs(P.X-Center.X)<Half.X+1200 && FMath::Abs(P.Y-Center.Y)<Half.Y+1200) continue;
        const FVector* Saved=SafePositions.Find(Player.PlayerState.Get());
        const FVector Position=Saved?*Saved:LessonPosition(Player,0)+FVector(0,0,100);
        Hero->CancelGameplayInput(); Hero->SetThroatCapture(nullptr);
        Hero->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->ClientThroatExit(Position,FVector::ZeroVector);
        if (Hero->ToothPhysics) Hero->ToothPhysics->TryRecover(); Hero->ForceNetUpdate();
    }
}

void AMCTutorialDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); UpdatePresentation();
    if (!HasAuthority() || !bStarted || Stage==EMCTutorialStage::Finished) return;
    SyncPlayers(); MaintenanceClock+=DeltaSeconds;
    if (MaintenanceClock>=.5f)
    {
        MaintenanceClock=0; RescuePlayers(); EnsureLessonTargets();
        if (Stage==EMCTutorialStage::BreakfastCleanup && !Players.IsEmpty())
        {
            for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
            {
                if (It->Batch!=TutorialFoodBatch || It->bBrushTool || It->IsDisposed() || It->Phase==EMCFoodPhase::Swallowing || It->StackCarrier || !It->Holders.IsEmpty()) continue;
                const FVector Offset=It->GetActorLocation()-Settings->ArenaCenter;
                if (Offset.Z> -900 && FMath::Abs(Offset.X)<Settings->ArenaHalfSize.X+900 && FMath::Abs(Offset.Y)<Settings->ArenaHalfSize.Y+900) continue;
                It->SetActorLocation(LessonPosition(Players[0],0)+FVector(0,0,70),false,nullptr,ETeleportType::TeleportPhysics);
                It->Body->SetPhysicsLinearVelocity(FVector::ZeroVector); It->Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector); It->ForceNetUpdate();
            }
        }
    }
    if (FeedbackEndsAt>0 && Now()>=FeedbackEndsAt) { FairyLine=DefaultFairyLine; FeedbackEndsAt=0; ForceNetUpdate(); }
    // The same wait applies to late arrivals. Their current lesson is created once their own client acknowledges possession.
    if (!FMCTutorialProgressRules::AllLoaded(Players)) return;
    if (Stage==EMCTutorialStage::Loading) { EnterStage(EMCTutorialStage::Intro); return; }
    if (FMCTutorialProgressRules::RequiredActions(Stage)!=0)
    {
        if (GetCompletedPlayers()==GetRequiredPlayers()) EnterStage(static_cast<EMCTutorialStage>(static_cast<uint8>(Stage)+1));
        return;
    }
    const double Elapsed=Now()-StageStartedAt;
    if (Stage==EMCTutorialStage::BreakfastRain)
    {
        const FName Menu[]={TEXT("Broccoli"),TEXT("Egg"),TEXT("Bacon")};
        while (BreakfastSpawned<3 && Elapsed>=double(BreakfastSpawned)*2.0/3.0)
        {
            const int32 I=BreakfastSpawned++; const auto& Player=Players[I%Players.Num()];
            SpawnFood(Menu[I],LessonPosition(Player,I)+FVector(0,0,550),nullptr);
        }
    }
    if (Stage==EMCTutorialStage::BreakfastCleanup)
    {
        TeamTasksLeft=CountTutorialFood(); TeamTasksTotal=FMath::Max(TeamTasksTotal,TeamTasksLeft);
        if (TeamTasksLeft==0) { EnterStage(EMCTutorialStage::CoffeeWarning); return; }
        if (StageEndsAt>0 && Now()>=StageEndsAt)
        {
            StageEndsAt=0;
            FairyLine=LOCTEXT("BreakfastOvertime","Время вышло — ничего страшного. Закончите доставку в своём темпе; я подожду.");
            DefaultFairyLine=FairyLine; ForceNetUpdate();
        }
        return;
    }
    if (Stage==EMCTutorialStage::CoffeeWaves)
    {
        if (!Flood || !Flood->IsActive()) EnterStage(EMCTutorialStage::CoffeeCleanup);
        return;
    }
    if (Stage==EMCTutorialStage::Complete)
    {
        if (!bFinishedBroadcast && FMCTutorialProgressRules::AllReady(Players))
        { bFinishedBroadcast=true; OnTutorialFinished.Broadcast(); }
        return;
    }
    if (StageEndsAt>0 && Now()>=StageEndsAt) EnterStage(static_cast<EMCTutorialStage>(static_cast<uint8>(Stage)+1));
}

void AMCTutorialDirector::OnRep_Presentation() { UpdatePresentation(); }

void AMCTutorialDirector::UpdatePresentation()
{
    if (GetNetMode()==NM_DedicatedServer) return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    const auto* Progress=PC && PC->IsLocalController()?GetPlayerProgress(PC->PlayerState):nullptr;
    auto* Target=Progress && Progress->StageProgress<Progress->StageRequired?Progress->GoalTarget.Get():nullptr;
    UPrimitiveComponent* Surface=nullptr;
    if (auto* Tooth=Cast<AMCArenaTooth>(Target)) Surface=Tooth->Visual;
    if (auto* Food=Cast<AMCFoodActor>(Target)) Surface=Food->Visual;
    if (auto* Patch=Cast<AMCMouthSurface>(Target)) Surface=Patch->Liquid;
    if (Highlighted.Get()!=Surface)
    {
        if (auto* Previous=Highlighted.Get()) { Previous->SetCustomDepthStencilValue(PreviousStencil); Previous->SetRenderCustomDepth(bPreviousCustomDepth); }
        Highlighted=Surface;
        if (Surface)
        {
            bPreviousCustomDepth=Surface->bRenderCustomDepth; PreviousStencil=Surface->CustomDepthStencilValue;
            Surface->SetCustomDepthStencilValue(250); Surface->SetRenderCustomDepth(true);
        }
    }
    GoalLabel->SetVisibility(IsValid(Target));
    if (Target)
    {
        FVector Position=Target->GetActorLocation()+FVector(0,0,110);
        if (const auto* Tooth=Cast<AMCArenaTooth>(Target)) Position=Tooth->Visual->Bounds.Origin+FVector(0,0,Tooth->Visual->Bounds.BoxExtent.Z+30);
        GoalLabel->SetWorldLocation(Position); GoalLabel->SetText(LOCTEXT("YourTarget","▼ ТВОЯ ЦЕЛЬ"));
        if (PC && PC->PlayerCameraManager) GoalLabel->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Position).Rotation());
    }
    if (Stage!=EMCTutorialStage::FoamParty)
    {
        for (UNiagaraComponent* Foam:PartyFoam) if (IsValid(Foam)) Foam->DestroyComponent(); PartyFoam.Reset(); return;
    }
    if (!PartyFoam.IsEmpty()) return;
    if (!PartyFoamSystem) PartyFoamSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NS_BrushFoam.NS_BrushFoam"));
    if (!PartyFoamSystem) return;
    for (int32 I=0;I<4;++I)
    {
        auto* Foam=NewObject<UNiagaraComponent>(this); AddInstanceComponent(Foam);
        Foam->SetupAttachment(GetRootComponent()); Foam->SetAutoActivate(false); Foam->SetAsset(PartyFoamSystem);
        Foam->SetCastShadow(false); Foam->SetWorldLocation(FVector(-200+I*150,I%2?-160:160,200));
        Foam->SetWorldScale3D(FVector(2)); Foam->RegisterComponent(); Foam->Activate(true); PartyFoam.Add(Foam);
    }
    if (auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Win.S_Win"))) UGameplayStatics::PlaySound2D(this,Sound);
}

void AMCTutorialDirector::RestoreArena()
{
    if (bRestored || !HasAuthority()) return; bRestored=true;
    for (const auto& Snapshot:ToothSnapshots)
        if (auto* Tooth=Snapshot.Tooth.Get())
        {
            // Restore() is a respawn helper that clamps health to at least one. Exact snapshots must also preserve previously lost teeth.
            Tooth->Status->State=Snapshot.Status; Tooth->StatusChanged(); Tooth->ResetGrime(); Tooth->GrimeMask=Snapshot.GrimeMask;
            if (Tooth->Calculus) { Tooth->Calculus->State=Snapshot.Calculus; Tooth->Calculus->RebuildForSurface(); }
            Tooth->ForceNetUpdate();
        }
    for (const auto& Snapshot:PlayerSnapshots)
        if (auto* Player=Snapshot.Key.Get()) if (auto* Hero=Cast<AMCToothCharacter>(Player->GetPawn()))
        { Hero->Status->State=Snapshot.Value; Hero->StatusChanged(); Hero->ForceNetUpdate(); }
}

void AMCTutorialDirector::Stop()
{
    if (!HasAuthority()) return;
    if (Flood) Flood->Stop(); ClearLessonTargets();
    TArray<AMCFoodActor*> Brushes;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It) if (It->Batch==TutorialFoodBatch && It->bBrushTool) Brushes.Add(*It);
    for (auto* Brush:Brushes)
    {
        if (auto* Hero=Brush->EquippedBy.Get()) { if (Hero->EquippedBrush==Brush) Hero->EquippedBrush=nullptr; Hero->ForceNetUpdate(); }
        Brush->Destroy();
    }
    for (AActor* Actor:SpawnedActors) if (IsValid(Actor)) Actor->Destroy(); SpawnedActors.Reset(); Flood=nullptr;
    RestoreArena(); Stage=EMCTutorialStage::Finished; bStarted=false; OnRep_Presentation(); ForceNetUpdate();
}

void AMCTutorialDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    Stop();
    if (auto* Component=Highlighted.Get()) { Component->SetCustomDepthStencilValue(PreviousStencil); Component->SetRenderCustomDepth(bPreviousCustomDepth); }
    for (UNiagaraComponent* Foam:PartyFoam) if (IsValid(Foam)) Foam->DestroyComponent(); PartyFoam.Reset();
    Super::EndPlay(Reason);
}

void AMCTutorialDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCTutorialDirector,Stage); DOREPLIFETIME(AMCTutorialDirector,Title);
    DOREPLIFETIME(AMCTutorialDirector,FairyLine); DOREPLIFETIME(AMCTutorialDirector,Instruction);
    DOREPLIFETIME(AMCTutorialDirector,StageStartedAt); DOREPLIFETIME(AMCTutorialDirector,StageEndsAt);
    DOREPLIFETIME(AMCTutorialDirector,Players); DOREPLIFETIME(AMCTutorialDirector,TeamTasksLeft); DOREPLIFETIME(AMCTutorialDirector,TeamTasksTotal);
}

#undef LOCTEXT_NAMESPACE
