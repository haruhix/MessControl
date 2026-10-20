#include "MCFrostNovaAssetLibrary.h"

#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_DynamicMaterialParameters.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "MaterialShaderPrecompileMode.h"
#include "ShaderCompiler.h"
#include "RHIGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"

namespace
{
const TCHAR* Folder=TEXT("/Game/Gameplay/Cold/VFX/FrostNova/");

template<typename T> T* Module(UNiagaraStatelessEmitter* Emitter)
{
    return Cast<T>(Emitter->GetModule(T::StaticClass()));
}
}
#endif

bool UMCFrostNovaAssetLibrary::AuthorAssets(bool bRebuildOwned)
{
#if WITH_EDITOR
    const FString Path=FString(Folder)+TEXT("NS_FrostNovaBurst");
    UNiagaraSystem* System=LoadObject<UNiagaraSystem>(nullptr,*(Path+TEXT(".NS_FrostNovaBurst")),nullptr,LOAD_NoWarn);
    if(!System && FPackageName::DoesPackageExist(Path))
    {
        UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_REBUILD_REFUSED unreadableExistingPackage=%s"),*Path);
        return false;
    }
    const bool bCreated=System==nullptr;
    if(System && !bRebuildOwned) return true;
    if(System)
    {
        const FString OwnerVersion=System->GetOutermost()->GetMetaData().GetValue(System,TEXT("MC.FrostNova.Authoring"));
        // Version 1 predates the system tag. Migrate only its exact generated
        // two-emitter signature; never rebuild arbitrary artist-authored art.
        const bool bLegacyOwned=OwnerVersion.IsEmpty() && System->GetNumEmitters()==2
            && System->GetEmitterHandle(0).GetName()==FName(TEXT("ShortPowderBurst"))
            && System->GetEmitterHandle(1).GetName()==FName(TEXT("IceSparkles"))
            && System->GetEmitterHandle(0).GetStatelessEmitter()
            && System->GetEmitterHandle(1).GetStatelessEmitter();
        if(OwnerVersion!=TEXT("1") && OwnerVersion!=TEXT("2") && !bLegacyOwned)
        {
            UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_REBUILD_REFUSED system=%s ownerVersion=%s"),*Path,*OwnerVersion);
            return false;
        }
        System->Modify();
    }
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    auto* Powder=LoadObject<UMaterial>(nullptr,*(FString(Folder)+TEXT("M_FrostNovaPowder.M_FrostNovaPowder")));
    auto* Snow=LoadObject<UMaterial>(nullptr,*(FString(Folder)+TEXT("M_FrostNovaSnow.M_FrostNovaSnow")));
    if(!Template || !Powder || !Snow || Template->GetNumEmitters()!=1) return false;
    UPackage* Package=System?System->GetOutermost():CreatePackage(*Path);
    if(!System)
    {
        System=DuplicateObject<UNiagaraSystem>(Template,Package,TEXT("NS_FrostNovaBurst"));
        System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
        // Duplicate before mutating either emitter; the array may reallocate.
        const FNiagaraEmitterHandle Original=System->GetEmitterHandle(0);
        System->DuplicateEmitterHandle(Original,TEXT("IceSparkles"));
    }
    if(System->GetNumEmitters()!=2) return false;
    const FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.ParticleColor"));
    System->GetExposedParameters().SetParameterValue(FLinearColor::White,Color,true);
    for(int32 Index=0;Index<2;++Index)
    {
        const bool bPowder=Index==0;
        auto& Handle=System->GetEmitterHandle(Index);
        Handle.SetIsEnabled(true,*System,false);
        Handle.SetName(bPowder?TEXT("ShortPowderBurst"):TEXT("IceSparkles"),*System);
        auto* Emitter=Handle.GetStatelessEmitter();
        if(!Emitter) return false;
        const FStructProperty* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
        if(!StateProperty) return false;
        auto* State=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);
        *State=FNiagaraEmitterStateData();
        State->LoopBehavior=ENiagaraLoopBehavior::Once;
        State->LoopDurationMode=ENiagaraLoopDurationMode::Fixed;
        State->LoopDuration=FNiagaraDistributionRangeFloat(1.f);
        auto* Init=Module<UNiagaraStatelessModule_InitializeParticle>(Emitter);
        auto* Shape=Module<UNiagaraStatelessModule_ShapeLocation>(Emitter);
        auto* Velocity=Module<UNiagaraStatelessModule_AddVelocity>(Emitter);
        auto* Gravity=Module<UNiagaraStatelessModule_GravityForce>(Emitter);
        auto* Dynamic=Module<UNiagaraStatelessModule_DynamicMaterialParameters>(Emitter);
        if(!Init || !Shape || !Velocity || !Gravity || !Dynamic) return false;
        const int32 Count=bPowder?16:28;
        if(!Emitter->GetNumSpawnInfos()) Emitter->AddSpawnInfo();
        for(int32 SpawnIndex=0;SpawnIndex<Emitter->GetNumSpawnInfos();++SpawnIndex)
        {
            auto* Spawn=Emitter->GetSpawnInfoByIndex(SpawnIndex);
            Spawn->bEnabled=SpawnIndex==0;
            Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->SpawnTime=0;
            Spawn->Amount=FNiagaraDistributionRangeInt(Count);
            Spawn->bSpawnProbabilityEnabled=false;
            Spawn->bLoopCountLimitEnabled=true;
            Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1);
        }
        Init->LifetimeDistribution=bPowder?FNiagaraDistributionRangeFloat(.46f,.78f):FNiagaraDistributionRangeFloat(.24f,.55f);
        Init->InitialPositionDistribution=FNiagaraDistributionPosition(FVector3f::ZeroVector);
        Init->SpriteSizeDistribution.InitConstant(bPowder?FVector2f(180,150):FVector2f(7,25));
        Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(-180,180);
        Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
        Init->ColorDistribution.Mode=ENiagaraDistributionMode::Binding;
        Init->ColorDistribution.ParameterBinding=Color;
        Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Ring;
        Shape->CoordinateSpace=ENiagaraCoordinateSpace::Local;
        // Start outside the solid candy silhouette; the former 0..32cm sphere
        // placed almost all early powder inside its ~67cm radius.
        Shape->RingRadius=bPowder?FNiagaraDistributionRangeFloat(95.f,130.f):FNiagaraDistributionRangeFloat(75.f,105.f);
        Shape->DiscCoverage=FNiagaraDistributionRangeFloat(0.f);
        Shape->TransformOffset=FNiagaraDistributionPosition(FVector3f::ZeroVector);
        Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::InCone;
        Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local;
        Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;
        Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
        Velocity->ConeAngle=160;
        Velocity->ConeVelocityDistribution=bPowder?FNiagaraDistributionRangeFloat(170,300):FNiagaraDistributionRangeFloat(420,700);
        Velocity->ConeVelocityScale=FNiagaraDistributionRangeFloat(1.f);
        Gravity->SetIsModuleEnabled(true);
        Gravity->GravityDistribution.InitConstant(FVector3f(0,0,bPowder?-100.f:-600.f));
        if(auto* Drag=Module<UNiagaraStatelessModule_Drag>(Emitter))
        {
            Drag->SetIsModuleEnabled(true);Drag->DragDistribution=FNiagaraDistributionRangeFloat(bPowder?2.f:.5f);
        }
        if(auto* Curl=Module<UNiagaraStatelessModule_CurlNoiseForce>(Emitter))
        {
            Curl->SetIsModuleEnabled(bPowder);Curl->NoiseStrength=8;Curl->NoiseFrequency=.025f;
        }
        if(auto* MeshScale=Module<UNiagaraStatelessModule_ScaleMeshSize>(Emitter)) MeshScale->SetIsModuleEnabled(false);
        if(auto* ColorScale=Module<UNiagaraStatelessModule_ScaleColor>(Emitter))
        {
            ColorScale->SetIsModuleEnabled(true);ColorScale->ScaleDistribution.InitConstant(FLinearColor::White);
        }
        if(auto* Scale=Module<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter))
        {
            Scale->SetIsModuleEnabled(true);
            auto& Curve=Scale->ScaleDistribution;
            Curve.Mode=ENiagaraDistributionMode::UniformCurve;
            Curve.ChannelCurves.SetNum(1);Curve.ChannelCurves[0].Reset();
            Curve.ChannelCurves[0].AddKey(0,bPowder?.55f:.8f);
            Curve.ChannelCurves[0].AddKey(.15f,1.f);
            Curve.ChannelCurves[0].AddKey(1,bPowder?1.45f:.35f);
            Curve.UpdateValuesFromDistribution();
        }
        Dynamic->SetIsModuleEnabled(true);
        Dynamic->bParameter0Enabled=true;
        Dynamic->bParameter1Enabled=Dynamic->bParameter2Enabled=Dynamic->bParameter3Enabled=false;
        Dynamic->Parameter0.bXChannelEnabled=true;
        Dynamic->Parameter0.bYChannelEnabled=Dynamic->Parameter0.bZChannelEnabled=Dynamic->Parameter0.bWChannelEnabled=false;
        Dynamic->Parameter0.XChannelDistribution=FNiagaraDistributionFloat({0.f,1.f});
        check(Dynamic->Parameter0.XChannelDistribution.Mode==ENiagaraDistributionMode::UniformCurve);
        const auto Renderers=Emitter->GetRenderers();
        for(auto* Renderer:Renderers) Emitter->RemoveRenderer(Renderer,FGuid());
        auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,NAME_None,RF_Transactional);
        Renderer->Material=bPowder?Powder:Snow;
        if(Renderer->PositionBinding.GetParamMapBindableVariable().GetName().IsNone()
            || Renderer->DynamicMaterialBinding.GetParamMapBindableVariable().GetName().IsNone())
        {
            UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_RENDERER_BINDINGS_NOT_READY emitter=%d"),Index);
            return false;
        }
        Emitter->AddRenderer(Renderer,FGuid());Emitter->PostEditChange();
    }
    // Local burst bounds only: the expanding front is separate native geometry.
    System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-800,-800,-160),FVector(800,800,450)));
    System->SetWarmupTime(0);System->PostEditChange();
    System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
    if(!System->IsReadyToRun()) return false;
    if(bCreated) FAssetRegistryModule::AssetCreated(System);
    Package->GetMetaData().SetValue(System,TEXT("MC.FrostNova.Authoring"),TEXT("2"));
    System->MarkPackageDirty();
    FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;Save.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return false;
    UE_LOG(LogTemp,Display,TEXT("MC_FROST_NOVA_AUTHORED path=%s version=2 rebuilt=%d emitters=2 burstParticles=44 maxLifetime=.78 ageMode=UniformCurve powderSpawnRing=95..130"),*Path,!bCreated);
    return true;
