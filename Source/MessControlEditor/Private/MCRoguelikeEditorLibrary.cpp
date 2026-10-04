#include "MCRoguelikeEditorLibrary.h"
#include "ActorFactories/ActorFactory.h"
#include "AI/NavigationSystemBase.h"
#include "AssetCompilingManager.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Model.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/UObjectGlobals.h"

bool UMCRoguelikeEditorLibrary::ConfigureBossNavigation(UWorld* World,FVector Center,FVector Extent)
{
    if (!GEditor || !IsValid(World) || World->WorldType!=EWorldType::Editor || !World->PersistentLevel
        || Center.ContainsNaN() || Extent.ContainsNaN() || Extent.X<=0.f || Extent.Y<=0.f || Extent.Z<=0.f
        || Extent.GetMax()>100000.f)
    {
        UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Invalid editor world or box bounds."));
        return false;
    }
    const FName OwnerTag(TEXT("MC_Roguelike_BossNavigation"));
    ANavMeshBoundsVolume* Volume=nullptr;
    for (TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)
    {
        if (!It->ActorHasTag(OwnerTag)) continue;
        // An ambiguous owner marker is a real authoring error; do not silently alter another tagged volume.
        if (Volume || It->GetLevel()!=World->PersistentLevel)
        {
            UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Duplicate or non-persistent owned volume."));
            return false;
        }
        Volume=*It;
    }
    auto* NavSystem=UNavigationSystemV1::GetCurrent(World);
    if (!NavSystem)
    {
        FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::EditorMode);
        NavSystem=UNavigationSystemV1::GetCurrent(World);
    }
    if (!NavSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Navigation system unavailable in this editor world."));
        return false;
    }
    const bool bCreated=Volume==nullptr;
    if (!Volume)
    {
        FActorSpawnParameters Params;
        Params.OverrideLevel=World->PersistentLevel;
        Params.ObjectFlags=RF_Transactional;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Volume=World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(),Center,FRotator::ZeroRotator,Params);
        if (!Volume) return false;
        Volume->Tags.Add(OwnerTag);
        Volume->SetActorLabel(TEXT("MC Boss Navigation"));
    }
    Volume->Modify();
    Volume->SetActorTransform(FTransform(FQuat::Identity,Center,FVector::OneVector));
    auto* Builder=NewObject<UCubeBuilder>(GetTransientPackage());
    Builder->X=Extent.X*2.f;
    Builder->Y=Extent.Y*2.f;
    Builder->Z=Extent.Z*2.f;
    Builder->Hollow=false;
    Builder->Tessellated=false;
    // Engine volume authoring initializes Brush/Polys, calls UCubeBuilder::Build on this exact actor,
    // prepares the brush's bounds/collision, clears invisible material references and registers components.
    UActorFactory::CreateBrushForVolumeActor(Volume,Builder);
    // The brush builder uses editor snapping internally. Preserve the caller's exact requested position.
    Volume->SetActorTransform(FTransform(FQuat::Identity,Center,FVector::OneVector));
    Volume->ReregisterAllComponents();
    if (!Volume->Brush || !Volume->Brush->Polys || Volume->Brush->Polys->Element.Num()!=6
        || Volume->GetBrushComponent()->Bounds.BoxExtent.IsNearlyZero())
    {
        UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Box brush creation failed."));
        if (bCreated) World->DestroyActor(Volume);
        return false;
    }
    Volume->MarkPackageDirty();
    World->PersistentLevel->MarkPackageDirty();
    NavSystem->OnNavigationBoundsUpdated(Volume);
    const FNavDataConfig* BossConfig=NavSystem->GetSupportedAgents().FindByPredicate([](const FNavDataConfig& Config)
    { return FMath::IsNearlyEqual(Config.AgentRadius,60.f) && FMath::IsNearlyEqual(Config.AgentHeight,220.f); });
    if (!BossConfig || !BossConfig->GetNavDataClass<ARecastNavMesh>())
    {
        UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Project SupportedAgents needs a Recast Boss agent with radius 60 and height 220."));
        return false;
    }
    ARecastNavMesh* BossNav=nullptr;
    for (TActorIterator<ARecastNavMesh> It(World);It;++It)
        if (It->GetConfig().IsEquivalent(*BossConfig)) { BossNav=*It; break; }
    if (!BossNav)
    {
        // Configure before FinishSpawning queues registration; never repurpose the smaller Default navmesh.
        BossNav=World->SpawnActorDeferred<ARecastNavMesh>(BossConfig->GetNavDataClass<ARecastNavMesh>(),
            FTransform::Identity,Volume,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!BossNav) return false;
        BossNav->SetConfig(*BossConfig);
        BossNav->Tags.Add(OwnerTag);
        BossNav->FinishSpawning(FTransform::Identity);
        BossNav->MarkPackageDirty();
    }
    // Python commandlets do not pump the editor's delayed unlock ticker. Finish the actual loading work
    // first, then release only its now-obsolete AsyncLoadLock; keep user auto-update and all other locks.
    if (NavSystem->IsNavigationBuildingLocked(ENavigationBuildLock::AsyncLoadLock))
    {
        FlushAsyncLoading();
        FAssetCompilingManager::Get().FinishAllCompilation();
        if (IsAsyncLoading() || FAssetCompilingManager::Get().GetNumRemainingAssets()!=0)
        {
            UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Loading/asset compilation still pending; AsyncLoadLock preserved."));
            return false;
        }
        NavSystem->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
            UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    }
    const uint8 BlockingLocks=uint8(~ENavigationBuildLock::NoUpdateInEditor);
    if (NavSystem->IsNavigationBuildingLocked(BlockingLocks))
    {
        UE_LOG(LogTemp, Error, TEXT("MC_BOSS_NAV Other navigation build lock remains; refusing to override world-loading/custom locks."));
        return false;
    }
    // Public navigation Tick drains pending bounds/registration/octree work without ticking unrelated actors.
    NavSystem->Tick(0.f);
    NavSystem->Build(); // UE editor implementation waits for each navigation generator's completion.
    const auto* Selected=NavSystem->GetNavDataForProps(*BossConfig);
    const bool bBuilt=IsValid(Selected) && Selected->GetConfig().IsEquivalent(*BossConfig)
        && !NavSystem->IsNavigationBuildInProgress();
    UE_LOG(LogTemp, Display, TEXT("MC_BOSS_NAV %s Volume=%s Center=%s Extent=%s Selected=%s Radius=%.1f Height=%.1f Registered=%d InProgress=%d"),
        bBuilt?TEXT("BUILT"):TEXT("NO_NAV_DATA"),*Volume->GetPathName(),*Center.ToString(),*Extent.ToString(),
        *GetNameSafe(Selected),Selected?Selected->GetConfig().AgentRadius:-1.f,Selected?Selected->GetConfig().AgentHeight:-1.f,
        Selected?Selected->IsRegistered():false,NavSystem->IsNavigationBuildInProgress());
    // Caller controls level saving and should inspect/project the desired boss positions onto the result.
    return bBuilt;
}
