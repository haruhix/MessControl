#include "MCVFXAssetBuilder.h"
#include "NiagaraSystem.h"
#if WITH_EDITOR
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_MeshIndex.h"
#include "NiagaraMeshRendererProperties.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#endif

UNiagaraSystem* UMCVFXAssetBuilder::CreateBrushFoam()
{
#if WITH_EDITOR
    const FString Path=TEXT("/Game/Gameplay/VFX/NS_BrushFoam");
    if(FPackageName::DoesPackageExist(Path)) return LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".NS_BrushFoam")));
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    if(!Template) return nullptr;
    auto* Package=CreatePackage(*Path);
    auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_BrushFoam"));
    System->SetFlags(RF_Public|RF_Standalone); System->ClearFlags(RF_Transient);
    if(System->GetNumEmitters()!=1) return nullptr;
    auto& Handle=System->GetEmitterHandle(0); auto* Emitter=Handle.GetStatelessEmitter(); if(!Emitter) return nullptr;
    Handle.SetName(TEXT("ContactFoam"),*System);
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
        auto* Spawn=Emitter->GetSpawnInfoByIndex(I); Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;
        Spawn->Rate=FNiagaraDistributionRangeFloat(65.f); Spawn->bEnabled=true;
    }
    auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
    auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
    auto* Scale=Cast<UNiagaraStatelessModule_ScaleMeshSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass()));
    if(!Init || !Velocity || !Shape || !Gravity || !Scale) return nullptr;
    Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(.35f,.85f);
    Init->MeshScaleDistribution.InitConstant(FVector3f(.055f));
    Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor(.83f,.96f,1.f));
    Velocity->SetIsModuleEnabled(true); Velocity->VelocityType=ENSM_VelocityType::InCone;
    Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(14.f,36.f); Velocity->ConeAngle=68;
    Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;
    Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector); Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local;
    Shape->SetIsModuleEnabled(true); Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere;
    Shape->SphereRadius=FNiagaraDistributionRangeFloat(0.f,10.f);
    Gravity->SetIsModuleEnabled(true); Gravity->GravityDistribution.InitConstant(FVector3f(0,0,-42));
    Scale->SetIsModuleEnabled(true); Scale->ScaleDistribution=FNiagaraDistributionVector3({.35f,1.f,1.f,.8f,0.f});
    if(auto* SpriteScale=Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass())) SpriteScale->SetIsModuleEnabled(false);
    const auto Renderers=Emitter->GetRenderers(); for(auto* Renderer:Renderers) Emitter->RemoveRenderer(Renderer,FGuid());
    auto* Renderer=NewObject<UNiagaraMeshRendererProperties>(Emitter,TEXT("FoamBubbles"),RF_Transactional);
    // InitBindings() adds an empty slot zero; particles select zero by default.
    Renderer->Meshes.Reset();
    FNiagaraMeshRendererMeshProperties Mesh; Mesh.Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")); Renderer->Meshes.Add(Mesh);
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushFoam.M_BrushFoam"));
    if(!Material) return nullptr;
    Material->SetMaterialUsage(MATUSAGE_NiagaraMeshParticles);
    Material->PostEditChange(); Material->MarkPackageDirty();
    Renderer->bOverrideMaterials=true; FNiagaraMeshMaterialOverride Override; Override.ExplicitMat=Material; Renderer->OverrideMaterials.Add(Override);
    Emitter->AddRenderer(Renderer,FGuid()); Emitter->PostEditChange();
    System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(false,false);
    FAssetRegistryModule::AssetCreated(System); System->MarkPackageDirty();
    FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone; Save.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(Package,System,*Filename,Save)) return nullptr;
    const FString MaterialFilename=FPackageName::LongPackageNameToFilename(Material->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(Material->GetOutermost(),Material,*MaterialFilename,Save)) return nullptr;
    UE_LOG(LogTemp,Display,TEXT("MC_FOAM_CREATED %s emitters=%d"),*System->GetPathName(),System->GetNumEmitters());
    return System;
#else
    return nullptr;
#endif
}

