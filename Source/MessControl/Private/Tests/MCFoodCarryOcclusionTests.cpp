#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace
{
struct FFoodCarryOcclusionWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;

    FFoodCarryOcclusionWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        if(Hero) Hero->GetCharacterMovement()->DisableMovement();
    }
    ~FFoodCarryOcclusionWorld()
    { World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false); }

    AMCFoodActor* Food(FVector Offset,FVector Scale,bool Fragment)
    {
        UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        if(!Mesh) return nullptr;
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};
        Row.Scale=Row.FragmentScale=Scale;Row.Mass=9;Row.Fragments=3;Row.SpoilSeconds=300;
        const FTransform Pose(Hero->GetActorLocation()+Offset);
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Food) return nullptr;
        FRandomStream Random(7);Food->ConfigureItem(TEXT("CarryOcclusionFixture"),Row,Random,Fragment);Food->FinishSpawning(Pose);
        Food->Settings.MaxHandDimensions=FVector(110);Food->Phase=EMCFoodPhase::Free;
        // A quiet heap is the case under test; there is no incoming impact.
        Food->Body->SetSimulatePhysics(false);Food->SetActorTickEnabled(false);
        return Food;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCReadyFoodInOversizedHeap,
    "MessControl.Food.Portions.ReadyPieceInOversizedFragmentHeap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCReadyFoodInOversizedHeap::RunTest(const FString&)
{
    for(bool LargeIsFragment:{true,false}) {
        FFoodCarryOcclusionWorld T;
        if(!TestNotNull(TEXT("The collection fixture has a player"),T.Hero)) return false;
        auto* Collection=T.Hero->FoodCollection.Get();
        auto* Ready=T.Food(FVector(150,0,0),FVector(.36),true);
        auto* Large=T.Food(FVector(70,0,0),FVector(.6,1.6,1.4),LargeIsFragment);
        if(!TestNotNull(TEXT("A ready portion spawns behind the heap"),Ready)
            || !TestNotNull(TEXT("An oversized heap item spawns"),Large)) return false;
        TestTrue(TEXT("The small portion fits the current hand limit"),Ready->FitsHandPortion());
        TestFalse(TEXT("The large item still exceeds the hand limit"),Large->FitsHandPortion());
        TestFalse(TEXT("Ignoring a heap obstacle cannot grant pickup of the oversized item"),Collection->CanCollect(Large));

        FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MCFoodCarryOcclusionTest),false,T.Hero);
        TraceParams.AddIgnoredActor(Ready);
        FHitResult Hit;
        TestTrue(TEXT("The fixture genuinely puts large food across the pickup line of sight"),
            T.World->LineTraceSingleByChannel(Hit,T.Hero->GetActorLocation(),Ready->GetActorLocation(),ECC_Visibility,TraceParams)
                && Hit.GetActor()==Large);

        Ready->PrepareHorizontalStackPose(&Ready->FoodData.Stack,0);
        float MinHeight,MaxHeight;Ready->StackVerticalBounds(MinHeight,MaxHeight);
        Ready->StackPickup.SlotHeight=-MinHeight;
        const FTransform Landing=Collection->StackPose(Ready);
        TArray<FOverlapResult> Occupants;
        T.World->OverlapMultiByChannel(Occupants,Landing.GetLocation(),Landing.GetRotation(),ECC_PhysicsBody,
            FCollisionShape::MakeBox(Ready->Body->GetScaledBoxExtent()*.9),TraceParams);
        TestTrue(TEXT("The oversized heap item also overlaps the real hand landing volume"),
            Occupants.ContainsByPredicate([Large](const FOverlapResult& Overlap){return Overlap.GetActor()==Large && Overlap.bBlockingHit;}));

        if(!LargeIsFragment) {
            TestFalse(TEXT("A large uncut whole ingredient keeps its existing solid-obstacle behavior"),Collection->CanCollect(Ready));
            continue;
        }
        TestTrue(TEXT("An oversized resting fragment must not hide a ready portion from pickup"),Collection->CanCollect(Ready));
        Collection->Toggle();
        TestTrue(TEXT("A ready portion can enter the hand despite overlap with the oversized loose fragment"),Collection->Collect(Ready));
        TestTrue(TEXT("The actual collection transaction retains the ready portion"),Collection->Contains(Ready) && Ready->StackCarrier==T.Hero);
        TestTrue(TEXT("The large fragment remains loose and still needs cutting"),!Large->StackCarrier && !Collection->CanCollect(Large));
        TestEqual(TEXT("Only the ready portion enters the hand stack"),Collection->Pieces.Num(),1);
    }
    return true;
}
#endif
