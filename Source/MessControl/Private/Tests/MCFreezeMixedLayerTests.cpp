// E-only staged regression. NOT compiled/executed in this packet.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFreezeDeathVFX.h"
#include "MCToothCharacter.h"
#include "MCVFXComposition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "EngineUtils.h"
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

namespace MCFreezeMixedTests
{
struct FWorld
{
    UWorld* World=MCVFXTestWorld::Create(EWorldType::EditorPreview,false);
    AMCToothCharacter* Hero=nullptr;
    FWorld(){GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);Hero=World->SpawnActor<AMCToothCharacter>();}
    ~FWorld(){if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}}
};
FMCPreparedPerkVFXComposition MutedEvent(UNiagaraSystem* System)
{
    FMCPreparedPerkVFXComposition P;P.Outer.Mode=EMCPerkVFXSlotMode::Composition;P.Snapshot=NewObject<UMCVFXComposition>();
    FMCVFXCompositionLayer L;L.System=System;L.bEnabled=false;P.Snapshot->Layers.Add(L);P.Systems.Add(System);return P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeDisabledKeepsRealBody,
    "MessControl.VFX.FreezeCapture.DisabledKeepsRealBodyWithoutCapture",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeDisabledKeepsRealBody::RunTest(const FString&)
{
    using namespace MCFreezeMixedTests;FWorld Test;FString Error;
    if(!TestNotNull(TEXT("Actual owner fixture"),Test.Hero))return false;
    Test.Hero->GetMesh()->SetHiddenInGame(false,true);
    FMCPreparedPerkVFXComposition Disabled;Disabled.Outer.Mode=EMCPerkVFXSlotMode::Disabled;
    auto* FX=Test.World->SpawnActor<AMCFreezeDeathVFX>();
    if(!TestTrue(TEXT("Disabled policies need no runtime capture/NI master"),FX->PrepareNiagara(Test.Hero,FGuid::NewGuid(),Disabled,Disabled,nullptr,nullptr,nullptr,Error)))
    {AddError(Error);return false;}
    TestEqual(TEXT("No native body resources allocated for Disabled"),FX->BodyFragments.Num(),0);
    TestEqual(TEXT("No native shard resources allocated for Disabled"),FX->IceFragments.Num(),0);
    TestTrue(TEXT("Actual public accepted owner entry point"),FX->CommitPrepared(Test.Hero,0));FX->Tick(0);
    TestFalse(TEXT("Disabled does not hide the actual held gameplay body"),Test.Hero->GetMesh()->bHiddenInGame);
    TestFalse(TEXT("Disabled does not pretend a replacement body exists"),FX->HasVisibleBody());
    TestEqual(TEXT("Commit did not resurrect native freeze sections"),FX->IceCube->GetNumSections(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeMutedGroupsKeepRealBody,
    "MessControl.VFX.FreezeCapture.AllMutedGroupsDoNotResurrectNative",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeMutedGroupsKeepRealBody::RunTest(const FString&)
{
    using namespace MCFreezeMixedTests;FWorld Test;FString Error;
    if(!TestNotNull(TEXT("Actual owner fixture"),Test.Hero))return false;
    Test.Hero->GetMesh()->SetHiddenInGame(false,true);
    // Represents the immutable post-ProjectGroups snapshot. It deliberately has
    // no valid particle instance: muted rows must not request one or native pose.
    auto* Source=NewObject<UNiagaraSystem>();const auto Muted=MutedEvent(Source);
    auto* FX=Test.World->SpawnActor<AMCFreezeDeathVFX>();
    if(!TestTrue(TEXT("All-muted ordinary composition requires no captured resources"),FX->PrepareNiagara(Test.Hero,FGuid::NewGuid(),Muted,Muted,nullptr,nullptr,nullptr,Error)))
    {AddError(Error);return false;}
    FX->CommitPrepared(Test.Hero,0);FX->Tick(0);
    TestEqual(TEXT("No native fallback body on mute"),FX->BodyFragments.Num(),0);
    TestEqual(TEXT("No native fallback shard set on mute"),FX->IceFragments.Num(),0);
    TestFalse(TEXT("Actual gameplay body stays visible"),Test.Hero->GetMesh()->bHiddenInGame);
    TestFalse(TEXT("No replacement falsely accepted"),FX->HasVisibleBody());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeOrdinaryNeedsNoCapture,
    "MessControl.VFX.FreezeCapture.OrdinaryGalleryRowsNeedNoCapturePacket",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeOrdinaryNeedsNoCapture::RunTest(const FString&)
{
    using namespace MCFreezeMixedTests;FWorld Test;FString Error;
    // Real saved gallery source, no fabricated IsReadyToRun flag or private
    // emitter injection. Wait for this saved source before testing layer policy.
    auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("Real ordinary source is loaded"),System))return false;
#if WITH_EDITOR
    System->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Actual ordinary source is loaded/compiled for this test"),System->IsReadyToRun()))return false;
    if(!TestFalse(TEXT("Ordinary fixture is not a captured-body graph"),FMCPreparedFreezeGeometryLayer::UsesCapture(System)))return false;
    FMCPreparedPerkVFXComposition P;P.Outer.Mode=EMCPerkVFXSlotMode::Composition;P.Snapshot=NewObject<UMCVFXComposition>();
    FMCVFXCompositionLayer L;L.System=System;L.StartSeconds=.2f;L.EmissionSeconds=.5f;P.Snapshot->Layers.Add(L);P.Systems.Add(System);
    auto* FX=Test.World->SpawnActor<AMCFreezeDeathVFX>();
    if(!TestTrue(TEXT("Enabled ordinary layers prepare with no packet/NI materials"),FX->PrepareNiagara(Test.Hero,FGuid::NewGuid(),P,P,nullptr,nullptr,nullptr,Error)))
    {AddError(Error);return false;}
    TestEqual(TEXT("Preparation did not build native pose fragments"),FX->BodyFragments.Num(),0);
    FX->CommitPrepared(Test.Hero,0);FX->Tick(0);
    TestEqual(TEXT("Ordinary accepted event did not create native shatter"),FX->IceFragments.Num(),0);
    TestFalse(TEXT("Ordinary sparks never claim to replace/hide the hero body"),FX->HasVisibleBody());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFreezeCapturedPreparationKeepsOrdinary,
    "MessControl.VFX.FreezeCapture.CapturedPreparationFailureKeepsOrdinaryPlan",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFreezeCapturedPreparationKeepsOrdinary::RunTest(const FString&)
{
    using namespace MCFreezeMixedTests;FWorld Test;FString Error;
    auto* Ordinary=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/ActiveSkills/VFX/NS_Rocket_Start.NS_Rocket_Start"));
    if(!TestNotNull(TEXT("Real ordinary source is loaded"),Ordinary))return false;
#if WITH_EDITOR
    Ordinary->WaitForCompilationComplete(false,false);
#endif
    if(!TestTrue(TEXT("Real ordinary source is resident/ready"),Ordinary->IsReadyToRun()))return false;
    // Deliberately unavailable captured schema, NOT a fabricated ready emitter.
    // UsesCapture classification is real; the exact captured readiness check
    // must reject only this row before it can affect an ordinary gallery row.
    auto* Captured=NewObject<UNiagaraSystem>();
    Captured->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(),TEXT("User.Native.FreezeCapture.Ready")));
    FMCPreparedPerkVFXComposition Mixed;Mixed.Outer.Mode=EMCPerkVFXSlotMode::Composition;Mixed.Snapshot=NewObject<UMCVFXComposition>();
    FMCVFXCompositionLayer Body;Body.Name=TEXT("Unavailable captured body");Body.System=Captured;
    FMCVFXCompositionLayer Sparks;Sparks.Name=TEXT("Independent ordinary gallery row");Sparks.System=Ordinary;Sparks.StartSeconds=.2f;Sparks.EmissionSeconds=.5f;
    Mixed.Snapshot->Layers={Body,Sparks};Mixed.Systems={Captured,Ordinary};
    FMCPreparedPerkVFXComposition Disabled;Disabled.Outer.Mode=EMCPerkVFXSlotMode::Disabled;
    auto* FX=Test.World->SpawnActor<AMCFreezeDeathVFX>();
    TestFalse(TEXT("Unavailable captured body must not certify readiness"),FX->PrepareNiagara(Test.Hero,FGuid::NewGuid(),Mixed,Disabled,nullptr,nullptr,nullptr,Error));
    TestTrue(TEXT("Failure explicitly retains ordinary event rows"),Error.Contains(TEXT("ordinary event rows remain prepared")));
    AMCPerkVFXCompositionPlayer* Player=nullptr;
    for(TActorIterator<AMCPerkVFXCompositionPlayer> It(Test.World);It;++It)if(It->Tags.Contains(TEXT("MC.VFXPresentationComponent.Player"))) {
        if(Player){AddError(TEXT("One Hold event unexpectedly allocated more than one player."));return false;}Player=*It;
    }
    if(!TestNotNull(TEXT("Independent event plan survived captured preparation failure"),Player))return false;
    TestTrue(TEXT("Actual capture rejection is explicit before commit"),Player->HasRejectedCapturedFreezeLayers());
    TestTrue(TEXT("Retained public owner entry still accepts the event boundary"),FX->CommitPrepared(Test.Hero,0));
    TestTrue(TEXT("Ordinary plan runs after capture rejection"),Player->IsRunning());
    TestEqual(TEXT("Only the independent ordinary delay remains pending"),Player->GetPendingLayerCount(),1);
    TestFalse(TEXT("Failed captured body has no admitted captured component"),Player->HasAdmittedCapturedFreezeLayers());
    TestEqual(TEXT("Event-local schedule starts at zero"),Player->GetPlaybackTime(),0.f);
    return true;
}

#endif
