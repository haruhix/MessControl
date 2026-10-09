#include "MCIceVFXAssetLibrary.h"
#include "NiagaraSystem.h"

#if WITH_EDITOR
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
#include "Stateless/Modules/NiagaraStatelessModule_InitialMeshOrientation.h"
#include "Stateless/Modules/NiagaraStatelessModule_MeshRotationRate.h"
#include "Stateless/Modules/NiagaraStatelessModule_DynamicMaterialParameters.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "RHIGlobals.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace MCIceVFXAuthoring
{
    struct FEffect
    {
        const TCHAR* Name;
        const TCHAR* Material;
        bool bLoop;
        bool bWind;
        bool bMesh;
        int32 Amount;
        float LifeMin, LifeMax;
        FVector2f Size;
        float SpeedMin, SpeedMax, Gravity, Radius, Drag;
    };

    template<typename T>
    T* Module(UNiagaraStatelessEmitter* Emitter)
    {
        return Cast<T>(Emitter->GetModule(T::StaticClass()));
    }

    template<typename T>
    void Bind(T& Distribution, const FNiagaraVariable& Parameter)
    {
        Distribution.Mode = ENiagaraDistributionMode::Binding;
        Distribution.ParameterBinding = Parameter;
    }
}
#endif

bool UMCIceVFXAssetLibrary::WaitForMaterialShaders()
{
#if WITH_EDITOR
    const TCHAR* Names[] = {
        TEXT("M_IceWindWisp"), TEXT("M_IceSnowStreak"), TEXT("M_IceImpactMist"),
        TEXT("M_IceChargeMote"), TEXT("M_IceWindSheet"), TEXT("M_IceShard")
    };
    TArray<UMaterial*> Materials;
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString(TEXT("/Game/Gameplay/Cold/VFX/")) + Name + TEXT(".") + Name;
        auto* Material = LoadObject<UMaterial>(nullptr, *Path);
        if (!Material) return false;
        Materials.Add(Material);
    }
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    for (UMaterial* Material : Materials)
    {
        const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
        if (!Resource || !Resource->IsGameThreadShaderMapComplete())
        {
            UE_LOG(LogTemp, Error, TEXT("MC_ICE_VFX_SHADER_FAIL incomplete material %s"), *Material->GetPathName());
            return false;
        }
        for (const FString& Error : Resource->GetCompileErrors())
            UE_LOG(LogTemp, Error, TEXT("MC_ICE_VFX_SHADER_FAIL %s: %s"), *Material->GetPathName(), *Error);
        if (!Resource->GetCompileErrors().IsEmpty()) return false;
    }
    UE_LOG(LogTemp, Display, TEXT("MC_ICE_VFX_SHADER_PASS materials=6 shader_platform=%d"), int32(GMaxRHIShaderPlatform));
    return true;
#else
    return false;
#endif
}

