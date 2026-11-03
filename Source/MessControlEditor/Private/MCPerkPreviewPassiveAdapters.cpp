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
#include "MCPerkPresentationEvents.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXNativeSources.h"
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
#include "UObject/UnrealType.h"

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
    if(Name==TEXT("Equipped"))return FText::FromString(TEXT("Получение пассивки"));
    if(Name==TEXT("State"))return FText::FromString(TEXT("Активное состояние"));
    if(Name==TEXT("Proc"))return FText::FromString(TEXT("Успешное срабатывание"));
    if(Name==TEXT("End"))return FText::FromString(TEXT("Завершение"));
    if(Name==TEXT("Hit"))return FText::FromString(TEXT("Принятое попадание"));
    if(Name==TEXT("Link"))return FText::FromString(TEXT("Настоящее звено цепочки"));
    if(Name==TEXT("Cleave"))return FText::FromString(TEXT("Дополнительная цель"));
    if(Name==TEXT("Windup"))return FText::FromString(TEXT("Подготовка эха"));
    if(Name==TEXT("Echo"))return FText::FromString(TEXT("Повторный удар"));
    if(Name==TEXT("Launch"))return FText::FromString(TEXT("Отбрасывание"));
    if(Name==TEXT("Impact"))return FText::FromString(TEXT("Вторичный контакт"));
    if(Name==TEXT("Vanish"))return FText::FromString(TEXT("Исчезновение"));
    if(Name==TEXT("Return"))return FText::FromString(TEXT("Возвращение"));
    if(Name==TEXT("Coat"))return FText::FromString(TEXT("Нанесение состава"));
    if(Name==TEXT("Refresh"))return FText::FromString(TEXT("Обновление состава"));
    if(Name==TEXT("Damage"))return FText::FromString(TEXT("Принятый урон от состава"));
    if(Name==TEXT("Contact"))return FText::FromString(TEXT("Контакт щётки"));
    if(Name==TEXT("FinishContact"))return FText::FromString(TEXT("Финальная очистка"));
    if(Name==TEXT("Clean"))return FText::FromString(TEXT("Фактическая очистка"));
    if(Name==TEXT("CarryStart"))return FText::FromString(TEXT("Начало переноса"));
    if(Name==TEXT("CarryEnd"))return FText::FromString(TEXT("Еда отпущена"));
    if(Name==TEXT("SprayStart"))return FText::FromString(TEXT("Начало распыления"));
    if(Name==TEXT("SprayEnd"))return FText::FromString(TEXT("Распыление завершено"));
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
    return FText::FromName(Name);
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
        const bool bAcquire=C.Scenario==EMCPerkPreviewScenario::Acquire;
        // Ownership is committed only after presentation/resource readiness.
        // Setup must not emit a saved canonical Equipped/State cue early.
        if(!PrepareResourceAnchors(C,Error)) return false;
        auto* Move=Cast<UMCToothMovementComponent>(C.Hero->GetCharacterMovement());
        if(!Move) {Error=TEXT("Нет штатного компонента движения");return false;}
        C.Hero->SetActorRotation(FRotator::ZeroRotator);
        // The evaluated authored skeleton faces actor +X at this neutral pose
        // (including its mesh-relative -90-degree yaw), as confirmed by the eyes/head.
        C.PreviewFront=C.Hero->GetActorForwardVector();
        Origin=C.Hero->GetActorLocation();
        Move->SetMovementMode(MOVE_Walking);
        if(bAcquire) {
            Status(C,TEXT("Пассивка будет выдана штатным ServerGrantPerk только после готовности ресурсов."));
            return true;
        }
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
            if(Spec.Context==EContext::Combat && (C.PerkID==TEXT("B2_MightyTooth") || C.PerkID==TEXT("B2_Billiards")))
                if(!MakeEnemy(C,FVector(180,65,0),Error) || !MakeEnemy(C,FVector(240,-30,0),Error)) return false;
            C.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
            // Native Thunder chains only to whole food or other teeth. Keep the
            // ordinary first enemy/weapon contact, then offer eligible real food;
            // more enemies can never satisfy the runtime chain target filter.
            if(C.PerkID==TEXT("B2_ThunderGod"))
                for(int32 I=0;I<3;++I)if(!MakeFood(C,I,Error,true))return false;
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
            // Offer enough real pieces for the native capacity resolver (<=12)
            // after grant; never duplicate the perk's balancing math here.
            const int32 Count=C.PerkID==TEXT("B1_BigPortion")?12:1;
            for(int32 I=0;I<Count;++I) if(!MakeFood(C,I,Error)) return false;
        }
        Status(C,TEXT("Родная механика и параметры DT. Слои читаются по ID пассивки и её настоящему событию."));
        return true;
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* P=C.Hero?C.Hero->PassivePresentation.Get():nullptr;
        if(!P){Error=TEXT("У зубика нет общего диспетчера представления пассивок.");return false;}
        Presentation=P;
        if(ReceiptHandle.IsValid())P->OnSourceEventAccepted.Remove(ReceiptHandle);
        ReceiptHandle=P->OnSourceEventAccepted.AddLambda([this,Source=C.PerkID,Record=C.RecordEvent]
            (FName SourceID,FName EventName,EMCVFXPresentationResult Result)
        {
            if(SourceID!=Source)return;
            if(Result!=EMCVFXPresentationResult::Rejected)ObservedSourceEvents.Add(EventName);
            if(Record)Record(Result==EMCVFXPresentationResult::Rejected?FName(TEXT("NativePassivePresentationRejected")):EventName);
        });
        const auto& Events=MCPassivePresentation::GetAllowedEvents(C.PerkID);
        if(C.PresentationOverride)return P->PrepareScopedOverride(C.PerkID,Events,C.PresentationOverride,Error);
        const auto* Row=C.PlayerState && C.PlayerState->Perks?C.PlayerState->Perks->FindDefinition(C.PerkID):nullptr;
        if(!Row){Error=TEXT("Сохранённая строка пассивки отсутствует.");return false;}
        auto* Profile=Row->VFXProfile.LoadSynchronous(); // explicit preparation, before the barrier
        if(!Row->VFXProfile.IsNull() && !Profile){Error=TEXT("Не удалось подготовить назначенный профиль пассивки.");return false;}
        return P->PrepareSourceProfile(C.PerkID,Events,Profile,Error,true);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {if(const auto* P=Presentation.Get())P->GetPreparedResources(Out);}

    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bStarted=true;
        const bool Granted=C.PlayerState->Perks->ServerGrantPerk(C.PerkID);
        Event(C,Granted?FName(TEXT("NativePassiveGrantAccepted")):FName(TEXT("NativePassiveGrantRejected")));
        if(!Granted){bDone=true;Status(C,TEXT("Штатная выдача пассивки отклонена."));return;}
        if(C.Scenario==EMCPerkPreviewScenario::Acquire) {
            Accepted=1;Status(C,TEXT("Настоящее получение пассивки после барьера готовности."));
            return;
        }
        auto Health=C.Hero->Status->State;
        Health.Health=Spec.Context==EContext::Damage && C.PerkID==TEXT("B2_Shield")?Health.MaxHealth*.15f:Health.MaxHealth*.55f;
        C.Hero->Status->Restore(Health);InitialHP=Health.Health;
        if(auto* Move=Cast<UMCToothMovementComponent>(C.Hero->GetCharacterMovement())) {
            auto Stamina=Move->GetStaminaPrediction();Stamina.Value=Move->GetMaxStamina()*.35f;Move->RestoreStaminaPrediction(Stamina);InitialStamina=Move->GetStamina();
        }
        Event(C,TEXT("NativeOwnedPassiveScenarioStarted"));
        if(Spec.Context==EContext::Spray && !CommitNativeSprayView(C)) {
            Release(C);bDone=true;Event(C,TEXT("NativeSprayViewRejected"));
            Status(C,TEXT("Не удалось подтвердить штатное направление спрея к настоящему пятну; контакт не подменяется."));return;
        }
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
        if(C.Scenario==EMCPerkPreviewScenario::Acquire) {if(Age>2.f)bDone=true;return;}
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
    void Cancel(FMCPerkPreviewContext& C) override
    {Release(C);if(auto* P=Presentation.Get())P->StopSource(C.PerkID,true);bDone=true;}
    void Reset(FMCPerkPreviewContext& C) override
    {
        Release(C);
        // These are only newly spawned disposable food fixtures, never borrowed
        // level actors. Reset must also clean whole Thunder targets before reuse.
        for(const auto& Food:Foods)if(Food.IsValid() && Food->GetWorld()==C.World && Food->HasAnyFlags(RF_Transient))Food->Destroy();
        Enemies.Reset();Stains.Reset();Foods.Reset();Age=0;LastStep=-1;Accepted=Attempts=0;
        if(auto* P=Presentation.Get()) {
            if(ReceiptHandle.IsValid())P->OnSourceEventAccepted.Remove(ReceiptHandle);
            P->StopSource(C.PerkID,true);P->ClearScopedOverride(C.PerkID);
        }
        ReceiptHandle.Reset();Presentation.Reset();
        ObservedSourceEvents.Reset();
        bDone=bStarted=bReleased=bDashRequested=bDashSeen=false;
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override {return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Сценарий завершён"):!bStarted?TEXT("Подготовка пассивки"):FString::Printf(TEXT("%s · родная механика"),Spec.Label);}
    float GetTailSeconds() const override {return 2.f;}

private:
    FSpec Spec;
    TWeakObjectPtr<UMCVFXPresentationComponent> Presentation;
    FDelegateHandle ReceiptHandle;
    TSet<FName> ObservedSourceEvents;
    TArray<TWeakObjectPtr<AMCNutEnemy>> Enemies;
    TArray<TWeakObjectPtr<AMCMouthSurface>> Stains;
    TArray<TWeakObjectPtr<AMCFoodActor>> Foods;
    FVector Origin=FVector::ZeroVector;
    float Age=0,LastStep=-1,InitialHP=0,InitialStamina=0;
    int32 Accepted=0,Attempts=0;
    bool bStarted=false,bDone=false,bReleased=false,bDashRequested=false,bDashSeen=false;

    bool CommitNativeSprayView(FMCPerkPreviewContext& C)
    {
        // The private preview deliberately has no LocalPlayer. Therefore the
        // production LocalSprayView cannot read its camera, even when the base
        // controller is local. Submit the same validated native view snapshot
        // as a real press; the ordinary inventory tick still owns all contact,
        // coating and cleaning. No production visibility/RNG/range is bypassed.
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !IsValid(C.Hero) || !C.Hero->HasAuthority() || C.Hero->GetWorld()!=C.World
            || !C.Hero->Inventory || !IsValid(C.AimCamera) || C.AimCamera->GetWorld()!=C.World
            || Stains.IsEmpty() || !Stains[0].IsValid())return false;
        const FVector ViewOrigin=C.AimCamera->GetActorLocation();
        const FVector Target=Stains[0]->GetActorLocation();
        const FVector ViewDirection=(Target-ViewOrigin).GetSafeNormal();
        if(ViewOrigin.ContainsNaN() || Target.ContainsNaN() || ViewDirection.IsNearlyZero())return false;
        struct FNativeSprayViewArgs {FVector_NetQuantize Origin;FVector_NetQuantizeNormal Direction;};
        UFunction* Function=C.Hero->Inventory->FindFunction(TEXT("ServerCommitSprayView"));
        const auto* OriginProperty=Function?FindFProperty<FStructProperty>(Function,TEXT("Origin")):nullptr;
        const auto* DirectionProperty=Function?FindFProperty<FStructProperty>(Function,TEXT("Direction")):nullptr;
        if(!Function || !Function->HasAllFunctionFlags(FUNC_Net|FUNC_NetServer|FUNC_NetReliable)
            || Function->NumParms!=2 || Function->ParmsSize!=sizeof(FNativeSprayViewArgs)
            || !OriginProperty || !DirectionProperty
            || OriginProperty->Struct!=FVector_NetQuantize::StaticStruct()
            || DirectionProperty->Struct!=FVector_NetQuantizeNormal::StaticStruct()
            || OriginProperty->GetOffset_ForUFunction()!=STRUCT_OFFSET(FNativeSprayViewArgs,Origin)
            || DirectionProperty->GetOffset_ForUFunction()!=STRUCT_OFFSET(FNativeSprayViewArgs,Direction))return false;
        FNativeSprayViewArgs Args{FVector_NetQuantize(ViewOrigin),FVector_NetQuantizeNormal(ViewDirection)};
        C.AimCamera->SetActorRotation(ViewDirection.Rotation());
        if(C.Controller)C.Controller->SetControlRotation(ViewDirection.Rotation());
        C.Hero->Inventory->ProcessEvent(Function,&Args);
        // Prove native aim acquisition accepted that input. A horizontal
        // fallback cannot satisfy this real stain-center check. Actual later
        // Coat/Clean receipts remain the separate authoritative contact proof.
        const FVector AcceptedAim=C.Hero->Inventory->SprayAim();
        if(AcceptedAim.ContainsNaN() || FVector::DistSquared(AcceptedAim,Target)>FMath::Square(8.f))return false;
        Event(C,TEXT("NativeSprayViewCommitted"));return true;
    }

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
    bool MakeFood(FMCPerkPreviewContext& C,int32 Index,FString& Error,bool bWholeChainTarget=false)
    {
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Menu?Menu->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Passive preview")):nullptr;
        if(!Row) {Error=TEXT("Нет сохранённого продукта Egg для родного подбора/броска");return false;}
        if(bWholeChainTarget && Row->Kind!=EMCFoodKind::Food) {Error=TEXT("Thunder preview requires the real whole-food Egg row.");return false;}
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.bDeferConstruction=true;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const float Forward=C.PerkID==TEXT("B1_GreedyHands")?C.Hero->FoodCollection->CollectionReach*1.15f:60+Index*3;
        const FVector Point=Origin+(bWholeChainTarget?FVector(255+Index*75,135+(Index%2)*110,0):FVector(Forward,(Index%2?1:-1)*25,0));
        auto* Food=C.World->SpawnActor<AMCFoodActor>(Point,FRotator::ZeroRotator,Spawn);
        if(!Food) {Error=TEXT("Не удалось создать настоящий фрагмент еды");return false;}
        FRandomStream Random(901+Index);Food->ConfigureItem(TEXT("Egg"),*Row,Random,!bWholeChainTarget);
        Food->FinishSpawning(FTransform(Point));
        // Stable warmup fixture: native collection/launch takes physics ownership
        // afterwards. Eight overlapping physics fragments must not scatter while
        // resources are preparing before the user presses play.
        Food->Body->SetSimulatePhysics(false);
        // Stable setup keeps the genuine whole food in native range/visibility.
        // HitFood still owns actual secondary damage, health and fragmentation.
        if(bWholeChainTarget) {
            // Initial target HP, like the enemy fixture's 1000 HP, keeps all
            // three whole targets available. Native damage/RNG are unchanged.
            Food->Health=1000.f;Food->SetActorTickEnabled(false);
        }
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
            Foods[0]->MarkManualThrow(C.Hero);Foods[0]->AwardDelivery();Event(C,TEXT("NativeAwardDeliveryCalled"));
            Status(C,FString::Printf(TEXT("Родная доставка: speed ×%.2f"),C.PlayerState->Perks->RogueValue(EMCRogueStat::NormalSpeed)));
        }
        bReleased=true;
    }
    void TickSpray(FMCPerkPreviewContext& C)
    {
        if(Age>3.2f && !bReleased) {C.Hero->SetPrimaryInputHeld(false);bReleased=true;Event(C,TEXT("SprayInputReleased"));}
        if(Accepted==0 && (ObservedSourceEvents.Contains(TEXT("SprayStart")) || ObservedSourceEvents.Contains(TEXT("Coat"))
            || ObservedSourceEvents.Contains(TEXT("Refresh")) || ObservedSourceEvents.Contains(TEXT("Clean")))) {
            Event(C,TEXT("NativeSprayEmitting"));Accepted=1;
        }
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
            Entry.Kind=EMCPerkPreviewAdapterKind::Passive;Entry.bSupportsProfileOverride=true;
            Entry.Stages=MCPassivePresentation::GetAllowedEvents(Entry.PerkID);
            Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::Acquire};
            for(FName Stage:Entry.Stages)Entry.StageLabels.Add(Stage,EventLabel(Stage));
            Entry.DescribeNativeSources=[ID=Entry.PerkID](FName Event){return MCVFXNativeSources::DescribePassiveFeedback(ID,Event);};
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Применение настоящей механики")));
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Acquire,FText::FromString(TEXT("Получение пассивки после прогрева")));
            Entry.Create=[Spec]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FPassiveAdapter>(Spec);};
            MCPerkPreviewAdapters::Register(MoveTemp(Entry));
        }
    }
} Register;
}
