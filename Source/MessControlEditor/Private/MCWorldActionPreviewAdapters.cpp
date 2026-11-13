#include "MCWorldActionPreviewAdapters.h"
#include "MCStainSurfacePreviewAdapter.h"
#include "MCSprayActionPreviewAdapter.h"
#include "MCColdColaPreviewAdapter.h"
#include "MCPianoActionPreviewAdapter.h"
#include "MCHurtActionPreviewAdapters.h"
#include "MCThroatSuctionPreviewAdapter.h"
#include "MCYawnWaterPreviewAdapters.h"
#include "MCNativeWorldActionPreviewAdapters.h"
#include "MCFoodRewardPreviewAdapters.h"
#include "MCFoodAutoMouthPreviewAdapter.h"
#include "MCNutThrowPreviewAdapter.h"
#include "MCStageBoosterPreviewAdapter.h"
#include "MCCoffeeRiverPreviewAdapter.h"
#include "MCFrostActionPreviewAdapters.h"
#include "MCIceActionPreviewAdapters.h"
#include "MCFreezeDeathPreviewAdapter.h"
#include "MCJellyActionPreviewAdapters.h"
#include "NiagaraSystem.h"

#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXActionCatalog.h"
#include "MCVFXProfileBinding.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCBrushContactComponent.h"
#include "MCToothCareFeedbackComponent.h"
#include "MCToothStatusComponent.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCThroat.h"
#include "MCDayPlan.h"
#include "Engine/DataTable.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
/** Real input/swing; Target also prepares a real saved-menu food contact. */
class FOrdinaryToolSwingAdapter final : public IMCPerkPreviewAdapter
{
public:
    explicit FOrdinaryToolSwingAdapter(EMCToolSlot InSlot,bool InKatana=false):Slot(InSlot),bKatana(InKatana){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World||!IsValid(C.Hero)||!C.Hero->HasAuthority()||!C.Hero->Inventory)
        {Error=TEXT("Нужен настоящий персонаж изолированной песочницы с обычным инвентарём.");return false;}
        ID=bKatana?TEXT("Tool.Katana"):Slot==EMCToolSlot::Knife?TEXT("Tool.Knife"):TEXT("Tool.Pickaxe");
        PreviousSlot=C.Hero->Inventory->Selected;
        if(bKatana) {
            const auto* Mask=FindFProperty<FByteProperty>(UMCInventoryComponent::StaticClass(),TEXT("DevToolUpgradeMask"));
            if(!Mask){Error=TEXT("Нет временного разрешения катаны для песочницы.");return false;}
            PreviousUpgradeMask=Mask->GetPropertyValue_InContainer(C.Hero->Inventory);
        }
        if(bKatana && !C.Hero->Inventory->HasUpgrade(EMCToolUpgrade::Chainsaw))
            C.Hero->Inventory->GrantDevToolUpgrade(EMCToolUpgrade::Chainsaw);
        C.Hero->SetPrimaryInputHeld(false);C.Hero->Inventory->ServerSelect(Slot);
        if(C.Hero->Inventory->Selected!=Slot){Error=TEXT("Штатный выбор инструмента отклонён.");return false;}
        Before=C.Hero->ValidatedSwingCount;HitsBefore=C.Hero->ConfirmedHitCount;FoodCutsBefore=C.Hero->ConfirmedFoodCutCount;Age=0;bAccepted=bHit=bCut=bReleased=bDone=false;
        bContactScenario=C.Scenario==EMCPerkPreviewScenario::Target;
        FoodBefore.Reset();if(bContactScenario)for(TActorIterator<AMCFoodActor> It(C.World);It;++It)FoodBefore.Add(*It);
        if(bContactScenario&&!PrepareContactTarget(C,Error))return false;
        Presentation=MCWorldPresentation::PrepareOwner(C.Hero,{ID});
        if(!Presentation.IsValid()){Error=TEXT("У инструмента нет источника Composer.");return false;}
        if(C.ReportStatus)C.ReportStatus(bContactScenario?TEXT("Настоящая еда: штатный выбор инструмента → ввод → взмах → принятый контакт и изменение HP цели."):
            TEXT("Настоящий взмах без цели: выбор инструмента → штатный основной ввод → принятый взмах. Попадание этим сценарием не заявляется."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();if(!Owner||!Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(ID);if(!Source){Error=TEXT("Профиль инструмента ещё не подготовлен.");return false;}
        const auto* Contract=MCVFXSourceContracts::Find(ID);
        return !C.PresentationOverride||Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {if(const auto* Owner=Presentation.Get())if(const auto* Source=Owner->GetSource(ID))Source->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C) override
    {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(true);}
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        Age+=Dt;
        if(!IsValid(C.Hero)){bDone=true;return;}
        if(!bAccepted&&C.Hero->ValidatedSwingCount>Before){bAccepted=true;if(C.RecordEvent)C.RecordEvent(TEXT("Swing"));}
        if(bContactScenario&&!bHit&&C.Hero->ConfirmedHitCount>HitsBefore) {
            const auto* Food=ContactFood.Get();
            if(!IsValid(Food)||Food->IsDisposed()||Food->Health<InitialHealth) {bHit=true;if(C.RecordEvent)C.RecordEvent(TEXT("Hit"));}
        }
        if(bContactScenario&&!bCut&&C.Hero->ConfirmedFoodCutCount>FoodCutsBefore) {
            bCut=true;
            // The accepted native knife contact may be followed by a queued split.
            if(!bHit) {bHit=true;if(C.RecordEvent)C.RecordEvent(TEXT("Hit"));}
            if(C.RecordEvent)C.RecordEvent(TEXT("FoodCut"));
        }
        const bool bKnifeTarget=bContactScenario&&Slot==EMCToolSlot::Knife;
        if(!bReleased&&(bKatana?Age>(bContactScenario?.65f:.08f):bKnifeTarget?(bCut||Age>.2f):Age>.08f)) Release(C);
        const float End=bKatana?(bContactScenario?2.f:1.2f):C.Hero->Inventory->SwingDuration()+.2f;
        if(bAccepted&&Age>End) {
            if(!bContactScenario||bHit)bDone=true;
            else if(Age>2.f){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeToolContactNotAccepted"));}
        }
        else if(!bAccepted&&Age>2.5f){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeToolSwingNotAccepted"));}
    }
    void Release(FMCPerkPreviewContext& C) override
    {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);bReleased=true;}
    void Cancel(FMCPerkPreviewContext& C) override
    {Release(C);if(auto* Owner=Presentation.Get())Owner->StopStates(ID);bDone=true;}
    void Reset(FMCPerkPreviewContext& C) override
    {
        Cancel(C);
        if(C.World&&bContactScenario) {
            TArray<AMCFoodActor*> Spawned;
            for(TActorIterator<AMCFoodActor> It(C.World);It;++It)
                if(!FoodBefore.Contains(TWeakObjectPtr<AMCFoodActor>(*It)))Spawned.Add(*It);
            for(auto* Food:Spawned)if(IsValid(Food))Food->Destroy();
        }
        if(auto* Throat=ContactThroat.Get())Throat->Destroy();
        if(bKatana && IsValid(C.Hero) && C.Hero->Inventory) {
            C.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
            // Restore only this sandbox pawn's reflected nonpersistent grant.
            if(const auto* Mask=FindFProperty<FByteProperty>(UMCInventoryComponent::StaticClass(),TEXT("DevToolUpgradeMask")))
                Mask->SetPropertyValue_InContainer(C.Hero->Inventory,PreviousUpgradeMask);
            C.Hero->Inventory->ServerSelect(PreviousSlot);
        }
        FoodBefore.Reset();ContactFood.Reset();ContactThroat.Reset();Presentation.Reset();
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Взмах завершён"):bAccepted?TEXT("Принятый обычный взмах"):TEXT("Ожидание штатного ввода");}
    float GetTailSeconds() const override{return .5f;}
private:
    bool PrepareContactTarget(FMCPerkPreviewContext& C,FString& Error)
    {
        const auto* Table=GetDefault<UMCDayPlan>()->Menu.LoadSynchronous();
        if(!Table||Table->GetRowStruct()!=FMCFoodRow::StaticStruct()){Error=TEXT("Нет настоящего игрового меню для пищевой цели.");return false;}
        TArray<FName> Names=Table->GetRowNames();Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
        const bool bKnife=Slot==EMCToolSlot::Knife;
        const FMCFoodRow* Chosen=nullptr;FName RowName;
        TSoftObjectPtr<UStaticMesh> KnifeFragment;
        FQuat Rotation=C.Hero->GetActorQuat();
        for(FName Name:Names)if(const auto* Row=Table->FindRow<FMCFoodRow>(Name,TEXT("ComposerToolContact"),false)) {
            if(Row->Kind!=EMCFoodKind::Food)continue;
            bool Hard=Row->Resistance==EMCFoodResistance::Hard;
            if(Row->Resistance==EMCFoodResistance::Automatic)
                Hard=Name==TEXT("Carrot")||Name==TEXT("Nut")||Name==TEXT("Crust")||Name==TEXT("Tartar");
            if(Hard!=(Slot==EMCToolSlot::Pickaxe))continue;
            if(bKnife) {
                // As in the intake preview, use a real CPU-readable menu portion
                // and orient its longest axis across the knife's cut plane.
                for(const auto& Ref:Row->FragmentMeshes) {
                    const auto* Mesh=Ref.LoadSynchronous();
                    if(!Mesh||!Mesh->bAllowCPUAccess)continue;
                    const FVector Size=Mesh->GetBoundingBox().GetSize()*Row->FragmentScale.GetAbs();
                    if(Size.ContainsNaN()||Size.GetMin()<=0)continue;
                    const int32 CutAxis=Size.X>=Size.Y&&Size.X>=Size.Z?0:Size.Y>=Size.Z?1:2;
                    Rotation=Rotation*(CutAxis==0?FRotator(0,90,0):CutAxis==2?FRotator(0,0,90):FRotator::ZeroRotator).Quaternion();
                    KnifeFragment=Ref;
                    break;
                }
                if(KnifeFragment.IsNull())continue;
            } else if(Row->WholeMeshes.IsEmpty())continue;
            Chosen=Row;RowName=Name;break;
        }
        if(!Chosen){Error=TEXT("Нет подходящей сохранённой еды для одного настоящего удара.");return false;}
        const FVector SpawnOffset=C.Hero->GetActorForwardVector()*110.f-FVector(0,0,bKnife?0.f:60.f);
        const FTransform Pose(Rotation,C.Hero->GetActorLocation()+SpawnOffset);
        auto* Food=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food){Error=TEXT("Не удалось создать настоящую пищевую цель.");return false;}
        ContactFood=Food;C.Targets.Add(Food);FRandomStream FixtureChoice(743);
        Food->Initialize(false,C.Hero->GetActorForwardVector());
        if(bKnife) {
            if(const auto* Profile=Food->Profile.LoadSynchronous())Food->Settings=Profile->Settings;
            FMCFoodRow Choice=*Chosen;
            Choice.FragmentMeshes={KnifeFragment};
            Food->ConfigureItem(RowName,Choice,FixtureChoice,true);
        } else Food->ConfigureItem(RowName,*Chosen,FixtureChoice);
        Food->FinishSpawning(Pose);
        // Keep the authentic saved mesh and scale, but place its near surface in reach.
        // Freeze only this sandbox fixture so initial overlap cannot launch it away.
        const FBox Bounds=Food->Visual->Bounds.GetBox();
        const FVector Forward=C.Hero->GetActorForwardVector().GetSafeNormal2D();
        const float SurfaceDistance=FMath::Clamp(C.Hero->Inventory->MeleeReach()*.40f,35.f,80.f);
        const float Along=FVector::DotProduct(Bounds.GetExtent(),Forward.GetAbs())+SurfaceDistance;
        const FVector DesiredCenter=C.Hero->GetActorLocation()+Forward*Along;
        Food->Body->SetSimulatePhysics(false);
        Food->SetActorLocation(Food->GetActorLocation()+DesiredCenter-Bounds.GetCenter(),false,nullptr,ETeleportType::TeleportPhysics);
        const auto ActualMesh=Food->Visual->GetStaticMesh();
        const auto& SavedMeshes=bKnife?Chosen->FragmentMeshes:Chosen->WholeMeshes;
        const bool FromSavedRow=SavedMeshes.ContainsByPredicate([ActualMesh](const TSoftObjectPtr<UStaticMesh>& Ref){return Ref.Get()==ActualMesh.Get();});
        if(!ActualMesh||!FromSavedRow||!C.Hero->Inventory->CanBreak(Food))
        {Error=TEXT("Штатный инструмент не подтвердил настоящую сохранённую пищевую цель.");return false;}
        FHitResult Contact;
        if(bKnife&&!bKatana&&!Food->FindToolContact(C.Hero,C.Hero->Inventory->MeleeReach(),Contact))
        {Error=TEXT("Сохранённый кусок не находится на линии контакта настоящего ножа.");return false;}
        if(bKnife&&Food->UsesAutomaticCutIntake()) {
            // Accepted automatic portions still need the same real destination
            // as in the intake scenario. Keep the contact camera focused on food.
            const FTransform ThroatPose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+Forward*700.f+FVector(0,0,80.f));
            auto* Throat=C.World->SpawnActor<AMCThroat>(AMCThroat::StaticClass(),ThroatPose);
            if(!Throat){Error=TEXT("Не удалось подготовить глотку для принятого разреза.");return false;}
            ContactThroat=Throat;
            Throat->SetActorTickEnabled(false);
        }
        InitialHealth=Food->Health;if(C.RequestFramingUpdate)C.RequestFramingUpdate();return true;
    }
    TWeakObjectPtr<AMCFoodActor> ContactFood;
    TWeakObjectPtr<AMCThroat> ContactThroat;
    TArray<TWeakObjectPtr<AMCFoodActor>> FoodBefore;
    float InitialHealth=0;
    int32 HitsBefore=0,FoodCutsBefore=0;
    bool bContactScenario=false,bHit=false,bCut=false;
    EMCToolSlot Slot;
    EMCToolSlot PreviousSlot=EMCToolSlot::Brush;
    uint8 PreviousUpgradeMask=0;
    bool bKatana=false;
    FName ID;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    int32 Before=0;float Age=0;
    bool bAccepted=false,bReleased=false,bDone=false;
};

AMCArenaTooth* PrepareCareCrown(FMCPerkPreviewContext& C,int32 Id,UStaticMesh* Mesh,FString& Error)
{
    if(!Mesh){Error=TEXT("Исходная коронка зуба не загружена.");return nullptr;}
    const FTransform Pose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*145.f);
    auto* Crown=C.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose,nullptr,nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Crown){Error=TEXT("Не удалось создать изолированную настоящую коронку.");return nullptr;}
    C.Targets.Add(Crown);
    FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;Settings.bPianoEnabled=false;
    Crown->Initialize(Id,Settings);
    // A small crown stays within native tool reach without changing tool tuning.
    const float Scale=100.f/FMath::Max(1.f,float(Mesh->GetBounds().BoxExtent.GetMax())*2.f);
    Crown->SetAppearance(Mesh,FVector(Scale));Crown->FinishSpawning(Pose);
    return Crown;
}

