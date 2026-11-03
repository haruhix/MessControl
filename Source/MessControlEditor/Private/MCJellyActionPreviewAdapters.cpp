#include "MCJellyActionPreviewAdapters.h"

#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCCataclysmEvent.h"
#include "MCCataclysmJelly.h"
#include "MCCoffeeFlood.h"
#include "MCJellyPresentationComponent.h"
#include "MCDayPlan.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "MCVFXSourceContracts.h"
#include "MCWorldPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FJellyNativeAdapter final:public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(C.PerkID!=UMCJellyPresentationComponent::SourceID() || !C.World || !IsValid(C.Hero)
            || C.Hero->GetWorld()!=C.World || C.World->WorldType!=EWorldType::Game
            || C.World->GetOutermost()!=GetTransientPackage() || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>())
        {Error=TEXT("Нужны изолированный Game-мир и настоящий зубик.");return false;}
        auto* Saved=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        auto* Private=Saved?DuplicateObject<UStaticMesh>(Saved,GetTransientPackage()):nullptr;
        if(!Private){Error=TEXT("Не подготовлена частная поверхность языка.");return false;}
        Private->ClearFlags(RF_Public|RF_Standalone);Private->SetFlags(RF_Transient);Private->bAllowCPUAccess=true;
        const FTransform Pose(FQuat::Identity,FVector(0,0,-.1f),FVector(70,70,1));
        auto* Floor=C.World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Floor){Error=TEXT("Не создан настоящий язык.");return false;}
        Tongue=Floor;C.Targets.Add(Floor);Floor->SetFlags(RF_Transient);Floor->SourceMesh=Private;Floor->bAutomaticYawns=false;
        Floor->FinishSpawning(Pose);Floor->SetActorTickEnabled(false);Floor->Surface->SetVisibility(false);
        auto* Owner=C.World->SpawnActorDeferred<AMCCataclysmEvent>(AMCCataclysmEvent::StaticClass(),FTransform::Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Owner){Error=TEXT("Не создан реальный катаклизм с желе.");return false;}
        Encounter=Owner;Owner->SetFlags(RF_Transient);C.Targets.Add(Owner);
        Plan.Reset(NewObject<UMCDayPlan>(Owner,NAME_None,RF_Transient));
        Presentation=MCWorldPresentation::PrepareOwner(Owner,{UMCJellyPresentationComponent::SourceID()});
        if(!Presentation.IsValid()){Error=TEXT("Не создан подготовленный канал желе.");return false;}
        if(C.ReportStatus)C.ReportStatus(TEXT("Настоящий Jellies: предупреждение → StartWave(false) → желе рисует грязь → диагностический летальный удар → родной разрыв/3 ребёнка. Paint и физика остаются нативными."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* Owner=Presentation.Get();const FName ID=UMCJellyPresentationComponent::SourceID();
        const auto* Contract=MCVFXSourceContracts::Find(ID);
        if(!Owner || !Contract || !Owner->PrepareSources(Error,true))return false;
        auto* Source=Owner->GetSource(ID);
        return Source && (!C.PresentationOverride || Source->PrepareScopedOverride(ID,Contract->Events,C.PresentationOverride,Error));
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {
        if(const auto* Owner=Presentation.Get())if(const auto* Source=Owner->GetSource(UMCJellyPresentationComponent::SourceID()))
        {TArray<UNiagaraSystem*> Systems;Source->GetPreparedResources(Systems);for(auto* NS:Systems)Out.AddUnique(NS);}
    }
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;
        if(auto* Owner=Encounter.Get())
        {Owner->FinishSpawning(FTransform::Identity);bStarted=Owner->Start(Plan.Get(),EMCCataclysmVariant::Jellies,7381);}
        if(!bStarted){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeJellyStartRejected"));}
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted || bDone)return;
        Age+=FMath::Max(0.f,Dt);
        auto* Owner=Encounter.Get();if(!Owner){bDone=true;return;}
        if(!bSawRealFirstWave && Owner->Phase==EMCCataclysmPhase::FirstWave && IsValid(Owner->Liquid.Get()) && Owner->Liquid->bRiverFlood)
        {bSawRealFirstWave=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeJellyFirstWave"));}
        if(!Jelly.IsValid())
            for(const auto& Entry:Owner->Jellies)
                if(IsValid(Entry.Get()) && Entry->JellySize==EMCCataclysmJellySize::Large)
                {Jelly=Entry.Get();C.Targets.Add(Entry.Get());if(C.RequestFramingUpdate)C.RequestFramingUpdate();break;}
        if(auto* Actual=Jelly.Get())
        {
            auto* P=Actual->JellyPresentation.Get();
            if(P && !bContact && P->GetAcceptedContactSerial()>0 && Actual->PaintStrokeRequests>0)
            {bContact=true;if(C.RecordEvent)C.RecordEvent(TEXT("Trail"));}
            if(bContact && !bDamaged)
            {
                bDamaged=Actual->ReceiveToolDamage(10000.f,C.Hero)>0.f;
                if(bDamaged && C.RecordEvent)C.RecordEvent(TEXT("NativeJellyLethalToolDamage"));
            }
            if(P && !bRupture && Actual->RuptureCount==1 && P->GetAcceptedRuptureSerial()>0)
            {
                bRupture=true;RuptureAt=Age;
                if(C.RecordEvent)C.RecordEvent(TEXT("Impact"));
                if(C.ReportStatus)C.ReportStatus(FString::Printf(TEXT("Реальный разрыв: родитель=%d, детей=%d, paint-bursts=%d. Заменяются только декоративные14капель; грязь/меш/клип/сила не подменены."),Actual->RuptureCount,Actual->ChildrenSpawned,Owner->DeathPaintBursts));
            }
        }
        if(bRupture && Age-RuptureAt>1.15f){bDone=true;return;}
        if(Owner->bFailed || Age>45.f)
        {bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("NativeJellyRuptureMissing"));}
    }
    void Release(FMCPerkPreviewContext&) override{}
    void Cancel(FMCPerkPreviewContext&) override
    {
        if(auto* Owner=Encounter.Get()){Owner->Stop(true);Owner->Destroy();}
        if(auto* Floor=Tongue.Get())Floor->Destroy();
        bDone=true;
    }
    void Reset(FMCPerkPreviewContext& C) override
    {Cancel(C);Encounter.Reset();Tongue.Reset();Jelly.Reset();Presentation.Reset();Plan.Reset();}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Контекст желе завершён"):bRupture?TEXT("Настоящий разрыв и paint"):bDamaged?TEXT("Штатное предупреждение о разрыве"):Jelly.IsValid()?TEXT("Желе рисует грязь"):bSawRealFirstWave?TEXT("Настоящая первая волна"):TEXT("Предупреждение Jellies");}
    float GetTailSeconds() const override{return 1.2f;}
private:
    TWeakObjectPtr<AMCCataclysmEvent> Encounter;TWeakObjectPtr<AMCCataclysmJelly> Jelly;TWeakObjectPtr<AMCTongue> Tongue;
    TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;TStrongObjectPtr<UMCDayPlan> Plan;
    float Age=0,RuptureAt=0;bool bCommitted=false,bStarted=false,bSawRealFirstWave=false,bContact=false,bDamaged=false,bRupture=false,bDone=false;
};
}

