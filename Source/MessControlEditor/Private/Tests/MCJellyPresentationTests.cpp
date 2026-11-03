#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCJellyPresentationComponent.h"
#include "MCCataclysmEvent.h"
#include "MCCataclysmJelly.h"
#include "MCCataclysmRaft.h"
#include "MCCataclysmDebris.h"
#include "MCMouthSurface.h"
#include "MCTongue.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCPerkVFXProfile.h"
#include "MCPerkVFXCompositionPlayer.h"
#include "MCVFXSourceContracts.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCJellySourceContract,"MessControl.VFX.Jelly.NativeContract",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCJellySourceContract::RunTest(const FString&)
{
    const auto* Contract=MCVFXSourceContracts::Find(UMCJellyPresentationComponent::SourceID());
    const auto* Entry=MCPerkPreviewAdapters::Find(UMCJellyPresentationComponent::SourceID());
    if(!TestNotNull(TEXT("Existing shared source is reused"),Contract)
        || !TestNotNull(TEXT("One real context provider"),Entry))return false;
    TestEqual(TEXT("Real existing event schema is retained"),Contract->Events.Num(),2);
    TestTrue(TEXT("Provider has exact Trail/Impact stages"),Entry->Stages==Contract->Events
        && Entry->Stages.Contains(TEXT("Trail"))&&Entry->Stages.Contains(TEXT("Impact")));
    TestTrue(TEXT("Context uses a real factory without a fake perk row"),Entry->Kind==EMCPerkPreviewAdapterKind::Context&&Entry->SourceRowID.IsNone()&&bool(Entry->Create));
    TestTrue(TEXT("Original procedural art is not advertised as a fabricated old Niagara"),Entry->DefaultSystems.IsEmpty());
    if(Entry->Create)
    {
        auto Adapter=Entry->Create();FMCPerkPreviewContext Empty;Empty.PerkID=UMCJellyPresentationComponent::SourceID();
        int32 Events=0;Empty.RecordEvent=[&](FName){++Events;};FString Error;
        TestFalse(TEXT("No user's level / missing context is accepted"),Adapter->Prepare(Empty,Error));
        TestFalse(TEXT("Rejected real context has a reason"),Error.IsEmpty());
        TestEqual(TEXT("No fabricated contact/rupture event"),Events,0);
    }
    FMCPerkVFXGameplayValues Values;UMCJellyPresentationComponent::SetNativeGeometry(Values,90,47.5f,350,1.25f,2);
    FString Error;TestTrue(TEXT("Actual geometry payload is valid"),Values.NativeInputs.Validate(Error));
    TestEqual(TEXT("Only accepted scalar geometry plus tier, no visual trajectories"),Values.NativeInputs.Entries.Num(),5);
    const auto* Radius=Values.NativeInputs.Find(TEXT("User.Native.RuptureRadius"));
    TestTrue(TEXT("Rupture radius is an exact typed native value"),Radius&&Radius->Type==EMCVFXNativeInputType::Float&&Radius->FloatValue==350.f);
    TestTrue(TEXT("Native body animation still has its own component"),GetDefault<AMCCataclysmJelly>()->JellyMesh!=nullptr);
    TestTrue(TEXT("Dormant raft branch remains disabled"),!AMCCataclysmEvent::IsVariantEnabled(EMCCataclysmVariant::Rafts));
    return true;
}

