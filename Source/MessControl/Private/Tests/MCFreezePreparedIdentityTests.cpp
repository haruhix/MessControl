#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MCFreezeDeathVFX.h"
#include "MCToothCharacter.h"
#include "MCVFXPresentationComponent.h"
#include "MCPerkVFXProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezePreparedIdentity,
    "MessControl.VFX.FreezeCapture.PreparedActualHeroEpochAndPolicyIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezePreparedIdentity::RunTest(const FString&)
{
    UWorld* W=MCVFXTestWorld::Create(EWorldType::EditorPreview,false);
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(W);
    ON_SCOPE_EXIT{GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto* H=W->SpawnActor<AMCToothCharacter>();auto* Other=W->SpawnActor<AMCToothCharacter>();
    if(!TestNotNull(TEXT("Actual subject"),H)||!TestNotNull(TEXT("Distinct actual subject"),Other))return false;
    auto* Profile=NewObject<UMCPerkVFXProfile>();
    Profile->Slots.FindOrAdd(TEXT("Hold")).Mode=EMCPerkVFXSlotMode::Disabled;
    Profile->Slots.FindOrAdd(TEXT("Shatter")).Mode=EMCPerkVFXSlotMode::Disabled;
    FString Error;const TArray<FName> Events={TEXT("Hold"),TEXT("Shatter")};
    if(!H->PassivePresentation->PrepareSourceProfile(TEXT("Character.FreezeDeath"),Events,Profile,Error,false)) {AddError(Error);return false;}
    const FGuid Epoch=FGuid::NewGuid();
    if(!H->PrepareFreezeDeathFromPreparedSource(Epoch,nullptr,nullptr,nullptr,Error)) {AddError(Error);return false;}
    auto* FX=H->GetPreparedFreezeDeathVisual();if(!TestNotNull(TEXT("Exact retained local owner"),FX))return false;
    TestTrue(TEXT("Same real subject/epoch/policy validates without capture"),FX->ValidatePreparation(H,Epoch,Error));
    TestFalse(TEXT("A different real hero cannot claim prepared resources"),FX->ValidatePreparation(Other,Epoch,Error));
    TestFalse(TEXT("A new run epoch cannot reuse old captured readiness"),FX->ValidatePreparation(H,FGuid::NewGuid(),Error));
    TestFalse(TEXT("Preparation does not begin native fatal state"),H->IsFreezingToDeath());
    TestFalse(TEXT("Preparation cannot hide the real held body"),H->GetMesh()->bHiddenInGame);
    TestEqual(TEXT("Disabled needs no native body pool"),FX->BodyFragments.Num(),0);
    // The shared source is actually replaced, not a test flag/mocked private state.
    Profile->Slots.FindChecked(TEXT("Hold")).Mode=EMCPerkVFXSlotMode::UseDefault;
    if(!H->PassivePresentation->PrepareSourceProfile(TEXT("Character.FreezeDeath"),Events,Profile,Error,false)) {AddError(Error);return false;}
    TestFalse(TEXT("Changed actual shared policy invalidates the prior owner"),FX->ValidatePreparation(H,Epoch,Error));
    H->ReleaseUncommittedFreezeDeathPresentation();
    TestNull(TEXT("Explicit precommit reset releases only its actual owner"),H->GetPreparedFreezeDeathVisual());
    return true;
}
#endif
