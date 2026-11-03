#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCVFXSourceContracts.h"
#include "MCFrostNovaVFXComponent.h"
#include "MCHazardWave.h"
#include "MCSmallMintCandy.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

namespace MCFrostPreviewTests
{
bool HasEvent(const FMCPerkPreviewRunner& Runner,FName Event)
{return Runner.GetEvents().ContainsByPredicate([&](const FMCPerkPreviewEvent& E){return E.Stage==Event;});}

class FRunNativeBurst final:public IAutomationLatentCommand
{
public:
    FRunNativeBurst(FAutomationTestBase* InTest,FName InID):Test(InTest),ID(InID){}
    bool Update() override
    {
        if(!Runner)
        {
            Started=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(!Runner->Initialize() || !Runner->StartAction(ID,EMCPerkPreviewScenario::Target,nullptr))
            {Test->AddError(ID.ToString()+TEXT(": ")+Runner->GetStatus());return true;}
            Hero=Runner->GetHero();Controller=Hero.IsValid()?Cast<APlayerController>(Hero->GetController()):nullptr;
            Identity=Hero.IsValid()?Hero->GetPlayerState<AMCPlayerState>():nullptr;
        }
        Runner->Tick(.05f);
        if(auto* W=Runner->GetWorld())
        {
            for(TActorIterator<AMCHazardWave> It(W);It;++It)
            {
                bSawNativeWave|=It->bFreezeLegs && IsValid(It->Source.Get()) && It->StartedAt>=0;
                if(ID==TEXT("World.FrostNova"))Observe(It->GetFrostNovaVFX());
            }
            for(TActorIterator<AMCSmallMintCandy> It(W);It;++It)
            {
                bSawFalling|=It->Stage==EMCSmallMintStage::Falling;
                bSawCandyWave|=It->WavesEmitted>0;
                if(ID==TEXT("World.MintCandy"))Observe(It->FrostNovaVFX.Get());
            }
        }
        const bool Finished=HasEvent(*Runner,TEXT("ActionFinished"));
        if(!Finished && Runner->IsRunning() && Runner->GetElapsed()<20.f && FPlatformTime::Seconds()-Started<60.)return false;
        Test->TestTrue(TEXT("Actual native wave was present"),bSawNativeWave);
        Test->TestTrue(TEXT("Native presenter accepted an actual Burst"),bSawAcceptedBurst);
        Test->TestTrue(TEXT("Shared provider observed the actual Wave and Burst"),HasEvent(*Runner,TEXT("Wave"))&&HasEvent(*Runner,TEXT("Burst")));
        Test->TestTrue(TEXT("Real action completed without fabricating an event"),Finished);
        Test->TestFalse(TEXT("Native start was not rejected"),HasEvent(*Runner,TEXT("NativeFrostStartRejected")));
        Test->TestFalse(TEXT("Native burst did not time out"),HasEvent(*Runner,TEXT("NativeFrostBurstMissing")));
        if(ID==TEXT("World.MintCandy"))
            Test->TestTrue(TEXT("Candy really fell and its native timer emitted the wave"),bSawFalling&&bSawCandyWave);
        if(!Finished)Test->AddError(Runner->GetStatus()+TEXT(" | ")+Runner->GetEventLog());
        Runner->Cancel();
        Test->TestTrue(TEXT("Reset preserves the borrowed tooth/controller/player identity"),Hero.IsValid()&&Controller.IsValid()&&Identity.IsValid()
            &&Controller->GetPawn()==Hero.Get()&&Hero->GetPlayerState<AMCPlayerState>()==Identity.Get());
        if(Hero.IsValid())Test->TestFalse(TEXT("Only resulting scenario leg ice is cleared"),Hero->HasFrozenLegs());
        int32 Left=0;
        for(TActorIterator<AActor> It(Runner->GetWorld());It;++It)
            if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&(It->IsA<AMCSmallMintCandy>()||It->IsA<AMCHazardWave>()||It->IsA<AMCTongue>()))++Left;
        Test->TestEqual(TEXT("Reset removes disposable native actors and ground"),Left,0);
        Runner.Reset();return true;
    }
private:
    void Observe(const UMCFrostNovaVFXComponent* VFX)
    {
        if(VFX && VFX->PresentationSourceID==ID && VFX->GetAcceptedBurstSerial()>0)
            bSawAcceptedBurst|=!VFX->GetLastAcceptedBurstPoint().ContainsNaN();
    }
    FAutomationTestBase* Test;FName ID;TUniquePtr<FMCPerkPreviewRunner> Runner;double Started=0;
    TWeakObjectPtr<AMCToothCharacter> Hero;TWeakObjectPtr<APlayerController> Controller;TWeakObjectPtr<AMCPlayerState> Identity;
    bool bSawNativeWave=false,bSawAcceptedBurst=false,bSawFalling=false,bSawCandyWave=false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFrostNativeSourceContract,"MessControl.VFX.FrostBurst.SharedSourcesAndRealDefault",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFrostNativeSourceContract::RunTest(const FString&)
{
    TestEqual(TEXT("Generic component has an explicit shared source"),GetDefault<UMCFrostNovaVFXComponent>()->PresentationSourceID,FName(TEXT("World.FrostNova")));
    TestEqual(TEXT("Candy retains a distinct originating mechanic"),GetDefault<AMCSmallMintCandy>()->FrostNovaVFX->PresentationSourceID,FName(TEXT("World.MintCandy")));
    for(const FName ID:{FName(TEXT("World.FrostNova")),FName(TEXT("World.MintCandy"))})
    {
        const auto* Contract=MCVFXSourceContracts::Find(ID);const auto* Entry=MCPerkPreviewAdapters::Find(ID);
        if(!TestNotNull(*ID.ToString(),Contract)||!TestNotNull(TEXT("Real shared preview provider"),Entry))continue;
        TestTrue(TEXT("Provider retains the whole native Wave/Burst contract"),Entry->Stages==Contract->Events);
        TestTrue(TEXT("No competing perk table row or fake factory"),Entry->Kind==EMCPerkPreviewAdapterKind::Context&&Entry->SourceRowID.IsNone()&&bool(Entry->Create));
        const auto* Default=Entry->DefaultSystems.Find(TEXT("Burst"));
        TestTrue(TEXT("Double-click default opens the exact original Niagara"),Default&&Default->ToSoftObjectPath()==GetDefault<UMCFrostNovaVFXComponent>()->BurstSystem.ToSoftObjectPath());
        const auto* EditableWave=Entry->DefaultSystems.Find(TEXT("Wave"));
        TestTrue(TEXT("Wave exposes the reviewed editable decoration; native danger footprint remains protected"),EditableWave&&EditableWave->ToSoftObjectPath()==FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/ComposerV1/Wave/NS_FrostNovaWave.NS_FrostNovaWave")));
        if(ID==TEXT("World.MintCandy"))
        {
            TestTrue(TEXT("Mint exposes its actual accepted Shatter event"),Entry->Stages.Contains(TEXT("Shatter")));
            const auto* Shatter=Entry->DefaultSystems.Find(TEXT("Shatter"));
            TestTrue(TEXT("Shatter source opens the exact original native system"),Shatter&&Shatter->ToSoftObjectPath()==GetDefault<AMCSmallMintCandy>()->ShatterSystem.ToSoftObjectPath());
            TestTrue(TEXT("Mint retains a real pickaxe shatter scenario"),Entry->Scenarios.Contains(EMCPerkPreviewScenario::NearWall));
        }
        if(Entry->Create)
        {
            auto Adapter=Entry->Create();FMCPerkPreviewContext Empty;Empty.PerkID=ID;FString Error;int32 Fabricated=0;
            Empty.RecordEvent=[&](FName){++Fabricated;};
            TestFalse(TEXT("Rejects missing/private-world context"),Adapter->Prepare(Empty,Error));
            TestFalse(TEXT("Refusal explains missing real context"),Error.IsEmpty());
            TestEqual(TEXT("Rejected setup never invents accepted events"),Fabricated,0);
        }
    }
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCFrostRealNativeBurst,"MessControl.VFX.FrostBurst.RealMechanic",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCFrostRealNativeBurst::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{for(const FString ID:{FString(TEXT("World.FrostNova")),FString(TEXT("World.MintCandy"))}){Names.Add(ID);Commands.Add(ID);}}
bool FMCFrostRealNativeBurst::RunTest(const FString& ID)
{ADD_LATENT_AUTOMATION_COMMAND(MCFrostPreviewTests::FRunNativeBurst(this,FName(*ID)));return true;}
#endif
