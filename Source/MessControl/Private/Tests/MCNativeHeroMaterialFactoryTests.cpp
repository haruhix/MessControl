#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCNiagaraMaterialContextComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "MCVFXTestWorld.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PSOPrecache.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNativeHeroPreparedFactory,
    "MessControl.VFX.MaterialContext.ActualNativeHeroFactoryAndRespawnSignature",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FMCNativeHeroPreparedFactory::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
    UWorld* World=MCVFXTestWorld::Create(EWorldType::EditorPreview,false,NAME_None,nullptr,false,GMaxRHIFeatureLevel,&Init);
    if(!TestNotNull(TEXT("Isolated factory fixture world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    struct FScope {UWorld* World;~FScope(){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}} Scope{World};
    auto* First=World->SpawnActor<AMCToothCharacter>();
    auto* Replacement=World->SpawnActor<AMCToothCharacter>();
    if(!TestNotNull(TEXT("Real native hero"),First)||!TestNotNull(TEXT("Later native replacement"),Replacement))return false;
    TestTrue(TEXT("Fixture uses actual constructor-selected native mesh class"),First->GetMesh()->GetClass()==UMCToothMeshComponent::StaticClass());
    auto* Actual=First->GetMesh();FPSOPrecacheParams Factory;Actual->SetupPrecachePSOParams(Factory);Factory.bAnyMaterialHasWorldPositionOffset=false;
    TArray<FString> Parents;
    for(auto* Material:Actual->GetMaterials()) {
        for(int32 I=0;I<16;++I){auto* MID=Cast<UMaterialInstanceDynamic>(Material);if(!MID)break;Material=MID->Parent;}
        Parents.Add(GetPathNameSafe(Material));
    }
    AddInfo(FString::Printf(TEXT("MC_NATIVE_HERO_FACTORY class=%s geometry=%s parentSlots=%s flags=%llu negative=%d forcedLOD=%d"),
        *Actual->GetClass()->GetPathName(),*GetPathNameSafe(Actual->GetSkeletalMeshAsset()),*FString::Join(Parents,TEXT("|")),
        static_cast<unsigned long long>(Factory.Data),Actual->GetRenderMatrix().Determinant()<0?1:0,Actual->GetForcedLOD()));
    FString Error;
    if(!TestTrue(TEXT("Actual hero is admitted by explicit stock-renderer adapter"),
        UMCNiagaraMaterialContextResources::ValidateTargetFactory(First->GetMesh(),Error))) {AddError(Error);return false;}
    auto* Resources=NewObject<UMCNiagaraMaterialContextResources>(World,NAME_None,RF_Transient);
    if(!Resources->Initialize(World,FGuid::NewGuid(),Error)||!Resources->RegisterPreparedTarget(First->GetMesh(),Error)
        ||!Resources->SealResources(Error)){AddError(Error);return false;}
    TestTrue(TEXT("Later real identity reuses exact prepared factory signature"),Resources->IsTargetPrepared(Replacement->GetMesh()));
    Replacement->GetMesh()->SetForcedLOD(2);
    TestFalse(TEXT("Different forced LOD is never inherited as prepared"),Resources->IsTargetPrepared(Replacement->GetMesh()));
    Resources->Reset();
    // This is the actual native factory admission/identity regression. It does
    // not activate Niagara, certify PSOs, capture a pose, or exercise GameMode.
    return true;
}
#endif