/** A disposable real crown is coated before the barrier. Only ordinary brush
 * input may remove that coating and produce accepted care feedback. */
class FBrushCrownContactAdapter final : public IMCPerkPreviewAdapter
{
public:
    explicit FBrushCrownContactAdapter(bool InCare):bCare(InCare){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World||!IsValid(C.Hero)||!C.Hero->HasAuthority()||!C.Hero->Inventory||!C.Hero->BrushContact)
        {Error=TEXT("Нужны настоящий персонаж, щётка и изолированный authority-мир.");return false;}
        ID=bCare?TEXT("EditorOnly.CleanArenaTooth"):TEXT("Tool.Brush");
        C.Hero->SetPrimaryInputHeld(false);C.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Brush){Error=TEXT("Штатный выбор щётки отклонён.");return false;}
        auto* Crown=PrepareCareCrown(C,500003,GetDefault<AMCArenaTooth>()->Visual->GetStaticMesh().Get(),Error);
        if(!Crown)return false;
        Tooth=Crown;
        Crown->SetCoffee(0.f);
        FHitResult Contact;FCollisionQueryParams Query(SCENE_QUERY_STAT(MCComposerBrushFixture),true,C.Hero);
        const FVector From=C.Hero->GetActorLocation()+FVector(0,0,25);
        if(!Crown->BrushSurface->LineTraceComponent(Contact,From,Crown->Visual->Bounds.Origin,Query))
        {Error=TEXT("Настоящая поверхность коронки не приняла контакт сценария.");return false;}
        const FVector Patch=Contact.ImpactPoint;
        if(!Crown->DepositLiquidGrime([Patch](FVector Point){return FVector::DistSquared(Point,Patch)<FMath::Square(32.f);},false))
        {Error=TEXT("На реальной поверхности не удалось подготовить локальное загрязнение.");return false;}
        auto* Owner=bCare?static_cast<AActor*>(Crown):static_cast<AActor*>(C.Hero);
        Presentation=MCWorldPresentation::PrepareOwner(Owner,{ID});
        if(!Presentation.IsValid()){Error=TEXT("Источнику щётки/ухода не назначен Composer.");return false;}
        WorkBefore=Crown->CareFeedback->GetAcceptedPresentationState().WorkSerial;
        CompleteBefore=Crown->CareFeedback->GetAcceptedPresentationState().CompletionSerial;
        StrictBrush=C.Hero->BrushContact;bPreviousStrict=StrictBrush->RequiresEvaluatedContact();
        StrictBrush->SetRequireEvaluatedContact(true);bStrictApplied=true;
        Age=0;bContact=bCompletion=bDone=false;
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящая щётка: удержание → подход руки → контакт щетины → удаление локального покрытия. Награды и профиль пользовательского уровня не меняются."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();if(!Owner||!Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(ID);const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!Source||!Contract){Error=TEXT("Настоящий источник ухода не подготовлен.");return false;}
        return !C.PresentationOverride||Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* Owner=Presentation.Get())if(const auto* Source=Owner->GetSource(ID))Source->GetPreparedResources(Out);
        if(IsValid(C.Hero)&&C.Hero->BrushContact)if(auto* NS=C.Hero->BrushContact->FoamSystem.Get())Out.AddUnique(NS);
    }
    void OnCommitted(FMCPerkPreviewContext& C) override {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(true);}
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        Age+=Dt;auto* Crown=Tooth.Get();
        if(!IsValid(C.Hero)||!IsValid(Crown)){bDone=true;return;}
        const auto& State=Crown->CareFeedback->GetAcceptedPresentationState();
        const bool ActualContact=bCare?State.WorkSerial>WorkBefore:
            C.Hero->BrushContact->Target==Crown&&C.Hero->BrushContact->IsWorkReady()&&C.Hero->BrushContact->IsTouchingSurface();
        if(!bContact&&ActualContact){bContact=true;ContactAge=Age;if(C.RecordEvent)C.RecordEvent(TEXT("Contact"));}
        if(bCare&&!bCompletion&&State.CompletionSerial>CompleteBefore)
        {bCompletion=true;ContactAge=Age;if(C.RecordEvent)C.RecordEvent(TEXT("Complete"));Release(C);}
        if((!bCare&&bContact&&Age-ContactAge>.8f)||(bCompletion&&Age-ContactAge>1.2f)) {Release(C);bDone=true;}
        if(Age>25.f&&!bDone) {
            if(C.RecordEvent)C.RecordEvent(TEXT("NativeBrushScenarioTimedOut"));
            if(C.ReportStatus)C.ReportStatus(TEXT("Реальный контакт/завершение не подтверждены. Сценарий остановлен без подстановки эффекта."));
            Release(C);bDone=true;
        }
    }
    void Release(FMCPerkPreviewContext& C) override {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        Release(C);if(auto* Owner=Presentation.Get())Owner->StopStates(ID);
        if(bStrictApplied)if(auto* Brush=StrictBrush.Get())Brush->SetRequireEvaluatedContact(bPreviousStrict);
        bStrictApplied=false;StrictBrush.Reset();bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);if(auto* Crown=Tooth.Get())Crown->Destroy();Tooth.Reset();Presentation.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Контакт завершён"):bCompletion?TEXT("Принятое завершение ухода"):bContact?TEXT("Настоящий контакт щётки"):TEXT("Подход руки к загрязнению");}
    float GetTailSeconds() const override{return 1.2f;}
