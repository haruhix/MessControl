#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCPerkPreviewRunner.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCFreezeDeathVFX.h"
#include "MCIceEvent.h"
#include "MCTongue.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXComposition.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

namespace MCFreezeRealPreviewTests
{
bool Has(const FMCPerkPreviewRunner& R,FName Stage)
{return R.GetEvents().ContainsByPredicate([&](const FMCPerkPreviewEvent& E){return E.Stage==Stage;});}
class FRun final:public IAutomationLatentCommand
{
public:
    FRun(FAutomationTestBase* InTest,FString InMode):Test(InTest),Mode(InMode){}
    bool Update() override
    {
        if(!Runner) {
            Started=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(Mode!=TEXT("Default")) {
                Override.Reset(NewObject<UMCPerkVFXProfile>());
                for(FName Stage:{FName(TEXT("Hold")),FName(TEXT("Shatter"))})Override->Slots.FindOrAdd(Stage).Mode=EMCPerkVFXSlotMode::Disabled;
                if(Mode==TEXT("Ordinary")) {
                    auto* NS=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
#if WITH_EDITOR
                    if(NS)NS->WaitForCompilationComplete(false,false);
#endif
                    if(!NS||!NS->IsReadyToRun()){Test->AddError(TEXT("Actual saved gallery fixture is not ready; no forged readiness."));return true;}
                    Ordinary.Reset(NewObject<UMCVFXComposition>(Override.Get()));auto* C=Ordinary.Get();
                    FMCVFXCompositionLayer L;L.System=NS;L.StartSeconds=.2f;L.EmissionSeconds=.5f;C->Layers.Add(L);
                    auto& Slot=Override->Slots.FindChecked(TEXT("Hold"));Slot.Mode=EMCPerkVFXSlotMode::Composition;Slot.Composition=C;
                }
            }
            if(!Runner->Initialize()||!Runner->StartAction(TEXT("Character.FreezeDeath"),EMCPerkPreviewScenario::Target,Override.Get()))
            {Test->AddError(Runner->GetStatus());return true;}
        }
        Runner->Tick(.05f);auto* Hero=Runner->GetHero();
        if(IsValid(Hero)) {
            for(TActorIterator<AMCIceEvent> It(Runner->GetWorld());It;++It) {
                for(const auto& P:It->Players)if(P.Hero==Hero&&P.Amount>0&&P.Amount<1)bSawActualMeter=true;
            }
            if(auto* FX=Hero->GetPreparedFreezeDeathVisual();IsValid(FX)) {
                if(!Has(*Runner,TEXT("Prepared"))) {
                    bSawUncommittedOwner=true;
                    Test->TestFalse(TEXT("Actual owner preparation never freezes gameplay"),Hero->IsFreezingToDeath());
                }
                if(Hero->IsFreezingToDeath()&&Hero->FreezeDeathAge()<1.4f&&Mode!=TEXT("Default")) {
                    bSawVisibleOrdinaryBody|=!Hero->GetMesh()->bHiddenInGame;
                    Test->TestEqual(TEXT("Disabled/ordinary never builds the old frozen body"),FX->BodyFragments.Num(),0);
                }
                bSawActualShatter|=FX->HasAcceptedShatter();
            }
        }
        const bool Finished=Has(*Runner,TEXT("ActionFinished"));
        if(!Finished&&Runner->IsRunning()&&Runner->GetElapsed()<22&&FPlatformTime::Seconds()-Started<90)return false;
        Test->TestTrue(TEXT("Prepared actual owner existed before accepted gameplay"),bSawUncommittedOwner);
        Test->TestTrue(TEXT("Real native cold meter accumulated before fatal threshold"),bSawActualMeter);
        Test->TestTrue(TEXT("Actual OnRep fatal state and native Shatter observed"),Has(*Runner,TEXT("Hold"))&&Has(*Runner,TEXT("Shatter"))&&bSawActualShatter);
        Test->TestTrue(TEXT("Native delayed lethal damage accepted"),Has(*Runner,TEXT("NativeFatalDamageAccepted")));
        Test->TestTrue(TEXT("Real scenario completed"),Finished);
        if(Mode!=TEXT("Default"))Test->TestTrue(TEXT("Muted/body-free ordinary mode keeps actual gameplay body visible"),bSawVisibleOrdinaryBody);
        if(!Finished)Test->AddError(Runner->GetStatus()+TEXT(" | ")+Runner->GetEventLog());
        Runner->Cancel();int32 Left=0;
        for(TActorIterator<AActor> It(Runner->GetWorld());It;++It)
            if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&(It->IsA<AMCIceEvent>()||It->IsA<AMCTongue>()||It->IsA<AMCFreezeDeathVFX>()))++Left;
        Test->TestEqual(TEXT("Cancel removes only disposable real fixtures/owned presentation"),Left,0);
        Runner->Reset();auto* Fresh=Runner->GetHero();
        Test->TestTrue(TEXT("Ordinary reset restores a fresh real living/unfrozen hero"),IsValid(Fresh)&&Fresh->Status->IsAlive()&&!Fresh->IsFreezingToDeath());
        Runner.Reset();Ordinary.Reset();Override.Reset();return true;
    }
private:
    FAutomationTestBase* Test;FString Mode;TUniquePtr<FMCPerkPreviewRunner> Runner;TStrongObjectPtr<UMCPerkVFXProfile> Override;
    TStrongObjectPtr<UMCVFXComposition> Ordinary;
    double Started=0;bool bSawUncommittedOwner=false,bSawActualMeter=false,bSawActualShatter=false,bSawVisibleOrdinaryBody=false;
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMCFreezeRealFatalPreview,"MessControl.VFX.FreezeCapture.RealFatalCondition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FMCFreezeRealFatalPreview::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Default"),TEXT("Disabled"),TEXT("Ordinary")};Commands=Names;}
bool FMCFreezeRealFatalPreview::RunTest(const FString& Mode)
{ADD_LATENT_AUTOMATION_COMMAND(MCFreezeRealPreviewTests::FRun(this,Mode));return true;}
#endif