void MCJellyActionPreviewAdapters::Register()
{
    const FName ID=UMCJellyPresentationComponent::SourceID();const auto* Contract=MCVFXSourceContracts::Find(ID);if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=ID;Entry.Label=Contract->Label;Entry.Kind=EMCPerkPreviewAdapterKind::Context;
    Entry.Stages=Contract->Events;Entry.bSupportsProfileOverride=true;Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.StageLabels.Add(TEXT("Trail"),FText::FromString(TEXT("Мокрый контакт желе · paint остаётся игровым")));
    Entry.StageLabels.Add(TEXT("Impact"),FText::FromString(TEXT("Разрыв ·14исходных капель")));
    // No invented original-NS defaults: native art is still procedural until
    // the converted resources are actually authored and canonically assigned.
    Entry.DescribeNativeSources=[](FName Event)
    {
        TArray<FMCVFXNativeSourcePart> Out;FMCVFXNativeSourcePart Material;
        Material.ID=TEXT("Jelly.Splatter.Material");Material.Label=FText::FromString(TEXT("Родной гель контакта/капель"));
        Material.Kind=EMCVFXNativeSourceKind::Material;Material.Asset=FSoftObjectPath(TEXT("/Game/Cataclysm/Jelly/M_CataclysmJellyGel.M_CataclysmJellyGel"));Out.Add(Material);
        FMCVFXNativeSourcePart Geometry;Geometry.ID=Event==TEXT("Impact")?TEXT("Jelly.Rupture.Geometry"):TEXT("Jelly.Contact.Geometry");
        Geometry.Label=FText::FromString(Event==TEXT("Impact")?TEXT("Исходные14баллистических капель"):TEXT("Исходный мокрый отпечаток"));
        Geometry.Kind=EMCVFXNativeSourceKind::Geometry;Geometry.SourceFile=TEXT("Source/MessControl/Private/MCCataclysmJelly.cpp");Out.Add(Geometry);return Out;
    };
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящая волна → желе → удар → разрыв")));
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FJellyNativeAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
