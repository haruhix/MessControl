#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCPerkPreviewAdapter.h"
#include "MCNutEnemy.h"
#include "MCReactionVFX.h"
#include "EngineUtils.h"
#include "MCPerkVFXProfile.h"
#include "MCVFXPresentationComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Package.h"

namespace MCWorldPresentationTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false);
    FWorld()
    {if(World && GEngine)GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);}
    ~FWorld()
    {if(World){if(GEngine)GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}}
    AMCNutEnemy* Enemy()
    {
        FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
        auto* Result=World?World->SpawnActor<AMCNutEnemy>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn):nullptr;
        if(Result){Result->SetActorTickEnabled(false);Result->Health=30;Result->Settings.MaxHealth=30;}
        return Result;
    }
};
UMCPerkVFXProfile* DisabledDeath()
{
    auto* Result=NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Disabled;
    Result->Slots.Add(TEXT("Death"),Slot);return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldVFXPreviewRegistration,
    "MessControl.VFX.WorldPresentation.ContextWithoutPerkOrActiveAction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldVFXPreviewRegistration::RunTest(const FString&)
{
    const auto* Entry=MCPerkPreviewAdapters::Find(TEXT("EditorOnly.NutEnemyDeath"));
    if(!TestNotNull(TEXT("Existing stable source ID has one real provider"),Entry))return false;
    TestTrue(TEXT("Ordinary enemy death is a Context, not an active skill"),Entry->Kind==EMCPerkPreviewAdapterKind::Context);
    TestTrue(TEXT("No dummy DT row is required"),Entry->SourceRowID.IsNone());
    TestTrue(TEXT("Both real original death artwork boundaries are exposed"),Entry->Stages.Num()==2 && Entry->Stages.Contains(TEXT("Death")) && Entry->Stages.Contains(TEXT("DeathDust")));
    TestTrue(TEXT("Death has an editable runtime profile binding"),Entry->bSupportsProfileOverride);
    auto Adapter=Entry->Create();FMCPerkPreviewContext WrongContext;FString Error;
    WrongContext.PerkID=Entry->PerkID;
    TestFalse(TEXT("Missing private world/real hero cannot pretend to play a death"),Adapter->Prepare(WrongContext,Error));
    TestFalse(TEXT("Rejected fixture explains the failure"),Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldVFXNativeDeathBoundary,
    "MessControl.VFX.WorldPresentation.NativeDamageOwnsDeathReceipt",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldVFXNativeDeathBoundary::RunTest(const FString&)
{
    using namespace MCWorldPresentationTests;
    FWorld T;auto* Enemy=T.Enemy();
    if(!TestNotNull(TEXT("Native ordinary enemy fixture"),Enemy)
        || !TestNotNull(TEXT("Native owner presentation component"),Enemy->Presentation.Get()))return false;
    FString Error;
    if(!TestTrue(TEXT("Disabled Death prepares without a perk row or active-skill actor"),
        Enemy->Presentation->PrepareProfileOverride(DisabledDeath(),Error))) {AddError(Error);return false;}
    int32 Receipts=0;EMCVFXPresentationResult Result=EMCVFXPresentationResult::Rejected;
    Enemy->Presentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult InResult)
        {if(Event==TEXT("Death")){++Receipts;Result=InResult;}});
    TestEqual(TEXT("Nonfatal native hit changes health"),Enemy->ReceiveToolDamage(5.f,nullptr),5.f);
    TestEqual(TEXT("Nonfatal contact does not manufacture Death"),Receipts,0);
    TestFalse(TEXT("Nonfatal contact leaves the enemy alive"),Enemy->bDefeated);
    TestEqual(TEXT("Fatal hit uses the normal accepted-damage resolver"),Enemy->ReceiveToolDamage(50.f,nullptr),25.f);
    TestTrue(TEXT("Defeat and zero HP remain gameplay-owned"),Enemy->bDefeated && Enemy->Health==0.f);
    TestEqual(TEXT("Exactly one native Death boundary receipt"),Receipts,1);
    TestTrue(TEXT("Disabled only suppresses the owned cosmetic"),Result==EMCVFXPresentationResult::Suppressed);
    TestTrue(TEXT("Native defeat still disables collision"),Enemy->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Native body-removal timing remains present"),Enemy->GetLifeSpan()>1.f);
    TestEqual(TEXT("Further damage cannot re-emit death"),Enemy->ReceiveToolDamage(50.f,nullptr),0.f);
    TestEqual(TEXT("Receipt remains exactly once"),Receipts,1);
    Enemy->Presentation->OnEventAccepted.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldVFXDeathInvalidOverride,
    "MessControl.VFX.WorldPresentation.InvalidProfilePreservesPreparedDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldVFXDeathInvalidOverride::RunTest(const FString&)
{
    using namespace MCWorldPresentationTests;
    FWorld T;auto* Enemy=T.Enemy();
    if(!TestNotNull(TEXT("Native owner"),Enemy) || !TestNotNull(TEXT("Presentation"),Enemy->Presentation.Get()))return false;
    FString Error;auto* Original=DisabledDeath();
    if(!TestTrue(TEXT("Valid disabled snapshot"),Enemy->Presentation->PrepareProfileOverride(Original,Error)))return false;
    auto* Invalid=NewObject<UMCPerkVFXProfile>(GetTransientPackage());
    FMCPerkVFXSlot Slot;Slot.Mode=EMCPerkVFXSlotMode::Disabled;Invalid->Slots.Add(TEXT("Impact"),Slot);
    TestFalse(TEXT("Wrong active-skill event cannot replace a Death-only owner"),Enemy->Presentation->PrepareProfileOverride(Invalid,Error));
    TestTrue(TEXT("Failed new preparation retains previous usable snapshot"),Enemy->Presentation->IsPrepared());
    int32 Suppressed=0;
    Enemy->Presentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result)
        {if(Event==TEXT("Death") && Result==EMCVFXPresentationResult::Suppressed)++Suppressed;});
    Enemy->ReceiveToolDamage(40.f,nullptr);
    TestEqual(TEXT("Actual fatal hit still consumes the previous Death snapshot"),Suppressed,1);
    TestTrue(TEXT("Source profile data was not edited"),Original->Slots.Num()==1
        && Original->Slots.FindChecked(TEXT("Death")).Mode==EMCPerkVFXSlotMode::Disabled);
    Enemy->Presentation->OnEventAccepted.Clear();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldVFXDeathLegacyGate,
    "MessControl.VFX.WorldPresentation.LegacyDeathSuppressionIsPreserved",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldVFXDeathLegacyGate::RunTest(const FString&)
{
    using namespace MCWorldPresentationTests;
    FWorld T;auto* Enemy=T.Enemy();
    if(!TestNotNull(TEXT("Native owner"),Enemy) || !TestNotNull(TEXT("Presentation"),Enemy->Presentation.Get()))return false;
    FString Error;
    if(!TestTrue(TEXT("Prepared override"),Enemy->Presentation->PrepareProfileOverride(DisabledDeath(),Error)))return false;
    Enemy->Tags.Add(TEXT("MC.LegacyCombatFeedback"));int32 Deaths=0;
    Enemy->Presentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult)
        {if(Event==TEXT("Death"))++Deaths;});
    TestEqual(TEXT("Legacy tag does not prevent real lethal damage"),Enemy->ReceiveToolDamage(50.f,nullptr),30.f);
    TestTrue(TEXT("Legacy enemy still reaches native defeated state"),Enemy->bDefeated);
    TestEqual(TEXT("Existing suppressed cosmetic does not reappear through Composer"),Deaths,0);
    Enemy->Presentation->OnEventAccepted.Clear();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWorldVFXIndependentDeathDust,
    "MessControl.VFX.WorldPresentation.DeathDustUsesActualFoodBreakTickOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWorldVFXIndependentDeathDust::RunTest(const FString&)
{
    using namespace MCWorldPresentationTests;
    FWorld T;auto* Enemy=T.Enemy();if(!TestNotNull(TEXT("Actual damage owner"),Enemy))return false;
    auto* Profile=DisabledDeath();FMCPerkVFXSlot Dust;Dust.Mode=EMCPerkVFXSlotMode::Disabled;
    Profile->Slots.Add(TEXT("DeathDust"),Dust);FString Error;
    if(!TestTrue(TEXT("Two independent disabled lanes prepare"),Enemy->Presentation->PrepareProfileOverride(Profile,Error)))return false;
    int32 Main=0,DustReceipts=0;
    Enemy->Presentation->OnEventAccepted.AddLambda([&](FName Event,EMCVFXPresentationResult Result){
        if(Result==EMCVFXPresentationResult::Suppressed){if(Event==TEXT("Death"))++Main;if(Event==TEXT("DeathDust"))++DustReceipts;}});
    Enemy->ReceiveToolDamage(50.f,nullptr);
    TestEqual(TEXT("Fatal damage owns main event immediately"),Main,1);
    TestEqual(TEXT("Dust is not emitted early from Defeat"),DustReceipts,0);
    AMCReactionVFX* NativeCue=nullptr;
    for(TActorIterator<AMCReactionVFX> It(T.World);It;++It)
        if(It->GetOwner()==Enemy&&It->Effect==EMCReactionEffect::FoodBreak){NativeCue=*It;break;}
    if(!TestNotNull(TEXT("Existing native FoodBreak carrier is retained"),NativeCue))return false;
    TestTrue(TEXT("Mute leaves native decorative body hidden"),NativeCue->bPresentationTextOnly);
    NativeCue->Tick(1.f/30);NativeCue->Tick(1.f/30);
    TestEqual(TEXT("Real native cue tick accepts only one independent dust boundary"),DustReceipts,1);
    TestTrue(TEXT("Native gameplay collision/lifespan preserved"),Enemy->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&Enemy->GetLifeSpan()>1.f);
    Enemy->Presentation->OnEventAccepted.Clear();return true;
}
#endif
