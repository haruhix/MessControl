#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCWorldPreloadSubsystem.h"
#include "MCIceEvent.h"
#include "MCIceEventVFX.h"
#include "MCGameState.h"
#include "NiagaraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "EngineUtils.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace MCIcePrewarmOwnershipTest
{
struct FWorld
{
    UWorld* World=MCVFXTestWorld::Create(EWorldType::Game,false);
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->bAllowAudioPlayback=false;World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPrewarmedIcePresentationOwnership,
    "MessControl.VFX.Ice.PreparedInstanceOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCPrewarmedIcePresentationOwnership::RunTest(const FString&)
{
    // Codec unit, not a network transport test or a readiness certificate.
    FMCIcePresentationRunScope Scope;Scope.GameplayLoadId=FGuid::NewGuid();Scope.Scope=EMCIcePresentationScope::Required;
    FBufferArchive Buffer;bool Written=false;Scope.NetSerialize(Buffer,nullptr,Written);
    FMCIcePresentationRunScope Decoded;FMemoryReader Reader(Buffer);bool Read=false;Decoded.NetSerialize(Reader,nullptr,Read);
    TestTrue(TEXT("Authoritative decision and exact epoch round-trip as one native field"),Written&&Read
        &&Decoded.GameplayLoadId==Scope.GameplayLoadId&&Decoded.Scope==Scope.Scope);
    using namespace MCIcePrewarmOwnershipTest;FWorld T;FString Error;
    auto* Loading=T.World->GetSubsystem<UMCWorldPreloadSubsystem>();
    if(!TestNotNull(TEXT("Actual world-scoped preparation service"),Loading))return false;
    // Explicit resident-parent fixture, not a real map/manifest barrier proof.
    auto* FrostParent=LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
    if(!TestNotNull(TEXT("Original frost parent is resident before presentation preparation"),FrostParent))return false;
    Loading->PreparedGameplayLoadId=FGuid::NewGuid();
    if(!TestTrue(TEXT("Actual native components prepare without a gameplay Ice event"),Loading->CreateIcePresentationOwner(Error)))
    {AddError(Error);return false;}
    auto* Prepared=Loading->PreparedIcePresentation.Get();
    TestTrue(TEXT("Preparation remains hidden and has no event/tail tick"),Prepared&&Prepared->IsHidden()&&!Prepared->IsActorTickEnabled());
    auto* FrostMID=Prepared->PreparedFrostSurfaceMID.Get();
    if(!TestNotNull(TEXT("Actual original-parent MID exists before gameplay Ice"),FrostMID))return false;
    TestTrue(TEXT("Prepared variant retains the exact original shader parent"),FrostMID->Parent.Get()==FrostParent);
    TestTrue(TEXT("The retained factory uses that exact MID before the barrier"),
        Prepared->PreparedFrostSurfaceFactory&&Prepared->PreparedFrostSurfaceFactory->IsRegistered()
        &&!Prepared->PreparedFrostSurfaceFactory->IsVisible()
        &&Prepared->PreparedFrostSurfaceFactory->GetMaterial(0)==FrostMID);
    TArray<UObject*> PreparedResources;Prepared->GetLocationPreparedResources(PreparedResources);
    TestTrue(TEXT("Exact MID is reachable by the existing location readiness resource closure"),PreparedResources.Contains(FrostMID));
    TestEqual(TEXT("Bounded frozen-foot facets exist before first contact"),Prepared->FootFacets.Num(),48);
    TestEqual(TEXT("Bounded frozen-foot motes exist before first contact"),Prepared->FootMotes.Num(),16);
    int32 Events=0;for(TActorIterator<AMCIceEvent> It(T.World);It;++It)++Events;
    TestEqual(TEXT("Presentation preparation never spawns gameplay Ice"),Events,0);
    TInlineComponentArray<UNiagaraComponent*> Systems(Prepared);
    for(auto* System:Systems)TestFalse(TEXT("Prepared systems never auto-activate"),System->IsActive());
    auto* Event=T.World->SpawnActorDeferred<AMCIceEvent>(AMCIceEvent::StaticClass(),FTransform::Identity);
    Event->FinishSpawning(FTransform::Identity);
    FGuid Token;
    TestNull(TEXT("Claim is refused before the readiness phase"),Loading->ClaimPreparedIcePresentation(Event,Token,Error));
    // Ownership unit only: phase precondition is supplied here, not certified.
    // This is not a real Mouth resource/readiness or first-visible-use proof.
    Loading->Phase=EMCWorldPreloadPhase::Ready;
    auto* ForeignMID=UMaterialInstanceDynamic::Create(FrostParent,Event);
    ForeignMID->SetScalarParameterValue(TEXT("Warm Radius"),123.f);
    Event->FrostSurface->SetMaterial(0,ForeignMID);
    TestNull(TEXT("Claim refuses to overwrite an unrelated per-source material variant"),Loading->ClaimPreparedIcePresentation(Event,Token,Error));
    TestTrue(TEXT("Rejected transfer preserves the source's exact material and uniform"),
        Event->FrostSurface->GetMaterial(0)==ForeignMID&&ForeignMID->K2_GetScalarParameterValue(TEXT("Warm Radius"))==123.f);
    TestTrue(TEXT("Rejected transfer leaves the one-time prepared owner unconsumed"),Loading->PreparedIcePresentation.Get()==Prepared);
    Event->FrostSurface->SetMaterial(0,FrostParent);
    TestTrue(TEXT("One claim transfers the exact already-created instance"),Loading->ClaimPreparedIcePresentation(Event,Token,Error)==Prepared);
    TestTrue(TEXT("Claim retains the exact existing run token"),Token==Loading->PreparedGameplayLoadId);
    TestTrue(TEXT("Gameplay receives the same MID instance prepared before readiness"),Event->FrostSurface->GetMaterial(0)==FrostMID);
    TestNull(TEXT("The preparation service no longer owns a transferred actor"),Loading->PreparedIcePresentation.Get());
    FGuid Duplicate;
    TestNull(TEXT("One-time preparation cannot be claimed twice"),Loading->ClaimPreparedIcePresentation(Event,Duplicate,Error));
    Loading->ReleaseIcePresentationOwner();
    TestTrue(TEXT("Preparation cleanup cannot destroy transferred runtime ownership"),IsValid(Prepared));
    TestFalse(TEXT("Wrong token cannot cancel the source's accepted presentation"),Prepared->CancelPresentationFor(Event,FGuid::NewGuid()));
    // Actual public preprepare -> Start reset. No tongue exists, so the real
    // native Start fails and must cancel the retained owned presenter. Losing
    // its token during Stop would orphan this actor and fail this check.
    Event->EventVFX=Prepared;Event->PresentationClaimToken=Token;Event->PrepareLocalPresentation();
    TWeakObjectPtr<AMCIceEventVFX> WeakPrepared=Prepared;Event->Start();
    TestTrue(TEXT("Start reset preserves the preprepared token for real owned cancellation"),
        !WeakPrepared.IsValid()||WeakPrepared->IsActorBeingDestroyed());
    Event->Destroy();if(WeakPrepared.IsValid())WeakPrepared->Destroy();
    return true;
}
#endif
