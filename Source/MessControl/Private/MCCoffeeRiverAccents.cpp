#include "MCCoffeeFlood.h"

#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraParameterStore.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

namespace MCCoffeeRiverAccents
{
constexpr int32 DropCount=72,PoseCount=73;
template<typename T>T* Resource(const UNiagaraSystem* System,const TCHAR* Name)
{
    const FNiagaraVariable Key{FNiagaraTypeDefinition(T::StaticClass()),FName(Name)};
    const auto& Store=System->GetExposedParameters();
    return Store.IndexOf(Key)==INDEX_NONE?nullptr:Cast<T>(Store.GetUObject(Key).Get());
}
bool Matches(const UNiagaraSystem* System,const UStaticMeshComponent* Drops,const UStaticMeshComponent* Drain,UMaterialInterface* OriginalPour)
{
    return System&&Drops&&Drain
        &&Resource<UStaticMesh>(System,TEXT("User.RiverAccentDropMesh"))==Drops->GetStaticMesh()
        &&Resource<UStaticMesh>(System,TEXT("User.RiverAccentDrainMesh"))==Drain->GetStaticMesh()
        &&Resource<UMaterialInterface>(System,TEXT("User.RiverAccentDropOriginalMaterial"))==Drops->GetMaterial(0)
        &&Resource<UMaterialInterface>(System,TEXT("User.RiverAccentPourOriginalMaterial"))==OriginalPour;
}
}

