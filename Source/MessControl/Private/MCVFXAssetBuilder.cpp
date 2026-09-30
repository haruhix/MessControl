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