private:
    FName ID;TWeakObjectPtr<AMCArenaTooth> Tooth;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    TWeakObjectPtr<UMCBrushContactComponent> StrictBrush;
    bool bPreviousStrict=false,bStrictApplied=false;
    int32 WorkBefore=0,CompleteBefore=0;float Age=0,ContactAge=0;
    bool bCare=false,bContact=false,bCompletion=false,bDone=false;
};

class FRepairCrownContactAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World||C.World->WorldType!=EWorldType::Game||C.World->GetOutermost()!=GetTransientPackage()
            ||!C.World->GetAuthGameMode<AMCPerkPreviewGameMode>()||!IsValid(C.Hero)||C.Hero->GetWorld()!=C.World
            ||!C.Hero->HasAuthority()||!C.Hero->CanWork()||!C.Hero->Inventory)
        {Error=TEXT("Нужны настоящий персонаж со спреем и изолированный мир Composer.");return false;}
        auto* Inventory=C.Hero->Inventory.Get();
        PreviousSlot=Inventory->Selected;PreviousPressure=Inventory->bPressureMode;bSelectedTool=true;
        C.Hero->SetPrimaryInputHeld(false);
        if(Inventory->Selected!=EMCToolSlot::Spray)Inventory->ServerSelect(EMCToolSlot::Spray);
        if(Inventory->HasUpgrade(EMCToolUpgrade::Watergun)&&Inventory->bPressureMode)
            Inventory->ServerSelect(EMCToolSlot::Spray);
        if(Inventory->Selected!=EMCToolSlot::Spray||(Inventory->HasUpgrade(EMCToolUpgrade::Watergun)&&Inventory->bPressureMode))
        {Error=TEXT("Штатный режим спрея для ухода отклонён.");return false;}

        const auto* Damage=GetDefault<AMCArenaTooth>()->DamageProfile.LoadSynchronous();
        const auto* Variant=Damage?Damage->Teeth.FindByPredicate([](const FMCToothDamageMeshes& Row) {
            return Row.Intact&&Row.Chipped&&Row.Intact->GetPathName().StartsWith(TEXT("/Game/Gameplay/Arena/V10/"));
        }):nullptr;
        if(!Variant){Error=TEXT("Нет сохранённой пары целой и повреждённой коронки арены.");return false;}
        IntactMesh=Variant->Intact.Get();ChippedMesh=Variant->Chipped.Get();
        auto* Crown=PrepareCareCrown(C,500004,IntactMesh.Get(),Error);
        if(!Crown)return false;
        Tooth=Crown;
        // Chipped starts above the regrowth milestone. Complete witnesses full HP.
        FMCToothStatus Initial=Crown->Status->State;
        Initial.Health=Initial.MaxHealth*.6f;
        Initial.RepairLeft=FMath::CeilToInt(Crown->Status->Settings.RepairContacts*.4f);
        Crown->Status->Restore(Initial);
        InitialHealth=Crown->Status->State.Health;
        if(!Crown->Status->NeedsCare(false)) {Error=TEXT("Коронка не требует восстановления.");return false;}
        Presentation=MCWorldPresentation::PrepareOwner(Crown,{ID});
        if(!Presentation.IsValid()){Error=TEXT("Источнику восстановления не назначен Composer.");return false;}
        const auto& State=Crown->CareFeedback->GetAcceptedPresentationState();
        WorkBefore=State.WorkSerial;CompleteBefore=State.CompletionSerial;
        if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящий спрей: удержание → контакт с повреждённой коронкой → восстановление HP."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!Owner||!Contract||!Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(ID);
        if(!Source){Error=TEXT("Настоящий источник восстановления не подготовлен.");return false;}
        return !C.PresentationOverride||Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error);
    }
    bool PrepareResidentPresentation(FMCPerkPreviewContext& C,FGuid,bool& bWaiting,FString& Error) override
    {
        auto* Loading=C.World?C.World->GetSubsystem<UMCWorldPreloadSubsystem>():nullptr;
        if(!Loading||!IntactMesh.IsValid()||!ChippedMesh.IsValid()) {
            Error=TEXT("Нет подготовленных моделей восстановления коронки.");return false;
        }
        const TArray<UObject*> Resources={IntactMesh.Get(),ChippedMesh.Get()};
        bWaiting=!Loading->PrepareResidentVisualResources(Resources,Error);
        return true;
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext& C,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* Owner=Presentation.Get())if(const auto* Source=Owner->GetSource(ID))Source->GetPreparedResources(Out);
        if(IsValid(C.Hero)&&C.Hero->Inventory) {
            const auto* Inventory=C.Hero->Inventory.Get();
            const auto& System=Inventory->HasUpgrade(EMCToolUpgrade::Watergun)?Inventory->OriginalWaterCareSystem:Inventory->OriginalSpraySystem;
            if(auto* NS=System.Get())Out.AddUnique(NS);
        }
    }
    void OnCommitted(FMCPerkPreviewContext& C) override {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(true);}
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        Age+=Dt;
        const auto* Crown=Tooth.Get();
        if(!IsValid(C.Hero)||!IsValid(Crown)){Release(C);bDone=true;return;}
        const auto& State=Crown->CareFeedback->GetAcceptedPresentationState();
        if(!bContact&&State.WorkSerial>WorkBefore&&State.Kind==EMCToothCareFeedbackKind::Repair
            &&Crown->Status->State.Health>InitialHealth) {
            bContact=true;
            if(C.RecordEvent)C.RecordEvent(TEXT("Contact"));
        }
        if(bContact&&State.CompletionSerial>CompleteBefore&&State.CompletionKind==EMCToothCareFeedbackKind::Repair
            &&!Crown->Status->NeedsCare(false)) {
            if(C.RecordEvent)C.RecordEvent(TEXT("Complete"));
            Release(C);bDone=true;
        } else if(Age>=8.f) {
            if(C.RecordEvent)C.RecordEvent(TEXT("NativeRepairScenarioTimedOut"));
            Release(C);bDone=true;
        }
    }
    void Release(FMCPerkPreviewContext& C) override {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        Release(C);
        if(auto* Owner=Presentation.Get())Owner->StopStates(ID);
        if(bSelectedTool&&IsValid(C.Hero)&&C.Hero->Inventory) {
            auto* Inventory=C.Hero->Inventory.Get();Inventory->ResetSprayPresentationForPreview();
            if(Inventory->Selected==EMCToolSlot::Spray&&Inventory->HasUpgrade(EMCToolUpgrade::Watergun)
                &&Inventory->bPressureMode!=PreviousPressure)Inventory->ServerSelect(EMCToolSlot::Spray);
            if(Inventory->Selected!=PreviousSlot)Inventory->ServerSelect(PreviousSlot);
        }
        bSelectedTool=false;bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);if(auto* Crown=Tooth.Get())Crown->Destroy();Tooth.Reset();Presentation.Reset();IntactMesh.Reset();ChippedMesh.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bContact?TEXT("Восстановление HP коронки"):TEXT("Ожидание контакта спрея");}
    float GetTailSeconds() const override{return 1.2f;}
