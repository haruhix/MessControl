#include "MCFoodAutoMouthPreviewAdapter.h"

#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCInventoryComponent.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"

namespace MCFoodAutoMouthPreview
{
namespace
{
const FName SourceID(TEXT("Food.AutoMouth"));

/** A real saved oversized slice, unchanged in scale, crosses the portion limit
 * through ordinary knife input. The stable GameState source owns the effect. */
class FAdapter final : public IMCPerkPreviewAdapter
{
public:
    ~FAdapter() override { Unlisten(); }

    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero)
            || !C.Hero->HasAuthority() || !C.Hero->Inventory)
        { Error=TEXT("Нужен изолированный authority Composer-мир с настоящим ножом."); return false; }
        Context=&C;
        for(TActorIterator<AMCFoodActor> It(C.World);It;++It) FoodBefore.Add(*It);
        auto* State=C.World->GetGameState<AMCGameState>();
        Source=State?State->FoodCutIntakePresentation.Get():nullptr;
        if(!Source.IsValid()) { Error=TEXT("Нет настоящего стабильного источника Food.AutoMouth."); return false; }
        C.Hero->SetPrimaryInputHeld(false);
        C.Hero->Inventory->ServerSelect(EMCToolSlot::Knife);
        if(C.Hero->Inventory->Selected!=EMCToolSlot::Knife)
        { Error=TEXT("Штатный выбор ножа отклонён."); return false; }

