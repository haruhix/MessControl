#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewRunner.h"
#include "MCFoodActor.h"
#include "MCNutEnemy.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCPerkComponent.h"
#include "MCInventoryComponent.h"
#include "MCWorldPreloadSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"

namespace MCThunderPreviewFixtureTests
{
class FInspectWholeChainTargets final:public IAutomationLatentCommand
{
public:
    explicit FInspectWholeChainTargets(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        if(!Runner) {
            Started=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(!Runner->Initialize() || !Runner->StartAction(TEXT("B2_ThunderGod"),EMCPerkPreviewScenario::Target,nullptr))
            {Test->AddError(Runner->GetStatus());return true;}
        }
        Runner->Tick(.033f);
        const bool Prepared=Runner->GetEvents().ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("Prepared");});
        if(!Prepared && Runner->IsRunning() && FPlatformTime::Seconds()-Started<60.)return false;
        if(!Test->TestTrue(TEXT("Ordinary provider commits only after real resource readiness"),Prepared))
        {Test->AddError(Runner->GetStatus());return true;}
        auto* World=Runner->GetWorld();auto* Hero=Runner->GetHero();
        auto* Loading=World->GetSubsystem<UMCWorldPreloadSubsystem>();
        auto* Player=Hero?Hero->GetPlayerState<AMCPlayerState>():nullptr;
        if(!Test->TestNotNull(TEXT("Actual isolated Hero"),Hero) || !Test->TestNotNull(TEXT("Real perk owner"),Player)
            || !Test->TestNotNull(TEXT("Actual readiness owner"),Loading))return true;
        Test->TestTrue(TEXT("Initial ordinary knife contact flow is retained"),Hero->Inventory->Selected==EMCToolSlot::Knife);
        TArray<AMCFoodActor*> Foods;AMCNutEnemy* Primary=nullptr;int32 Enemies=0;
        for(TActorIterator<AMCNutEnemy> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()){Primary=*It;++Enemies;}
        for(TActorIterator<AMCFoodActor> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed())Foods.Add(*It);
        Test->TestEqual(TEXT("Original primary enemy remains; irrelevant secondary nuts are removed"),Enemies,1);
        Test->TestEqual(TEXT("Provider supplies three actual native chain-eligible foods"),Foods.Num(),3);
        const float Radius=Player->Perks->PerkParameter(TEXT("B2_ThunderGod"),TEXT("Radius"),300.f);
        const auto* Definition=Player->Perks->FindDefinition(TEXT("B2_ThunderGod"));
        if(Test->TestNotNull(TEXT("Saved enabled passive definition"),Definition)) {
            const float* Chance=Definition->Parameters.Find(TEXT("Chance"));
            Test->TestEqual(TEXT("Actual saved native chance is preserved, not forced by the preview"),
                Player->Perks->PerkParameter(TEXT("B2_ThunderGod"),TEXT("Chance"),.2f),Chance?*Chance:.2f);
        }
        bool EligibleVisibleHop=false;
        const FVector From=Primary?Primary->GetToolTargetPoint(Hero->GetActorLocation()):Hero->GetActorLocation();
        for(auto* Food:Foods) {
            Test->TestTrue(TEXT("Native target is whole edible food, not a fragment or disposed prop"),
                Food->FoodData.Kind==EMCFoodKind::Food && !Food->bFragment && !Food->IsDisposed() && Food->Phase!=EMCFoodPhase::Swallowing);
            Test->TestTrue(TEXT("Only transient owned targets are introduced"),Food->HasAnyFlags(RF_Transient));
            auto* Mesh=Food->Visual->GetStaticMesh().Get();
            Test->TestTrue(TEXT("Actual target geometry was retained before the first swing"),Mesh && Loading->IsVisualResourceRetained(Mesh));
            for(int32 I=0;I<Food->Visual->GetNumMaterials();++I)if(auto* Material=Food->Visual->GetMaterial(I))
                Test->TestTrue(TEXT("Actual target material belongs to the same readiness closure"),Loading->IsVisualResourceRetained(Material));
            const FVector Point=Food->Visual->Bounds.GetBox().GetClosestPointTo(From);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(MCThunderPreviewFixture),false,Hero);Query.AddIgnoredActor(Food);if(Primary)Query.AddIgnoredActor(Primary);
            FHitResult Hit;
            EligibleVisibleHop|=FVector::DistSquared(From,Point)<FMath::Square(Radius)
                && !World->LineTraceSingleByChannel(Hit,From,Point,ECC_Visibility,Query);
        }
        Test->TestTrue(TEXT("At least one real secondary target is in native range and line of sight"),EligibleVisibleHop);
        Test->TestFalse(TEXT("Setup does not fabricate an accepted lightning event"),
            Runner->GetEvents().ContainsByPredicate([](const FMCPerkPreviewEvent& E){return E.Stage==TEXT("Link");}));
        Runner->Cancel();Runner->Reset();int32 Remaining=0;
        for(TActorIterator<AActor> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&(It->IsA<AMCFoodActor>()||It->IsA<AMCNutEnemy>()))++Remaining;
        Test->TestEqual(TEXT("Reset removes the actual disposable food and enemy fixtures"),Remaining,0);
        Runner.Reset();return true;
    }
private:
    FAutomationTestBase* Test;TUniquePtr<FMCPerkPreviewRunner> Runner;double Started=0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCThunderPreviewWholeFoodTargets,
    "MessControl.VFX.PassivePreview.ThunderProvidesEligibleWholeFoodBeforeFirstSwing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCThunderPreviewWholeFoodTargets::RunTest(const FString&)
{ADD_LATENT_AUTOMATION_COMMAND(MCThunderPreviewFixtureTests::FInspectWholeChainTargets(this));return true;}
#endif
