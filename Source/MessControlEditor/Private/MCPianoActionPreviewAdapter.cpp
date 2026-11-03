#include "MCPianoActionPreviewAdapter.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewGameMode.h"
#include "MCVFXSourceContracts.h"
#include "MCVFXPresentationComponent.h"
#include "MCWorldPresentationComponent.h"
#include "MCArenaTooth.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Package.h"

namespace
{
const FName PianoID(TEXT("World.ArenaTooth.Piano"));

/** The character's normal movement collision invokes Landed -> NotifyPianoLanding.
 * No synthetic source event, hit result, MIDI decision or piano clock is supplied. */
class FPianoLandingAdapter final : public IMCPerkPreviewAdapter
{
public:
    bool Prepare(FMCPerkPreviewContext& C,FString& Error) override
    {
        if(!C.World || C.World->WorldType!=EWorldType::Game || C.World->GetOutermost()!=GetTransientPackage()
            || !C.World->GetAuthGameMode<AMCPerkPreviewGameMode>() || C.PerkID!=PianoID
            || !IsValid(C.Hero) || C.Hero->GetWorld()!=C.World || !C.Hero->HasAuthority()
            || !C.Hero->Status || !C.Hero->Status->IsAlive() || !C.Hero->GetCharacterMovement())
        {Error=TEXT("Нужен живой зубик и настоящий CharacterMovement в изолированной песочнице.");return false;}
        HeroBefore=C.Hero->GetActorTransform();ModeBefore=C.Hero->GetCharacterMovement()->MovementMode;
        CustomModeBefore=C.Hero->GetCharacterMovement()->CustomMovementMode;VelocityBefore=C.Hero->GetCharacterMovement()->Velocity;
        bHaveHeroBefore=true;
        auto* Defaults=GetDefault<AMCArenaTooth>();auto* Mesh=Defaults->Visual?Defaults->Visual->GetStaticMesh().Get():nullptr;
        if(!Mesh){Error=TEXT("Нет исходного меша большого зуба.");return false;}
        // BeginPlay computes the real collision extent from the authored mesh.
        FVector Position=C.Hero->GetActorLocation()+FVector(230,0,0);Position.Z=300;
        auto* Key=C.World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),FTransform(Position),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Key){Error=TEXT("Не создан настоящий фортепианный зуб.");return false;}
        Tooth=Key;C.Targets.Add(Key);Key->SetFlags(RF_Transient);
        FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;Settings.bPianoEnabled=true;
        Key->Initialize(1,Settings);Key->FinishSpawning(FTransform(Position));Key->Label->SetHiddenInGame(true);
        if(!Key->IsAvailable() || !Key->Body || !Key->Visual || !Key->PianoNoteSound())
        {Error=TEXT("Настоящая клавиша/коллизия/исходная нота не готовы.");return false;}
        Position.Z=Key->Body->GetScaledBoxExtent().Z+35;Key->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
        Presentation=Key->FindComponentByClass<UMCWorldPresentationComponent>();
        if(!Presentation.IsValid()){Error=TEXT("Общий канал принятого события клавиши отсутствует.");return false;}
        BeforeSerial=Key->PianoState.Serial;BeforeHealth=Key->State.Health;ExpectedMidi=Key->PianoMidiNote();
        DropPoint=Key->Body->GetComponentLocation()+FVector(0,0,Key->Body->GetScaledBoxExtent().Z+C.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+100);
        C.PreviewFront=FVector(-1,0,0);if(C.RequestFramingUpdate)C.RequestFramingUpdate();
        if(C.ReportStatus)C.ReportStatus(TEXT("Реальное приземление на коронку → родное нажатие и нота. Исходных частиц нет; Hit принимает добавленные Niagara/SFX слои. Физическая клавиша остаётся нативной анимацией."));
        return true;
    }
    bool PreparePresentation(FMCPerkPreviewContext& C,FString& Error) override
    {
        auto* P=Presentation.Get();const auto* Contract=MCVFXSourceContracts::Find(PianoID);
        if(!P || !Contract || !P->PrepareSources(Error,true))return false;
        auto* Source=P->GetSource(PianoID);if(!Source){Error=TEXT("Не подготовлен Hit настоящей клавиши.");return false;}
        return !C.PresentationOverride || Source->PrepareScopedOverride(PianoID,Contract->Events,C.PresentationOverride,Error);
    }
    void GetPreparedVisualSystems(const FMCPerkPreviewContext&,TArray<UNiagaraSystem*>& Out) const override
    {if(const auto* P=Presentation.Get())if(const auto* Source=P->GetSource(PianoID))Source->GetPreparedResources(Out);}
    void OnCommitted(FMCPerkPreviewContext& C) override
    {
        bCommitted=true;
        // The native collision resolver derives the actual landing speed/contact.
        C.Hero->SetActorLocation(DropPoint,false,nullptr,ETeleportType::TeleportPhysics);
        auto* Move=C.Hero->GetCharacterMovement();Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Falling);
        Move->Velocity=FVector(0,0,-120);
    }
    void Tick(FMCPerkPreviewContext& C,float Dt) override
    {
        if(!bCommitted || bDone)return;Age+=FMath::Max(0.f,Dt);auto* Key=Tooth.Get();
        if(!Key || !Key->IsAvailable()){Fail(C,TEXT("PianoOwnerMissing"));return;}
        if(Key->State.Health!=BeforeHealth || Key->PianoMidiNote()!=ExpectedMidi){Fail(C,TEXT("PianoNativeStateChanged"));return;}
        if(!bAccepted && Key->PianoState.Serial>BeforeSerial) {
            bAccepted=true;
            if(C.RecordEvent)C.RecordEvent(TEXT("Hit"));
            auto* P=Presentation.Get();auto* Source=P?P->GetSource(PianoID):nullptr;
            if(!Source || !Source->GetLastError().IsEmpty()){Fail(C,TEXT("PianoPresentationRejected"));return;}
        }
        if(bAccepted) {
            bPressed|=Key->PianoOffset()>1.f;
            bReturned|=bPressed && FMath::IsNearlyZero(Key->PianoOffset());
            if(bReturned){bDone=true;if(C.RecordEvent)C.RecordEvent(TEXT("PianoReturned"));}
        }
        if(Age>4.f && !bDone)Fail(C,TEXT("PianoLandingOrPressMissing"));
    }
    void Cancel(FMCPerkPreviewContext& C) override
    {
        if(auto* P=Presentation.Get()){P->StopStates(PianoID);if(auto* Source=P->GetSource(PianoID))Source->StopPresentation(true);}
        if(auto* Key=Tooth.Get())Key->Destroy();Tooth.Reset();Presentation.Reset();bDone=true;
        if(bHaveHeroBefore && C.Hero && C.Hero->GetCharacterMovement()) {
            C.Hero->SetActorTransform(HeroBefore,false,nullptr,ETeleportType::TeleportPhysics);
            C.Hero->GetCharacterMovement()->SetMovementMode(ModeBefore,CustomModeBefore);
            C.Hero->GetCharacterMovement()->Velocity=VelocityBefore;bHaveHeroBefore=false;
        }
    }
    void Reset(FMCPerkPreviewContext& C) override{Cancel(C);}
    bool IsComplete(const FMCPerkPreviewContext&) const override{return bDone;}
    FString GetPhaseName(const FMCPerkPreviewContext&) const override
    {return bDone?TEXT("Фортепианный контакт завершён"):bAccepted?TEXT("Родное нажатие → возврат клавиши"):TEXT("Реальное падение на физическую коронку");}
    float GetTailSeconds() const override{return 2.f;}