UNiagaraSystem* UMCVFXAssetBuilder::CreateSprayMist()
{
#if WITH_EDITOR
    const FString Path=TEXT("/Game/Gameplay/VFX/NS_SprayMist");
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    if(!Template) return nullptr;
    auto* Package=CreatePackage(*Path);
    auto* System=LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".NS_SprayMist")));
    const bool Created=System==nullptr;
    if(!System) System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_SprayMist"));
    System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
    if(System->GetNumEmitters()!=1) return nullptr;
    auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter();if(!Emitter) return nullptr;
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {auto* Spawn=Emitter->GetSpawnInfoByIndex(I);Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;Spawn->Rate=FNiagaraDistributionRangeFloat(260);Spawn->bEnabled=true;Spawn->bLoopCountLimitEnabled=false;Spawn->bSpawnProbabilityEnabled=false;}
    auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    if(!Init) return nullptr;
    Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(.30f,.42f);
    Init->MeshScaleDistribution.Mode=ENiagaraDistributionMode::UniformRange;
    Init->MeshScaleDistribution.Min=FVector3f(.022f);Init->MeshScaleDistribution.Max=FVector3f(.034f);
    Init->MeshScaleDistribution.ChannelConstantsAndRanges={.022f,.034f};
    Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor(.64f,.93f,1.f));
    auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    if(!Velocity) return nullptr;
    Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::InCone;
    Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(550,720);Velocity->ConeVelocityScale=FNiagaraDistributionRangeFloat(1);Velocity->ConeAngle=16;Velocity->InnerCone=0;
    Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local;
    auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
    if(!Shape) return nullptr;
    Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere;Shape->SphereRadius=FNiagaraDistributionRangeFloat(0,2.5f);
    auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
    if(!Gravity) return nullptr;
    Gravity->SetIsModuleEnabled(true);Gravity->GravityDistribution.InitConstant(FVector3f(0,0,-110));
    auto* Scale=Cast<UNiagaraStatelessModule_ScaleMeshSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass()));
    if(!Scale) return nullptr;
    Scale->SetIsModuleEnabled(true);Scale->ScaleDistribution=FNiagaraDistributionVector3({.4f,.8f,1.f,1.15f,0.f});
    if(auto* Sprite=Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass())) Sprite->SetIsModuleEnabled(false);
    const auto OldRenderers=Emitter->GetRenderers();
    for(auto* Renderer:OldRenderers) Emitter->RemoveRenderer(Renderer,FGuid());
    auto* Renderer=NewObject<UNiagaraMeshRendererProperties>(Emitter,TEXT("MistDroplets"),RF_Transactional);
    // An appended sphere otherwise becomes slot one behind the default empty slot.
    Renderer->Meshes.Reset();
    FNiagaraMeshRendererMeshProperties Mesh;Mesh.Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));Renderer->Meshes.Add(Mesh);
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushFoam.M_BrushFoam"));if(!Material) return nullptr;
    Material->SetMaterialUsage(MATUSAGE_NiagaraMeshParticles);Material->PostEditChange();
    Renderer->bOverrideMaterials=true;FNiagaraMeshMaterialOverride Override;Override.ExplicitMat=Material;Renderer->OverrideMaterials.Add(Override);
    Renderer->PostEditChange();Emitter->AddRenderer(Renderer,FGuid());Emitter->PostEditChange();
    // Stateless emitters use fixed bounds, so the fountain template's bounds must
    // contain the full rotated 720 cm/s * .42 s spray rather than just its nozzle.
    System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-70,-70,-30),FVector(70,70,340)));
    System->PostEditChange();
    System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
    if(Created) FAssetRegistryModule::AssetCreated(System);Package->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args)) return nullptr;
    const FString MaterialFilename=FPackageName::LongPackageNameToFilename(Material->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(Material->GetOutermost(),Material,*MaterialFilename,Args)) return nullptr;
    UE_LOG(LogTemp,Display,TEXT("MC_SPRAY_CREATED %s meshSlots=%d firstMesh=%s rate=260 cone=16"),*System->GetPathName(),Renderer->Meshes.Num(),*GetNameSafe(Renderer->Meshes[0].Mesh));
    return System;
