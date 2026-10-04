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
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_DynamicMaterialParameters.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#endif

UNiagaraSystem* UMCVFXAssetBuilder::CreateAmbientParticles()
{
#if WITH_EDITOR
    const FString Path=TEXT("/Game/Gameplay/VFX/Ambient/NS_AmbientParticles");
    if(FPackageName::DoesPackageExist(Path))
        return LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".NS_AmbientParticles")));
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    auto* Material=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/VFX/Ambient/M_AmbientParticle.M_AmbientParticle"));
    if(!Template || !Material) return nullptr;
    auto* Package=CreatePackage(*Path);
    auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_AmbientParticles"));
    System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
    if(System->GetNumEmitters()!=1) return nullptr;
    auto& Handle=System->GetEmitterHandle(0);
    auto* Emitter=Handle.GetStatelessEmitter();if(!Emitter) return nullptr;
    auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
    auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    auto* Curl=Cast<UNiagaraStatelessModule_CurlNoiseForce>(Emitter->GetModule(UNiagaraStatelessModule_CurlNoiseForce::StaticClass()));
    auto* Drag=Cast<UNiagaraStatelessModule_Drag>(Emitter->GetModule(UNiagaraStatelessModule_Drag::StaticClass()));
    auto* SpriteScale=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()));
    if(!Init || !Shape || !Velocity || !Curl || !Drag || !SpriteScale) return nullptr;
    Handle.SetName(TEXT("FloatingMotes"),*System);

    const FNiagaraVariable Rate(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.SpawnRate"));
    const FNiagaraVariable Size(FNiagaraTypeDefinition::GetVec2Def(),TEXT("User.ParticleSize"));
    const FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.ParticleColor"));
    const FNiagaraVariable Volume(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.VolumeSize"));
    const FNiagaraVariable Drift(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.DriftVelocity"));
    auto& Parameters=System->GetExposedParameters();
    Parameters.SetParameterValue(40.f,Rate,true);
    Parameters.SetParameterValue(FVector2f(3.f),Size,true);
    Parameters.SetParameterValue(FLinearColor(.82f,.9f,1.f,.45f),Color,true);
    Parameters.SetParameterValue(FVector3f(3400,2500,1000),Volume,true);
    Parameters.SetParameterValue(FVector3f(4,-2,5),Drift,true);
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
        auto* Spawn=Emitter->GetSpawnInfoByIndex(I);
        Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;
        Spawn->bEnabled=true;Spawn->bLoopCountLimitEnabled=false;Spawn->bSpawnProbabilityEnabled=false;
        Spawn->Rate=FNiagaraDistributionRangeFloat(40.f);
        Spawn->Rate.Mode=ENiagaraDistributionMode::Binding;Spawn->Rate.ParameterBinding=Rate;
    }
    Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(20,30);
    Init->SpriteSizeDistribution.InitConstant(FVector2f(3.f));
    Init->SpriteSizeDistribution.Mode=ENiagaraDistributionMode::Binding;
    Init->SpriteSizeDistribution.ParameterBinding=Size;
    Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
    Init->ColorDistribution.Mode=ENiagaraDistributionMode::Binding;Init->ColorDistribution.ParameterBinding=Color;
    Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Box;
    Shape->bBoxSurfaceOnly=false;Shape->BoxSize.InitConstant(FVector3f(3400,2500,1000));
    Shape->BoxSize.Mode=ENiagaraDistributionMode::Binding;Shape->BoxSize.ParameterBinding=Volume;
    Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::Linear;
    Velocity->LinearVelocityDistribution.InitConstant(FVector3f(4,-2,5));
    Velocity->LinearVelocityDistribution.Mode=ENiagaraDistributionMode::Binding;
    Velocity->LinearVelocityDistribution.ParameterBinding=Drift;
    Curl->SetIsModuleEnabled(true);Curl->NoiseStrength=4;Curl->NoiseFrequency=.008f;
    Drag->SetIsModuleEnabled(true);Drag->DragDistribution=FNiagaraDistributionRangeFloat(.15f);
    SpriteScale->SetIsModuleEnabled(true);SpriteScale->ScaleDistribution.InitConstant(FVector2f(1.f));
    if(auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()))) Gravity->SetIsModuleEnabled(false);
    if(auto* MeshScale=Cast<UNiagaraStatelessModule_ScaleMeshSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass()))) MeshScale->SetIsModuleEnabled(false);
    const auto Renderers=Emitter->GetRenderers();
    for(auto* Renderer:Renderers) Emitter->RemoveRenderer(Renderer,FGuid());
    auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,TEXT("SoftMotes"),RF_Transactional);
    Renderer->Material=Material;Emitter->AddRenderer(Renderer,FGuid());
    // Allow artists to enlarge the volume without template-sized culling bounds.
    const FBox Bounds(FVector(-5000,-5000,-2500),FVector(5000,5000,2500));
    System->bFixedBounds=true;System->SetFixedBounds(Bounds);
    System->SetWarmupTickDelta(1.f/15.f);System->SetWarmupTime(25.f);
    Emitter->PostEditChange();System->PostEditChange();
    System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
    FAssetRegistryModule::AssetCreated(System);System->MarkPackageDirty();
    FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;Save.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return nullptr;
    return System;