private:
    void Fail(FMCPerkPreviewContext& C,FName Event){bDone=true;if(C.RecordEvent)C.RecordEvent(Event);}
    TWeakObjectPtr<AMCArenaTooth> Tooth;TWeakObjectPtr<UMCWorldPresentationComponent> Presentation;
    FVector DropPoint=FVector::ZeroVector;int32 BeforeSerial=0,ExpectedMidi=0;float BeforeHealth=0,Age=0;
    FTransform HeroBefore=FTransform::Identity;FVector VelocityBefore=FVector::ZeroVector;
    EMovementMode ModeBefore=MOVE_Walking;uint8 CustomModeBefore=0;bool bHaveHeroBefore=false;
    bool bCommitted=false,bDone=false,bAccepted=false,bPressed=false,bReturned=false;
};
}

void MCPianoActionPreviewAdapter::Register()
{
    const auto* Contract=MCVFXSourceContracts::Find(PianoID);if(!Contract)return;
    FMCPerkPreviewAdapterEntry Entry;Entry.PerkID=PianoID;Entry.Label=Contract->Label;Entry.Stages=Contract->Events;
    Entry.Kind=EMCPerkPreviewAdapterKind::Context;Entry.bSupportsProfileOverride=true;Entry.Scenarios={EMCPerkPreviewScenario::Target};
    Entry.StageLabels.Add(TEXT("Hit"),FText::FromString(TEXT("Принятое нажатие · исходная нота и добавленные слои")));
    Entry.ScenarioLabels.Add(EMCPerkPreviewScenario::Target,FText::FromString(TEXT("Настоящее приземление на клавишу")));
    // No DefaultSystems: the original key has physical animation + notes, no particle artwork.
    Entry.Create=[]()->TUniquePtr<IMCPerkPreviewAdapter>{return MakeUnique<FPianoLandingAdapter>();};
    MCPerkPreviewAdapters::Register(MoveTemp(Entry));
}
