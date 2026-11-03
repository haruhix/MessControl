#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCNutCombatEffect.h"
#include "MCTongue.h"
#include "MCVFXPresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace MCKnightShieldPresentationTests
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    AMCNutBoss* Boss=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
        Boss=World->SpawnActor<AMCNutBoss>();Boss->SetActorTickEnabled(false);
        Boss->BossRole=EMCNutBossRole::Tank;Boss->State=EMCNutBossState::Idle;
        Boss->Attack=EMCNutBossAttack::None;Boss->Health=400.f;Boss->Settings.MaxHealth=400.f;
        Boss->ShieldHealth=50.f;Boss->Tongue=World->SpawnActor<AMCTongue>();
    }
    ~FFixture(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    bool Prepare(bool bDisabled,FString& Error)
    {
        auto* Profile=NewObject<UMCPerkVFXProfile>();
        if(bDisabled) {
            FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Disabled;
            Profile->Slots.Add(TEXT("Hit"),Slot);Profile->Slots.Add(TEXT("Break"),Slot);
        }
        return Boss->ShieldPresentation->PrepareProfileOverride(Profile,Error);
    }
    int32 LegacyShieldCues() const
    {
        int32 Count=0;for(TActorIterator<AMCNutCombatEffect> It(World);It;++It)
            if(IsValid(*It) && !It->IsActorBeingDestroyed() && It->Cue.Type==EMCNutCombatCue::ShieldHit)++Count;
        return Count;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnightShieldToolEvents,
    "MessControl.VFX.KnightShield.ToolEventsPreserveDurability",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnightShieldToolEvents::RunTest(const FString&)
{
    using namespace MCKnightShieldPresentationTests;FFixture T;FString Error;
    if(!TestTrue(TEXT("Knight exposes its public native presentation component"),IsValid(T.Boss->ShieldPresentation))
        || !TestTrue(TEXT("Disabled Hit/Break prepare without active-action phases"),T.Prepare(true,Error))) {AddError(Error);return false;}
    int32 Hit=0,Break=0;bool SawBrokenState=false;
    T.Boss->ShieldPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
        if(Result!=EMCVFXPresentationResult::Suppressed)return;
        if(Event==TEXT("Hit"))++Hit;
        if(Event==TEXT("Break")){++Break;SawBrokenState=T.Boss->ShieldHealth==0.f && T.Boss->State==EMCNutBossState::Stunned;}
    });
    TestEqual(TEXT("Rejected zero damage has no cosmetic event"),T.Boss->ReceiveToolDamage(0.f,nullptr),0.f);
    TestEqual(TEXT("First shield contact removes no boss HP"),T.Boss->ReceiveToolDamage(10.f,nullptr),0.f);
    TestEqual(TEXT("Native shield durability still changes by the actual tool damage"),T.Boss->ShieldHealth,40.f);
    TestEqual(TEXT("Actual accepted first hit emits one Hit"),Hit,1);
    T.Boss->ReceiveToolDamage(5.f,nullptr);
    TestEqual(TEXT("Repeated same-frame contacts retain the existing visual throttle"),Hit,1);
    TestEqual(TEXT("Throttling is presentation only; second damage still applies"),T.Boss->ShieldHealth,35.f);
    TestEqual(TEXT("Breaking overflow is discarded instead of damaging HP"),T.Boss->ReceiveToolDamage(100.f,nullptr),0.f);
    TestEqual(TEXT("Breaking blow emits Break despite the Hit throttle"),Break,1);
    TestTrue(TEXT("Break callback observes already accepted native durability/stun"),SawBrokenState);
    TestEqual(TEXT("Break does not emit an extra ordinary Hit"),Hit,1);
    TestEqual(TEXT("Boss HP is unchanged through shield break"),T.Boss->Health,400.f);
    TestTrue(TEXT("Native guaranteed stun remains at least five seconds"),T.Boss->AttackEndAt-T.Boss->StateStartedAt>=5.);
    TestEqual(TEXT("Explicit Disabled prevents the old ShieldHit actor as well"),T.LegacyShieldCues(),0);
    TestEqual(TEXT("Later unshielded damage enters the native HP resolver"),T.Boss->ReceiveToolDamage(10.f,nullptr),10.f);
    TestEqual(TEXT("No shield event appears for later HP damage"),Hit+Break,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnightShieldExternalEvents,
    "MessControl.VFX.KnightShield.ExternalEventsPreserveDurability",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnightShieldExternalEvents::RunTest(const FString&)
{
    using namespace MCKnightShieldPresentationTests;FFixture T;FString Error;
    if(!TestTrue(TEXT("External-impact fixture prepares its real shield provider"),T.Prepare(true,Error))) {AddError(Error);return false;}
    int32 Hit=0,Break=0;
    T.Boss->ShieldPresentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult){
        if(Event==TEXT("Hit"))++Hit;if(Event==TEXT("Break"))++Break;
    });
    auto* Causer=T.World->SpawnActor<AActor>();const FVector Origin(200,70,30);
    TestEqual(TEXT("Invalid external causer is rejected before an event"),T.Boss->ReceiveExternalImpact(10.f,nullptr,nullptr,Origin),0.f);
    TestEqual(TEXT("Accepted area hit cannot bypass the shield gate"),T.Boss->ReceiveExternalImpact(10.f,Causer,nullptr,Origin),0.f);
    TestEqual(TEXT("Area damage changes native shield durability"),T.Boss->ShieldHealth,40.f);
    TestEqual(TEXT("Area contact emits Hit"),Hit,1);
    T.Boss->ReceiveExternalImpact(5.f,Causer,nullptr,Origin);
    TestEqual(TEXT("External contacts share the native visual throttle"),Hit,1);
    T.Boss->ReceiveExternalImpact(100.f,Causer,nullptr,Origin);
    TestEqual(TEXT("External breaking contact emits one Break"),Break,1);
    TestEqual(TEXT("External break preserves discarded overflow"),T.Boss->Health,400.f);
    TestEqual(TEXT("External break preserves native stunned state"),T.Boss->State,EMCNutBossState::Stunned);
    TestEqual(TEXT("Disabled external feedback creates no duplicate native cue"),T.LegacyShieldCues(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnightShieldNativeDefault,
    "MessControl.VFX.KnightShield.DefaultRetainsNativeContact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnightShieldNativeDefault::RunTest(const FString&)
{
    using namespace MCKnightShieldPresentationTests;FFixture T;FString Error;
    if(!TestTrue(TEXT("Default profile is valid for the two semantic events"),T.Prepare(false,Error))) {AddError(Error);return false;}
    int32 Defaults=0;T.Boss->ShieldPresentation->OnEventAccepted.AddLambda([&](FName,EMCVFXPresentationResult Result){
        if(Result==EMCVFXPresentationResult::UseDefault)++Defaults;
    });
    const FVector ExpectedContact=AMCNutCombatEffect::GetShieldContactOrigin(T.Boss);
    T.Boss->ReceiveToolDamage(10.f,nullptr);
    TestEqual(TEXT("UseDefault remains one native accepted event"),Defaults,1);
    TestEqual(TEXT("UseDefault retains exactly one native ShieldHit actor"),T.LegacyShieldCues(),1);
    for(TActorIterator<AMCNutCombatEffect> It(T.World);It;++It)if(It->Cue.Type==EMCNutCombatCue::ShieldHit) {
        TestTrue(TEXT("Legacy origin is the actual authored shield contact"),It->Cue.Origin.Equals(ExpectedContact,.01f));
        TestTrue(TEXT("Legacy target direction remains the real native forward"),It->Cue.Target.Equals(
            ExpectedContact-T.Boss->GetActorForwardVector()*T.Boss->Settings.BodyRadius,.01f));
        TestEqual(TEXT("Legacy ordinary contact duration is preserved"),It->Cue.ActiveSeconds,.32f);
    }
    return true;
}
#endif