#else
    return nullptr;
#endif
}

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

UNiagaraSystem* UMCVFXAssetBuilder::RefineBrushFoam()
{
#if WITH_EDITOR
    auto* System=CreateBrushFoam();
    auto* FoamMaterial=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushFoam.M_BrushFoam"));
    auto* BubbleMaterial=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Care/M_BrushBubble.M_BrushBubble"));
    if(!System || !FoamMaterial || !BubbleMaterial || System->GetNumEmitters()<1 || System->GetNumEmitters()>2) return nullptr;
    // Preserve other artist emitters rather than silently deleting them.
    for(int32 I=0;I<System->GetNumEmitters();++I) {
        const auto& Handle=System->GetEmitterHandle(I);
        if(!Handle.GetStatelessEmitter() || (Handle.GetName()!=TEXT("ContactFoam") && Handle.GetName()!=TEXT("FloatingBubbles"))) return nullptr;
    }
    if(System->GetNumEmitters()==1) {
        // DuplicateEmitterHandle grows the handle array, then reads its argument again.
        const FNiagaraEmitterHandle Source=System->GetEmitterHandle(0);
        System->DuplicateEmitterHandle(Source,TEXT("FloatingBubbles"));
        System->GetEmitterHandle(1).SetName(TEXT("FloatingBubbles"),*System);
    }
    System->Modify();
    for(int32 I=0;I<System->GetNumEmitters();++I) {
        auto& Handle=System->GetEmitterHandle(I);
        const bool Bubbles=Handle.GetName()==TEXT("FloatingBubbles");
        auto* Emitter=Handle.GetStatelessEmitter();
        auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
        auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
        auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
        auto* SpriteScale=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()));
        auto* ColorScale=Cast<UNiagaraStatelessModule_ScaleColor>(Emitter->GetModule(UNiagaraStatelessModule_ScaleColor::StaticClass()));
        auto* Dynamic=Cast<UNiagaraStatelessModule_DynamicMaterialParameters>(Emitter->GetModule(UNiagaraStatelessModule_DynamicMaterialParameters::StaticClass()));
        if(!Init || !Shape || !Velocity || !SpriteScale || !ColorScale || !Dynamic) return nullptr;
        Emitter->Modify();
        Handle.SetIsEnabled(true,*System,false);
        if(!Emitter->GetNumSpawnInfos()) Emitter->AddSpawnInfo();
        for(int32 SpawnIndex=0;SpawnIndex<Emitter->GetNumSpawnInfos();++SpawnIndex) {
            auto* Spawn=Emitter->GetSpawnInfoByIndex(SpawnIndex);
            Spawn->bEnabled=SpawnIndex==0; Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;
            Spawn->Rate=FNiagaraDistributionRangeFloat(Bubbles?18.f:48.f);
            Spawn->bLoopCountLimitEnabled=false; Spawn->bSpawnProbabilityEnabled=false;
        }
        Init->LifetimeDistribution=Bubbles?FNiagaraDistributionRangeFloat(.55f,.95f):FNiagaraDistributionRangeFloat(.35f,.55f);
        const float MinimumSize=Bubbles?5.f:4.f,MaximumSize=Bubbles?9.f:8.f;
        Init->SpriteSizeDistribution.InitConstant(FVector2f(MinimumSize));
        Init->SpriteSizeDistribution.Mode=ENiagaraDistributionMode::UniformRange;
        Init->SpriteSizeDistribution.Min=FVector2f(MinimumSize); Init->SpriteSizeDistribution.Max=FVector2f(MaximumSize);
        Init->SpriteSizeDistribution.ChannelConstantsAndRanges={MinimumSize,MaximumSize};
        Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(-28.f,28.f);
        Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
        Init->InitialPositionDistribution=FNiagaraDistributionPosition(FVector3f(0,0,Bubbles?5.f:3.f));
        Shape->SetIsModuleEnabled(true); Shape->ShapePrimitive=ENSM_ShapePrimitive::Plane;
        Shape->PlaneSize.InitConstant(Bubbles?FVector2f(10,6):FVector2f(14,7));
        Shape->bPlaneEdgesOnly=false; Shape->CoordinateSpace=ENiagaraCoordinateSpace::Local;
        Velocity->SetIsModuleEnabled(true); Velocity->VelocityType=ENSM_VelocityType::InCone;
        Velocity->ConeVelocityDistribution=Bubbles?FNiagaraDistributionRangeFloat(12.f,24.f):FNiagaraDistributionRangeFloat(3.f,9.f);
        Velocity->ConeAngle=Bubbles?24.f:70.f; Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;
        Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
        // Stateless particles remain near the moving contact; world-up velocity
        // keeps the small rising layer useful on both crowns and the tongue.
        Velocity->CoordinateSpace=Bubbles?ENiagaraCoordinateSpace::World:ENiagaraCoordinateSpace::Local;
        for(const auto& Module:Emitter->GetModules()) {
            if(Module->IsA<UNiagaraStatelessModule_GravityForce>() || Module->IsA<UNiagaraStatelessModule_CurlNoiseForce>()
                || Module->IsA<UNiagaraStatelessModule_ScaleMeshSize>()) Module->SetIsModuleEnabled(false);
        }
        if(auto* Drag=Cast<UNiagaraStatelessModule_Drag>(Emitter->GetModule(UNiagaraStatelessModule_Drag::StaticClass()))) {
            Drag->SetIsModuleEnabled(true); Drag->DragDistribution=FNiagaraDistributionRangeFloat(Bubbles?.18f:.6f);
        }
        SpriteScale->SetIsModuleEnabled(true);
        auto& SizeCurve=SpriteScale->ScaleDistribution;
        SizeCurve.Mode=ENiagaraDistributionMode::UniformCurve; SizeCurve.ChannelCurves.SetNum(1);
        SizeCurve.ChannelCurves[0].Reset();
        SizeCurve.ChannelCurves[0].AddKey(0,.55f); SizeCurve.ChannelCurves[0].AddKey(.2f,.95f);
        SizeCurve.ChannelCurves[0].AddKey(.7f,Bubbles?1.12f:1.f); SizeCurve.ChannelCurves[0].AddKey(1,Bubbles?1.3f:.65f);
        SizeCurve.UpdateValuesFromDistribution();
        ColorScale->SetIsModuleEnabled(true);
        auto& Fade=ColorScale->ScaleDistribution;
        Fade.Mode=ENiagaraDistributionMode::NonUniformCurve; Fade.ChannelCurves.SetNum(4);
        for(int32 Channel=0;Channel<4;++Channel) {
            Fade.ChannelCurves[Channel].Reset();
            Fade.ChannelCurves[Channel].AddKey(0,Channel==3?0.f:1.f);
            Fade.ChannelCurves[Channel].AddKey(.1f,1.f); Fade.ChannelCurves[Channel].AddKey(.72f,Channel==3?.92f:1.f);
            Fade.ChannelCurves[Channel].AddKey(1,Channel==3?0.f:1.f);
        }
        Fade.UpdateValuesFromDistribution();
        // Lightweight templates do not export ParticleRelativeTime. Supply
        // normalized age explicitly so the saved material fades at every tier.
        Dynamic->SetIsModuleEnabled(true); Dynamic->bParameter0Enabled=true;
        Dynamic->bParameter1Enabled=false; Dynamic->bParameter2Enabled=false; Dynamic->bParameter3Enabled=false;
        Dynamic->Parameter0.bXChannelEnabled=true; Dynamic->Parameter0.bYChannelEnabled=false;
        Dynamic->Parameter0.bZChannelEnabled=false; Dynamic->Parameter0.bWChannelEnabled=false;
        Dynamic->Parameter0.XChannelDistribution=FNiagaraDistributionFloat({0.f,1.f});
        const auto OldRenderers=Emitter->GetRenderers();
        for(auto* Renderer:OldRenderers) Emitter->RemoveRenderer(Renderer,FGuid());
        auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,NAME_None,RF_Transactional);
        Renderer->Material=Bubbles?BubbleMaterial:FoamMaterial;
        Renderer->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
        Renderer->SortMode=ENiagaraSortMode::ViewDepth;
        Emitter->AddRenderer(Renderer,FGuid()); Emitter->PostEditChange();
    }
    System->bFixedBounds=true; System->SetFixedBounds(FBox(FVector(-100),FVector(100)));
    System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(false,false);
    System->MarkPackageDirty();
    FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone; Save.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(System->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(System->GetOutermost(),System,*Filename,Save)) return nullptr;
    UE_LOG(LogTemp,Display,TEXT("MC_FOAM_REFINED emitters=%d rate=48+18 maxLive=44 renderers=sprites"),System->GetNumEmitters());
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
        if(const auto* Index=Cast<UNiagaraStatelessModule_MeshIndex>(Emitter->GetModule(UNiagaraStatelessModule_MeshIndex::StaticClass()));Index && Index->IsModuleEnabled()) {
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
    // Ice must not inherit the saved brush asset's renderer or emitter count.
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight")); if(!Template) return nullptr;
    auto* Package=CreatePackage(*Path); auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_IceShatter"));
    System->SetFlags(RF_Public|RF_Standalone); auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter(); if(!Emitter) return nullptr;
    for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) { auto* Spawn=Emitter->GetSpawnInfoByIndex(I); Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst; Spawn->SpawnTime=0; Spawn->Amount=FNiagaraDistributionRangeInt(40); Spawn->bLoopCountLimitEnabled=true; Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1); }
    auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
    Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(.55f,1.25f); Init->MeshScaleDistribution.InitConstant(FVector3f(.06f,.11f,.035f));
    auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
    Velocity->SetIsModuleEnabled(true); Velocity->VelocityType=ENSM_VelocityType::InCone;
    Velocity->ConeRotationType=ENSM_ConeRotationType::Direction; Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
    Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local;
    Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(160,430); Velocity->ConeAngle=82;
    auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
    Gravity->SetIsModuleEnabled(true); Gravity->GravityDistribution.InitConstant(FVector3f(0,0,-600));
    auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
    Shape->SetIsModuleEnabled(true); Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere; Shape->SphereRadius=FNiagaraDistributionRangeFloat(0,10);
    auto* Scale=Cast<UNiagaraStatelessModule_ScaleMeshSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass()));
    Scale->SetIsModuleEnabled(true); Scale->ScaleDistribution=FNiagaraDistributionVector3({.35f,1.f,1.f,.8f,0.f});
    if(auto* SpriteScale=Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass())) SpriteScale->SetIsModuleEnabled(false);
    const auto OldRenderers=Emitter->GetRenderers(); for(auto* Renderer:OldRenderers) Emitter->RemoveRenderer(Renderer,FGuid());
    auto* Renderer=NewObject<UNiagaraMeshRendererProperties>(Emitter,NAME_None,RF_Transactional);
    Renderer->Meshes.Reset(); FNiagaraMeshRendererMeshProperties Cube;
    Cube.Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); Renderer->Meshes.Add(Cube);
    auto* Mat=LoadObject<UMaterial>(nullptr,TEXT("/Game/Gameplay/Cold/M_StylizedIce.M_StylizedIce")); if(!Mat) return nullptr;
    Mat->SetMaterialUsage(MATUSAGE_NiagaraMeshParticles); Mat->PostEditChange();
    Renderer->bOverrideMaterials=true; FNiagaraMeshMaterialOverride IceMaterial; IceMaterial.ExplicitMat=Mat;
    Renderer->OverrideMaterials.Add(IceMaterial); Emitter->AddRenderer(Renderer,FGuid());
    Emitter->PostEditChange(); System->PostEditChange(); System->RequestCompile(true); System->WaitForCompilationComplete(false,false);
    FAssetRegistryModule::AssetCreated(System); System->MarkPackageDirty(); FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return nullptr;
    return System;
#else
    return nullptr;
#endif
}
