#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewRunner.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCVFXLabBossStation.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"

namespace MCNutMeleeSequencePreviewTests
{
class FRunNativeSequence final:public IAutomationLatentCommand
{
public:
    explicit FRunNativeSequence(FAutomationTestBase* InTest):Test(InTest){}
    bool Update()override
    {
        if(!Runner) {
            StartedAt=FPlatformTime::Seconds();Runner=MakeUnique<FMCPerkPreviewRunner>();
            if(!Runner->Initialize() || !Runner->StartAction(TEXT("Boss.NutKnight.Melee"),EMCPerkPreviewScenario::Target,nullptr)) {
                Test->AddError(Runner->GetStatus());return true;
            }
            Hero=Runner->GetHero();Controller=Hero.IsValid()?Cast<APlayerController>(Hero->GetController()):nullptr;
            Identity=Hero.IsValid()?Hero->GetPlayerState<AMCPlayerState>():nullptr;
        }
        // Real shared editor frames advance the Runner's owned world clock.
        Runner->Tick(.05f);
        if(auto* World=Runner->GetWorld()) {
            for(TActorIterator<AMCNutBoss> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()) {
                if(!Knight.IsValid())Knight=*It;
                if(Knight.Get()!=*It){Test->AddError(TEXT("Melee scenario recreated/replaced its native knight."));bFailed=true;}
                if(It->State==EMCNutBossState::Telegraph && LastState!=EMCNutBossState::Telegraph) {
                    const int32 Index=Variants.Num();
                    Test->TestEqual(TEXT("Actual BeginAttack cycles Attack1, Attack2, then Combo"),int32(It->TankMeleeVariant),Index);
                    if(Index>0) {
                        Test->TestTrue(TEXT("Each next start follows observed native recovery"),Recoveries.Contains(Index-1));
                        Test->TestTrue(TEXT("Each next start respects the preceding native readiness timestamp"),It->StateStartedAt+.0001>=ReadyAt);
                    }
                    Variants.Add(It->TankMeleeVariant);
                }
                if(It->State==EMCNutBossState::Recovery) {
                    Recoveries.Add(int32(It->TankMeleeVariant));ReadyAt=It->NextMeleeAt;
                }
                bFinalIdle|=Variants.Num()==3 && It->TankMeleeVariant==2 && It->State==EMCNutBossState::Idle;
                LastState=It->State;
            }
            for(TActorIterator<AMCNutCombatEffect> It(World);It;++It)
                if(It->SourceActor==Knight.Get() && It->Cue.PresentationSource==TEXT("Boss.NutKnight.Melee")
                    && It->Cue.PresentationEvent==TEXT("Slam") && It->Cue.Type==EMCNutCombatCue::SlamImpact) {
                    bActualSlam=true;
                    const auto* B=Knight.Get();
                    Test->TestTrue(TEXT("The original Slam cue uses the actual third-contact damage centre"),B
                        && It->Cue.Target.Equals(B->LockedStart+B->AttackForward*B->BossSettings.MeleeRange*.55f,.01f));
                }
        }
        const auto Has=[&](FName Stage){return Runner->GetEvents().ContainsByPredicate([&](const FMCPerkPreviewEvent& E){return E.Stage==Stage;});};
        const bool Finished=Has(TEXT("ActionFinished"));
        if(!Finished && !bFailed && Runner->IsRunning() && Runner->GetElapsed()<47.f && FPlatformTime::Seconds()-StartedAt<100.)return false;
        Test->TestTrue(TEXT("One real scenario completes all three ordinary attacks"),Finished);
        Test->TestEqual(TEXT("Exactly three native starts were observed"),Variants.Num(),3);
        Test->TestTrue(TEXT("Third variant actually spawns Slam before complete recovery"),bActualSlam&&bFinalIdle&&Recoveries.Contains(2));
        Test->TestTrue(TEXT("The provider observes native Tell, Slash and Slam"),Has(TEXT("Tell"))&&Has(TEXT("Slash"))&&Has(TEXT("Slam")));
        Test->TestFalse(TEXT("No rejected native start"),Has(TEXT("NutNativeActionRejected"))||Has(TEXT("NutNativeMeleeSequenceRejected")));
        Test->TestFalse(TEXT("No native scenario timeout"),Has(TEXT("NutNativeActionTimeout")));
        if(!Finished)Test->AddError(Runner->GetStatus()+TEXT(" | ")+Runner->GetEventLog());
        Runner->Cancel();
        Test->TestTrue(TEXT("Cancel preserves the borrowed hero/controller/identity"),Hero.IsValid()&&Controller.IsValid()&&Identity.IsValid()
            &&Controller->GetPawn()==Hero.Get()&&Hero->GetPlayerState<AMCPlayerState>()==Identity.Get());
        int32 Remaining=0;
        for(TActorIterator<AMCVFXLabBossStation> It(Runner->GetWorld());It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed())++Remaining;
        Test->TestEqual(TEXT("Cancel releases the one retained comparison station"),Remaining,0);
        Runner.Reset();return true;
    }
private:
    FAutomationTestBase* Test;TUniquePtr<FMCPerkPreviewRunner> Runner;
    TWeakObjectPtr<AMCNutBoss> Knight;TWeakObjectPtr<AMCToothCharacter> Hero;
    TWeakObjectPtr<APlayerController> Controller;TWeakObjectPtr<AMCPlayerState> Identity;
    TArray<uint8> Variants;TSet<int32> Recoveries;
    EMCNutBossState LastState=EMCNutBossState::Falling;
    double StartedAt=0,ReadyAt=0;bool bActualSlam=false,bFinalIdle=false,bFailed=false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutMeleeThreeNativeStarts,"MessControl.VFX.NutPreview.MeleeThreeNativeStartsAndSlam",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutMeleeThreeNativeStarts::RunTest(const FString&)
{
    if(!GEditor || GEditor->PlayWorld){AddError(TEXT("Run the isolated real Knight preview outside PIE."));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(MCNutMeleeSequencePreviewTests::FRunNativeSequence(this));
    return true;
}
#endif