#else
    return nullptr;
#endif
}

int32 UMCVFXAssetBuilder::RepairMeshRendererSlots(UNiagaraSystem* System)
{
#if WITH_EDITOR
    if(!System) return -1;
    int32 Repaired=0;
    for(int32 I=0;I<System->GetNumEmitters();++I) {
        auto* Emitter=System->GetEmitterHandle(I).GetStatelessEmitter();if(!Emitter) continue;
        // Keep intentional mesh selection and every saved artist parameter.
        if(const auto* Index=Emitter->GetModule<UNiagaraStatelessModule_MeshIndex>();Index && Index->IsModuleEnabled()) {
            if(Index->MeshIndex.IsBinding() || Index->MeshIndex.IsExpression()) continue;
            const auto Range=Index->MeshIndex.CalculateRange();
            if(Range.Min!=0 || Range.Max!=0) continue;
        }
        bool Changed=false;
        for(auto* Base:Emitter->GetRenderers()) if(auto* Renderer=Cast<UNiagaraMeshRendererProperties>(Base)) {
            if(Renderer->Meshes.Num()!=2 || Renderer->Meshes[0].Mesh || !Renderer->Meshes[1].Mesh) continue;
            if(Renderer->Meshes[0].MeshParameterBinding.ResolvedParameter.IsValid()) continue;
            Renderer->Modify();Renderer->Meshes.RemoveAt(0);Renderer->PostEditChange();Changed=true;++Repaired;
        }
        if(Changed) Emitter->PostEditChange();
    }
    if(Repaired>0) {
        System->PostEditChange();System->RequestCompile(true);System->WaitForCompilationComplete(false,false);System->MarkPackageDirty();
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        const FString Filename=FPackageName::LongPackageNameToFilename(System->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if(!UPackage::SavePackage(System->GetOutermost(),System,*Filename,Args)) return -1;
    }
    UE_LOG(LogTemp,Display,TEXT("MC_VFX_MESH_SLOTS_REPAIRED %s renderers=%d"),*System->GetPathName(),Repaired);
    return Repaired;
#else
    return -1;
#endif
}
UNiagaraSystem* UMCVFXAssetBuilder::CreateIceShatter()
{
#if WITH_EDITOR
    const FString Path=TEXT("/Game/Gameplay/VFX/NS_IceShatter");
    if(FPackageName::DoesPackageExist(Path)) return LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".NS_IceShatter")));
    auto* Template=CreateBrushFoam(); if(!Template) return nullptr;
    auto* Package=CreatePackage(*Path); auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_IceShatter"));
    System->SetFlags(RF_Public|RF_Standalone); auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter(); if(!Emitter) return nullptr;
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) { auto* Spawn=Emitter->GetSpawnInfoByIndex(I); Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst; Spawn->SpawnTime=0; Spawn->Amount=FNiagaraDistributionRangeInt(40); Spawn->bLoopCountLimitEnabled=true; Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1); }
    auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(.55f,1.25f); Init->MeshScaleDistribution.InitConstant(FVector3f(.06f,.11f,.035f));
    auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(160,430); Velocity->ConeAngle=82;
    Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()))->GravityDistribution.InitConstant(FVector3f(0,0,-600));
    for(auto* R:Emitter->GetRenderers()) if(auto* Mesh=Cast<UNiagaraMeshRendererProperties>(R)) {
        Mesh->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        auto* Mat=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Cold/M_StylizedIce.M_StylizedIce")); if(!Mat) return nullptr;
        Mat->SetMaterialUsage(MATUSAGE_NiagaraMeshParticles); Mat->PostEditChange(); Mesh->OverrideMaterials[0].ExplicitMat=Mat;
    }
    Emitter->PostEditChange(); System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(false,false);
    FAssetRegistryModule::AssetCreated(System); System->MarkPackageDirty(); FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return nullptr;
    return System;
#else
    return nullptr;
#endif
}
