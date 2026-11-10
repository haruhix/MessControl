#include "MCNutThrowPreviewAdapter.h"

#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCNutRainEvent.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace MCNutThrowPreview
{
namespace
{
const FName SourceID(TEXT("Food.NutThrow"));
const FName ImpactEvent(TEXT("BossImpact"));

class FAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        const auto* State=C.World?C.World->GetGameState<AMCGameState>():nullptr;
        if(!State || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero) || !C.Hero->HasAuthority()
            || !C.Hero->FoodCollection || C.Scenario!=EMCPerkPreviewScenario::Target)
        {Error=TEXT("Для броска нужен отдельный мир Composer с настоящим зубиком и рыцарем.");return false;}
        Source=State->NutThrowPresentation;
        const auto* Data=LoadObject<UMCNutRainProfile>(nullptr,TEXT("/Game/Gameplay/CoreLoop/DA_NutRain.DA_NutRain"));
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        const auto* Menu=Data?Data->Menu.LoadSynchronous():nullptr;
        const auto* Row=Menu?Menu->FindRow<FMCFoodRow>(Data->NutRow,TEXT("Nut throw preview"),false):nullptr;
        const auto* Whole=Row&&!Row->WholeMeshes.IsEmpty()?Row->WholeMeshes[0].LoadSynchronous():nullptr;
        if(!Source.IsValid() || !Plane || !Row || !Whole || Row->FragmentMeshes.IsEmpty())
        {Error=TEXT("Не найдены сохранённые настройки ореха, рыцаря или источник эффекта.");return false;}

        const FTransform FloorPose(FRotator::ZeroRotator,FVector(0,0,-.1f),FVector(35,35,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        auto* Encounter=C.World->SpawnActor<AMCNutRainEvent>();
        if(!Floor || !Encounter){Error=TEXT("Не удалось создать изолированный полигон броска.");return false;}
        C.Targets.Add(Floor);C.Targets.Add(Encounter);
        Floor->SourceMesh=Plane;Floor->bAutomaticYawns=false;Floor->FinishSpawning(FloorPose);
        Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);
        Encounter->Settings=Data->Settings;Encounter->Stage=EMCNutRainStage::Enemies;Encounter->SetActorTickEnabled(false);

        const FVector Forward=C.Hero->GetActorForwardVector().GetSafeNormal2D();
        const FVector BossXY=C.Hero->GetActorLocation()+Forward*380.f;
        const float BossRadius=Data->Settings.Boss.BodyRadiusForRole(EMCNutBossRole::Tank);
        FHitResult Ground;
        if(!Floor->SurfacePoint(BossXY,Ground)){Error=TEXT("Полигон не подтвердил поверхность под рыцарем.");return false;}
        const FVector BossPoint=Ground.ImpactPoint+FVector(0,0,BossRadius+4.f);
        const FTransform BossPose((-Forward).Rotation(),BossPoint);
        auto* Target=C.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),BossPose,Encounter,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Target){Error=TEXT("Не удалось создать рыцаря.");return false;}
        Boss=Target;C.Targets.Add(Target);
        Target->ConfigureEncounter(Floor,Encounter,EMCNutBossRole::Tank,Data->Settings.Boss,1,137);
        Target->State=EMCNutBossState::Idle;Target->SetActorLocation(BossPoint);Target->FinishSpawning(BossPose);
        Encounter->RegisterEncounterEnemy(Target);
        Target->SetActorTickEnabled(false);
        for(TActorIterator<AMCNutCombatEffect> It(C.World);It;++It)
            if(It->GetOwner()==Target && It->Cue.Type==EMCNutCombatCue::JumpTell)It->Destroy(false,false);
        if(!Target->TankModel->GetSkeletalMeshAsset() || Target->ShieldHealth<=0.f)
        {Error=TEXT("Сохранённая модель или щит рыцаря недоступны.");return false;}
        InitialShield=Target->ShieldHealth;

        FMCFoodRow PieceRow=*Row;
        PieceRow.FragmentScale=FVector(Data->Settings.LargeNutHeight/FMath::Max(.001,Whole->GetBounds().BoxExtent.Z*2));
        PieceRow.FragmentMeshes.SetNum(1); // One saved shell piece, at the rain encounter's scale.
        const FTransform PiecePose(C.Hero->GetActorLocation()+Forward*105.f+FVector(0,0,25));
        auto* Food=C.World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),PiecePose,Encounter,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food){Error=TEXT("Не удалось создать фрагмент ореха.");return false;}
        Piece=Food;C.Targets.Add(Food);
        FRandomStream Random(137);Food->ConfigureItem(Data->NutRow,PieceRow,Random,true);
        Food->Tags.AddUnique(TEXT("MCNutRain"));Food->FinishSpawning(PiecePose);Food->Body->SetEnableGravity(false);
        return true;
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Component=Source.Get();
        if(!Component){Error=TEXT("Источник попадания недоступен.");return false;}
        if(C.PresentationOverride ? !Component->PrepareProfileOverride(C.PresentationOverride,Error) : !Component->Prepare(Error))return false;
        Receipt=Component->OnEventAccepted.AddLambda([this,Record=C.RecordEvent](FName Event,EMCVFXPresentationResult Result) {
            if(!bLaunched || Event!=ImpactEvent)return;
            bImpact=Result==EMCVFXPresentationResult::Played || Result==EMCVFXPresentationResult::Suppressed;
            if(Record)Record(bImpact?ImpactEvent:FName(TEXT("BossImpactRejected")));
        });
        return true;
    }

    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {if(const auto* Component=Source.Get())Component->GetPreparedResources(Out);}

    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;
        C.Hero->FoodCollection->RequestCollecting(true);
    }

    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted || bDone)return;
        Age+=Dt;
        auto* Food=Piece.Get();auto* Target=Boss.Get();
        if(!Food || !Target || !IsValid(C.Hero)){Fail(C);return;}
        if(!bLaunched && Age>=.55f && C.Hero->FoodCollection->Contains(Food)) {
            if(C.RecordEvent)C.RecordEvent(TEXT("NutPickedUp"));
            FVector Aim=Target->Body->GetComponentLocation()-Food->GetActorLocation();
            const double FlightSeconds=Aim.Size()/C.Hero->FoodCollection->EffectiveThrowSpeed();
            Aim.Z-=.5*C.World->GetGravityZ()*FMath::Square(FlightSeconds);
            bLaunched=true;
            C.Hero->ServerSetThrowHeld(true,Aim.GetSafeNormal());
            C.Hero->ServerSetThrowHeld(false,FVector::ForwardVector);
        }
        if(bImpact && Food->IsDisposed() && Food->ConfirmedImpacts==1 && Target->ShieldHealth<InitialShield) {
            if(!bConfirmed && C.RecordEvent)C.RecordEvent(TEXT("NutShattered"));
            bConfirmed=true;
        }
        if(Age>=2.f && bConfirmed)bDone=true;
        if(Age>=3.f && !bDone)Fail(C);
    }

    bool IsComplete(const FMCPerkPreviewContext&) const override {return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bFailed?TEXT("Бросок не подтверждён"):bConfirmed?TEXT("Попадание в щит и раскалывание"):bLaunched?TEXT("Орех в полёте"):TEXT("Подбор фрагмента ореха");}
    float GetTailSeconds() const override {return .3f;}
    void Cancel(FMCPerkPreviewContext& C) override
    {
        if(IsValid(C.Hero) && C.Hero->FoodCollection) {
            C.Hero->ServerSetThrowHeld(false,FVector::ForwardVector);
            C.Hero->FoodCollection->RequestCollecting(false);
        }
        if(auto* Component=Source.Get()) {
            Component->OnEventAccepted.Remove(Receipt);
            Component->StopSource(SourceID,true);
        }
        Receipt.Reset();
    }
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);Source.Reset();Piece.Reset();Boss.Reset();}

private:
    void Fail(FMCPerkPreviewContext& C)
    {
        bDone=bFailed=true;
        if(C.ReportStatus)C.ReportStatus(TEXT("Подбор, удар по щиту и раскалывание не подтверждены вместе."));
        if(C.RecordEvent)C.RecordEvent(TEXT("ScenarioRejected"));
    }
    TWeakObjectPtr<UMCVFXPresentationComponent> Source;
    TWeakObjectPtr<AMCFoodActor> Piece;
    TWeakObjectPtr<AMCNutBoss> Boss;
    FDelegateHandle Receipt;
    float Age=0,InitialShield=0;
    bool bCommitted=false,bLaunched=false,bImpact=false,bConfirmed=false,bDone=false,bFailed=false;
};
}

void Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(SourceID);
    if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;
    Entry.PerkID=SourceID;Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Подбор → бросок → попадание в рыцаря")));
    Entry.StageLabels.Add(ImpactEvent,FText::FromString(TEXT("Раскалывание ореха при попадании")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
}
