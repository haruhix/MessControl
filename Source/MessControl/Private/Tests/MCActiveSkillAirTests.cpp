#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCAirSkillAction.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGameState.h"
#include "MCNutRainEvent.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCActiveSkillAirTests
{
struct FWorld
{
    AMCAirSkillAction* CDO=GetMutableDefault<AMCAirSkillAction>();
    TSoftObjectPtr<UStaticMesh> SavedProp=CDO->PropAsset;
    TSoftObjectPtr<UNiagaraSystem> SavedLoop=CDO->LoopVFX,SavedImpact=CDO->ImpactVFX,SavedWarning=CDO->WarningVFX,SavedEnd=CDO->EndVFX;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    UMCPerkComponent* Perks=nullptr;
    TStrongObjectPtr<UStaticMesh> Cube;
    bool HadCPU=false,HadDirty=false;
    FWorld()
    {
        // Mechanics fixtures intentionally avoid testing art/readiness. Exact
        // saved CDO references and Engine Cube flags are restored in teardown.
        CDO->PropAsset.Reset();CDO->LoopVFX.Reset();CDO->ImpactVFX.Reset();CDO->WarningVFX.Reset();CDO->EndVFX.Reset();
        Cube.Reset(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube.Get()});
#endif
        HadCPU=Cube->bAllowCPUAccess;HadDirty=Cube->GetOutermost()->IsDirty();Cube->bAllowCPUAccess=true;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* GS=World->SpawnActor<AMCGameState>();World->SetGameState(GS);GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;
        auto* Floor=World->SpawnActor<AActor>();auto* FloorBody=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(FloorBody);
        FloorBody->SetBoxExtent(FVector(5000,5000,20));FloorBody->SetCollisionProfileName(TEXT("BlockAll"));
        FloorBody->RegisterComponent();Floor->SetActorLocation(FVector(0,0,9980));
        auto* PC=World->SpawnActor<AMCPlayerController>();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,PC);
        auto* Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        FMCPerkDefinition Air;Air.Kind=EMCPerkKind::Consumable;Air.InitialCharges=1;Air.EffectClass=UMCAirSkillEffect::StaticClass();
        Air.Parameters={{TEXT("BagCapacity"),2},{TEXT("OverflowGrace"),.4f},{TEXT("MaxHoldSeconds"),5},
            {TEXT("IntakeX"),80},{TEXT("IntakeY"),0},{TEXT("IntakeZ"),0},{TEXT("SuctionRange"),350},
            {TEXT("CaptureRadius"),70},{TEXT("BlowSpeed"),1000}};
        Table->AddRow(TEXT("AirLast"),Air);Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        PC->SetPlayerState(Player);Player->SetOwner(PC);GS->AddPlayerState(Player);Perks=Player->Perks;
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10085),FRotator::ZeroRotator);
        PC->Possess(Hero);PC->bShowMouseCursor=false;Hero->GetCharacterMovement()->DisableMovement();Hero->Status->Initialize(100);
    }
    ~FWorld()
    {
        TStrongObjectPtr<UPackage> OwnedPackage(World->GetOutermost());
        const FString OwnedPackageName=OwnedPackage->GetName();
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        // This fixture owns only its freshly-created /Temp/ world package.
        if(OwnedPackageName.StartsWith(TEXT("/Temp/")) && OwnedPackage->GetName()==OwnedPackageName)
            OwnedPackage->SetDirtyFlag(false);
        Cube->bAllowCPUAccess=HadCPU;Cube->GetOutermost()->SetDirtyFlag(HadDirty);
        CDO->PropAsset=SavedProp;CDO->LoopVFX=SavedLoop;CDO->ImpactVFX=SavedImpact;CDO->WarningVFX=SavedWarning;CDO->EndVFX=SavedEnd;
    }
    void Step(float Seconds)
    {for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=1.f/60) {++GFrameCounter;World->Tick(LEVELTICK_All,FMath::Min(1.f/60,Seconds-At));}}
    AMCFoodActor* Food(FVector Point,bool bFragment=true)
    {
        FMCFoodRow Row;Row.WholeMeshes={Cube.Get()};Row.FragmentMeshes={Cube.Get()};Row.Scale=Row.FragmentScale=FVector(.15);
        Row.Health=75;Row.Fragments=3;Row.SpoilSeconds=300;
        const FTransform Pose(Point);
        auto* F=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream R(7);F->ConfigureItem(TEXT("AirIdentityFixture"),Row,R,bFragment);F->Batch=733;F->FinishSpawning(Pose);return F;
    }
    AActor* Container()
    {auto* A=World->SpawnActor<AActor>();A->SetInstigator(Hero);return A;}
    FMCActiveSkillInputHandle Handle() const
    {FMCActiveSkillInputHandle H;H.SlotIndex=0;H.SlotInstanceID=Perks->ActiveSkillSlots[0].InstanceID;H.PressSequence=1;return H;}
    AMCAirSkillAction* Begin(FMCActiveSkillInputHandle& HandleOut)
    {
        if(!Perks->TryAcquirePerk(TEXT("AirLast"))) return nullptr;
        HandleOut=Handle();if(!Perks->BeginSkillSlot(HandleOut)) return nullptr;
        return Cast<AMCAirSkillAction>(Perks->FindRunningAction(HandleOut.SlotInstanceID));
    }
    static FVector Intake(const AMCAirSkillAction* A)
    {return A->PropMesh->GetComponentTransform().TransformPosition(FVector(80,0,0));}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAirExclusiveClaim,"MessControl.ActiveSkills.Air.ExclusiveIdentityAndStaleRelease",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAirExclusiveClaim::RunTest(const FString&)
{
    using namespace MCActiveSkillAirTests;
    FWorld T;T.Step(.02f);auto* F=T.Food(T.Hero->GetActorLocation()+FVector(120,0,0));
    // Positive elapsed absolute deadline, resolved by the native food tick;
    // never call the private Spoil implementation or manufacture its result.
    F->SpoilAt=T.World->GetTimeSeconds()-.0001;F->Tick(0.f);
    if(!TestTrue(TEXT("Native freshness expiry actually spoils the initial piece"),F->bSpoiled)) return false;
    const auto* Identity=F;const double Spoil=F->SpoilAt;const float Health=F->Health;
    auto* First=T.Container();auto* Second=T.Container();const FGuid Claim=FGuid::NewGuid(),NextClaim=FGuid::NewGuid();
    TestTrue(TEXT("An ordinary food fragment accepts one native bag claim"),F->TryEnterActiveContainer(First,T.Hero,Claim));
    TestTrue(TEXT("Contained food hides and stops collision/simulation"),F->IsHidden() && F->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !F->Body->IsSimulatingPhysics());
    TestEqual(TEXT("The actual live carrier is discoverable for freshness perks"),F->GetActiveContainerCarrier(),T.Hero);
    TestFalse(TEXT("A second bag cannot steal the retained actor"),F->TryEnterActiveContainer(Second,T.Hero,NextClaim));
    TestFalse(TEXT("The ordinary stack cannot acquire a contained actor"),T.Hero->FoodCollection->CanCollect(F));
    TestFalse(TEXT("A contained actor rejects blast mutations"),F->ReceiveActiveSkillImpact(100,FVector::ForwardVector,T.Player));
    TestFalse(TEXT("Wrong container cannot release another bag's food"),F->ExitActiveContainer(Second,Claim,F->GetActorLocation(),FVector::ForwardVector,1000,true));
    TestFalse(TEXT("Wrong claim cannot release the right container"),F->ExitActiveContainer(First,NextClaim,F->GetActorLocation(),FVector::ForwardVector,1000,true));
    const FVector Release=T.Hero->GetActorLocation()+FVector(200,0,0);
    TestTrue(TEXT("The owning claim restores the same physical actor"),F->ExitActiveContainer(First,Claim,Release,FVector::ForwardVector,1000,true));
    TestTrue(TEXT("Bag exit keeps exact actor identity and real physical throw"),F==Identity && F->Body->IsSimulatingPhysics() && F->WasManuallyThrown() && F->Body->GetPhysicsLinearVelocity().X>900);
    TestEqual(TEXT("Item identity survives retention"),F->ItemName,FName(TEXT("AirIdentityFixture")));
    TestEqual(TEXT("Original batch survives retention"),F->Batch,733);
    TestEqual(TEXT("Absolute freshness clock is not reset"),F->SpoilAt,Spoil);
    TestTrue(TEXT("Already spoiled food is never rejuvenated"),F->bSpoiled);
    TestEqual(TEXT("Retention/blow does not change piece health"),F->Health,Health);
    TestEqual(TEXT("Native handling credit belongs to original player"),F->GetLastHandledBy(),T.Player);
    TestFalse(TEXT("Duplicate release cannot launch the actor twice"),F->ExitActiveContainer(First,Claim,Release,FVector::RightVector,1000,true));
    TestTrue(TEXT("A new owner may claim the released piece"),F->TryEnterActiveContainer(Second,T.Hero,NextClaim));
    TestFalse(TEXT("Old token cannot clear the new bag's claim"),F->ExitActiveContainer(First,Claim,Release,FVector::ForwardVector,1000,true));
    TestEqual(TEXT("Stale release leaves current ownership intact"),F->ActiveContainer.Get(),Second);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAirFilterAndRecovery,"MessControl.ActiveSkills.Air.StrictFoodFilterAndDestroyedContainerRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAirFilterAndRecovery::RunTest(const FString&)
{
    using namespace MCActiveSkillAirTests;
    FWorld T;auto* Bag=T.Container();const auto Claim=FGuid::NewGuid();const auto Point=T.Hero->GetActorLocation()+FVector(160,0,0);
    auto* Whole=T.Food(Point,false);auto* Nut=T.Food(Point+FVector(0,70,0));
    auto* Encounter=T.World->SpawnActor<AMCNutRainEvent>();Encounter->SetActorTickEnabled(false);Encounter->Settings.bBossEncounter=true;Nut->SetOwner(Encounter);
    TestTrue(TEXT("The excluded nut fixture is actual boss encounter ammunition"),Nut->IsNutEncounterFragment());
    TestFalse(TEXT("Air bag never captures whole ingredients"),Whole->TryEnterActiveContainer(Bag,T.Hero,Claim));
    TestFalse(TEXT("Air bag never captures boss E/Q ammunition"),Nut->TryEnterActiveContainer(Bag,T.Hero,Claim));
    auto* Piece=T.Food(Point+FVector(0,-70,0));const TWeakObjectPtr<AMCFoodActor> Lifetime=Piece;
    const double Spoil=Piece->SpoilAt;
    if(!TestTrue(TEXT("Loose ordinary piece enters the test container"),Piece->TryEnterActiveContainer(Bag,T.Hero,Claim))) return false;
    Bag->Destroy();T.Step(.1f);
    if(!TestTrue(TEXT("External destruction preserves the retained actor"),Lifetime.IsValid())) return false;
    TestTrue(TEXT("Food's native recovery releases destroyed-container ownership"),!Piece->IsInActiveContainer() && !Piece->ActiveContainer && !Piece->GetActiveContainerCarrier());
    TestTrue(TEXT("Recovered actor is visible and physical"),!Piece->IsHidden() && Piece->Body->IsSimulatingPhysics() && Piece->Body->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    TestEqual(TEXT("Destruction recovery retains freshness deadline"),Piece->SpoilAt,Spoil);
    TestEqual(TEXT("Destruction recovery retains original batch"),Piece->Batch,733);
    TestEqual(TEXT("Destruction recovery retains credit"),Piece->GetLastHandledBy(),T.Player);
    TestFalse(TEXT("Destroyed ownership cannot release a recovered piece again"),Piece->ExitActiveContainer(nullptr,Claim,Point,FVector::UpVector,500,false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCAirNativeCycle,"MessControl.ActiveSkills.Air.LastChargeNativeSuctionBlowAndOverflow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCAirNativeCycle::RunTest(const FString&)
{
    using namespace MCActiveSkillAirTests;
    for(bool bOverflow:{false,true}) {
        FWorld T;FMCActiveSkillInputHandle Press;auto* Action=T.Begin(Press);
        if(!TestNotNull(TEXT("Native last-charge Air cycle commits"),Action)) return false;
        TestTrue(TEXT("Empty slot retains its accepted suction cycle"),T.Perks->ActiveSkillSlots[0].PerkID.IsNone() && Action->bSucking);
        const TWeakObjectPtr<AMCAirSkillAction> ActionLifetime=Action;
        auto* First=T.Food(FWorld::Intake(Action)+FVector(35,-10,0));const double Spoil=First->SpoilAt;
        AMCFoodActor* Second=bOverflow?T.Food(FWorld::Intake(Action)+FVector(35,10,0)):nullptr;
        T.Step(.1f);
        if(!TestTrue(TEXT("Actual suction scan captures an eligible physical piece"),First->ActiveContainer==Action)) return false;
        TestEqual(TEXT("Replicated stored count matches real retained actors"),Action->StoredCount,bOverflow?2:1);
        if(bOverflow) {
            TestTrue(TEXT("Full retained bag raises an actual overflow warning"),Action->bOverflowWarning);
            T.Step(.5f);
            TestTrue(TEXT("Overflow retains its brief held rupture recovery"),ActionLifetime.IsValid() && ActionLifetime->bRuptured);
            TestTrue(TEXT("Overflow releases every retained actor"),!First->IsInActiveContainer() && Second && !Second->IsInActiveContainer());
            TestFalse(TEXT("Overflow spill is not mislabeled as a deliberate throw"),First->WasManuallyThrown());
            T.Step(.4f);
            TestFalse(TEXT("Rupture recovery terminates and releases the action"),ActionLifetime.IsValid());
        } else {
            TestTrue(TEXT("Completed release reaches the original depleted-slot press"),T.Perks->ReleaseSkillInput(Press));
            TestTrue(TEXT("Native blow keeps the held prop for its brief release recovery"),ActionLifetime.IsValid());
            TestTrue(TEXT("Native blow restores simulation and real forward velocity"),!First->IsInActiveContainer() && First->Body->IsSimulatingPhysics() && First->WasManuallyThrown() && First->Body->GetPhysicsLinearVelocity().X>900);
            const FVector Velocity=First->Body->GetPhysicsLinearVelocity();
            TestFalse(TEXT("A duplicate input cannot relaunch retained food"),T.Perks->ReleaseSkillInput(Press));
            TestTrue(TEXT("Duplicate release leaves physical velocity unchanged"),First->Body->GetPhysicsLinearVelocity().Equals(Velocity,.01));
            T.Step(.4f);
            TestFalse(TEXT("Blow recovery finishes without retaining the action"),ActionLifetime.IsValid());
        }
        TestEqual(TEXT("Suction and release preserve absolute freshness"),First->SpoilAt,Spoil);
        TestEqual(TEXT("Suction and release preserve item identity"),First->ItemName,FName(TEXT("AirIdentityFixture")));
        TestEqual(TEXT("Suction and release preserve original source credit"),First->GetLastHandledBy(),T.Player);
        TestEqual(TEXT("Air rupture/blow adds no self HP damage"),T.Hero->Status->State.Health,100.f);
    }
    return true;
}
#endif
