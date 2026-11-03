#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCNutVFXCatalog.h"
#include "MCNutBoss.h"
#include "MCNutKamikaze.h"
#include "MCNutLandingShadow.h"
#include "MCVFXLabBossStation.h"
#include "MCFoodActor.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"

namespace MCNutAuxPreviewTests
{
const FName Sources[]={TEXT("Enemy.NutKamikaze.Fuse"),TEXT("Enemy.NutKamikaze.Blast"),
    TEXT("Event.NutRain.Entry"),TEXT("Boss.NutKnight.CounterShove"),TEXT("Boss.NutKnight.Transform")};

bool HasEvent(const FMCPerkPreviewRunner& Runner,FName Name)
{return Runner.GetEvents().ContainsByPredicate([&](const FMCPerkPreviewEvent& E){return E.Stage==Name;});}

/** Latent editor frames service the shared Runner clock; never advances a
 * second world clock or mutates GFrameCounter to bypass its ownership gate. */
class FRunRealCase final:public IAutomationLatentCommand
{
public:
    FRunRealCase(FAutomationTestBase* InTest,FName InSource):Test(InTest),Source(InSource){}
    bool Update()override
    {
        if(!Runner) {
            StartedAt=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(!Runner->Initialize() || !Runner->StartAction(Source,EMCPerkPreviewScenario::Target,nullptr)) {
                Test->AddError(Source.ToString()+TEXT(": ")+Runner->GetStatus());return true;
            }
            Hero=Runner->GetHero();Controller=Hero.IsValid()?Cast<APlayerController>(Hero->GetController()):nullptr;
            Identity=Hero.IsValid()?Hero->GetPlayerState<AMCPlayerState>():nullptr;
        }
        Runner->Tick(.05f);
        if(auto* World=Runner->GetWorld()) {
            for(TActorIterator<AMCNutKamikaze> It(World);It;++It) {
                bSawArmed|=It->bArmed;bSawExploded|=It->bExploded;
                bSawRealModel|=It->SummonModel && It->SummonModel->GetSkeletalMeshAsset();
            }
            for(TActorIterator<AMCFoodActor> It(World);It;++It)if(It->ActorHasTag(TEXT("MCNutRain"))) {
                bSawEntry|=It->IsMouthEntryActive();
                bSawPhysicalEntry|=It->IsMouthEntryActive()&&It->Body->IsSimulatingPhysics();
                LastEntryPosition=It->GetActorLocation();LastEntryPhase=int32(It->Phase);
            }
            if(Source==TEXT("Event.NutRain.Entry"))for(TActorIterator<AMCTongue> It(World);It;++It) {
                bSawPhysicalTongue|=It->Surface&&It->Surface->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics;
                bSawPrivateCPUFloor|=It->SourceMesh&&It->SourceMesh->HasAnyFlags(RF_Transient)&&It->SourceMesh->bAllowCPUAccess;
            }
            for(TActorIterator<AMCNutLandingShadow> It(World);It;++It)bSawImpact|=It->bImpacted;
            for(TActorIterator<AMCNutBoss> It(World);It;++It) {
                MaximumPressure=FMath::Max(MaximumPressure,It->GetTankPressureHits());
                bSawCounter|=It->IsCounterShoving();bSawTransform|=It->State==EMCNutBossState::Transforming;
                bSawMage|=It->BossRole==EMCNutBossRole::Mage;
                bSawPreparedMage|=It->BossRole==EMCNutBossRole::Tank && It->MageModel && It->MageModel->GetSkeletalMeshAsset();
            }
        }
        const bool Finished=HasEvent(*Runner,TEXT("ActionFinished"));
        if(!Finished && Runner->IsRunning() && Runner->GetElapsed()<22.f && FPlatformTime::Seconds()-StartedAt<60.)return false;
        Test->TestTrue(*(Source.ToString()+TEXT(" completed its observed native action")),Finished);
        if(!Finished)Test->AddError(Runner->GetStatus()+TEXT(" | ")+Runner->GetEventLog());
        Test->TestFalse(TEXT("No fabricated native-action rejection"),HasEvent(*Runner,TEXT("NutNativeActionRejected")));
        Test->TestFalse(TEXT("No native action timeout"),HasEvent(*Runner,TEXT("NutNativeActionTimeout")));
        if(Source.ToString().StartsWith(TEXT("Enemy.NutKamikaze."))) {
            Test->TestTrue(TEXT("The actual summoned model, armed fuse and real explosion were observed"),bSawRealModel&&bSawArmed&&bSawExploded);
            if(Source==TEXT("Enemy.NutKamikaze.Fuse"))Test->TestTrue(TEXT("Actual fuse and protected danger footprint are reported"),HasEvent(*Runner,TEXT("Fuse"))&&HasEvent(*Runner,TEXT("Danger")));
            else Test->TestTrue(TEXT("Native blast impact is reported"),HasEvent(*Runner,TEXT("Impact")));
        } else if(Source==TEXT("Event.NutRain.Entry")) {
            Test->TestTrue(TEXT("The real mouth-entry state was observed"),bSawEntry);
            Test->TestTrue(TEXT("The real entry food simulates physical motion"),bSawPhysicalEntry);
            Test->TestTrue(TEXT("The private CPU-readable tongue participates in actual physics"),bSawPhysicalTongue&&bSawPrivateCPUFloor);
            Test->TestTrue(TEXT("The actual tongue collision resolved the real shadow impact"),bSawImpact);
            Test->TestTrue(TEXT("Approach and impact are observed from the real shadow"),HasEvent(*Runner,TEXT("Approach"))&&HasEvent(*Runner,TEXT("Impact")));
            if(!bSawImpact)Test->AddError(FString::Printf(TEXT("Entry diagnostic: active=%d simulated=%d tonguePhysics=%d privateCPU=%d phase=%d last=%s | %s"),
                bSawEntry,bSawPhysicalEntry,bSawPhysicalTongue,bSawPrivateCPUFloor,LastEntryPhase,*LastEntryPosition.ToString(),*Runner->GetEventLog()));
        } else if(Source==TEXT("Boss.NutKnight.CounterShove")) {
            Test->TestTrue(TEXT("Actual contacts accumulated pressure before the native counter"),MaximumPressure>0&&bSawCounter);
            Test->TestTrue(TEXT("Native counter tell and impact were observed"),HasEvent(*Runner,TEXT("Tell"))&&HasEvent(*Runner,TEXT("Impact")));
        } else {
            Test->TestTrue(TEXT("Native transforming state leads to the real Mage role"),bSawTransform&&bSawMage);
            Test->TestTrue(TEXT("Actual future Mage component was populated before its native role switch"),bSawPreparedMage);
            Test->TestTrue(TEXT("Actual head split and accepted original dust are observed"),HasEvent(*Runner,TEXT("HeadSplit"))&&HasEvent(*Runner,TEXT("HeadDust")));
        }
        Runner->Cancel();
        Test->TestTrue(TEXT("Cancel preserves the borrowed tooth and its identity"),Hero.IsValid()&&Controller.IsValid()&&Identity.IsValid()
            &&Controller->GetPawn()==Hero.Get()&&Hero->GetPlayerState<AMCPlayerState>()==Identity.Get());
        int32 Remaining=0;
        for(TActorIterator<AMCVFXLabBossStation> It(Runner->GetWorld());It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed())++Remaining;
        Test->TestEqual(TEXT("Cancel releases the disposable Lab, without rebuilding another fixture"),Remaining,0);
        Runner.Reset();return true;
    }
private:
    FAutomationTestBase* Test;FName Source;TUniquePtr<FMCPerkPreviewRunner> Runner;
    TWeakObjectPtr<AMCToothCharacter> Hero;TWeakObjectPtr<APlayerController> Controller;TWeakObjectPtr<AMCPlayerState> Identity;
    double StartedAt=0;int32 MaximumPressure=0;
    bool bSawArmed=false,bSawExploded=false,bSawRealModel=false,bSawEntry=false,bSawImpact=false;
    bool bSawPhysicalEntry=false,bSawPhysicalTongue=false,bSawPrivateCPUFloor=false;
    FVector LastEntryPosition=FVector::ZeroVector;int32 LastEntryPhase=INDEX_NONE;
    bool bSawCounter=false,bSawTransform=false,bSawMage=false,bSawPreparedMage=false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutAuxPreviewContracts,"MessControl.VFX.NutAuxPreview.FiveRealProvidersAndStableEnum",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutAuxPreviewContracts::RunTest(const FString&)
{
    TestEqual(TEXT("Serialized old laboratory enum values stay intact"),uint8(EMCVFXLabBossKind::Phase3AreaAttack),uint8(23));
    for(FName Source:MCNutAuxPreviewTests::Sources) {
        const auto* Contract=MCNutVFX::Find(Source);const auto* Entry=MCPerkPreviewAdapters::Find(Source);
        if(!TestNotNull(*Source.ToString(),Contract)||!TestNotNull(TEXT("Registered contextual provider"),Entry))continue;
        TestTrue(TEXT("Provider has a real factory and no dummy perk row"),bool(Entry->Create)&&Entry->Kind==EMCPerkPreviewAdapterKind::Context&&Entry->SourceRowID.IsNone());
        TestTrue(TEXT("Provider exposes the actual native event contract"),Entry->Stages==Contract->Events);
        TestTrue(TEXT("Source has a named real Lab scenario"),!Contract->LabScenario.IsNone()&&Contract->LabScenario!=TEXT("None"));
        if(Entry->Create) {
            auto Adapter=Entry->Create();FMCPerkPreviewContext Empty;Empty.PerkID=Source;FString Error;int32 InventedEvents=0;
            Empty.RecordEvent=[&](FName){++InventedEvents;};
            TestFalse(TEXT("No live-game or missing-world fallback is accepted"),Adapter->Prepare(Empty,Error));
            TestFalse(TEXT("Refusal explains the actual missing context"),Error.IsEmpty());
            TestEqual(TEXT("Rejected setup emits no fabricated gameplay events"),InventedEvents,0);
        }
    }
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCNutAuxRealPreview,"MessControl.VFX.NutAuxPreview.RealMechanic",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCNutAuxRealPreview::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    for(FName Source:MCNutAuxPreviewTests::Sources){Names.Add(Source.ToString());Commands.Add(Source.ToString());}
}
bool FMCNutAuxRealPreview::RunTest(const FString& Command)
{
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Run isolated native previews outside PIE."));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(MCNutAuxPreviewTests::FRunRealCase(this,FName(*Command)));
    return true;
}
#endif
