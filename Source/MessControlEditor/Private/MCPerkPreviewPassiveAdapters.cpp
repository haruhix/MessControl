#include "MCPerkPreviewAdapter.h"

#include "MCCombatPassiveSubsystem.h"
#include "MCDefenseSprayPassives.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCMouthSurface.h"
#include "MCNutEnemy.h"
#include "MCPassiveCombatCue.h"
#include "MCPassiveFeedbackCue.h"
#include "MCPerkComponent.h"
#include "MCPerkEffect.h"
#include "MCPlayerState.h"
#include "MCResultPassives.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCCoffeeWipe.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

namespace MCPerkPassivePreview
{
enum class EContext : uint8 { Combat, Cleaning, Damage, Dash, Movement, Food, Spray };

struct FSpec {const TCHAR* ID;const TCHAR* Label;EContext Context;const TCHAR* Event;};
// IDs are the canonical runtime IDs, not copies of balancing data. Tuning is
// always read from the actual saved DT through the real player perk component.
const FSpec Specs[]={
    {TEXT("B1_HeavyHit"),TEXT("Тяжёлый удар"),EContext::Combat,TEXT("MeleeHit")},
    {TEXT("B1_LightFoot"),TEXT("Лёгкая походка"),EContext::Movement,TEXT("Movement")},
    {TEXT("B1_DeepBreath"),TEXT("Глубокий вдох"),EContext::Movement,TEXT("Stamina")},
    {TEXT("B1_BigPortion"),TEXT("Большая порция"),EContext::Food,TEXT("Carry")},
    {TEXT("B1_ConfidentThrow"),TEXT("Уверенный бросок"),EContext::Food,TEXT("Throw")},
    {TEXT("B1_FreshDelivery"),TEXT("Свежая доставка"),EContext::Food,TEXT("Carry")},
    {TEXT("B1_GreedyHands"),TEXT("Руки загребущие"),EContext::Food,TEXT("Pickup")},
    {TEXT("B1_WideBristles"),TEXT("Широкая щетина"),EContext::Cleaning,TEXT("BrushContact")},
    {TEXT("B1_UnderPressure"),TEXT("Под давлением"),EContext::Spray,TEXT("Spray")},
    {TEXT("B2_ReturnTrip"),TEXT("Обратный рейс"),EContext::Food,TEXT("Delivery")},
    {TEXT("B2_CleanBreath"),TEXT("Чистое дыхание"),EContext::Cleaning,TEXT("CleanComplete")},
    {TEXT("B2_NeatFreak"),TEXT("Чистюля"),EContext::Cleaning,TEXT("CleanComplete")},
    {TEXT("B2_LastStroke"),TEXT("Последний штрих"),EContext::Cleaning,TEXT("BrushContact")},
    {TEXT("B2_CleanAcceleration"),TEXT("Чистый разгон"),EContext::Cleaning,TEXT("CleanComplete")},
    {TEXT("B2_RefreshingWork"),TEXT("Освежающий труд"),EContext::Cleaning,TEXT("MouthClean")},
    {TEXT("B2_ThickEnamel"),TEXT("Толстая эмаль"),EContext::Damage,TEXT("IncomingDamage")},
    {TEXT("B2_Shield"),TEXT("Щит"),EContext::Damage,TEXT("IncomingDamage")},
    {TEXT("B2_SecondWind"),TEXT("Второе дыхание"),EContext::Dash,TEXT("DodgeThreat")},
    {TEXT("B2_HoudiniDash"),TEXT("Рывок Гудини"),EContext::Dash,TEXT("Dash")},
    {TEXT("B2_MightyTooth"),TEXT("Могучий зуб"),EContext::Combat,TEXT("MeleeHit")},
    {TEXT("B2_StartFromDash"),TEXT("С места в карьер"),EContext::Combat,TEXT("MeleeAfterDash")},
    {TEXT("B2_ThunderGod"),TEXT("Бог грома"),EContext::Combat,TEXT("MeleeHit")},
    {TEXT("B2_EchoSwing"),TEXT("Эхо замаха"),EContext::Combat,TEXT("MeleeHit")},
    {TEXT("B2_Billiards"),TEXT("Бильярд"),EContext::Combat,TEXT("MeleeHit")},
    {TEXT("B2_Softener"),TEXT("Размягчитель"),EContext::Spray,TEXT("SprayThenBrush")},
    {TEXT("B2_Antiseptic"),TEXT("Антисептик"),EContext::Spray,TEXT("Spray")},
    {TEXT("B2_LastingFormula"),TEXT("Стойкая формула"),EContext::Spray,TEXT("SprayResidue")}
};

void Event(FMCPerkPreviewContext& C,FName Name) {if(C.RecordEvent) C.RecordEvent(Name);}
void Status(FMCPerkPreviewContext& C,const FString& Text) {if(C.ReportStatus) C.ReportStatus(Text);}
FText EventLabel(FName Name)
{
    if(Name==TEXT("MeleeHit")) return FText::FromString(TEXT("Попадание оружием"));
    if(Name==TEXT("MeleeAfterDash")) return FText::FromString(TEXT("Удар после рывка"));
    if(Name==TEXT("BrushContact")) return FText::FromString(TEXT("Контакт щётки"));
    if(Name==TEXT("CleanComplete")) return FText::FromString(TEXT("Завершение уборки"));
    if(Name==TEXT("MouthClean")) return FText::FromString(TEXT("Рот полностью чистый"));
    if(Name==TEXT("IncomingDamage")) return FText::FromString(TEXT("Получение урона"));
    if(Name==TEXT("DodgeThreat")) return FText::FromString(TEXT("Уклонение от угрозы"));
    if(Name==TEXT("Dash")) return FText::FromString(TEXT("Рывок"));
    if(Name==TEXT("Movement")) return FText::FromString(TEXT("Движение"));
    if(Name==TEXT("Stamina")) return FText::FromString(TEXT("Запас выносливости"));
    if(Name==TEXT("Carry")) return FText::FromString(TEXT("Перенос еды"));
    if(Name==TEXT("Throw")) return FText::FromString(TEXT("Бросок еды"));
    if(Name==TEXT("Pickup")) return FText::FromString(TEXT("Подбор еды"));
    if(Name==TEXT("Delivery")) return FText::FromString(TEXT("Доставка еды"));
    if(Name==TEXT("SprayThenBrush")) return FText::FromString(TEXT("Распыление → щётка"));
    if(Name==TEXT("SprayResidue")) return FText::FromString(TEXT("Остаточный состав"));
    return FText::FromString(TEXT("Распыление"));
}

/** One bounded native scenario, kept separate from production gameplay. There
 * are deliberately no AMCPassiveFeedbackCue::Spawn / PlayVFX calls here. */
class FPassiveAdapter final : public IMCPerkPreviewAdapter
{
public:
    explicit FPassiveAdapter(FSpec InSpec):Spec(InSpec) {}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || !C.Hero || !C.PlayerState || !C.Controller || C.World->WorldType==EWorldType::Editor
            || C.World->WorldType==EWorldType::PIE || !C.Hero->HasAuthority() || C.Hero->GetWorld()!=C.World
            || !C.Hero->Status || !C.Hero->Inventory || !C.Hero->FoodCollection || !C.PlayerState->Perks) {
            Error=TEXT("Пассивка требует отдельный полноценный мир предпросмотра с зубиком и PlayerState");return false;
        }
        auto* Perks=C.PlayerState->Perks.Get();
        const auto* Row=Perks->FindDefinition(C.PerkID);
        if(!Row || !Row->bEnabled || Row->Kind!=EMCPerkKind::Passive || !Row->EffectClass) {
            Error=TEXT("В текущей сохранённой DT нет включённой пассивки с рабочим EffectClass");return false;
        }
        if(Spec.Context==EContext::Cleaning && !UMCResultPassives::ForWorld(C.Hero)) {
            Error=TEXT("В этом preview World ещё нет UMCResultPassives; очистку нельзя достоверно показать");return false;
        }
        if(C.PerkID==TEXT("B2_ReturnTrip") && !C.World->GetAuthGameMode<AMCGameMode>()) {
            Error=TEXT("Обратный рейс ожидает preview GameMode с настоящим AwardDelivery; отдельный визуальный proc не подменяет доставку");return false;
        }
        if(auto* GS=C.World->GetGameState<AMCGameState>()) {
            GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;GS->bTutorialActive=false;
            if(!GS->PlayerArray.Contains(C.PlayerState)) GS->AddPlayerState(C.PlayerState);
        } else if(C.PerkID==TEXT("B2_RefreshingWork")) {
            Error=TEXT("Освежающий труд ожидает настоящий GameState/PlayerArray в песочнице");return false;
        }
        if(!Perks->ServerGrantPerk(C.PerkID) && Perks->GetStacks(C.PerkID)<=0) {
            Error=TEXT("Штатная выдача пассивки отклонена текущей DT/состоянием игрока");return false;
        }
        if(!PrepareResourceAnchors(C,Error)) return false;
        auto* Move=Cast<UMCToothMovementComponent>(C.Hero->GetCharacterMovement());
        if(!Move) {Error=TEXT("Нет штатного компонента движения");return false;}
        C.Hero->SetActorRotation(FRotator::ZeroRotator);
        Origin=C.Hero->GetActorLocation();
        Move->SetMovementMode(MOVE_Walking);
        // Health/stamina are explicit scenario starting conditions, not perk
        // outcomes. All later damage, recovery and procs use gameplay owners.
        auto Health=C.Hero->Status->State;
        Health.Health=Spec.Context==EContext::Damage && C.PerkID==TEXT("B2_Shield")?Health.MaxHealth*.15f:Health.MaxHealth*.55f;
        C.Hero->Status->Restore(Health);InitialHP=C.Hero->Status->State.Health;
        auto Stamina=Move->GetStaminaPrediction();Stamina.Value=Move->GetMaxStamina()*.35f;Move->RestoreStaminaPrediction(Stamina);
        InitialStamina=Move->GetStamina();
        if(Spec.Context==EContext::Combat || Spec.Context==EContext::Damage || Spec.Context==EContext::Dash) {
            // Place the post-dash target before playback, beyond the real
            // authored root-motion dash distance. It must not stay behind the
            // tooth or be teleported into a swing after the dash has happened.
            const float TargetForward=C.PerkID==TEXT("B2_StartFromDash")
                ?FMath::Clamp(Move->DashSpeed,500.f,1600.f)*Move->GetDashDuration()+135.f:135.f;
            if(!MakeEnemy(C,FVector(TargetForward,0,0),Error)) return false;
            if(Spec.Context==EContext::Combat && (C.PerkID==TEXT("B2_MightyTooth") || C.PerkID==TEXT("B2_ThunderGod") || C.PerkID==TEXT("B2_Billiards")))
                if(!MakeEnemy(C,FVector(180,65,0),Error) || !MakeEnemy(C,FVector(240,-30,0),Error)) return false;
            C.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
        }
        if(Spec.Context==EContext::Cleaning || Spec.Context==EContext::Spray) {
            if(!MakeStain(C,FVector(115,0,0),Error)) return false;
            if(C.PerkID==TEXT("B2_NeatFreak"))
                if(!MakeStain(C,FVector(160,150,0),Error) || !MakeStain(C,FVector(250,-100,0),Error)) return false;
            C.Hero->Inventory->ServerSelect(Spec.Context==EContext::Spray?EMCToolSlot::Spray:EMCToolSlot::Brush);
            if(Spec.Context==EContext::Spray && C.AimCamera) {
                const FRotator Aim(-45,0,0);
                C.AimCamera->SetActorLocationAndRotation(Origin+FVector(0,0,35),Aim);
                C.Controller->SetControlRotation(Aim);C.Controller->SetViewTarget(C.AimCamera);
            }
        }
        if(Spec.Context==EContext::Food) {
            const int32 Count=C.PerkID==TEXT("B1_BigPortion")?C.Hero->FoodCollection->EffectiveCapacity():1;
            for(int32 I=0;I<Count;++I) if(!MakeFood(C,I,Error)) return false;
        }
        Status(C,TEXT("Родная механика и текущие параметры DT; процедурные VFX пассивки пока не поддерживают подмену через Composer"));
        return true;
    }

    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bStarted=true;Event(C,TEXT("PassiveAcquired"));
        if(Spec.Context==EContext::Spray || Spec.Context==EContext::Cleaning) C.Hero->SetPrimaryInputHeld(true);
        if(Spec.Context==EContext::Food) {
            C.Hero->SetFoodCollectionInput(true);
            for(const auto& Food:Foods) if(Food.IsValid() && C.Hero->FoodCollection->Collect(Food.Get())) ++Accepted;
            Event(C,Accepted>0?FName(TEXT("FoodCollected")):FName(TEXT("FoodPickupRejected")));
            Status(C,FString::Printf(TEXT("Настоящий стек: %d/%d; радиус %.0f см; бросок %.0f см/с. Отдельный proc VFX не выдуман"),
                C.Hero->FoodCollection->Pieces.Num(),C.Hero->FoodCollection->EffectiveCapacity(),
                Foods.IsEmpty()?0.f:C.Hero->FoodCollection->EffectiveCollectionReach(Foods[0].Get()),C.Hero->FoodCollection->EffectiveThrowSpeed()));
        }
    }

    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bStarted || bDone) return;
        if(!IsValid(C.Hero) || !C.Hero->Status || !C.Hero->Status->IsAlive()) {
            Release(C);bDone=true;Event(C,TEXT("PassiveScenarioInterrupted"));
            Status(C,TEXT("Сценарий прерван потерей/смертью зубика; результат не подтверждён"));return;
        }
        Age+=FMath::Clamp(Dt,0.f,.1f);
        switch(Spec.Context) {
        case EContext::Combat: TickCombat(C,Dt);break;
        case EContext::Cleaning: TickCleaning(C,Dt);break;
        case EContext::Damage: TickDamage(C);break;
        case EContext::Dash: TickDash(C);break;
        case EContext::Movement: TickMovement(C);break;
        case EContext::Food: TickFood(C);break;
        case EContext::Spray: TickSpray(C);break;
        }
        if(Age>MaxSeconds()) {
            Release(C);bDone=true;Event(C,TEXT("PassiveScenarioFinished"));
            if(Accepted==0) Status(C,TEXT("Сценарий завершён, но родной контакт/результат не подтверждён; это не успешное превью эффекта"));
        }
    }
    void Release(FMCPerkPreviewContext& C) override
    {
        if(IsValid(C.Hero)) {C.Hero->SetPrimaryInputHeld(false);C.Hero->SetFoodCollectionInput(false);C.Hero->SetSprintInputHeld(false);}
    }
    void Cancel(FMCPerkPreviewContext& C) override {Release(C);bDone=true;}
    void Reset(FMCPerkPreviewContext& C) override
    {
        Release(C);Enemies.Reset();Stains.Reset();Foods.Reset();Age=0;LastStep=-1;Accepted=Attempts=0;
        bDone=bStarted=bReleased=bDashRequested=bDashSeen=false;
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override {return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Сценарий завершён"):!bStarted?TEXT("Подготовка пассивки"):FString::Printf(TEXT("%s · родная механика"),Spec.Label);}
    float GetTailSeconds() const override {return 2.f;}

private:
    FSpec Spec;
    TArray<TWeakObjectPtr<AMCNutEnemy>> Enemies;
    TArray<TWeakObjectPtr<AMCMouthSurface>> Stains;
    TArray<TWeakObjectPtr<AMCFoodActor>> Foods;
    FVector Origin=FVector::ZeroVector;
    float Age=0,LastStep=-1,InitialHP=0,InitialStamina=0;
    int32 Accepted=0,Attempts=0;
    bool bStarted=false,bDone=false,bReleased=false,bDashRequested=false,bDashSeen=false;

    bool PrepareResourceAnchors(FMCPerkPreviewContext& C,FString& Error)
    {
        if(!C.AddBox) {Error=TEXT("Не предоставлена подготовка ресурсов preview World");return false;}
        const auto* Cue=GetDefault<AMCPassiveFeedbackCue>();
        const auto* Combat=GetDefault<AMCPassiveCombatCue>();
        const auto* Coating=GetDefault<AMCSprayCoating>();
        const TArray<UMaterialInterface*> Materials={Cue->SoftMaterial.Get(),Cue->GlowMaterial.Get(),Cue->SolidMaterial.Get(),
            Combat->LinkMaterial.Get(),Combat->GhostMaterial.Get(),Coating->CoatingMaterial.Get()};
        for(int32 Index=0;Index<Materials.Num();++Index) {
            if(!Materials[Index]) {Error=TEXT("Не загружен родной материал passive cue; запуск не будет маскировать позднюю загрузку");return false;}
            auto* Anchor=C.AddBox(FName(*FString::Printf(TEXT("PassiveResource_%d"),Index)),FVector(0,0,-10000-Index*10),FVector(.001f));
            if(!Anchor) {Error=TEXT("Не удалось подготовить material anchor");return false;}
            Anchor->SetActorHiddenInGame(true);Anchor->SetActorEnableCollision(false);
            Anchor->GetStaticMeshComponent()->SetMaterial(0,Materials[Index]);
            C.Targets.Add(Anchor);
        }
        return true;
    }

    float MaxSeconds() const
    {return Spec.Context==EContext::Spray?8.f:Spec.Context==EContext::Combat && FCString::Strcmp(Spec.ID,TEXT("B2_ThunderGod"))==0?7.f:5.f;}
    bool MakeEnemy(FMCPerkPreviewContext& C,FVector Offset,FString& Error)
    {
        UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole"));
        if(!Mesh) {Error=TEXT("Не найден сохранённый меш ореха для родной цели");return false;}
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Point=FVector(Origin.X+Offset.X,Origin.Y+Offset.Y,36);
        auto* Enemy=C.World->SpawnActor<AMCNutEnemy>(Point,FRotator(0,180,0),Spawn);
        if(!Enemy) {Error=TEXT("Не удалось создать настоящую цель ореха");return false;}
        Enemy->Settings.MaxHealth=1000;Enemy->Settings.BodyRadius=30;Enemy->Settings.AttackDamage=0;
        Enemy->NutMesh=Mesh;Enemy->MeshScale=FVector(60.f/FMath::Max(1.f,Mesh->GetBounds().BoxExtent.Z*2.f));Enemy->Health=1000;
        Enemy->FinishSpawning(FTransform(FRotator(0,180,0),Point));
        // Keep the enemy target stationary: this fixture has no arena Tongue.
        // Its native damage resolver and feedback remain active. Billiards uses
        // the public movement owner explicitly, rather than pretending AI ran.
        Enemy->SetActorTickEnabled(false);Enemies.Add(Enemy);C.Targets.Add(Enemy);return true;
    }
    bool MakeStain(FMCPerkPreviewContext& C,FVector Offset,FString& Error)
    {
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Point(Origin.X+Offset.X,Origin.Y+Offset.Y,3);
        auto* Stain=C.World->SpawnActor<AMCMouthSurface>(Point,FRotator::ZeroRotator,Spawn);
        if(!Stain) {Error=TEXT("Не удалось создать настоящее пятно");return false;}
        Stain->bRandomizeLiquidSize=false;Stain->LiquidHalfSize=85;Stain->GroundResponse=EMCGroundSurface::Normal;
        Stain->FinishSpawning(FTransform(Point));Stain->Status->ApplyCoffee(1);
        TArray<uint8> Deposit;Deposit.Init(0,FMCCoffeeWipe::Count);
        for(int32 Y=22;Y<42;++Y) for(int32 X=22;X<42;++X)
            if(FMath::Square(X-32)+FMath::Square(Y-32)<90) Deposit[Y*FMCCoffeeWipe::Size+X]=255;
        Stain->SetLiquidDepositMask(Deposit);Stains.Add(Stain);C.Targets.Add(Stain);return true;
    }
    bool MakeFood(FMCPerkPreviewContext& C,int32 Index,FString& Error)
    {
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Menu?Menu->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Passive preview")):nullptr;
        if(!Row) {Error=TEXT("Нет сохранённого продукта Egg для родного подбора/броска");return false;}
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const float Forward=C.PerkID==TEXT("B1_GreedyHands")?C.Hero->FoodCollection->CollectionReach*1.15f:60+Index*3;
        const FVector Point=Origin+FVector(Forward,(Index%2?1:-1)*25,0);
        auto* Food=C.World->SpawnActor<AMCFoodActor>(Point,FRotator::ZeroRotator,Spawn);
        if(!Food) {Error=TEXT("Не удалось создать настоящий фрагмент еды");return false;}
        FRandomStream Random(901+Index);Food->ConfigureItem(TEXT("Egg"),*Row,Random,true);
        Food->FinishSpawning(FTransform(Point));
        // Stable warmup fixture: native collection/launch takes physics ownership
        // afterwards. Eight overlapping physics fragments must not scatter while
        // resources are preparing before the user presses play.
        Food->Body->SetSimulatePhysics(false);
        Foods.Add(Food);C.Targets.Add(Food);return true;
    }
    void TickCombat(FMCPerkPreviewContext& C,float Dt)
    {
        if(C.PerkID==TEXT("B2_StartFromDash") && !bDashRequested) {
            C.Hero->RequestDashInput();bDashRequested=true;Event(C,TEXT("DashInput"));
        }
        if(C.Hero->IsDashing()) bDashSeen=true;
        const float First=C.PerkID==TEXT("B2_StartFromDash")?.75f:.35f;
        const int32 Maximum=C.PerkID==TEXT("B2_ThunderGod")?6:1;
        if(Age>=First && Age-LastStep>=1.f && Attempts<Maximum) {
            // Native melee input owns pose, hit timing, actual damage and procs.
            C.Hero->SwingBrush();LastStep=Age;++Attempts;Event(C,TEXT("MeleeInput"));
        }
        for(const auto& E:Enemies) if(E.IsValid()) {
            if(C.PerkID==TEXT("B2_Billiards")) UMCCombatPassiveSubsystem::TickBilliards(E.Get(),Dt);
            if(E->Health<1000 && Accepted==0) {++Accepted;Event(C,TEXT("AcceptedMeleeHit"));}
        }
    }
    void TickCleaning(FMCPerkPreviewContext& C,float Dt)
    {
        if(Age<.6f) return;
        // A native spatial contact fixture, not an invented VFX timeline. It
        // deliberately excludes range/acquisition from its acceptance claim;
        // native BrushLiquid input above is allowed to acquire first.
        auto* Stain=Stains.IsEmpty()?nullptr:Stains[0].Get();
        if(!Stain || Stain->IsClean()) return;
        const float Radius=C.Hero->Inventory->CleaningRadius(Stain->LiquidHalfSize);
        const float Rate=C.Hero->Inventory->CleaningSpeedMultiplier()
            *UMCDefenseSprayPassives::BrushMultiplier(Stain,Stain->GetActorLocation());
        if(Stain->CleanLiquidAt(C.Hero,Stain->GetActorLocation(),Radius,Dt,Rate,EMCStainCleanReason::Brush)) {
            if(!bReleased) {Event(C,TEXT("AcceptedSpatialBrushContact"));bReleased=true;}
            if(Stain->IsClean()) {++Accepted;Event(C,TEXT("CleanComplete"));
                Status(C,FString::Printf(TEXT("Родное завершение пятна: HP %.0f → %.0f; speed ×%.2f. Пространственный contact fixture, не полная проверка захвата щёткой"),
                    InitialHP,C.Hero->Status->State.Health,C.PlayerState->Perks->RogueValue(EMCRogueStat::NormalSpeed)));}
        }
    }
    void TickDamage(FMCPerkPreviewContext& C)
    {
        if(Age<.6f || Age-LastStep<.7f || Attempts>=4) return;
        const float Before=C.Hero->Status->State.Health;
        const float Amount=C.PerkID==TEXT("B2_Shield")?2.f:6.f;
        const bool Applied=C.Hero->Status->Damage(Amount,-FVector::ForwardVector,Enemies.IsEmpty()?nullptr:Enemies[0].Get());
        LastStep=Age;++Attempts;++Accepted;Event(C,Applied?FName(TEXT("DamageAccepted")):FName(TEXT("DamageRejected")));
        Status(C,FString::Printf(TEXT("Реальный Damage: HP %.1f → %.1f; шанс из DT не принудительный, отсутствие proc может быть нормальным исходом"),Before,C.Hero->Status->State.Health));
    }
    void TickDash(FMCPerkPreviewContext& C)
    {
        if(Age>.4f && !bDashRequested) {
            if(C.PerkID==TEXT("B2_SecondWind") && !Enemies.IsEmpty()) {
                UMCDefenseSprayPassives::RegisterThreat(C.Hero,Enemies[0].Get(),1);
                Event(C,TEXT("NativeThreatRegistered"));
            }
            C.Hero->RequestDashInput();bDashRequested=true;Event(C,TEXT("DashInput"));
        }
        if(C.Hero->IsDashing() && !bDashSeen) {bDashSeen=true;++Accepted;Event(C,TEXT("NativeDashStarted"));}
        if(bDashSeen && !C.Hero->IsDashing() && !bReleased) {
            if(C.PerkID==TEXT("B2_SecondWind") && !Enemies.IsEmpty())
                UMCDefenseSprayPassives::NotifyAttackFinished(Enemies[0].Get(),1,false);
            bReleased=true;Event(C,TEXT("NativeDashFinished"));
            const auto* Move=Cast<UMCToothMovementComponent>(C.Hero->GetCharacterMovement());
            Status(C,FString::Printf(TEXT("Настоящий рывок: stamina %.1f → %.1f. Для Второго дыхания используется native threat receipt, не полный бой AI"),InitialStamina,Move?Move->GetStamina():0));
        }
    }
    void TickMovement(FMCPerkPreviewContext& C)
    {
        if(Age>.5f && Age<2.0f) {C.Hero->AddMovementInput(FVector::ForwardVector,.45f);++Accepted;}
        if(!bReleased && Age>2.f) {
            bReleased=true;Event(C,TEXT("NativeMovementSample"));
            const auto* Move=Cast<UMCToothMovementComponent>(C.Hero->GetCharacterMovement());
            Status(C,FString::Printf(TEXT("Числовая пассивка без отдельного proc VFX: max speed %.1f, stamina %.1f/%.1f; реальные значения компонента"),
                Move?Move->GetMaxSpeed():0,Move?Move->GetStamina():0,Move?Move->GetMaxStamina():0));
        }
    }
    void TickFood(FMCPerkPreviewContext& C)
    {
        if(bReleased || Age<1.2f) return;
        if(C.PerkID==TEXT("B1_ConfidentThrow")) {
            const bool Thrown=C.Hero->FoodCollection->ThrowOne(FVector(1,0,.35f).GetSafeNormal());
            Event(C,Thrown?FName(TEXT("NativeFoodThrown")):FName(TEXT("FoodThrowRejected")));
        } else if(C.PerkID==TEXT("B2_ReturnTrip") && !Foods.IsEmpty() && Foods[0].IsValid()) {
            Foods[0]->MarkManualThrow(C.Hero);Foods[0]->AwardDelivery();Event(C,TEXT("NativeAwardDelivery"));
            Status(C,FString::Printf(TEXT("Родная доставка: speed ×%.2f"),C.PlayerState->Perks->RogueValue(EMCRogueStat::NormalSpeed)));
        }
        bReleased=true;
    }
    void TickSpray(FMCPerkPreviewContext& C)
    {
        if(Age>3.2f && !bReleased) {C.Hero->SetPrimaryInputHeld(false);bReleased=true;Event(C,TEXT("SprayInputReleased"));}
        if(C.Hero->Inventory->IsSprayCycleEmitting()) {if(Accepted==0) Event(C,TEXT("NativeSprayEmitting"));++Accepted;}
        if(C.PerkID==TEXT("B2_Softener") && Age>3.8f && Attempts==0) {
            C.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);C.Hero->SetPrimaryInputHeld(true);++Attempts;
            Event(C,TEXT("BrushAfterSprayInput"));
        }
    }
};

struct FRegister
{
    FRegister()
    {
        for(const FSpec& Spec:Specs) {
            FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=FName(Spec.ID);Entry.Label=FText::FromString(Spec.Label);
            Entry.Kind=EMCPerkPreviewAdapterKind::Passive;Entry.bSupportsProfileOverride=false;
            Entry.Stages={FName(Spec.Event)};Entry.Scenarios={EMCPerkPreviewScenario::Target};
            Entry.StageLabels.Add(FName(Spec.Event),EventLabel(FName(Spec.Event)));
            Entry.Create=[Spec]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FPassiveAdapter>(Spec);};
            MCPerkPreviewAdapters::Register(MoveTemp(Entry));
        }
    }
} Register;
}