        FTransform Pose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+C.Hero->GetActorForwardVector()*110.f);
        auto* Food=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food) return false;
        Target=Food; C.Targets.Add(Food);
        if(const auto* Profile=Food->Profile.LoadSynchronous()) Food->Settings=Profile->Settings;
        const auto* Table=GetDefault<UMCDayPlan>()->Menu.LoadSynchronous();
        if(!Table || Table->GetRowStruct()!=FMCFoodRow::StaticStruct())
        { Error=TEXT("Сохранённое меню еды недоступно."); return false; }
        const FVector Limit=Food->Settings.MaxAutoConsumeDimensions;
        auto Names=Table->GetRowNames(); Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
        bool Found=false;
        for(FName Name:Names)
        {
            const auto* Row=Table->FindRow<FMCFoodRow>(Name,TEXT("ComposerAutoMouth"),false);
            if(!Row || Row->Kind!=EMCFoodKind::Food) continue;
            for(const auto& Ref:Row->FragmentMeshes)
            {
                auto* Mesh=Ref.LoadSynchronous();
                if(!Mesh || !Mesh->bAllowCPUAccess) continue;
                const FVector Size=Mesh->GetBoundingBox().GetSize()*Row->FragmentScale.GetAbs();
                if(Size.ContainsNaN() || Size.GetMin()<=0) continue;
                int32 CutAxis=INDEX_NONE;
                for(int32 Axis=0;Axis<3;++Axis)
                    if(Size[Axis]>Limit[Axis] && Size[Axis]<=Limit[Axis]*2.f
                        && Size[(Axis+1)%3]<=Limit[(Axis+1)%3] && Size[(Axis+2)%3]<=Limit[(Axis+2)%3])
                    { CutAxis=Axis; break; }
                if(CutAxis==INDEX_NONE) continue;
                // Align the source's long axis with the real Hero.Right knife
                // plane. Rotation preserves authored geometry and scale.
                Pose.SetRotation(C.Hero->GetActorQuat()*(CutAxis==0?FRotator(0,90,0):CutAxis==2?FRotator(0,0,90):FRotator::ZeroRotator).Quaternion());
                FMCFoodRow Choice=*Row;
                Choice.FragmentMeshes={Ref}; // Select one existing variant; preserve all authored tuning.
                FRandomStream Random(743);
                Food->Initialize(false,C.Hero->GetActorForwardVector());
                Food->ConfigureItem(Name,Choice,Random,true);
                RowName=Name; Found=true; break;
            }
            if(Found) break;
        }
        if(!Found) { Error=TEXT("Нет сохранённого куска, который один настоящий разрез делит до размера порции. Масштаб ради превью не меняется."); return false; }
        Food->FinishSpawning(Pose);
        if(!Food->UsesAutomaticCutIntake() || Food->FitsAutomaticIntakePortion() || !C.Hero->Inventory->CanBreak(Food))
        { Error=TEXT("Настоящая еда не подтвердила новый режим, исходный размер или нож."); return false; }
        const FBox Bounds=Food->Visual->Bounds.GetBox();
        const FVector Forward=C.Hero->GetActorForwardVector().GetSafeNormal2D();
        const float Reach=FMath::Clamp(C.Hero->Inventory->MeleeReach()*.40f,35.f,80.f);
        const float Along=FVector::DotProduct(Bounds.GetExtent(),Forward.GetAbs())+Reach;
        Food->Body->SetSimulatePhysics(false); // Isolated fixture: preserve the real mesh, size and query.
        Food->SetActorLocation(Food->GetActorLocation()+C.Hero->GetActorLocation()+Forward*Along-Bounds.GetCenter(),false,nullptr,ETeleportType::TeleportPhysics);

        const FTransform MouthPose(C.Hero->GetActorQuat(),C.Hero->GetActorLocation()+Forward*700.f+FVector(0,0,80.f));
        auto* Mouth=C.World->SpawnActor<AMCThroat>(AMCThroat::StaticClass(),MouthPose);
        if(!Mouth) return false;
        Throat=Mouth; C.Targets.Add(Mouth); Mouth->SetActorTickEnabled(false);
        BeforeSwing=C.Hero->ValidatedSwingCount; BeforeCut=C.Hero->ConfirmedFoodCutCount;
        if(C.ReportStatus) C.ReportStatus(FString::Printf(TEXT("%s: сохранённый крупный кусок → настоящий разрез ножом → мелкие порции → принятый зачёт → крошки к настоящей глотке. Масштаб и пороги не меняются."),*RowName.ToString()));
        if(C.RequestFramingUpdate) C.RequestFramingUpdate();
        return true;
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* P=Source.Get(); const auto* Contract=MCVFXSourceContracts::Find(SourceID);
        if(!P || !Contract || !P->Prepare(Error,true)) return false;
        if(C.PresentationOverride && !P->PrepareScopedOverride(SourceID,Contract->Events,C.PresentationOverride,Error)) return false;
        EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        if(!P->GetPreparedEventMode(SourceID,TEXT("Consume"),Mode) || Mode==EMCPerkVFXSlotMode::UseDefault)
        { Error=TEXT("У Consume нет сохранённого Niagara-слоя; механический зачёт без эффекта не считается готовым VFX-превью."); return false; }
        EventHandle=P->OnSourceEventAccepted.AddRaw(this,&FAdapter::OnAccepted);
        return true;
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    { if(const auto* P=Source.Get()) P->GetPreparedResources(Out); }
    void OnCommitted(FMCPerkPreviewContext& C) override
    { bCommitted=true; if(IsValid(C.Hero)) C.Hero->SetPrimaryInputHeld(true); }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted || bDone || bRejected) return;
        Age+=FMath::Max(0.f,Dt);
        if(!IsValid(C.Hero)) { Reject(C,TEXT("Персонаж исчез до принятого разреза.")); return; }
        if(!bSwing && C.Hero->ValidatedSwingCount>BeforeSwing)
        { bSwing=true; if(C.RecordEvent) C.RecordEvent(TEXT("NativeKnifeSwing")); }
        if(!bCut && C.Hero->ConfirmedFoodCutCount>BeforeCut)
        { bCut=true; if(C.RecordEvent) C.RecordEvent(TEXT("NativeFoodCut")); }
        if(!bPortion)
            for(TActorIterator<AMCFoodActor> It(C.World);It;++It)
                if(!FoodBefore.Contains(TWeakObjectPtr<AMCFoodActor>(*It)) && It->WasAutomaticallyConsumed())
                { bPortion=true; if(C.RecordEvent) C.RecordEvent(TEXT("NativePortionAccepted")); break; }
        if(!bReleased && (bCut || Age>.2f)) Release(C);
        if(bSwing && bCut && bPortion && bConsume && Age>=AcceptedAge+1.2f) bDone=true;
        if(Age>4.f && !bDone) Reject(C,TEXT("Настоящий разрез, мелкая порция и Consume не подтверждены вместе."));
    }
    void Release(FMCPerkPreviewContext& C) override
    { if(IsValid(C.Hero)) C.Hero->SetPrimaryInputHeld(false); bReleased=true; }
    void Cancel(FMCPerkPreviewContext& C) override
    { Release(C); if(auto* P=Source.Get()) P->StopSource(SourceID,true); Unlisten(); }
    void Reset(FMCPerkPreviewContext& C) override
    {
        Cancel(C);
        if(C.World) {
            TArray<AMCFoodActor*> Owned;
            for(TActorIterator<AMCFoodActor> It(C.World);It;++It)
                if(!FoodBefore.Contains(TWeakObjectPtr<AMCFoodActor>(*It))) Owned.Add(*It);
            for(auto* A:Owned) if(IsValid(A)) A->Destroy();
        }
        if(auto* A=Throat.Get()) A->Destroy();
        Target.Reset(); Throat.Reset(); Source.Reset(); FoodBefore.Reset(); Context=nullptr;
    }
    bool IsComplete(const FMCPerkPreviewContext&) const override { return bDone || bRejected; }
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    { return bRejected?TEXT("Сценарий не подтверждён"):bDone?TEXT("Разрез и автоматическая подача завершены"):bConsume?TEXT("Настоящие порции → крошки в рот"):TEXT("Обычный ввод ножа → пересечение порога порции"); }
    float GetTailSeconds() const override { return .25f; }
