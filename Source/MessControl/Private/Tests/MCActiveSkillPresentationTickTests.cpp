#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCActiveSkillTestTypes.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"

namespace
{
bool HasActorEdge(const FTickFunction& Tick,const AActor* Actor)
{
    return Actor && Tick.GetPrerequisites().ContainsByPredicate(
        [Actor](const FTickPrerequisite& Edge) {return Edge.Get()==&Actor->PrimaryActorTick;});
}
struct FPresentationTickWorld
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* OwnedPackage=W->GetOutermost();
    const FName OwnedPackageName=OwnedPackage->GetFName();
    AMCPlayerController* PC=nullptr;
    AMCPlayerState* PS=nullptr;
    AMCToothCharacter* H=nullptr;
    UMCPerkComponent* P=nullptr;
    uint32 Sequence=0;
    bool bWorldEnded=false;
    FPresentationTickWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        auto* GS=W->SpawnActor<AMCGameState>();W->SetGameState(GS);GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;
        auto* Table=NewObject<UDataTable>(W);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Consumable;Row.InitialCharges=1;Row.EffectClass=UMCActiveSkillProtocolProbeEffect::StaticClass();
        Table->AddRow(TEXT("TickLease"),Row);
        PC=W->SpawnActor<AMCPlayerController>();
        PS=W->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,PC);
        PS->Perks->PerkTable=Table;PS->FinishSpawning(FTransform::Identity);
        PC->SetPlayerState(PS);PS->SetOwner(PC);GS->AddPlayerState(PS);P=PS->Perks;
        H=W->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        PC->Possess(H);H->GetCharacterMovement()->DisableMovement();H->Status->Initialize(100);
    }
    AMCActiveSkillProtocolProbe* Start()
    {
        if(!P->TryAcquirePerk(TEXT("TickLease"))) return nullptr;
        FMCActiveSkillInputHandle Input;Input.SlotIndex=0;Input.SlotInstanceID=P->ActiveSkillSlots[0].InstanceID;Input.PressSequence=++Sequence;
        return P->BeginSkillSlot(Input)?Cast<AMCActiveSkillProtocolProbe>(P->FindRunningAction(Input)):nullptr;
    }
    ~FPresentationTickWorld()
    {
        if(!bWorldEnded) W->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(W);W->DestroyWorld(false);
        if(IsValid(OwnedPackage) && OwnedPackage->GetFName()==OwnedPackageName && OwnedPackage->GetName().StartsWith(TEXT("/Temp/")))
            OwnedPackage->SetDirtyFlag(false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActivePresentationTickLeaseTest,"MessControl.Skills.Presentation.ScopedTickLease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActivePresentationTickLeaseTest::RunTest(const FString&)
{
    FPresentationTickWorld W;auto* Mesh=W.H->GetMesh();auto* Movement=W.H->GetCharacterMovement();
    const ETickingGroup MeshGroup=Mesh->PrimaryComponentTick.TickGroup;
    const ETickingGroup MovementGroup=Movement->PrimaryComponentTick.TickGroup;
    auto* A=W.Start();if(!TestNotNull(TEXT("First committed presentation"),A)) return false;
    TestTrue(TEXT("Held pose mesh waits for its actual action"),HasActorEdge(Mesh->PrimaryComponentTick,A));
    TestEqual(TEXT("No permanent mesh tick-group change"),ETickingGroup(Mesh->PrimaryComponentTick.TickGroup),MeshGroup);
    TestEqual(TEXT("Movement tick group unchanged"),ETickingGroup(Movement->PrimaryComponentTick.TickGroup),MovementGroup);
    TestFalse(TEXT("Movement never waits for presentation"),HasActorEdge(Movement->PrimaryComponentTick,A));
    TestFalse(TEXT("Character input/gameplay never waits for presentation"),HasActorEdge(W.H->PrimaryActorTick,A));
    A->ReleasePresentation();
    TestFalse(TEXT("Release removes exact actor edge"),HasActorEdge(Mesh->PrimaryComponentTick,A));
    auto* B=W.Start();if(!TestNotNull(TEXT("Replacement committed presentation"),B)) return false;
    TestTrue(TEXT("New owner installs its own edge"),HasActorEdge(Mesh->PrimaryComponentTick,B));
    A->ReleasePresentation();A->Finish();
    TestTrue(TEXT("Old release/destruction preserves replacement edge"),HasActorEdge(Mesh->PrimaryComponentTick,B));
    B->ReleasePresentation();
    TestFalse(TEXT("Replacement releases its own edge"),HasActorEdge(Mesh->PrimaryComponentTick,B));
    Mesh->AddTickPrerequisiteActor(B);B->ClaimPresentation();B->ReleasePresentation();
    TestTrue(TEXT("Borrowed pre-existing edge is not removed by lease cleanup"),HasActorEdge(Mesh->PrimaryComponentTick,B));
    Mesh->RemoveTickPrerequisiteActor(B);
    AddExpectedError(TEXT("skipped presentation tick prerequisite: action already depends on mesh"),EAutomationExpectedErrorFlags::Contains,1);
    B->AddTickPrerequisiteComponent(Mesh);B->ClaimPresentation();
    TestFalse(TEXT("Existing action-to-mesh edge cannot be made cyclic"),HasActorEdge(Mesh->PrimaryComponentTick,B));
    B->RemoveTickPrerequisiteComponent(Mesh);B->ReleasePresentation();B->Finish();
    auto* C=W.Start();if(!TestNotNull(TEXT("Pawn-change presentation"),C)) return false;
    W.PC->UnPossess();C->Tick(.016f);
    TestFalse(TEXT("Pawn change removes old mesh prerequisite"),HasActorEdge(Mesh->PrimaryComponentTick,C));
    TestTrue(TEXT("Pawn-change cancels committed action"),C->IsFinished());
    W.PC->Possess(W.H);
    auto* D=W.Start();if(!TestNotNull(TEXT("World-end presentation"),D)) return false;
    TestTrue(TEXT("World-end fixture starts with active edge"),HasActorEdge(Mesh->PrimaryComponentTick,D));
    W.W->EndPlay(EEndPlayReason::Quit);W.bWorldEnded=true;
    TestFalse(TEXT("World end releases its mesh edge"),HasActorEdge(Mesh->PrimaryComponentTick,D));
    TestTrue(TEXT("World end finishes presentation"),D->IsFinished());
    return true;
}
#endif
