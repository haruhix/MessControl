#include "MCDevCommands.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

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
    if (static_cast<uint8>(Action)>static_cast<uint8>(EMCDevAction::TongueUlcer))
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
            return FText::FromString(TEXT("Язва перед тобой: наступи на неё — по языку пойдёт красная волна и толкнёт игроков. Без контакта заживает сама."));
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
        auto* Food=DayDirector->SpawnMenuFood(Hit.ImpactPoint+FVector(0,0,Action==EMCDevAction::Infection?100:650),2);
        if (Food && Action==EMCDevAction::Infection) Food->SpoilAt=GetWorld()->GetTimeSeconds()+.1;
        return FText::FromString(Food?(Action==EMCDevAction::Infection?TEXT("Перед тобой портится еда: убери её и защищай язву, пока она заживает."):TEXT("Еда падает перед тобой. Бей, хватай и тащи в глотку.")):TEXT("Не удалось создать еду."));
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