private:
    void OnAccepted(FName ID,FName Event,EMCVFXPresentationResult Result)
    {
        if(ID!=SourceID || Event!=TEXT("Consume") || (Result!=EMCVFXPresentationResult::Played && Result!=EMCVFXPresentationResult::Suppressed)) return;
        if(!bConsume) { bConsume=true; AcceptedAge=Age; if(Context && Context->RecordEvent) Context->RecordEvent(Event); }
    }
    void Unlisten()
    { if(auto* P=Source.Get()) P->OnSourceEventAccepted.Remove(EventHandle); EventHandle.Reset(); }
    void Reject(FMCPerkPreviewContext& C,const FString& Why)
    { bRejected=true; Release(C); if(C.ReportStatus) C.ReportStatus(Why); if(C.RecordEvent) C.RecordEvent(TEXT("ScenarioRejected")); }
    TWeakObjectPtr<AMCFoodActor> Target;
    TWeakObjectPtr<AMCThroat> Throat;
    TWeakObjectPtr<UMCVFXPresentationComponent> Source;
    TArray<TWeakObjectPtr<AMCFoodActor>> FoodBefore;
    FMCPerkPreviewContext* Context=nullptr;
    FDelegateHandle EventHandle;
    FName RowName;
    int32 BeforeSwing=0,BeforeCut=0;
    float Age=0,AcceptedAge=0;
    bool bCommitted=false,bSwing=false,bCut=false,bPortion=false,bConsume=false,bReleased=false,bDone=false,bRejected=false;
};
}

void Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(SourceID); if(!Contract) return;
    FMCPerkPreviewAdapterEntry Entry;
    Entry.PerkID=SourceID; Entry.Kind=EMCPerkPreviewAdapterKind::Context; Entry.Label=Contract->Label;
    Entry.Stages=Contract->Events; Entry.bSupportsProfileOverride=true;
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Нож · от крупного куска к порциям")));
    Entry.StageLabels.Add(TEXT("Consume"),FText::FromString(TEXT("Крошки → рот")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{ return MakeUnique<FAdapter>(); };
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
}
