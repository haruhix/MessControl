#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCFoodDirectorHooks.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCFoodFrontDiscardTests
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AMCFoodDisposal* Exit=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UStaticMesh* Cube=nullptr;
    int32 NextBatch=74100;
    FFixture(bool bAuthored=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,bAuthored?TEXT("/Game/Gameplay/Arena/V10/SM_TongueSurface.SM_TongueSurface"):TEXT("/Engine/BasicShapes/Plane.Plane"));
        Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Mesh && Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh,Cube});
#endif
        // Translation makes an absolute world-X disposal cutoff fail this case.
        const FTransform Pose(bAuthored?FRotator(0,90.637834,0).Quaternion():FQuat::Identity,
            FVector(10000,8000,1000),bAuthored?FVector(1.875,1.5,1):FVector(30,20,1));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
        Tongue->SourceMesh=Mesh;Tongue->Settings.IdleHeight=0;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Pose);Tongue->SetActorTickEnabled(false);
        const FBox Bounds=Tongue->Surface->Bounds.GetBox();
        Exit=World->SpawnActor<AMCFoodDisposal>(FVector(Bounds.Min.X+180,Bounds.GetCenter().Y,Bounds.GetCenter().Z),FRotator::ZeroRotator);
        Exit->bBrushBin=true;Exit->bEnableLegacyDeliveryZone=false;Exit->Volume->SetBoxExtent(FVector(250,Bounds.GetExtent().Y,400));
        Exit->SetActorTickEnabled(false);
        Hero=World->SpawnActor<AMCToothCharacter>(Bounds.GetCenter()+FVector(0,0,98),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCFoodActor* Food(bool Fragment,bool Spoiled,EMCFoodKind Kind=EMCFoodKind::Food,bool RecordSafePoint=true)
    {
        FMCFoodRow Row;Row.Kind=Kind;Row.WholeMeshes={Cube};Row.FragmentMeshes={Cube};Row.Scale=Row.FragmentScale=FVector(.2);Row.SpoilSeconds=300;
        const FTransform Pose(Hero->GetActorLocation()+FVector(100,0,-60));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FRandomStream Random(17);Food->ConfigureItem(TEXT("FrontDiscardFixture"),Row,Random,Fragment);
        Food->Batch=NextBatch++;Food->bSpoiled=Spoiled;Food->FinishSpawning(Pose);
        Food->Body->SetEnableGravity(false);Food->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        if(RecordSafePoint) Food->Tick(.76f); // Record its real accessible recovery point before Q.
        return Food;
    }
    FVector FrontPoint() const
    {
        const FBox Bounds=Tongue->Surface->Bounds.GetBox();
        return FVector(Bounds.Min.X-150,Bounds.GetCenter().Y,Bounds.GetCenter().Z+80);
    }
    void ThrowTo(AMCFoodActor* Food,FVector Position,FVector Direction=FVector(-1,0,0))
    {
        Food->LaunchManualThrow(Hero,Direction,1200);Food->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
        Food->Tick(.76f); // First throttled access check while the throw is fast.
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpoiledFrontArcDiscard,"MessControl.Food.Discard.SpoiledThrowsClearFrontArc",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpoiledFrontArcDiscard::RunTest(const FString&)
{
    for(bool Authored:{false,true}) {
        MCFoodFrontDiscardTests::FFixture F(Authored);
        TArray<FVector> Outer,Inner;
        if(!TestTrue(TEXT("The disabled legacy exit still exposes the real mouth-front silhouette"),F.Exit->GetDeliveryZoneOutline(Outer,Inner))) return false;
        for(bool Fragment:{false,true}) {
            auto* Food=F.Food(Fragment,true);
            auto* Dirt=F.World->SpawnActor<AMCMouthSurface>(F.Hero->GetActorLocation()+FVector(200,200,-80),FRotator::ZeroRotator);
            Dirt->Batch=Food->Batch;Dirt->Tags.Add(TEXT("MCFoodServedDirt"));Dirt->SetOwner(Food);Dirt->Status->ApplyCoffee();
            TestEqual(TEXT("The original meal ingredient is initially outstanding"),MCMeasureFoodPipeline(F.World,Food->Batch).OutstandingActors,1);
            F.ThrowTo(Food,F.FrontPoint());
            TestTrue(TEXT("A fast Q throw of spoiled whole food or a fragment is discarded beyond the front lip"),Food->IsDisposed());
            TestEqual(TEXT("Discarded food leaves the actual batch meal pipeline"),MCMeasureFoodPipeline(F.World,Food->Batch).OutstandingActors,0);
            TestEqual(TEXT("Discarding the ingredient does not silently complete its stain"),MCCountDirectedFoodDirt(F.World,Food->Batch),1);
            Dirt->Destroy();
        }
        if(Authored) {
            // Select a curved side of the actual front arc, not the rectangular
            // extreme-X cutoff. Move along that arc segment's outward normal.
            const int32 I=FMath::Clamp(Outer.Num()/4,1,Outer.Num()-1);
            const FVector Edge=Outer[I]-Outer[I-1];
            const FVector Outward=FVector(-Edge.Y,Edge.X,0).GetSafeNormal();
            const FVector Position=(Outer[I]+Outer[I-1])*.5+Outward*180+FVector(0,0,80);
            auto* Curved=F.Food(true,true);F.ThrowTo(Curved,Position,Outward);
            TestTrue(TEXT("The curved front arc accepts an angled throw outside its actual silhouette"),Curved->IsDisposed());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFrontDiscardScope,"MessControl.Food.Discard.FrontThrowRetainsFreshRecoveryAndHeldStacks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFrontDiscardScope::RunTest(const FString&)
{
    MCFoodFrontDiscardTests::FFixture F;
    for(bool Fragment:{false,true}) {
        auto* Fresh=F.Food(Fragment,false);F.ThrowTo(Fresh,F.FrontPoint());
        TestFalse(TEXT("Fresh whole food and fragments are not accepted by the new front discard rule"),Fresh->IsDisposed());
        Fresh->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        for(int32 I=0;I<3;++I) Fresh->Tick(.76f);
        FHitResult Floor;
        TestTrue(TEXT("Fresh missed food still returns to a reachable tongue footprint"),!Fresh->IsDisposed() && F.Tongue->GameplaySpawnFootprint(Fresh->GetActorLocation(),35,Floor));
        Fresh->Destroy();
    }
    const FBox Bounds=F.Tongue->Surface->Bounds.GetBox();
    for(const FVector Position:{FVector(Bounds.GetCenter().X,Bounds.Max.Y+180,Bounds.GetCenter().Z+80),
        FVector(Bounds.Max.X+180,Bounds.GetCenter().Y,Bounds.GetCenter().Z+80)}) {
        auto* Missed=F.Food(true,true);F.ThrowTo(Missed,Position);
        TestFalse(TEXT("Sideways and backward fast misses do not enter the new early front rule"),Missed->IsDisposed());Missed->Destroy();
    }
    auto* Unthrown=F.Food(true,true);Unthrown->SetActorLocation(F.FrontPoint());Unthrown->Body->SetPhysicsLinearVelocity(FVector(-1200,0,0));Unthrown->Tick(.76f);
    TestFalse(TEXT("A spoiled fragment knocked out without Q is not an intentional front discard"),Unthrown->IsDisposed());Unthrown->Destroy();
    auto* Foreign=F.Food(false,false,EMCFoodKind::ForeignObject);F.ThrowTo(Foreign,F.FrontPoint());
    TestFalse(TEXT("The early front rule does not widen foreign-object disposal"),Foreign->IsDisposed());Foreign->Destroy();
    auto* Held=F.Food(true,true);F.Hero->FoodCollection->Toggle();
    if(!TestTrue(TEXT("A spoiled fragment can be held in the normal stack"),F.Hero->FoodCollection->Collect(Held))) return false;
    Held->MarkManualThrow(F.Hero); // Model a stale throw marker while already held.
    Held->SetActorLocation(F.FrontPoint());Held->Tick(.76f);
    TestTrue(TEXT("A held stack is never discarded by an overboard position check"),!Held->IsDisposed() && Held->StackCarrier==F.Hero && F.Hero->FoodCollection->Contains(Held));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLostCutFragmentRecovery,"MessControl.Food.Recovery.OutsideCutRetainsOutstandingFragments",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLostCutFragmentRecovery::RunTest(const FString&)
{
    for(bool Authored:{false,true}) for(int32 Anchor=0;Anchor<3;++Anchor) {
        MCFoodFrontDiscardTests::FFixture F(Authored);
        FRandomStream Random(91);FHitResult SafeFloor;
        if(!TestTrue(TEXT("The actual tongue has a reachable recovery footprint"),
            F.Tongue->RandomGameplaySpawnPoint(Random,35,70,TConstArrayView<FVector>(),SafeFloor))) return false;
        // No access tick before the cut unless this case deliberately records
        // the parent's last safe point. The other anchor is an entry landing.
        auto* Whole=F.Food(false,false,EMCFoodKind::Food,false);
        if(Anchor==1) {
            Whole->SetActorLocation(SafeFloor.ImpactPoint+FVector(0,0,30),false,nullptr,ETeleportType::TeleportPhysics);
            Whole->Tick(.76f);
        } else if(Anchor==2)
            Whole->BeginMouthEntry(FVector(100,0,50),0,SafeFloor.ImpactPoint);
        const FBox Bounds=F.Tongue->Surface->Bounds.GetBox();
        const FVector Outside(Bounds.GetCenter().X,Bounds.Max.Y+700,Bounds.GetCenter().Z+80);
        Whole->SetActorLocation(Outside,false,nullptr,ETeleportType::TeleportPhysics);
        const int32 Batch=Whole->Batch,Expected=Whole->FoodData.FragmentCount();
        if(!TestTrue(TEXT("Cutting outside the arena creates the normal delivery fragments"),
            Whole->HitFood(100000,FVector::ForwardVector,F.Hero))) return false;
        TArray<AMCFoodActor*> Pieces;
        for(TActorIterator<AMCFoodActor> It(F.World);It;++It)
            if(It->Batch==Batch && It->bFragment && !It->IsDisposed()) Pieces.Add(*It);
        if(!TestEqual(TEXT("The cut preserves the configured fragment count"),Pieces.Num(),Expected)) return false;
        for(AMCFoodActor* Piece:Pieces) {
            Piece->Body->SetEnableGravity(false);Piece->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
            FHitResult Floor;
            const float Margin=FMath::Max(35.f,float(Piece->Body->GetScaledBoxExtent().Size2D())+15);
            TestFalse(TEXT("Fragments initially remain at the cut, beyond the local recovery radius"),
                F.Tongue->GameplaySpawnFootprint(Piece->GetActorLocation(),Margin,Floor));
            for(int32 I=0;I<3;++I) Piece->Tick(.76f);
            TestTrue(TEXT("A lost cut fragment returns to an accessible real tongue footprint"),
                F.Tongue->GameplaySpawnFootprint(Piece->GetActorLocation(),Margin,Floor));
            TestFalse(TEXT("Recovery never counts as disposal"),Piece->IsDisposed());
            if(Anchor>0) TestTrue(TEXT("A parent's valid landing is retained instead of choosing an unrelated return"),
                FVector::Dist2D(Piece->GetActorLocation(),SafeFloor.ImpactPoint)<1);
        }
        TestEqual(TEXT("Recovered fragments still require delivery to complete the meal"),
            MCMeasureFoodPipeline(F.World,Batch).OutstandingActors,Expected);
    }
    return true;
}
#endif