namespace MCJellyTests
{
bool Has(const FMCPerkPreviewRunner& Runner,FName Event)
{return Runner.GetEvents().ContainsByPredicate([&](const FMCPerkPreviewEvent& Row){return Row.Stage==Event;});}

class FRunJelly final:public IAutomationLatentCommand
{
public:
    FRunJelly(FAutomationTestBase* InTest,bool bInDisabled):Test(InTest),bDisabled(bInDisabled){}
    bool Update() override
    {
        if(!Runner)
        {
            Started=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(bDisabled)
            {
                Override.Reset(NewObject<UMCPerkVFXProfile>(GetTransientPackage()));
                Override->Slots.FindOrAdd(TEXT("Trail")).Mode=EMCPerkVFXSlotMode::Disabled;
                Override->Slots.FindOrAdd(TEXT("Impact")).Mode=EMCPerkVFXSlotMode::Disabled;
            }
            if(!Runner->Initialize()||!Runner->StartAction(UMCJellyPresentationComponent::SourceID(),EMCPerkPreviewScenario::Target,Override.Get()))
            {Test->AddError(Runner->GetStatus());return true;}
        }
        Runner->Tick(.05f);
        for(TActorIterator<AMCCataclysmJelly> It(Runner->GetWorld());It;++It)
            if(It->RuptureCount==1)
            {
                bRupture=true;Children=FMath::Max(Children,It->ChildrenSpawned);
                if(It->JellyPresentation)bCorrectNativeGate|=It->JellyPresentation->KeepNativeDroplets()!=bDisabled;
            }
        for(TActorIterator<AMCCataclysmEvent> It(Runner->GetWorld());It;++It)
            PaintBursts=FMath::Max(PaintBursts,It->DeathPaintBursts);
        if(Runner->IsRunning()&&!Has(*Runner,TEXT("ActionFinished"))&&Runner->GetElapsed()<50.f&&FPlatformTime::Seconds()-Started<90.)return false;
        Test->TestTrue(TEXT("Real StartWave(false), not Rafts, was observed"),Has(*Runner,TEXT("NativeJellyFirstWave")));
        Test->TestTrue(TEXT("Real contact and rupture events were observed"),Has(*Runner,TEXT("Trail"))&&Has(*Runner,TEXT("Impact")));
        Test->TestTrue(TEXT("Normal lethal tool damage initiated native rupture"),Has(*Runner,TEXT("NativeJellyLethalToolDamage"))&&bRupture);
        Test->TestEqual(TEXT("Actual large jelly still creates its three native children"),Children,3);
        Test->TestTrue(TEXT("Actual brush-cleanable death paint still commits"),PaintBursts>0);
        Test->TestTrue(TEXT("Default/Disabled gates only native decorative droplets"),bCorrectNativeGate);
        Test->TestFalse(TEXT("Native scenario did not time out"),Has(*Runner,TEXT("NativeJellyRuptureMissing")));
        Test->TestTrue(TEXT("Actual native scenario finished"),Has(*Runner,TEXT("ActionFinished")));
        Runner->Cancel();int32 Left=0;
        for(TActorIterator<AActor> It(Runner->GetWorld());It;++It)
            if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&(It->IsA<AMCCataclysmEvent>()||It->IsA<AMCCataclysmJelly>()
                ||It->IsA<AMCCataclysmRaft>()||It->IsA<AMCCataclysmDebris>()||It->IsA<AMCMouthSurface>()||It->IsA<AMCTongue>()))++Left;
        Test->TestEqual(TEXT("Cancel removes its real children, paint and ground"),Left,0);
        Runner.Reset();Override.Reset();return true;
    }
private:
    FAutomationTestBase* Test;bool bDisabled=false,bRupture=false,bCorrectNativeGate=false;
    int32 Children=0,PaintBursts=0;double Started=0;
    TUniquePtr<FMCPerkPreviewRunner> Runner;TStrongObjectPtr<UMCPerkVFXProfile> Override;
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCJellyNativeRupture,"MessControl.VFX.Jelly.RealMechanic",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCJellyNativeRupture::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{Names={TEXT("Original"),TEXT("Disabled")};Commands=Names;}
bool FMCJellyNativeRupture::RunTest(const FString& Command)
{ADD_LATENT_AUTOMATION_COMMAND(MCJellyTests::FRunJelly(this,Command==TEXT("Disabled")));return true;}
#endif
