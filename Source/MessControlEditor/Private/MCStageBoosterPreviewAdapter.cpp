#include "MCStageBoosterPreviewAdapter.h"

#include "MCMouthSurface.h"
#include "MCPerkComponent.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCPlayerState.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCSingleDayDirector.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCTongue.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace MCStageBoosterPreview
{
namespace
{
const FName SourceID(TEXT("Reward.StageBooster"));
const FName SpitEvent(TEXT("Spit"));
const FName ArrivalEvent(TEXT("Arrival"));
constexpr int32 PreviewSeed=743;

class FAdapter final : public IMCPerkPreviewAdapter
{
public:
    ~FAdapter() override { Unlisten(); }

    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || !IsValid(C.Hero) || !C.Hero->HasAuthority())
        { Error=TEXT("Для вылета сундука нужен изолированный мир Composer.");return false; }
        const auto* Perks=IsValid(C.PlayerState)?C.PlayerState->Perks.Get():nullptr;
        RewardTable=Perks?Perks->GetPerkTable():nullptr;
        if(!RewardTable.IsValid())
        { Error=TEXT("Каталог бустеров для настоящего сундука недоступен.");return false; }

        auto* SavedPlane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* FloorMesh=SavedPlane?DuplicateObject<UStaticMesh>(SavedPlane,GetTransientPackage()):nullptr;
        if(!FloorMesh)
        { Error=TEXT("Не удалось загрузить поверхность изолированного полигона.");return false; }
        FloorMesh->ClearFlags(RF_Public|RF_Standalone);
        FloorMesh->SetFlags(RF_Transient);
        FloorMesh->bAllowCPUAccess=true;
        // Lift the actual tongue collision a little above the immutable preview floor.
        const FTransform FloorPose(FQuat::Identity,FVector(500,0,8),FVector(38,30,1));
        auto* Ground=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),FloorPose,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Ground)
        { Error=TEXT("Не удалось создать опору языка.");return false; }
        Tongue=Ground;
        Ground->SourceMesh=FloorMesh;
        Ground->bAutomaticYawns=false;
        Ground->FinishSpawning(FloorPose);
        Ground->SetActorTickEnabled(false);
        Ground->VisualSurface->SetVisibility(false);
        Ground->Surface->SetVisibility(true);
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/MI_MouthPalate.MI_MouthPalate")))
            Ground->Surface->SetMaterial(0,Material);

        auto* ExitMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FromBlender4/SK_Exit.SK_Exit"));
        auto* UvulaMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/Arena/ManualIntake/SM_InteractiveUvula.SM_InteractiveUvula"));
        if(!ExitMesh || !UvulaMesh)
        { Error=TEXT("Сохранённая модель глотки или язычка не найдена.");return false; }
        // Keep the L_Mouth throat's art scale, local transforms and morph setup.
        // Only its world X/Y position is staged for this private camera.
        const FTransform ThroatPose(FQuat::Identity,FVector(730,0,-220),FVector(1.64));
        auto* Exit=C.World->SpawnActorDeferred<AMCThroat>(AMCThroat::StaticClass(),ThroatPose,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Exit)
        { Error=TEXT("Не удалось создать настоящую глотку.");return false; }
        Throat=Exit;
        C.Targets.Add(Exit);
        Exit->ManualAuthoredExpansion=.75f;
        Exit->AuthoredMouth->SetSkeletalMesh(ExitMesh);
        Exit->AuthoredMouth->SetRelativeTransform(FTransform(FRotator(0,-90,0),FVector(183.259,-52.485,578.657)));
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Cold/M_ExitClimate.M_ExitClimate")))
            Exit->AuthoredMouth->SetMaterial(0,Material);
        Exit->bUseAuthoredUvula=true;
        Exit->Uvula->SetStaticMesh(UvulaMesh);
        Exit->AuthoredUvulaRestTransform=FTransform(FRotator(0,-90,0),FVector(155.519,-29.215,805.019),FVector(.514240,.419810,.564700));
        Exit->FinishSpawning(ThroatPose);
        VomitCountBefore=Exit->VomitCount;
        for(TActorIterator<AMCMouthSurface> It(C.World);It;++It) ++SurfaceCountBefore;

        const auto RewardClass=GetDefault<AMCSingleDayDirector>()->StageRewardChestClass;
        const auto* Defaults=RewardClass?RewardClass->GetDefaultObject<AMCRewardChest>():nullptr;
        auto* Zone=C.World->SpawnActor<AMCRewardDropZone>(FVector(375,0,8),FRotator::ZeroRotator);
        if(!Zone || !Defaults)
        { Error=TEXT("Не удалось подготовить сохранённый сундук и зону приземления.");return false; }
        DropZone=Zone;
        Zone->AllowedLandingSurface=Ground;
        Zone->bAllowRewardDrops=false;
        Zone->InwardMargin=30;
        Zone->Area->SetBoxExtent(FVector(340,340,300));
        FRandomStream Choice(PreviewSeed);
        FVector Landing,Normal;
        const TArray<AActor*> Ignored={C.Hero};
        const FVector Extent=Defaults->GetPlacementHalfExtent();
        bool bFoundLanding=false;
        for(int32 Attempt=0;Attempt<24 && !bFoundLanding;++Attempt)
            bFoundLanding=Zone->FindLanding(Choice,Extent,450,Ignored,Landing,Normal);
        if(!bFoundLanding)
        { Error=TEXT("Штатная проверка отклонила площадку приземления на языке.");return false; }
        ChestPose=FTransform(FRotationMatrix::MakeFromXZ(FVector::ForwardVector,Normal).ToQuat(),Landing);
        auto* Reward=C.World->SpawnActorDeferred<AMCRewardChest>(RewardClass,ChestPose,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Reward)
        { Error=TEXT("Не удалось подготовить экземпляр сохранённого сундука.");return false; }
        Chest=Reward;
        C.Targets.Add(Reward);
        Presentation=MCWorldPresentation::PrepareOwner(Reward,{SourceID});
        C.PreviewFront=FVector(-1,.65,0);
        if(C.RequestFramingUpdate) C.RequestFramingUpdate();
        if(C.ReportStatus) C.ReportStatus(TEXT("Сохранённая глотка, настоящий сундук этапа и проверяемая опора языка. Сценарий показывает вылет и приземление; выбор бустеров остаётся в игре."));
        return Presentation.IsValid();
    }

    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();
        const auto* Contract=MCVFXSourceContracts::Find(SourceID);
        if(!Owner || !Contract || !Owner->PrepareSources(Error,true)) return false;
        auto* Channel=Owner->GetSource(SourceID);
        if(!Channel || (C.PresentationOverride && !Channel->PrepareScopedOverride(SourceID,Contract->Events,C.PresentationOverride,Error)))
            return false;
        ObservedChannel=Channel;
        EventHandle=Channel->OnSourceEventAccepted.AddLambda([this,Record=C.RecordEvent](FName ID,FName Event,EMCVFXPresentationResult Result) {
            if(!bCommitted || ID!=SourceID) return;
            const bool Accepted=Result==EMCVFXPresentationResult::Played || Result==EMCVFXPresentationResult::Suppressed;
            if(Event==SpitEvent && Accepted && !bSpit) {
                bSpit=true;
                if(Record) Record(Event);
            }
            if(Event==ArrivalEvent && Result!=EMCVFXPresentationResult::Rejected && !bArrival) {
                bArrival=true;
                if(Record) Record(Event);
            }
        });
        return true;
    }

    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* Channel=ObservedChannel.Get()) Channel->GetPreparedResources(Out);
    }

    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        auto* Reward=Chest.Get();
        auto* Exit=Throat.Get();
        if(!Reward || !Exit || !DropZone.IsValid() || !RewardTable.IsValid())
        { Reject(C,TEXT("Владелец награды, глотка или место приземления исчезли."));return; }
        const FVector Landing=ChestPose.GetLocation();
        Reward->InitializeTeamStageReward(Landing,Landing+FVector(0,0,450),PreviewSeed,RewardTable.Get(),DropZone.Get());
        if(!Reward->ConfigureThroatLaunch(Exit))
        { Reject(C,TEXT("Штатный вылет из глотки отклонён. Вертикальная запасная подача в этом сценарии не считается вылетом."));return; }
        bCommitted=true;
        Reward->FinishSpawning(ChestPose);
        if(Reward->Stage!=EMCRewardChestStage::Falling || Reward->bMimic)
        { Reject(C,TEXT("Настоящий сундук не вошёл в подготовку вылета."));return; }
        if(C.RecordEvent) C.RecordEvent(TEXT("NativeAnticipation"));
        if(C.RequestFramingUpdate) C.RequestFramingUpdate();
    }

    void Tick(FMCPerkPreviewContext& C,float) override
    {
        if(!bCommitted || bDone || bRejected) return;
        auto* Reward=Chest.Get();
        if(!Reward || !Throat.IsValid())
        { Reject(C,TEXT("Сундук или глотка исчезли до окончания вылета."));return; }
        if(!bAirborne && bSpit && Reward->Stage==EMCRewardChestStage::Falling
            && FVector::DistSquared2D(Reward->GetActorLocation(),Reward->FallStart)>FMath::Square(35.))
        {
            bAirborne=true;
            if(C.RecordEvent) C.RecordEvent(TEXT("NativeAirborne"));
        }
        if(LandedAt<0 && Reward->Stage==EMCRewardChestStage::Landed
            && Reward->GetActorLocation().Equals(Reward->LandingPoint,3))
        {
            LandedAt=C.Elapsed;
            if(C.RecordEvent) C.RecordEvent(TEXT("NativeLanded"));
        }
        if(bSpit && bAirborne && bArrival && LandedAt>=0 && C.Elapsed>=LandedAt+.65f)
        {
            int32 SurfaceCount=0;
            for(TActorIterator<AMCMouthSurface> It(C.World);It;++It) ++SurfaceCount;
            if(SurfaceCount!=SurfaceCountBefore || Throat->VomitCount!=VomitCountBefore)
            { Reject(C,TEXT("Вылет создал игровое загрязнение или изменил счётчик рвоты."));return; }
            bDone=true;
            if(C.RecordEvent) C.RecordEvent(TEXT("NativeCleanLanding"));
        }
        if(LandedAt<0 && C.Elapsed>2.35f)
            Reject(C,TEXT("Настоящий сундук не подтвердил приземление."));
        else if(C.Elapsed>3.3f && !bDone)
            Reject(C,TEXT("Не подтверждены вылет, приземление и оба события представления."));
    }

    void Cancel(FMCPerkPreviewContext&) override
    {
        if(auto* Channel=ObservedChannel.Get()) Channel->StopSource(SourceID,true);
        Unlisten();
    }

    void Reset(FMCPerkPreviewContext& C) override
    {
        Cancel(C);
        if(auto* Actor=Chest.Get()) Actor->Destroy(false,false);
        if(auto* Actor=DropZone.Get()) Actor->Destroy(false,false);
        if(auto* Actor=Throat.Get()) Actor->Destroy(false,false);
        if(auto* Actor=Tongue.Get()) Actor->Destroy(false,false);
        Chest.Reset();DropZone.Reset();Throat.Reset();Tongue.Reset();Presentation.Reset();RewardTable.Reset();
    }

    bool IsComplete(const FMCPerkPreviewContext&) const override { return bDone || bRejected; }
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {
        if(bRejected) return TEXT("Вылет сундука не подтверждён");
        if(LandedAt>=0) return TEXT("Сундук на языке · чистое приземление");
        if(bSpit) return TEXT("Вылет из глотки · полёт сундука");
        return TEXT("Глотка готовится выплюнуть сундук");
    }
    float GetTailSeconds() const override { return .25f; }