void AMCCoffeeFlood::UpdateComposerRiverAccents(float Time)
{
    using namespace MCCoffeeRiverAccents;
    bRiverAccentsCustomActive=false;
    if(!RiverComposer)return;
    auto Stop=[&]() {FString Error;RiverComposer->UpdateState(TEXT("World.CoffeeRiver"),TEXT("Accents"),false,FTransform::Identity,FMCPerkVFXGameplayValues(),nullptr,Error);RiverAccentsEpisode.Invalidate();};
    if(!bActive||!bRiverFlood||bCataclysmStorm||!Drops||!DrainRibbon||!DrainMaterial){Stop();return;}
    auto* Channel=RiverComposer->GetSource(TEXT("World.CoffeeRiver"));if(!Channel)return;
    if(Drops->GetInstanceCount()!=DropCount){Stop();return;}
    bool ExactLayer=false,IntentionalEmpty=false;
    if(const auto* Prepared=Channel->GetPreparedSourceEvent(TEXT("World.CoffeeRiver"),TEXT("Accents")))
        if(Prepared->Snapshot) {
            // PrepareSlot has already projected authored disabled groups into
            // bEnabled. Empty/all-disabled is intentional silence, unlike a
            // delayed/newborn/failed enabled layer with no live renderer yet.
            IntentionalEmpty=!Prepared->Snapshot->Layers.ContainsByPredicate([](const auto& Layer){return Layer.bEnabled;});
            for(int32 I=0;I<Prepared->Snapshot->Layers.Num();++I)
            if(Prepared->Snapshot->Layers[I].bEnabled&&Prepared->Systems.IsValidIndex(I)
                &&Matches(Prepared->Systems[I],Drops,DrainRibbon,DrainMaterial->Parent))ExactLayer=true;
        }
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(!Channel->GetPreparedEventMode(TEXT("World.CoffeeRiver"),TEXT("Accents"),Mode)){Stop();return;}
    if(!ExactLayer&&!IntentionalEmpty&&Mode!=EMCPerkVFXSlotMode::Disabled){Stop();return;}
    FMCPerkVFXGameplayValues Values;Values.Duration=Seconds;Values.LocalStateAgeSeconds=Time;
    Values.NativeInputs.Entries.Reserve(8);
    if(!RiverAccentsEpisode.IsValid())RiverAccentsEpisode=FGuid::NewGuid();Values.EpisodeID=RiverAccentsEpisode;
    auto Entry=[&](const TCHAR* Name,EMCVFXNativeInputType Type)->FMCVFXNativeInput& {
        auto& Value=Values.NativeInputs.Entries.AddDefaulted_GetRef();Value.Name=FName(Name);Value.Type=Type;return Value;
    };
    // A fixed 73-pose layout: native breakers [0,71], native outflow [72].
    // These are sampled AFTER UpdatePour. No random draw, trace or integration is repeated.
    auto& P=Entry(TEXT("User.Native.RiverAccentPositions"),EMCVFXNativeInputType::PositionArray);P.PositionValues.Reserve(PoseCount);
    auto& Q=Entry(TEXT("User.Native.RiverAccentQuaternionXYZ"),EMCVFXNativeInputType::VectorArray);Q.VectorValues.Reserve(PoseCount);
    auto& W=Entry(TEXT("User.Native.RiverAccentQuaternionW"),EMCVFXNativeInputType::FloatArray);W.FloatValues.Reserve(PoseCount);
    auto& S=Entry(TEXT("User.Native.RiverAccentScales"),EMCVFXNativeInputType::VectorArray);S.VectorValues.Reserve(PoseCount);
    auto& C=Entry(TEXT("User.Native.RiverAccentShaderXYZ"),EMCVFXNativeInputType::VectorArray);C.VectorValues.Reserve(PoseCount);
    auto& A=Entry(TEXT("User.Native.RiverAccentShaderW"),EMCVFXNativeInputType::FloatArray);A.FloatValues.Reserve(PoseCount);
    // Entry references above require the owning array's fixed capacity.
    auto Append=[&](const FTransform& Pose,bool Visible,FLinearColor Shader) {
        const FQuat Rotation=Pose.GetRotation();
        P.PositionValues.Add(Pose.GetLocation());Q.VectorValues.Add(FVector(Rotation.X,Rotation.Y,Rotation.Z));W.FloatValues.Add(Rotation.W);
        S.VectorValues.Add(Visible?Pose.GetScale3D():FVector::ZeroVector);C.VectorValues.Add(FVector(Shader.R,Shader.G,Shader.B));A.FloatValues.Add(Shader.A);
    };
    for(int32 I=0;I<DropCount;++I) {
        FTransform Pose;if(!Drops->GetInstanceTransform(I,Pose,true)){Stop();return;}
        Append(Pose,Drops->IsVisible(),FLinearColor::White);
    }
    const FLinearColor Shader(DrainMaterial->K2_GetScalarParameterValue(TEXT("WaterTime")),DrainMaterial->K2_GetScalarParameterValue(TEXT("Strength")),
        DrainMaterial->K2_GetScalarParameterValue(TEXT("IsCrown")),DrainMaterial->K2_GetScalarParameterValue(TEXT("IsDrain")));
    Append(DrainRibbon->GetComponentTransform(),DrainRibbon->IsVisible(),Shader);
    auto& Count=Entry(TEXT("User.Native.RiverAccentCount"),EMCVFXNativeInputType::Int);Count.IntValue=PoseCount;
    auto& Ready=Entry(TEXT("User.Native.RiverAccentContextReady"),EMCVFXNativeInputType::Bool);Ready.BoolValue=true;
    FString Error;if(!Values.NativeInputs.Validate(Error)){Stop();return;}
    const bool Accepted=RiverComposer->UpdateState(TEXT("World.CoffeeRiver"),TEXT("Accents"),true,FTransform::Identity,Values,Drops,Error);
    const bool Disabled=Mode==EMCPerkVFXSlotMode::Disabled;
    bRiverAccentsCustomActive=Accepted&&!RiverComposer->UsesNativeVisuals(TEXT("World.CoffeeRiver"),TEXT("Accents"))
        &&(Disabled||IntentionalEmpty||(ExactLayer&&Channel->IsSourceEpisodeVisualActive(TEXT("World.CoffeeRiver"),RiverAccentsEpisode)));
    if(bRiverAccentsCustomActive){Drops->SetVisibility(false);DrainRibbon->SetVisibility(false);}
}
