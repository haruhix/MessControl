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
#include "MCCoffeeRiverPreviewAdapter.h"
#include "MCFrostActionPreviewAdapters.h"
#include "MCIceActionPreviewAdapters.h"
#include "MCFreezeDeathPreviewAdapter.h"
#include "MCJellyActionPreviewAdapters.h"
#include "NiagaraSystem.h"

#include "MCPerkPreviewAdapter.h"
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
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCDayPlan.h"
#include "Engine/DataTable.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
/** Real input/swing; Target also prepares a real saved-menu food contact. */
class FOrdinaryToolSwingAdapter final : public IMCPerkPreviewAdapter
{
public:
    explicit FOrdinaryToolSwingAdapter(EMCToolSlot InSlot):Slot(InSlot){}
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World||!IsValid(C.Hero)||!C.Hero->HasAuthority()||!C.Hero->Inventory)
        {Error=TEXT("Нужен настоящий персонаж изолированной песочницы с обычным инвентарём.");return false;}
        ID=Slot==EMCToolSlot::Knife?TEXT("Tool.Knife"):TEXT("Tool.Pickaxe");
        C.Hero->SetPrimaryInputHeld(false);C.Hero->Inventory->ServerSelect(Slot);
        if(C.Hero->Inventory->Selected!=Slot){Error=TEXT("Штатный выбор инструмента отклонён.");return false;}
        Before=C.Hero->ValidatedSwingCount;HitsBefore=C.Hero->ConfirmedHitCount;Age=0;bAccepted=bHit=bReleased=bDone=false;
        bContactScenario=C.Scenario==EMCPerkPreviewScenario::Target;
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
        if(bContactScenario&&!bHit&&C.Hero->ConfirmedHitCount>HitsBefore)if(const auto* Food=ContactFood.Get())
            if(Food->Health<InitialHealth){bHit=true;if(C.RecordEvent)C.RecordEvent(TEXT("Hit"));}
        if(!bReleased&&Age>.08f){C.Hero->SetPrimaryInputHeld(false);bReleased=true;}
        if(bAccepted&&Age>C.Hero->Inventory->SwingDuration()+.2f) {
            if(!bContactScenario||bHit)bDone=true;
            else if(Age>2.f){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeToolContactNotAccepted"));}
        }
        else if(!bAccepted&&Age>2.f){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeToolSwingNotAccepted"));}
    }
    void Release(FMCPerkPreviewContext& C) override
    {if(IsValid(C.Hero))C.Hero->SetPrimaryInputHeld(false);bReleased=true;}
    void Cancel(FMCPerkPreviewContext& C) override
    {Release(C);if(auto* Owner=Presentation.Get())Owner->StopStates(ID);bDone=true;}
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);if(auto* Food=ContactFood.Get())Food->Destroy();ContactFood.Reset();Presentation.Reset();}
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
        const FMCFoodRow* Chosen=nullptr;FName RowName;
        for(FName Name:Names)if(const auto* Row=Table->FindRow<FMCFoodRow>(Name,TEXT("ComposerToolContact"),false)) {
            if(Row->Kind!=EMCFoodKind::Food||Row->WholeMeshes.IsEmpty()||Row->Health<=C.Hero->Inventory->Damage())continue;
            bool Hard=Row->Resistance==EMCFoodResistance::Hard;
            if(Row->Resistance==EMCFoodResistance::Automatic)
                Hard=Name==TEXT("Carrot")||Name==TEXT("Nut")||Name==TEXT("Crust")||Name==TEXT("Tartar");
            if(Hard!=(Slot==EMCToolSlot::Pickaxe))continue;
            Chosen=Row;RowName=Name;break;
        }
        if(!Chosen){Error=TEXT("Нет подходящей сохранённой еды для одного настоящего удара.");return false;}
        const FTransform Pose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*110.f-FVector(0,0,60));
        auto* Food=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food){Error=TEXT("Не удалось создать настоящую пищевую цель.");return false;}
        ContactFood=Food;C.Targets.Add(Food);FRandomStream FixtureChoice(743);
        Food->Initialize(false,C.Hero->GetActorForwardVector());Food->ConfigureItem(RowName,*Chosen,FixtureChoice);Food->FinishSpawning(Pose);
        const auto ActualMesh=Food->Visual->GetStaticMesh();
        const bool FromSavedRow=Chosen->WholeMeshes.ContainsByPredicate([ActualMesh](const TSoftObjectPtr<UStaticMesh>& Ref){return Ref.Get()==ActualMesh.Get();});
        if(!ActualMesh||!FromSavedRow||!C.Hero->Inventory->CanBreak(Food)||Food->Health<=C.Hero->Inventory->Damage())
        {Error=TEXT("Штатный инструмент не подтвердил настоящую сохранённую пищевую цель.");return false;}
        InitialHealth=Food->Health;if(C.RequestFramingUpdate)C.RequestFramingUpdate();return true;
    }
    TWeakObjectPtr<AMCFoodActor> ContactFood;
    float InitialHealth=0;
    int32 HitsBefore=0;
    bool bContactScenario=false,bHit=false;
    EMCToolSlot Slot;
    FName ID;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    int32 Before=0;float Age=0;
    bool bAccepted=false,bReleased=false,bDone=false;
};

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
        auto* Mesh=GetDefault<AMCArenaTooth>()->Visual->GetStaticMesh().Get();
        if(!Mesh){Error=TEXT("Исходная коронка зуба не загружена.");return false;}
        const FTransform Pose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*145.f);
        auto* Crown=C.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Crown){Error=TEXT("Не удалось создать изолированную настоящую коронку.");return false;}
        Tooth=Crown;C.Targets.Add(Crown);
        FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;Settings.bPianoEnabled=false;
        Crown->Initialize(500003,Settings);
        // A small real crown keeps the complete surface within ordinary hand
        // reach. This changes only the disposable scenario, never tool radii.
        const float Scale=100.f/FMath::Max(1.f,float(Mesh->GetBounds().BoxExtent.GetMax())*2.f);
        Crown->SetAppearance(Mesh,FVector(Scale));Crown->FinishSpawning(Pose);
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
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящий удар по еде")));
        Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Timeout,FText::FromString(TEXT("Обычный взмах · без попадания")));
        Entry.DescribeNativeSources=[](FName Event)
        {
            TArray<FMCVFXNativeSourcePart> Out;if(Event!=TEXT("Swing"))return Out;
            FMCVFXNativeSourcePart Material;Material.ID=TEXT("WeaponTrail.Material");Material.Label=FText::FromString(TEXT("Материал настоящего следа инструмента"));
            Material.Kind=EMCVFXNativeSourceKind::Material;Material.Asset=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/WeaponTrail/M_WeaponSwingTrail.M_WeaponSwingTrail"));Out.Add(Material);
            FMCVFXNativeSourcePart Geometry;Geometry.ID=TEXT("WeaponTrail.Geometry");Geometry.Label=FText::FromString(TEXT("Родная лента по настоящему лезвию"));
            Geometry.Kind=EMCVFXNativeSourceKind::Geometry;Geometry.SourceFile=TEXT("Source/MessControl/Private/MCWeaponTrailComponent.cpp");Out.Add(Geometry);return Out;
        };
        Entry.Create=[Slot]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FOrdinaryToolSwingAdapter>(Slot);};
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
        if(!Repair) {
            Entry.Scenarios={EMCPerkPreviewScenario::Target};Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Щётка · реальная загрязнённая коронка")));
            Entry.Create=[Brush]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FBrushCrownContactAdapter>(!Brush);};
        }
        MCPerkPreviewAdapters::Register(MoveTemp(Entry));
    }
    // These owners already consume their shared saved library at runtime.
    // Editing that real binding is independent of a contextual scenario factory.
    // Keep scenarios unavailable until their actual-owner adapter is registered.
    for(const FName ID:{FName(TEXT("Food.Break")),FName(TEXT("Food.Spicy")),
        FName(TEXT("Food.Spoiled")),FName(TEXT("World.FirePatch")),FName(TEXT("Reward.TaskCoins"))})
    {
        if(MCPerkPreviewAdapters::Find(ID))continue;
        const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)continue;
        FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.SourceRowID=NAME_None;
        Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
        Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
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
    MCHurtActionPreviewAdapters::Register();
    MCPianoActionPreviewAdapter::Register();
    MCSprayActionPreviewAdapter::Register();
    MCColdColaPreviewAdapter::Register();
    MCStainSurfacePreviewAdapter::Register();
    // Exact runtime hooks are editable even while their full contextual
    // gameplay/UI scenario is unavailable. No synthetic scenario factory.
    for(const FName ID:{FName(TEXT("Reward.Chest")),FName(TEXT("Reward.StageBooster")),FName(TEXT("Reward.PerkPickup")),FName(TEXT("Reward.TaskComplete")),FName(TEXT("UI.ShiftFeedback"))}) {
        if(MCPerkPreviewAdapters::Find(ID))continue;
        const auto* C=MCVFXSourceContracts::Find(ID);if(!C)continue;
        FMCPerkPreviewAdapterEntry E;E.PerkID=ID;E.Kind=EMCPerkPreviewAdapterKind::Context;E.Label=C->Label;E.Stages=C->Events;E.bSupportsProfileOverride=true;MCPerkPreviewAdapters::Register(MoveTemp(E));
    }
}