private:
    void Reject(FMCPerkPreviewContext& C,const FString& Reason)
    {
        bRejected=true;
        if(C.ReportStatus) C.ReportStatus(Reason);
        if(C.RecordEvent) C.RecordEvent(TEXT("ScenarioRejected"));
    }

    void Unlisten()
    {
        if(auto* Channel=ObservedChannel.Get()) Channel->OnSourceEventAccepted.Remove(EventHandle);
        ObservedChannel.Reset();
        EventHandle.Reset();
    }

    TWeakObjectPtr<AMCRewardChest> Chest;
    TWeakObjectPtr<AMCRewardDropZone> DropZone;
    TWeakObjectPtr<AMCThroat> Throat;
    TWeakObjectPtr<AMCTongue> Tongue;
    TWeakObjectPtr<UDataTable> RewardTable;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    TWeakObjectPtr<UMCVFXPresentationComponent> ObservedChannel;
    FTransform ChestPose;
    FDelegateHandle EventHandle;
    int32 SurfaceCountBefore=0,VomitCountBefore=0;
    float LandedAt=-1.f;
    bool bCommitted=false,bSpit=false,bAirborne=false,bArrival=false,bDone=false,bRejected=false;
};
}

void Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(SourceID);
    if(!Contract) return;
    FMCPerkPreviewAdapterEntry Entry;
    Entry.PerkID=SourceID;
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.Label=Contract->Label;
    Entry.Stages=Contract->Events;
    Entry.bSupportsProfileOverride=true;
    Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Сундук этапа · вылет из глотки и приземление")));
    Entry.StageLabels.Add(SpitEvent,FText::FromString(TEXT("Вылет из глотки")));
    Entry.StageLabels.Add(ArrivalEvent,FText::FromString(TEXT("Приземление на языке")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter> { return MakeUnique<FAdapter>(); };
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
}