#else
    return false;
#endif
}

bool UMCFrostNovaAssetLibrary::WaitForMaterialShaders()
{
#if WITH_EDITOR
    const TCHAR* Names[]={TEXT("M_FrostNovaFront"),TEXT("M_FrostNovaCrystal"),TEXT("M_FrostNovaPowder"),TEXT("M_FrostNovaSnow")};
    TArray<UMaterial*> Materials;
    for(const TCHAR* Name:Names)
    {
        const FString Path=FString(Folder)+Name+TEXT(".")+Name;
        auto* Material=LoadObject<UMaterial>(nullptr,*Path);
        if(!Material)
        {
            UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_SHADER_CHECK missingMaterial=%s"),*Path);
            return false;
        }
        Materials.Add(Material);
    }
    const ERHIFeatureLevel::Type ActiveFeatureLevel=GMaxRHIFeatureLevel;
    const EShaderPlatform ActivePlatform=GShaderPlatformForFeatureLevel[ActiveFeatureLevel];
    if(ActivePlatform==SP_NumPlatforms)
    {
        UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_SHADER_CHECK activePlatformUnavailable feature=%d"),static_cast<int32>(ActiveFeatureLevel));
        return false;
    }
    // In UE 5.8 PostLoad may cache a material with PrecompileMode::None.
    // FinishAllCompilation alone cannot wait for jobs that were never submitted.
    // This is an explicit editor-authoring check for these four assets only;
    // keep their shader IDs and cached results rather than forcing a recompile.
    for(UMaterial* Material:Materials)
    {
        Material->ConditionalPostLoad();
        // An unused material in an idle editor may have no required feature
        // levels. Both CacheShaders and EnsureIsComplete would then do nothing.
        Material->SetFeatureLevelToCompile(ActiveFeatureLevel,true);
        UE_LOG(LogTemp,Display,TEXT("MC_FROST_NOVA_SHADER_REQUEST material=%s feature=%d platform=%d requestedFeatureBit=%u"),
            *Material->GetPathName(),static_cast<int32>(ActiveFeatureLevel),static_cast<int32>(ActivePlatform),
            1u<<ActiveFeatureLevel);
        Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
        Material->EnsureIsComplete();
    }
    if(GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    bool bAllReady=true;
    for(UMaterial* Material:Materials)
    {
        const FMaterialResource* Resource=Material->GetMaterialResource(ActivePlatform);
        const FMaterialShaderMap* Map=Resource?Resource->GetGameThreadShaderMap():nullptr;
        const bool bComplete=Resource && Resource->IsGameThreadShaderMapComplete();
        const int32 ErrorCount=Resource?Resource->GetCompileErrors().Num():0;
        // Cached/on-demand shader maps in 5.8 can have complete rendering
        // content without the legacy live-compilation bookkeeping flags.
        const bool bReady=bComplete && Map && ErrorCount==0 && !Material->IsCompiling();
        UE_LOG(LogTemp,Display,TEXT("MC_FROST_NOVA_SHADER_CHECK material=%s resource=%d platform=%d quality=%d complete=%d shaderMap=%d finalized=%d success=%d compiling=%d remainingJobs=%d errors=%d ready=%d"),
            *Material->GetPathName(),Resource!=nullptr,static_cast<int32>(ActivePlatform),
            Resource?static_cast<int32>(Resource->GetQualityLevel()):-1,bComplete,Map!=nullptr,Map && Map->IsCompilationFinalized(),
            Map && Map->CompiledSuccessfully(),Material->IsCompiling(),
            GShaderCompilingManager?GShaderCompilingManager->GetNumRemainingJobs():0,ErrorCount,bReady);
        if(Resource)
        {
            for(const FString& Error:Resource->GetCompileErrors())
                UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_SHADER_ERROR material=%s error=%s"),*Material->GetPathName(),*Error);
        }
        if(!bReady)
            UE_LOG(LogTemp,Error,TEXT("MC_FROST_NOVA_SHADER_CHECK_NOT_READY material=%s (no fallback accepted)"),*Material->GetPathName());
        bAllReady&=bReady;
    }
    return bAllReady;
#else
    return false;
#endif
}
