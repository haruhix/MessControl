#include "MCDevCommands.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCPlaytestSession.h"
#include "MCRoguelikeDirector.h"
#include "MCBossCharacter.h"
#include "MCBossProfile.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCColdCola.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCToothCalculusComponent.h"
#include "MCInventoryComponent.h"
#include "MCTongue.h"
#include "MCFirePatch.h"
#include "MCMouthSurface.h"
#include "MCLocomotionSurface.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "MCPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

namespace
{
#if !UE_BUILD_SHIPPING
    bool PlaceForCalculus(UWorld* World,AMCToothCharacter* Hero,AMCArenaTooth* Tooth)
    {
        AMCTongue* Tongue=nullptr;
        for (TActorIterator<AMCTongue> It(World);It;++It) { Tongue=*It; break; }
        if (!Tongue || !Tooth->Calculus || !Tooth->Calculus->HasCalculus()) return false;
        const FTransform Original=Hero->GetActorTransform();
        const FVector Inward=(Tongue->Surface->Bounds.Origin-Tooth->GetActorLocation()).GetSafeNormal2D();
        const FVector Side=FVector::CrossProduct(Inward,FVector::UpVector);
        const FVector Extent=Tooth->Visual->Bounds.BoxExtent;
        const float Radius=FMath::Abs(Inward.X)*Extent.X+FMath::Abs(Inward.Y)*Extent.Y;
        FVector Approach=Tooth->GetActorLocation()+Inward*Radius;
        FHitResult Face;
        FCollisionQueryParams Surface(SCENE_QUERY_STAT(MCCalculusPracticeSurface),true,Hero);
        FVector Probe=Tooth->GetActorLocation();
        Probe.Z=Hero->GetActorLocation().Z+20;
        if (Tooth->BrushSurface->LineTraceComponent(Face,Probe+Inward*600,Probe-Inward*250,Surface)) Approach=Face.ImpactPoint;
        FCollisionQueryParams Room(SCENE_QUERY_STAT(MCCalculusPracticeRoom),false,Hero);
        FVector Contact,Normal;
        for (float Gap:{75.f,90.f,110.f,130.f,150.f}) for (int32 Offset:{0,-1,1,-2,2,-3,3,-4,4,-5,5})
        {
            FVector Position=Approach+Inward*Gap+Side*(Offset*22.f);
            FHitResult Floor;
            if (!Tongue->SurfacePoint(Position,Floor) || Floor.ImpactNormal.Z<.65f) continue;
            Position.Z=Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3;
            if (World->OverlapBlockingTestByProfile(Position,FQuat::Identity,Hero->GetCapsuleComponent()->GetCollisionProfileName(),
                Hero->GetCapsuleComponent()->GetCollisionShape(),Room)) continue;
            Hero->SetActorLocationAndRotation(Position,(-Inward).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
            if (!Tooth->Calculus->FindContact(Hero,Contact,Normal)) continue;
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->SetActorRotation(FRotator(0,(Contact-Position).Rotation().Yaw,0));
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Hero->ForceNetUpdate(); return true;
        }
        Hero->SetActorTransform(Original,false,nullptr,ETeleportType::TeleportPhysics);
        return false;
    }

    enum class EDevBossVariant : uint8 { Phase1, Phase3 };

    AMCBossCharacter* FindDevBoss(UWorld* World,EDevBossVariant Variant=EDevBossVariant::Phase1)
    {
        for (TActorIterator<AMCBossCharacter> It(World);It;++It)
            if (It->ActorHasTag(TEXT("MC_DevBoss")) && !It->IsActorBeingDestroyed()
                && It->ActorHasTag(TEXT("MC_DevBossPhase3"))==(Variant==EDevBossVariant::Phase3)) return *It;
        return nullptr;
    }

    AMCBossCharacter* SpawnDevBoss(UWorld* World,APlayerController* Requester,EDevBossVariant Variant=EDevBossVariant::Phase1)
    {
        if (AMCBossCharacter* Existing=FindDevBoss(World,Variant)) return Existing;
        const AMCToothCharacter* Hero=Cast<AMCToothCharacter>(Requester->GetPawn());
        if (!AMCBossCharacter::IsLivingPlayer(Hero)) return nullptr;
        UClass* BossClass=LoadClass<AMCBossCharacter>(nullptr,Variant==EDevBossVariant::Phase3?
            TEXT("/Game/Gameplay/Boss/Phase3/BP_BossPhase3.BP_BossPhase3_C"):
            TEXT("/Game/Gameplay/Boss/BP_ZombieBoss.BP_ZombieBoss_C"));
        const AMCBossCharacter* Defaults=BossClass?BossClass->GetDefaultObject<AMCBossCharacter>():nullptr;
        UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if (!Defaults || !Nav) return nullptr;
        const float Radius=Defaults->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const float HalfHeight=Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FNavAgentProperties Agent=Defaults->GetNavAgentPropertiesRef();
        // A CDO has not run the movement component's owner-collision update yet.
        Agent.AgentRadius=Radius; Agent.AgentHeight=HalfHeight*2.f;
        const ANavigationData* NavData=Nav->GetNavDataForProps(Agent,Hero->GetActorLocation());
        if (!NavData) return nullptr;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDevBossSpawn),false,Hero);
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        for (float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f,180.f})
        {
            const FVector Direction=Hero->GetActorForwardVector().RotateAngleAxis(Angle,FVector::UpVector);
            FNavLocation Floor;
            if (!Nav->ProjectPointToNavigation(Hero->GetActorLocation()+Direction*500.f,Floor,FVector(180,180,500),NavData)) continue;
            if (FVector::DistSquared2D(Floor.Location,Hero->GetActorLocation())<FMath::Square(250.f)) continue;
            const FVector Position=Floor.Location+FVector(0,0,HalfHeight+3.f);
            if (World->OverlapAnyTestByObjectType(Position,FQuat::Identity,Objects,FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) continue;
            FActorSpawnParameters Spawn;
            Spawn.Owner=Requester;
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
            const FRotator Facing=(Hero->GetActorLocation()-Position).Rotation();
            AMCBossCharacter* Boss=World->SpawnActor<AMCBossCharacter>(BossClass,Position,FRotator(0,Facing.Yaw,0),Spawn);
            if (Boss)
            {
                Boss->Tags.AddUnique(TEXT("MC_DevBoss"));
                if (Variant==EDevBossVariant::Phase3) Boss->Tags.AddUnique(TEXT("MC_DevBossPhase3"));
                Boss->PreviewAnimation(EMCBossAnimationPreview::Idle);
                return Boss;
            }
        }
        return nullptr;
    }

    bool CenterDevBoss(UWorld* World,AMCBossCharacter* Boss,APlayerController* Requester)
    {
        const float Radius=Boss->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const float HalfHeight=Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        const ANavigationData* NavData=Nav?Nav->GetNavDataForProps(Boss->GetNavAgentPropertiesRef(),Boss->GetActorLocation()):nullptr;
        if (!NavData) return false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCDevBossIntro),false,Boss);
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
        for (TActorIterator<AMCTongue> It(World);It;++It)
        {
            const FVector Center=It->Surface->Bounds.Origin;
            for (float Distance:{0.f,180.f,350.f}) for (float Angle:{0.f,60.f,120.f,180.f,240.f,300.f})
            {
                FHitResult Support;
                const FVector Offset=FVector::ForwardVector.RotateAngleAxis(Angle,FVector::UpVector)*Distance;
                if (!It->InteriorSurfacePoint(Center+Offset,Radius+100.f,Support)) continue;
                FNavLocation Floor;
                if (!Nav->ProjectPointToNavigation(Support.ImpactPoint,Floor,FVector(150,150,200),NavData)) continue;
                const FVector Position=Floor.Location+FVector(0,0,HalfHeight+4.f);
                if (World->OverlapAnyTestByObjectType(Position,FQuat::Identity,Objects,FCollisionShape::MakeCapsule(Radius,HalfHeight),Query)) continue;
                const FVector Facing=(Requester->GetPawn()?Requester->GetPawn()->GetActorLocation():Requester->GetFocalLocation())-Position;
                Boss->DeactivateBoss(); Boss->GetCharacterMovement()->StopMovementImmediately();
                Boss->SetActorLocationAndRotation(Position,FRotator(0,Facing.Rotation().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
                Boss->ForceNetUpdate(); return true;
            }
        }
        return false;
    }
#endif
}