private:
    const FName ID=TEXT("EditorOnly.RepairArenaTooth");
    TWeakObjectPtr<AMCArenaTooth> Tooth;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    TWeakObjectPtr<UStaticMesh> IntactMesh,ChippedMesh;
    EMCToolSlot PreviousSlot=EMCToolSlot::Brush;
    bool PreviousPressure=false,bSelectedTool=false,bContact=false,bDone=false;
    int32 WorkBefore=0,CompleteBefore=0;
    float Age=0,InitialHealth=0;
};
}

void MCWorldActionPreviewAdapters::Register()
{
    MCCoffeeRiverPreview::Register();
    for(const auto& Contract:MCVFXSourceContracts::GetAll())
    {
        MCVFXProfileBindings::RegisterLibrarySource(Contract.SourceID,Contract.Library,Contract.SourceID,Contract.Events);
        FMCVFXActionCatalogueEntry Entry;Entry.ID=Contract.SourceID;Entry.Label=Contract.Label;Entry.Category=Contract.Category;
        Entry.SourceOwner=Contract.SourceOwner;Entry.Stages=Contract.Events;Entry.Description=FText::FromString(Contract.Description);
        for(const auto& Audio:Contract.OriginalAudio)Entry.SearchNames.Add(Audio.Value.Resource.GetAssetName());
        MCVFXActionCatalog::RegisterSource(MoveTemp(Entry));
    }
    for(const auto Slot:{EMCToolSlot::Knife,EMCToolSlot::Pickaxe})
    {
        const FName ID=Slot==EMCToolSlot::Knife?TEXT("Tool.Knife"):TEXT("Tool.Pickaxe");const auto* Contract=MCVFXSourceContracts::Find(ID);
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
        Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::Timeout};
        Entry.StageLabels.Add(TEXT("Swing"),FText::FromString(TEXT("Взмах")));
        Entry.StageLabels.Add(TEXT("Hit"),FText::FromString(TEXT("Настоящее попадание")));
        if(Slot==EMCToolSlot::Knife)Entry.StageLabels.Add(TEXT("FoodCut"),FText::FromString(TEXT("Разрез еды · всплеск")));
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий удар по еде")));
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TEXT("Обычный взмах · без попадания")));
        Entry.DescribeNativeSources=[](FName Event)
        {
            TArray<FMCVFXNativeSourcePart> Out;if(Event!=TEXT("Swing"))return Out;
            FMCVFXNativeSourcePart Material;Material.ID=TEXT("WeaponTrail.Material");Material.Label=FText::FromString(TEXT("Материал настоящего следа инструмента"));
            Material.Kind=EMCVFXNativeSourceKind::Material;Material.Asset=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/WeaponTrail/MI_WeaponSwingTrail.MI_WeaponSwingTrail"));Out.Add(Material);
            FMCVFXNativeSourcePart Geometry;Geometry.ID=TEXT("WeaponTrail.Geometry");Geometry.Label=FText::FromString(TEXT("Родная лента по настоящему лезвию"));
            Geometry.Kind=EMCVFXNativeSourceKind::Geometry;Geometry.SourceFile=TEXT("Source/MessControl/Private/MCWeaponTrailComponent.cpp");Out.Add(Geometry);return Out;
        };
        Entry.Create=[Slot]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FOrdinaryToolSwingAdapter>(Slot);};
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    {
        const FName ID(TEXT("Tool.Katana"));const auto* Contract=MCVFXSourceContracts::Find(ID);
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
        Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
        Entry.Scenarios={EMCPerkPreviewScenario::Target,EMCPerkPreviewScenario::Timeout};
        Entry.StageLabels.Add(TEXT("Swing"),FText::FromString(TEXT("Режущая кромка · след взмаха")));
        Entry.StageLabels.Add(TEXT("Hit"),FText::FromString(TEXT("Настоящее попадание")));
        Entry.StageLabels.Add(TEXT("FoodCut"),FText::FromString(TEXT("Разрез еды · всплеск")));
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Заряженная серия · настоящая еда")));
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TEXT("Одиночный режущий взмах")));
        Entry.DefaultSystems.Add(TEXT("Swing"),TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/VFX/WeaponTrail/NS_KatanaSwingTrail.NS_KatanaSwingTrail"))));
        Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FOrdinaryToolSwingAdapter>(EMCToolSlot::Knife,true);};
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    for(const FName ID:{FName(TEXT("Tool.Brush")),FName(TEXT("EditorOnly.CleanArenaTooth")),FName(TEXT("EditorOnly.RepairArenaTooth"))})
    {
        const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        const bool Brush=ID==TEXT("Tool.Brush"),Repair=ID==TEXT("EditorOnly.RepairArenaTooth");
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
        Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
        Entry.StageLabels.Add(TEXT("Contact"),FText::FromString(Brush?TEXT("Контакт щетины"):TEXT("Принятый уход")));
        Entry.StageLabels.Add(TEXT("Complete"),FText::FromString(TEXT("Завершение ухода")));
        if(Brush)Entry.DefaultSystems.Add(TEXT("Contact"),GetDefault<UMCBrushContactComponent>()->FoamSystem);
        Entry.DescribeNativeSources=[Brush](FName) {
            TArray<FMCVFXNativeSourcePart> Out;FMCVFXNativeSourcePart Resource;
            if(Brush) {
                Resource.ID=TEXT("Brush.Contact.Original");Resource.Label=FText::FromString(TEXT("Исходная пена щётки"));
                Resource.Kind=EMCVFXNativeSourceKind::Niagara;Resource.Asset=GetDefault<UMCBrushContactComponent>()->FoamSystem.ToSoftObjectPath();
            } else {
                Resource.ID=TEXT("Care.Pearl.Material");Resource.Label=FText::FromString(TEXT("Исходные жемчужины, пузыри, блики и проход"));
                Resource.Kind=EMCVFXNativeSourceKind::Material;Resource.Asset=GetDefault<UMCToothCareFeedbackComponent>()->CareMaterial.ToSoftObjectPath();
            }
            Out.Add(Resource);return Out;
        };
        Entry.Scenarios={EMCPerkPreviewScenario::Target};
        if(Repair) {
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Спрей · восстановление повреждённой коронки")));
            Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FRepairCrownContactAdapter>();};
        } else {
            Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Щётка · реальная загрязнённая коронка")));
            Entry.Create=[Brush]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FBrushCrownContactAdapter>(!Brush);};
        }
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    // These owners already consume their shared saved library at runtime.
    // Editing that real binding is independent of a contextual scenario factory.
    // Keep scenarios unavailable until their actual-owner adapter is registered.
    for(const FName ID:{FName(TEXT("Food.Break")),FName(TEXT("Food.Spicy")),
        FName(TEXT("Food.Spoiled")),FName(TEXT("World.FirePatch")),FName(TEXT("Reward.TaskCoins")),
        FName(TEXT("Character.DeathSpirit"))})
    {
        if(MCPerkPreviewAdapters::Find(ID))continue;
        const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.SourceRowID=NAME_None;
        Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
        Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
        if(ID==TEXT("Character.DeathSpirit")) {
            Entry.StageLabels.Add(TEXT("Merge"),FText::FromString(TEXT("Соединение · белое свечение")));
            Entry.DescribeNativeSources=[](FName Event) {
                TArray<FMCVFXNativeSourcePart> Resources;
                if(Event!=TEXT("Merge")) return Resources;
                const auto AddMaterial=[&Resources](FName MaterialID,const TCHAR* Label,const TCHAR* Path) {
                    auto& Material=Resources.AddDefaulted_GetRef();
                    Material.ID=MaterialID;Material.Label=FText::FromString(Label);
                    Material.Kind=EMCVFXNativeSourceKind::Material;Material.Asset=FSoftObjectPath(Path);
                };
                AddMaterial(TEXT("Merge.Spirit"),TEXT("Свечение духа"),
                    TEXT("/Game/Gameplay/VFX/DeathSpirit/M_DeathSpirit_Translucent.M_DeathSpirit_Translucent"));
                AddMaterial(TEXT("Merge.Crown"),TEXT("Свечение коронки"),
                    TEXT("/Game/Gameplay/VFX/DeathSpirit/M_DeathSpirit_CrownWhite.M_DeathSpirit_CrownWhite"));
                return Resources;
            };
        }
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    MCFrostActionPreviewAdapters::Register();
    MCIceActionPreviewAdapters::Register();
    MCFreezeDeathPreviewAdapter::Register();
    MCJellyActionPreviewAdapters::Register();
    MCFoodRewardPreview::Register();
    MCNativeWorldActionPreviewAdapters::Register();
    MCYawnWaterPreview::Register();
    MCThroatSuctionPreview::Register();
    MCFoodAutoMouthPreview::Register();
    MCNutThrowPreview::Register();
    MCStageBoosterPreview::Register();
    MCHurtActionPreviewAdapters::Register();
    MCPianoActionPreviewAdapter::Register();
    MCSprayActionPreviewAdapter::Register();
    MCColdColaPreviewAdapter::Register();
    MCStainSurfacePreviewAdapter::Register();
    // Exact runtime hooks are editable even while their full contextual
    // gameplay/UI scenario is unavailable. No synthetic scenario factory.
    for(const FName ID:{FName(TEXT("Reward.Chest")),FName(TEXT("Reward.PerkPickup")),FName(TEXT("Reward.TaskComplete")),
        FName(TEXT("Hub.Grill")),FName(TEXT("UI.ShiftFeedback"))}) {
        if(MCPerkPreviewAdapters::Find(ID))continue;
        const auto* C=MCVFXSourceContracts::Find(ID);if(!C)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;E.bSupportsProfileOverride=true;MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
}