bool UMCIceVFXAssetLibrary::AuthorAssets()
{
#if WITH_EDITOR
    using namespace MCIceVFXAuthoring;
    auto* Template = LoadObject<UNiagaraSystem>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    auto* ShardMesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/Gameplay/Cold/VFX/SM_IceShard.SM_IceShard"));
    if (!Template || !ShardMesh || Template->GetNumEmitters() != 1)
    {
        UE_LOG(LogTemp, Error, TEXT("MC_ICE_VFX_AUTHOR_FAIL missing lightweight template or owned shard mesh"));
        return false;
    }

    const FEffect Effects[] = {
        {TEXT("NS_IceWindWisps"), TEXT("M_IceWindWisp"), true, true, false, 28, .75f, 1.20f, FVector2f(72,230), 1700,2100, 0,40,.05f},
        {TEXT("NS_IceWindSnow"), TEXT("M_IceSnowStreak"), true, true, false, 42, .55f, 1.10f, FVector2f(3,48), 1900,2400, 0,38,.03f},
        {TEXT("NS_IceImpactMist"), TEXT("M_IceImpactMist"), false, false, false, 18, .28f,.65f, FVector2f(92,92), 50,170,-90,12,1.0f},
        {TEXT("NS_IceShardBurst"), TEXT("M_IceShard"), false, false, true, 22, .35f,.90f, FVector2f(1), 120,340,-420,14,.45f},
        {TEXT("NS_IceChargeMotes"), TEXT("M_IceChargeMote"), true, false, false, 14, .35f,.70f, FVector2f(8,14), 8,28,10,78,.25f}
    };

    for (const FEffect& Effect : Effects)
    {
        const FString Name = Effect.Name;
        const FString Path = TEXT("/Game/Gameplay/Cold/VFX/") + Name;
        if (FPackageName::DoesPackageExist(Path))
        {
            if (!LoadObject<UNiagaraSystem>(nullptr, *(Path + TEXT(".") + Name)))
            {
                UE_LOG(LogTemp, Error, TEXT("MC_ICE_VFX_AUTHOR_FAIL unexpected asset at %s"), *Path);
                return false;
            }
            continue;
        }
        const FString MaterialName = Effect.Material;
        const FString MaterialPath = TEXT("/Game/Gameplay/Cold/VFX/") + MaterialName + TEXT(".") + MaterialName;
        auto* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
        if (!Material)
        {
            UE_LOG(LogTemp, Error, TEXT("MC_ICE_VFX_AUTHOR_FAIL missing %s"), *MaterialPath);
            return false;
        }

        auto* Package = CreatePackage(*Path);
        auto* System = DuplicateObject<UNiagaraSystem>(Template, Package, *Name);
        System->SetFlags(RF_Public | RF_Standalone);
        System->ClearFlags(RF_Transient);
        auto* Emitter = System->GetEmitterHandle(0).GetStatelessEmitter();
        if (!Emitter) return false;
        System->GetEmitterHandle(0).SetName(Effect.Name, *System);
        const auto* StateProperty = FindFProperty<FStructProperty>(Emitter->GetClass(), TEXT("EmitterState"));
        if (!StateProperty) return false;
        auto* State = StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);
        State->LoopBehavior = Effect.bLoop ? ENiagaraLoopBehavior::Infinite : ENiagaraLoopBehavior::Once;
        State->LoopDurationMode = ENiagaraLoopDurationMode::Fixed;
        State->LoopDuration = FNiagaraDistributionRangeFloat(1.f);

        auto* Init = Module<UNiagaraStatelessModule_InitializeParticle>(Emitter);
        auto* Velocity = Module<UNiagaraStatelessModule_AddVelocity>(Emitter);
        auto* Shape = Module<UNiagaraStatelessModule_ShapeLocation>(Emitter);
        auto* Gravity = Module<UNiagaraStatelessModule_GravityForce>(Emitter);
        auto* Age = Module<UNiagaraStatelessModule_DynamicMaterialParameters>(Emitter);
        if (!Init || !Velocity || !Shape || !Gravity || !Age) return false;

        const FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.ParticleColor"));
        const FNiagaraVariable Speed(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SpeedScale"));
        const FNiagaraVariable Rate(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SpawnRate"));
        const FNiagaraVariable Size(FNiagaraTypeDefinition::GetVec2Def(), TEXT("User.ParticleSize"));
        const FNiagaraVariable MeshSize(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.MeshScale"));
        const FNiagaraVariable FXMaterial(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), TEXT("User.FXMaterial"));
        auto& Parameters = System->GetExposedParameters();
        Parameters.SetParameterValue(FLinearColor(.50f,.86f,1.f,1.f), Color, true);
        Parameters.SetParameterValue(1.f, Speed, true);
        if (Effect.bLoop) Parameters.SetParameterValue(float(Effect.Amount), Rate, true);
        if (Effect.bMesh) Parameters.SetParameterValue(FVector3f(.065f,.075f,.11f), MeshSize, true);
        else
        {
            Parameters.SetParameterValue(Effect.Size, Size, true);
            Parameters.AddParameter(FXMaterial);
            Parameters.SetUObject(Material, FXMaterial);
        }

        for (int32 Index = 0; Index < Emitter->GetNumSpawnInfos(); ++Index)
        {
            auto* Spawn = Emitter->GetSpawnInfoByIndex(Index);
            Spawn->bEnabled = Index == 0;
            Spawn->SpawnTime = 0;
            Spawn->bSpawnProbabilityEnabled = false;
            Spawn->Type = Effect.bLoop ? ENiagaraStatelessSpawnInfoType::Rate : ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->Amount = FNiagaraDistributionRangeInt(Effect.Amount);
            Spawn->Rate = FNiagaraDistributionRangeFloat(float(Effect.Amount));
            Spawn->bLoopCountLimitEnabled = !Effect.bLoop;
            Spawn->LoopCountLimit = FNiagaraDistributionRangeInt(1);
            if (Effect.bLoop) Bind(Spawn->Rate, Rate);
        }
        Init->LifetimeDistribution = FNiagaraDistributionRangeFloat(Effect.LifeMin, Effect.LifeMax);
        Init->ColorDistribution = FNiagaraDistributionColor(FLinearColor::White);
        Bind(Init->ColorDistribution, Color);
        Init->SpriteSizeDistribution.InitConstant(Effect.Size);
        Init->SpriteRotationDistribution = FNiagaraDistributionRangeFloat(Effect.bWind ? 0.f : -180.f, Effect.bWind ? 0.f : 180.f);
        Init->InitialPositionDistribution = FNiagaraDistributionPosition(FVector3f::ZeroVector);
        if (Effect.bMesh)
        {
            Init->MeshScaleDistribution.InitConstant(FVector3f(.065f,.075f,.11f));
            Bind(Init->MeshScaleDistribution, MeshSize);
        }
        else Bind(Init->SpriteSizeDistribution, Size);

        Shape->SetIsModuleEnabled(true);
        Shape->CoordinateSpace = ENiagaraCoordinateSpace::Local;
        Shape->ShapePrimitive = Effect.bWind ? ENSM_ShapePrimitive::Box : ENSM_ShapePrimitive::Sphere;
        Shape->BoxSize.InitConstant(FVector3f(40,80,45));
        Shape->bBoxSurfaceOnly = false;
        Shape->SphereRadius = FNiagaraDistributionRangeFloat(0, Effect.Radius);
        Velocity->SetIsModuleEnabled(true);
        Velocity->CoordinateSpace = ENiagaraCoordinateSpace::Local;
        if (Effect.bWind)
        {
            // A flat fan keeps snow at the players' feet rather than spraying into the roof.
            Velocity->VelocityType = ENSM_VelocityType::Linear;
            const float Spread = Effect.SpeedMax * .50f;
            Velocity->LinearVelocityDistribution.Mode = ENiagaraDistributionMode::NonUniformRange;
            Velocity->LinearVelocityDistribution.Min = FVector3f(Effect.SpeedMin, -Spread, -25);
            Velocity->LinearVelocityDistribution.Max = FVector3f(Effect.SpeedMax, Spread, 65);
            Velocity->LinearVelocityDistribution.ChannelConstantsAndRanges = {
                Effect.SpeedMin, -Spread, -25, Effect.SpeedMax, Spread, 65};
            Velocity->LinearVelocityScale = FNiagaraDistributionRangeFloat(1.f);
            Bind(Velocity->LinearVelocityScale, Speed);
        }
        else
        {
            Velocity->VelocityType = ENSM_VelocityType::InCone;
            Velocity->ConeRotationType = ENSM_ConeRotationType::Direction;
            Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
            Velocity->ConeAngle = Effect.bMesh ? 110.f : 100.f;
            Velocity->ConeVelocityDistribution = FNiagaraDistributionRangeFloat(Effect.SpeedMin, Effect.SpeedMax);
            Velocity->ConeVelocityScale = FNiagaraDistributionRangeFloat(1.f);
            Bind(Velocity->ConeVelocityScale, Speed);
        }
        Gravity->SetIsModuleEnabled(Effect.Gravity != 0);
        Gravity->GravityDistribution.InitConstant(FVector3f(0,0,Effect.Gravity));
        if (auto* Drag = Module<UNiagaraStatelessModule_Drag>(Emitter))
        {
            Drag->SetIsModuleEnabled(true);
            Drag->DragDistribution = FNiagaraDistributionRangeFloat(Effect.Drag);
        }
        if (auto* Curl = Module<UNiagaraStatelessModule_CurlNoiseForce>(Emitter))
        {
            Curl->SetIsModuleEnabled(Effect.bWind && Effect.Amount == 28);
            Curl->NoiseStrength = 16;
            Curl->NoiseFrequency = .008f;
        }
        if (auto* Scale = Module<UNiagaraStatelessModule_ScaleColor>(Emitter))
        {
            Scale->SetIsModuleEnabled(true);
            Scale->ScaleDistribution.InitConstant(FLinearColor::White);
        }
        if (auto* Scale = Module<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter))
        {
            Scale->SetIsModuleEnabled(!Effect.bMesh);
            auto& Curve = Scale->ScaleDistribution;
            Curve.Mode = ENiagaraDistributionMode::UniformCurve;
            Curve.ChannelCurves.SetNum(1);
            Curve.ChannelCurves[0].Reset();
            Curve.ChannelCurves[0].AddKey(0,.7f);
            Curve.ChannelCurves[0].AddKey(.18f,1.f);
            Curve.ChannelCurves[0].AddKey(.65f,1.13f);
            Curve.ChannelCurves[0].AddKey(1,1.2f);
            Curve.UpdateValuesFromDistribution();
        }
        if (auto* Scale = Module<UNiagaraStatelessModule_ScaleMeshSize>(Emitter))
        {
            Scale->SetIsModuleEnabled(Effect.bMesh);
            Scale->ScaleDistribution = FNiagaraDistributionVector3({1.f,1.f,1.f,.95f,0.f});
        }
        if (auto* Orientation = Module<UNiagaraStatelessModule_InitialMeshOrientation>(Emitter))
        {
            Orientation->SetIsModuleEnabled(Effect.bMesh);
            Orientation->MeshOrientationMode = ENSMInitialMeshOrientationMode::Random;
        }
        if (auto* Spin = Module<UNiagaraStatelessModule_MeshRotationRate>(Emitter))
        {
            Spin->SetIsModuleEnabled(Effect.bMesh);
            Spin->RotationRateDistribution.Mode = ENiagaraDistributionMode::NonUniformRange;
            Spin->RotationRateDistribution.Min = FVector3f(-210,-260,-190);
            Spin->RotationRateDistribution.Max = FVector3f(210,260,190);
            Spin->RotationRateDistribution.ChannelConstantsAndRanges = {-210,-260,-190,210,260,190};
        }
        // The stateless template does not export ParticleRelativeTime. Provide real
        // normalized age explicitly; every sprite shader reads DynamicParameter.x.
        Age->SetIsModuleEnabled(!Effect.bMesh);
        Age->bParameter0Enabled = true;
        Age->bParameter1Enabled = Age->bParameter2Enabled = Age->bParameter3Enabled = false;
        Age->Parameter0.bXChannelEnabled = true;
        Age->Parameter0.bYChannelEnabled = Age->Parameter0.bZChannelEnabled = Age->Parameter0.bWChannelEnabled = false;
        Age->Parameter0.XChannelDistribution = FNiagaraDistributionFloat({0.f,1.f});

        const auto OldRenderers = Emitter->GetRenderers();
        for (auto* Renderer : OldRenderers) Emitter->RemoveRenderer(Renderer, FGuid());
        if (Effect.bMesh)
        {
            auto* Renderer = NewObject<UNiagaraMeshRendererProperties>(Emitter, NAME_None, RF_Transactional);
            Renderer->Meshes.Reset();
            FNiagaraMeshRendererMeshProperties Mesh;
            Mesh.Mesh = ShardMesh;
            Renderer->Meshes.Add(Mesh);
            Renderer->bOverrideMaterials = true;
            FNiagaraMeshMaterialOverride Override;
            Override.ExplicitMat = Material;
            Renderer->OverrideMaterials.Add(Override);
            Renderer->bCastShadows = false;
            Emitter->AddRenderer(Renderer, FGuid());
        }
        else
        {
            auto* Renderer = NewObject<UNiagaraSpriteRendererProperties>(Emitter, NAME_None, RF_Transactional);
            Renderer->Material = Material;
            Renderer->MaterialUserParamBinding.Parameter = FXMaterial;
            Renderer->Alignment = Effect.bWind ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Unaligned;
            Renderer->FacingMode = ENiagaraSpriteFacingMode::FaceCameraPosition;
            Renderer->SortMode = ENiagaraSortMode::ViewDepth;
            Emitter->AddRenderer(Renderer, FGuid());
        }
        const FBox Bounds = Effect.bWind
            ? FBox(FVector(-3100,-3100,-450), FVector(3100,3100,450))
            : FBox(FVector(-400), FVector(400));
        if (const auto* BoundsProperty = FindFProperty<FStructProperty>(Emitter->GetClass(), TEXT("FixedBounds")))
            *BoundsProperty->ContainerPtrToValuePtr<FBox>(Emitter) = Bounds;
        System->bFixedBounds = true;
        System->SetFixedBounds(Bounds);
        System->MaxPoolSize = 16;
        System->PoolPrimeSize = 0;
        Emitter->PostEditChange();
        System->PostEditChange();
        System->RequestCompile(true);
        System->WaitForCompilationComplete(false, false);
        FAssetRegistryModule::AssetCreated(System);
        System->MarkPackageDirty();
        FSavePackageArgs Save;
        Save.TopLevelFlags = RF_Public | RF_Standalone;
        Save.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, System,
            *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Save))
            return false;
        UE_LOG(LogTemp, Display, TEXT("MC_ICE_VFX_ASSET_CREATED %s amount=%d loop=%d"), *Path, Effect.Amount, Effect.bLoop);
    }
    return true;
#else
    return false;
#endif
}