bool AMCGameMode::CanUseDevPanel(const APlayerController* Requester) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    return HasAuthority() && IsValid(Requester) && Requester->GetWorld()==GetWorld()
        && Requester->HasAuthority() && Requester->IsLocalController() && GetNetMode()!=NM_DedicatedServer;
#endif
}

FText AMCGameMode::ExecuteDevAction(APlayerController* Requester,EMCDevAction Action,int32 StepIndex)
{
    if (!CanUseDevPanel(Requester)) return FText::FromString(TEXT("Доступно только хосту в Development / Editor."));
#if !UE_BUILD_SHIPPING
    auto* GS=GetGameState<AMCGameState>();
    if (!GS) return FText::FromString(TEXT("Мир ещё не готов."));
    AMCPlaytestSession* BotSession=AMCPlaytestSession::Find(GetWorld());
    if (Action==EMCDevAction::BotsStart)
    {
        if (StepIndex<0 || StepIndex>=24) return FText::FromString(TEXT("Выбери 1–4 бота, уровень и режим теста."));
        if (BotSession && BotSession->IsActive()) return FText::FromString(TEXT("Боты уже играют. Сначала нажми «Остановить ботов»."));
        if (GS->bLobbyWaiting || GS->bTutorialActive || GS->bDevManualEvents)
            return FText::FromString(TEXT("Для ботов нужен обычный день: заверши обучение / лобби или нажми «Обычный день 1 — полный перезапуск»."));
        const int32 Count=StepIndex%4+1;
        const auto Skill=static_cast<EMCPlaytestBotSkill>((StepIndex/4)%3);
        const bool bObserve=StepIndex>=12;
        const int32 Cap=FMath::Clamp(GS->RunSettings.MaxPlayers,1,4);
        const int32 Humans=GetGameplayParticipantCount();
        if (Count+(bObserve?0:Humans)>Cap)
            return FText::FromString(FString::Printf(TEXT("В команде максимум %d: игроков %d, запрошено ботов %d. Уменьши число или выбери наблюдение."),Cap,bObserve?0:Humans,Count));
        if (!BotSession) BotSession=GetWorld()->SpawnActor<AMCPlaytestSession>();
        FString Error;
        if (!BotSession || !BotSession->StartSession(Count,Skill,bObserve,GS->RunSeed,Error))
        {
            if (BotSession && !BotSession->IsActive()) BotSession->Destroy();
            return FText::FromString(TEXT("Боты не запущены: ")+Error);
        }
        return FText::FromString(FString::Printf(TEXT("Новый обычный день: %d бота, %s. Seed %d. F3 — %s. CSV сохраняется автоматически."),
            Count,Skill==EMCPlaytestBotSkill::Novice?TEXT("новички"):Skill==EMCPlaytestBotSkill::Skilled?TEXT("опытные"):TEXT("обычные"),
            GS->RunSeed,bObserve?TEXT("наблюдать"):TEXT("играть вместе")));
    }
    if (Action==EMCDevAction::BotsStop)
    {
        if (!BotSession || !BotSession->IsActive()) return FText::FromString(TEXT("Активного теста с ботами нет."));
        const FString Csv=BotSession->GetReportPath();
        BotSession->StopSession();
        return FText::FromString(TEXT("Боты остановлены, игроки возвращены, обычный день перезапущен. CSV: ")+Csv);
    }
    if (Action==EMCDevAction::BotsReport)
    {
        if (!BotSession || !BotSession->IsActive()) return FText::FromString(TEXT("Сначала запусти ботов."));
        BotSession->PrintReport();
        return FText::FromString(TEXT("Текущий срез сохранён. Детали — Output Log, CSV: ")+BotSession->GetReportPath());
    }
    if (BotSession && BotSession->IsActive())
        return FText::FromString(TEXT("Во время теста ботов ручные события отключены. Сначала нажми «Остановить ботов»."));
    if (Action==EMCDevAction::CalculusClear)
    {
        for (TActorIterator<AMCArenaTooth> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("MC_CalculusPractice")))
        { if (It->Calculus) It->Calculus->ClearCalculus(); It->Tags.Remove(TEXT("MC_CalculusPractice")); }
        return FText::FromString(TEXT("Тестовый зубной камень убран."));
    }
    if (Action==EMCDevAction::CalculusPractice)
    {
        auto* Hero=Cast<AMCToothCharacter>(Requester->GetPawn());
        if (!Hero || !Hero->Status->IsAlive() || !Hero->Inventory)
            return FText::FromString(TEXT("Нужен живой игрок хоста."));
        AMCArenaTooth* Tooth=nullptr;
        double Nearest=DBL_MAX;
        for (AMCArenaTooth* Candidate:GS->ArenaTeeth) if (IsValid(Candidate) && Candidate->IsAvailable() && Candidate->Calculus)
        {
            const double Distance=FVector::DistSquared2D(Hero->GetActorLocation(),Candidate->GetActorLocation());
            if (Distance<Nearest) { Nearest=Distance; Tooth=Candidate; }
        }
        if (!Tooth) return FText::FromString(TEXT("На карте нет доступного декоративного зуба."));
        Hero->CancelGameplayInput(); Hero->DropFood();
        Tooth->Calculus->GrowCalculus(Tooth->State.ToothId+1,3);
        Tooth->Tags.AddUnique(TEXT("MC_CalculusPractice"));
        GS->bDevManualEvents=true; GS->PhaseEndsAt=0; GS->ForceNetUpdate();
        const bool Placed=PlaceForCalculus(GetWorld(),Hero,Tooth);
        Hero->Inventory->ServerSelect(EMCToolSlot::Pickaxe);
        return FText::FromString(Placed?
            TEXT("Три участка камня восстановлены. Кирка выбрана: F3 — закрыть, удерживай ЛКМ. Сколы изменяют форму в точке удара; эмаль сохраняется."):
            TEXT("Камень восстановлен на ближайшем зубе, кирка выбрана. Нет свободной позиции для переноса: подойди к нему со стороны языка и удерживай ЛКМ."));
    }
    if (Action==EMCDevAction::RewardChest)
    {
        if (!IsValid(RoguelikeDirector)) return FText::FromString(TEXT("Система наград ещё не готова."));
        RoguelikeDirector->NotifyTaskCompleted();
        return FText::FromString(TEXT("Награда поставлена в очередь: сундук выберет свободную зону с наименьшим числом игроков."));
    }
    if (Action==EMCDevAction::BossPhase3Remove)
    {
        if (AMCBossCharacter* Boss=FindDevBoss(GetWorld(),EDevBossVariant::Phase3)) Boss->Destroy();
        return FText::FromString(TEXT("Тестовый босс фазы 3 убран."));
    }
    if (Action==EMCDevAction::BossPhase3Spawn || Action==EMCDevAction::BossPhase3Animation
        || Action==EMCDevAction::BossPhase3Activate || Action==EMCDevAction::BossPhase3Deactivate)
    {
        if (Action==EMCDevAction::BossPhase3Animation && (StepIndex<1 || StepIndex>8))
            return FText::FromString(TEXT("Неизвестная анимация босса фазы 3."));
        AMCBossCharacter* Boss=SpawnDevBoss(GetWorld(),Requester,EDevBossVariant::Phase3);
        if (!Boss) return FText::FromString(TEXT("Для фазы 3 нужно свободное место на Boss NavMesh рядом с живым игроком."));
        if (Action==EMCDevAction::BossPhase3Activate)
        {
            Boss->ResetForRun(); Boss->ActivateBoss();
            return FText::FromString(TEXT("Фаза 3: здоровье восстановлено, AI и бой включены."));
        }
        if (Action==EMCDevAction::BossPhase3Deactivate)
        {
            Boss->ResetForRun(); Boss->DeactivateBoss();
        }
        const auto Clip=Action==EMCDevAction::BossPhase3Animation?static_cast<EMCBossAnimationPreview>(StepIndex):EMCBossAnimationPreview::Idle;
        if (!Boss->PreviewAnimation(Clip)) return FText::FromString(TEXT("Клип пока не назначен в DA_BossPhase3."));
        return FText::FromString(Action==EMCDevAction::BossPhase3Spawn?TEXT("Фаза 3 создана для просмотра. AI и урон выключены."):
            Action==EMCDevAction::BossPhase3Deactivate?TEXT("Фаза 3: AI остановлен, здоровье восстановлено, играет idle."):
            TEXT("Фаза 3: показ выбранной анимации без AI и игрового урона. F3 — закрыть панель."));
    }
    if (Action==EMCDevAction::BossRemove)
    {
        if (AMCBossCharacter* Boss=FindDevBoss(GetWorld())) Boss->Destroy();
        return FText::FromString(TEXT("Тестовый Zombie убран. Обычная игра не спавнит босса."));
    }
    if (Action==EMCDevAction::BossIntro)
    {
        AMCBossCharacter* Boss=SpawnDevBoss(GetWorld(),Requester);
        if (!Boss || !CenterDevBoss(GetWorld(),Boss,Requester))
            return FText::FromString(TEXT("Для интро нужно свободное место в центре языка на Boss NavMesh."));
        if (!Boss->PreviewAnimation(EMCBossAnimationPreview::Roar))
            return FText::FromString(TEXT("Назначь Roar Animation в DA_ZombieBoss."));
        const int32 Serial=Boss->Runtime.PreviewSerial;
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if (auto* PC=Cast<AMCPlayerController>(It->Get()))
            { PC->SetBossIntroGuard(Boss); PC->ClientPlayBossIntro(Boss); }
        FTimerHandle Finish;
        GetWorldTimerManager().SetTimer(Finish,FTimerDelegate::CreateWeakLambda(Boss,[Boss,Serial]()
        {
            if (Boss->Runtime.AnimationPreview==EMCBossAnimationPreview::Roar && Boss->Runtime.PreviewSerial==Serial)
                Boss->PreviewAnimation(EMCBossAnimationPreview::Idle);
        }),5.1f,false);
        return FText::FromString(TEXT("Интро: 5 секунд Sequencer, рёв в центре, чёрные полосы. После него F3 → AI включает бой."));
    }
    if (Action==EMCDevAction::BossPractice || Action==EMCDevAction::BossAI || Action==EMCDevAction::BossStop || Action==EMCDevAction::BossAnimation)
    {
        if (Action==EMCDevAction::BossAnimation && (StepIndex<1 || StepIndex>8)) return FText::FromString(TEXT("Неизвестная анимация босса."));
        AMCBossCharacter* Boss=SpawnDevBoss(GetWorld(),Requester);
        if (!Boss) return FText::FromString(TEXT("Нет свободного места на Boss NavMesh рядом с живым игроком. Перейди к центру языка."));
        if (Action==EMCDevAction::BossAI)
        {
            Boss->ResetForRun(); Boss->ActivateBoss();
            return FText::FromString(TEXT("Тест AI: Zombie преследует игроков, бьёт руками и пинает. Перед ударом есть замах; нож и кирка наносят урон."));
        }
        const auto Clip=Action==EMCDevAction::BossAnimation?static_cast<EMCBossAnimationPreview>(StepIndex):EMCBossAnimationPreview::Idle;
        if (!Boss->PreviewAnimation(Clip)) return FText::FromString(TEXT("Клип пока не назначен в DA_ZombieBoss."));
        return FText::FromString(Action==EMCDevAction::BossPractice?TEXT("Zombie создан для просмотра. AI и урон выключены; F3 → AI включает бой."):
            Action==EMCDevAction::BossStop?TEXT("AI остановлен, здоровье восстановлено. Zombie показывает idle."):
            TEXT("Показ выбранной анимации: без движения AI и без нанесения урона. F3 — закрыть панель."));
    }
    if(Action==EMCDevAction::ActiveRagdoll) {
        if(StepIndex<0 || StepIndex>2) return FText::FromString(TEXT("Неизвестный режим физики."));
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            It->ToothPhysics->SetActiveRagdollMode(static_cast<EMCActiveRagdollMode>(StepIndex));
        return FText::FromString(StepIndex==0?TEXT("Исходный режим анимации возвращён всем текущим игрокам."):
            StepIndex==1?TEXT("Мягкий Active Ragdoll: стабилизированный корпус и физические конечности, как в референсе. WASD, Shift, Space; проверь повороты и удары."):
            TEXT("Упругий Active Ragdoll: более сильные мышцы. Точный хват, инструменты, плавание и лазание сохраняют контактную позу."));
    }
    if (Action==EMCDevAction::RestartDay)
    {
        bUseDayOnePlan=true; RestartShift();
        return FText::FromString(TEXT("Обычный первый день перезапущен. Автопереходы включены."));
    }
    if (Action==EMCDevAction::StartStep)
    {
        auto* Plan=FirstDayPlan.LoadSynchronous();
        if (!Plan || !Plan->Steps.IsValidIndex(StepIndex) || Plan->Steps[StepIndex].Step==EMCDayStep::Complete)
            return FText::FromString(TEXT("Этап отсутствует в DA_Day01."));
        RestartShift(); GS->Day=1;
        DayDirector=GetWorld()->SpawnActor<AMCDayDirector>();
        DayDirector->Start(Plan,StepIndex,true);
        return FText::FromString(TEXT("Чистый тест: ")+Plan->Steps[StepIndex].Title.ToString()+TEXT(". F3 — вернуться в игру."));
    }
    if (static_cast<uint8>(Action)>static_cast<uint8>(EMCDevAction::SwimCoffee))
        return FText::FromString(TEXT("Неизвестная команда."));
    if (GS->Phase==EMCShiftPhase::Lost || GS->Phase==EMCShiftPhase::Won || GS->bDayOneComplete)
        return FText::FromString(TEXT("Сначала запусти этап или перезапусти день."));
    if (!IsValid(DayDirector))
    {
        if (!bUseDayOnePlan) return FText::FromString(TEXT("Сначала выбери этап первого дня."));
        GS->Day=0; StartDay();
    }
    if (!IsValid(DayDirector)) return FText::FromString(TEXT("Не удалось запустить первый день."));
    GS->bDevManualEvents=true; GS->PhaseEndsAt=0; GS->ForceNetUpdate();
    auto* Hero=Cast<AMCToothCharacter>(Requester->GetPawn());
    switch (Action)
    {
    case EMCDevAction::SwimCoffee:
        if (!IsValid(DayDirector->Flood)) DayDirector->Flood=GetWorld()->SpawnActor<AMCCoffeeFlood>();
        if (DayDirector->ColdCola) DayDirector->ColdCola->Stop();
        if (DayDirector->Flood) DayDirector->Flood->Start(DayDirector->Settings,600);
        return FText::FromString(TEXT("Кофе наполняет рот и держится 10 минут. WASD — плавать; F3 → убрать кофе — закончить тест."));
    case EMCDevAction::ColdCola:
        if(DayDirector->ColdCola) { DayDirector->ColdCola->Stop(); DayDirector->ColdCola->Destroy(); }
        DayDirector->ColdCola=GetWorld()->SpawnActor<AMCColdColaEvent>(); DayDirector->ColdCola->Start(DayDirector->Settings);
        return FText::FromString(TEXT("Холодная кола сверху, иней и скользкий язык. Слот 2 + ЛКМ: разбей лёд."));
    case EMCDevAction::SpicyPepper:
    case EMCDevAction::VomitMeal:
    {
        if(!Hero) return FText::FromString(TEXT("Нужен живой игрок хоста."));
        auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const FName Name=Action==EMCDevAction::SpicyPepper?TEXT("SpicyPepper"):TEXT("Broccoli");
        const auto* Row=Menu?Menu->FindRow<FMCFoodRow>(Name,TEXT("Hazard practice")):nullptr;
        if(!Row) return FText::FromString(TEXT("Предмет отсутствует в DT_BreakfastMenu."));
        FVector P=Hero->GetActorLocation()+Hero->GetActorForwardVector()*170;
        if(Action==EMCDevAction::VomitMeal) {
            bool Found=false;
            for(TActorIterator<AMCThroat> It(GetWorld());It;++It) if(It->ThroatPhase==EMCThroatPhase::Collecting) {
                P=It->GetActorTransform().TransformPosition(It->ZoneCenter); Found=true; break;
            }
            if(!Found) return FText::FromString(TEXT("Дождись окончания проглатывания."));
        }
        FHitResult Floor; bool Found=false;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) if(It->SurfacePoint(P,Floor)) { Found=true; break; }
        if(!Found) return FText::FromString(TEXT("Предмету нужно место на языке."));
        const int32 Count=Action==EMCDevAction::VomitMeal?2:1;
        for(int32 I=0;I<Count;++I) {
            const FTransform T(Floor.ImpactPoint+FVector(0,(I-.5f)*100,80));
            auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
            if(!Food) continue;
            FRandomStream R(41+I); Food->ConfigureItem(Name,*Row,R); Food->Batch=12000;
            Food->FinishSpawning(T);
            if(Action==EMCDevAction::VomitMeal && I==1) { Food->bSpoiled=true; Food->SpoilAt=0; }
        }
        return FText::FromString(Action==EMCDevAction::SpicyPepper?
            TEXT("Перец падает перед тобой: таймер 8–6 секунд по раунду. E — взять, Q — бросить в круг. Автоматическое проглатывание останавливает таймер."):
            TEXT("В круге два куска, один испорчен. Глотка автоматически сократится и выплюнет заказ со струёй и брызгами. Пятна останутся на языке до чистки щёткой."));
    }
    case EMCDevAction::LocomotionGround:
        if (Hero && StepIndex>=0 && StepIndex<=2)
        {
            for (TActorIterator<AMCLocomotionSurface> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DevStrideSurface"))) It->Destroy();
            if (StepIndex==0) return FText::FromString(TEXT("Проверочная поверхность убрана. WASD — шаг, Shift — бег."));
            const FVector Center=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            auto* Patch=GetWorld()->SpawnActor<AMCLocomotionSurface>(Center,FRotator::ZeroRotator);
            Patch->Tags.Add(TEXT("DevStrideSurface")); Patch->HalfExtent=FVector(350,250,50);
            Patch->Surface=StepIndex==1?EMCGroundSurface::Sticky:EMCGroundSurface::Slippery; Patch->RefreshBounds(); Patch->ForceNetUpdate();
            DrawDebugBox(GetWorld(),Center,Patch->HalfExtent,StepIndex==1?FColor::Orange:FColor::Cyan,false,15);
            return FText::FromString(StepIndex==1?TEXT("Липкий участок 7 × 5 м под игроком: длиннее опора, тяжелее отрыв ноги. Shift — попытка бега."):TEXT("Скользкий участок 7 × 5 м: разгонись и отпусти WASD, затем потяни груз. Граница видна 15 секунд."));
        }
        return FText::FromString(TEXT("Нужен живой игрок и тип поверхности 0–2."));
    case EMCDevAction::TonguePressurePreset:
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            if (!It->Profile || !It->Profile->PressurePresets.IsValidIndex(StepIndex) || !It->Profile->PressurePresets[StepIndex])
                return FText::FromString(TEXT("Пресет отсутствует в DA_Tongue → Pressure Presets."));
            auto* Preset=It->Profile->PressurePresets[StepIndex].Get(); It->ApplyPressurePreset(Preset);
            return FText::FromString(TEXT("Применён ")+Preset->Label.ToString()+TEXT(". Геометрия, коллизия и материал обновлены у всех игроков. Ассет не перезаписан."));
        }
        return FText::FromString(TEXT("На карте нет подвижного языка."));
    case EMCDevAction::TonguePressureReload:
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It) It->ReloadPressureProfile();
        return FText::FromString(TEXT("Применены настройки DA_Tongue и его Default Pressure Preset. Повтори кнопку пресета, чтобы применить его правки во время PIE."));
    case EMCDevAction::TonguePressureClear:
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It) It->ResetPressure();
        return FText::FromString(TEXT("История следов очищена. Стоящие объекты снова создадут давление."));
    case EMCDevAction::GripPractice:
        if (Hero) for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            FHitResult Hit; if (!It->SurfacePoint(Hero->GetActorLocation()+Hero->GetActorForwardVector()*170,Hit))
                return FText::FromString(TEXT("Встань на язык: перед игроком нужно свободное место."));
            for (TActorIterator<AMCFoodActor> Food(GetWorld());Food;++Food) if (Food->ActorHasTag(TEXT("DevGripFood"))) Food->Destroy();
            const FTransform T(Hit.ImpactPoint+FVector(0,0,60));
            auto* Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),T);
            FMCFoodRow Row; Row.Kind=EMCFoodKind::ForeignObject; Row.Label=FText::FromString(TEXT("GRIP PRACTICE")); Row.Mass=4; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
            Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
            FRandomStream GripRandom(1); Food->ConfigureItem(TEXT("GripPractice"),Row,GripRandom); Food->Tags.Add(TEXT("DevGripFood")); Food->FinishSpawning(T);
            for (const float Side:{146.f,214.f})
            {
                FHitResult SmallFloor;
                if (!It->SurfacePoint(Hit.ImpactPoint+Hero->GetActorRightVector()*Side,SmallFloor)) continue;
                const FTransform SmallT(SmallFloor.ImpactPoint+FVector(0,0,35));
                auto* Small=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),SmallT);
                Row.FragmentMeshes=Row.WholeMeshes; Row.Label=FText::FromString(TEXT("CARRY PRACTICE"));
                Small->ConfigureItem(TEXT("CarryPractice"),Row,GripRandom,true); Small->Tags.Add(TEXT("DevGripFood")); Small->FinishSpawning(SmallT);
            }
            return FText::FromString(TEXT("Держи ЛКМ: большой куб толкается или тянется, два маленьких поднимаются над головой. Выбор по размеру на карте. WASD — движение, отпустить ЛКМ — сбросить оба, Q — бросить."));
        }
        return FText::FromString(TEXT("Нужны игрок и подвижный язык."));
    case EMCDevAction::TongueWeightToggle:
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            It->PressureSettings.bEnabled=!It->PressureSettings.bEnabled; It->ForceNetUpdate();
            return FText::FromString(It->PressureSettings.bEnabled?TEXT("Вес продавливает язык. Глубина и восстановление — DA_Tongue → Pressure."):TEXT("Продавливание отключено, существующие вмятины разглаживаются."));
        }
        return FText::FromString(TEXT("На карте нет подвижного языка."));
    case EMCDevAction::TongueWeight:
        if (Hero) for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            FHitResult Hits[2];
            for (int32 I=0;I<2;++I)
                if (!It->SurfacePoint(Hero->GetActorLocation()+Hero->GetActorForwardVector()*250+Hero->GetActorRightVector()*(I==0?-140:140),Hits[I]))
                    return FText::FromString(TEXT("Нужно свободное место на языке перед игроком."));
            for (TActorIterator<AMCFoodActor> Food(GetWorld());Food;++Food) if (Food->ActorHasTag(TEXT("DevPressureFood"))) Food->Destroy();
            It->PressureSettings.bEnabled=true; It->ForceNetUpdate();
            for (int32 I=0;I<2;++I)
            {
                auto* Food=GetWorld()->SpawnActor<AMCFoodActor>(Hits[I].ImpactPoint+FVector(0,0,260),FRotator::ZeroRotator);
                Food->Tags.Add(TEXT("DevPressureFood")); Food->Settings.Mass=I==0?4:28;
                Food->Body->SetMassOverrideInKg(NAME_None,Food->Settings.Mass,true); Food->ForceNetUpdate();
            }
            return FText::FromString(TEXT("Слева 4 кг, справа 28 кг. Потяни ЛКМ: тяжёлый удобнее вдвоём. После перемещения язык постепенно выпрямится."));
        }
        return FText::FromString(TEXT("Нужны игрок и подвижный язык."));
    case EMCDevAction::GazePractice:
        if (Hero) Hero->SpawnPracticeTooth();
        return FText::FromString(TEXT("Зуб перед тобой: обойди его, посмотри на глаза, затем урони рядом еду. Настройки взгляда — F1 и DA_Gaze."));
    case EMCDevAction::TongueMotion:
        if (Hero) for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            if (!It->Profile || !It->Profile->DevMotions.IsValidIndex(StepIndex)) return FText::FromString(TEXT("Профиль движения отсутствует."));
            FHitResult Hit; if (!It->SurfacePoint(Hero->GetActorLocation()+Hero->GetActorForwardVector()*180,Hit)) return FText::FromString(TEXT("Перед игроком нет языка."));
            const bool Started=It->PlayMotion(It->Profile->DevMotions[StepIndex],Hit.ImpactPoint,Hero->GetActorForwardVector());
            return FText::FromString(Started?TEXT("Движение началось перед игроком. Направление — куда смотрел зуб. F3 — играть."):TEXT("Дождись конца текущего движения и паузы."));
        }
        return FText::FromString(TEXT("Нужны игрок и подвижный язык."));
    case EMCDevAction::Yawn:
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) It->StartYawn();
        break;
    case EMCDevAction::Fire:
        if(Hero) AMCFirePatch::Ignite(Hero,Hero->GetActorLocation()+Hero->GetActorForwardVector()*160-FVector(0,0,55));
        break;
    case EMCDevAction::TongueJolt:
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
            return FText::FromString(It->TriggerJolt()?TEXT("Язык подожмётся и резко поднимется. F3 — закрыть панель и увидеть бросок."):TEXT("Дождись окончания текущей реакции языка."));
        return FText::FromString(TEXT("На этой карте нет подвижного языка."));
    case EMCDevAction::TongueUlcer:
    {
        if (!Hero) return FText::FromString(TEXT("Нужен персонаж хоста."));
        for (TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            FHitResult Hit;
            if (!It->SurfacePoint(Hero->GetActorLocation()+Hero->GetActorForwardVector()*180,Hit)) continue;
            auto* Patch=GetWorld()->SpawnActor<AMCMouthSurface>(Hit.ImpactPoint+Hit.ImpactNormal*5,FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
            Patch->bUlcer=true;
            if (const auto* Plan=GS->DayPlan.Get()) { Patch->HealSeconds=Plan->UlcerHealSeconds; Patch->DamagePerSecond=Plan->UlcerDamagePerSecond; Patch->DisturbDamage=Plan->UlcerDisturbDamage; }
            return FText::FromString(TEXT("Язва перед тобой: Space — перепрыгнуть волну. Слот 4 + удерживать ЛКМ — лечить 7 секунд; отпускание сохраняет прогресс."));
        }
        return FText::FromString(TEXT("Перед игроком нет языка. Переместись ближе к центру."));
    }
    case EMCDevAction::DropFood:
    case EMCDevAction::Infection:
    {
        if (!Hero) return FText::FromString(TEXT("Нужен персонаж хоста."));
        const FVector Ahead=Hero->GetActorLocation()+Hero->GetActorForwardVector()*220;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCDevFood),false,Hero);
        if (!GetWorld()->LineTraceSingleByChannel(Hit,Ahead+FVector(0,0,300),Ahead-FVector(0,0,1000),ECC_WorldStatic,Params))
            return FText::FromString(TEXT("Перед игроком нет пола. Переместись на арену."));
        AMCFoodActor* Food=nullptr;
        if(Action==EMCDevAction::Infection) {
            // Choose ordinary food explicitly: a random spicy row has its own hazard and cannot absorb.
            auto* Table=GS->DayPlan?GS->DayPlan->Menu.LoadSynchronous():nullptr;
            const auto* Row=Table?Table->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Absorption preview")):nullptr;
            if(Row) {
                const FTransform Transform(Hit.ImpactPoint+FVector(0,0,100));
                Food=GetWorld()->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Transform);
                if(Food) {
                    FMCFoodRow Config=*Row; Config.SpoilSeconds=3; FRandomStream PreviewRandom(41);
                    Food->ConfigureItem(TEXT("Egg"),Config,PreviewRandom); Food->Batch=2; Food->FinishSpawning(Transform);
                }
            }
        }
        else Food=DayDirector->SpawnMenuFood(Hit.ImpactPoint+FVector(0,0,650),2);
        return FText::FromString(Food?(Action==EMCDevAction::Infection?TEXT("Порча за 3 секунды для проверки. Язвы появляются от огня и взрыва чили."):TEXT("Еда падает перед тобой. Бей, хватай и тащи в глотку.")):TEXT("Не удалось создать еду."));
    }
    case EMCDevAction::DropBrushes: DayDirector->DropBrushes(); break;
    case EMCDevAction::CoffeeDirt: DayDirector->DirtyMouth(true); DayDirector->DropBrushes(); break;
    case EMCDevAction::LooseTeeth:
        for (AMCArenaTooth* Tooth:GS->ArenaTeeth) if (IsValid(Tooth) && Tooth->IsAvailable()) Tooth->Status->Loosen();
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if (It->Status->IsAlive()) It->Status->Loosen();
        break;
    case EMCDevAction::DamageSelf: if (Hero) Hero->Status->Damage(25,Hero->GetActorForwardVector()); break;
    case EMCDevAction::KillSelf: if (Hero) Hero->Status->Damage(10000,FVector::UpVector); break;
    case EMCDevAction::Ragdoll:
        if (Hero) Hero->ToothPhysics->ApplyHit(Hero->GetActorForwardVector()*600+FVector(0,0,300),Hero->GetActorLocation());
        break;
    case EMCDevAction::RestoreMouth: GS->MouthHealth=GS->RunSettings.MaxMouthHealth; break;
    case EMCDevAction::StopCoffee: if (IsValid(DayDirector->Flood)) DayDirector->Flood->Stop(); break;
    default: break;
    }
    return FText::FromString(TEXT("Готово. Физика и урон работают; автопереходы отключены. F3 — играть."));
#else
    return FText::GetEmpty();
#endif
}
