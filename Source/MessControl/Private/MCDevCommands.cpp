#include "MCDevCommands.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCDayDirector.h"
#include "MCCoffeeFlood.h"
#include "MCColdCola.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCArenaTooth.h"
#include "MCTongue.h"
#include "MCMouthSurface.h"
#include "MCLocomotionSurface.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
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
            TEXT("Перец падает перед тобой: таймер 8–6 секунд по раунду. E — взять, Q — бросить в круг. Запуск увулы останавливает таймер."):
            TEXT("В круге два куска, один испорчен. Войди и нажми Space: глотка сократится и выплюнет заказ со струёй и брызгами. Пятна останутся на языке до чистки щёткой."));
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
            FMCFoodRow Row; Row.Label=FText::FromString(TEXT("GRIP PRACTICE")); Row.Mass=4; Row.HalfExtent=FVector(50); Row.SpoilSeconds=300;
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
        return FText::FromString(Food?(Action==EMCDevAction::Infection?TEXT("Оставь еду на языке на 3 секунды: она впитается и станет язвой. Слот 4 + удерживать ЛКМ — лечить."):TEXT("Еда падает перед тобой. Бей, хватай и тащи в глотку.")):TEXT("Не удалось создать еду."));
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
