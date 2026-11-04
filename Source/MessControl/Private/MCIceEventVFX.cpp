#include "MCIceEventVFX.h"
#include "MCWorldLabelPresenterComponent.h"
#include "MCWorldLabelLocalization.h"
#include "MCIceEvent.h"
#include "MCWorldPresentationComponent.h"
#include "MCVFXPresentationComponent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"

namespace
{
const FName ColorParameter(TEXT("User.ParticleColor"));
const FName SpeedParameter(TEXT("User.SpeedScale"));
const FName RateParameter(TEXT("User.SpawnRate"));
const FName SizeParameter(TEXT("User.ParticleSize"));
const FName MeshParameter(TEXT("User.MeshScale"));
const FName MaterialParameter(TEXT("User.FXMaterial"));
const FLinearColor IceColor(.42f,.79f,1.f,1.f);
const FLinearColor DangerColor(1.f,.18f,.08f,1.f);
const FName PhasePowderName(TEXT("M_IcePhasePowder"));
constexpr int32 PhaseFrontSectorCount=12;
constexpr int32 PhaseFrontLayerCount=3;
constexpr int32 PhaseFrontComponentCount=PhaseFrontSectorCount*PhaseFrontLayerCount;
constexpr int32 AmbientWinterSectorCount=4;

bool FloorAt(const AMCIceEvent& Event, FVector Anchor, FHitResult& Hit)
{
    return IsValid(Event.Tongue) && Event.Tongue->SurfacePoint(Event.Tongue->GetActorTransform().TransformPosition(Anchor),Hit);
}

FVector FeetAt(const AMCToothCharacter& Hero)
{
    return Hero.GetActorLocation()-FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-20);
}

float CrystalFallProgress(const FMCIceZoneCrystal& Crystal,double Time)
{
    return Crystal.bLanded?1.f:FMath::Clamp(float((Time-Crystal.SpawnedAt)/FMath::Max(.05,Crystal.ImpactAt-Crystal.SpawnedAt)),0.f,1.f);
}

FVector CrystalPosition(const AMCIceEvent& Event,const FMCIceZoneCrystal& Crystal,const FHitResult& Floor,double Time)
{
    return Event.CrystalWorldPosition(Crystal,Floor,Time);
}

bool CanPresentStrike(const FMCIcicleStrike& Strike)
{
    if(Strike.bCancelled) return false;
    if(!Strike.bPrisonPunishment || Strike.bImpacted) return true;
    const auto* Hero=Strike.Prisoner.Get();
    return IsValid(Hero) && Hero->HasFrozenLegs() && Hero->Status && Hero->Status->IsAlive()
        && !Hero->IsFreezingToDeath() && !Hero->IsMimicCaptured() && !Hero->SwallowedBy && !Hero->IsHidden();
}
}

AMCIceEventVFX::AMCIceEventVFX()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.1f;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("EffectsRoot"));
    SetRootComponent(Root);
    ComposerPresentation=CreateDefaultSubobject<UMCWorldPresentationComponent>(TEXT("IceComposerPresentation"));
    ComposerPresentation->Sources.Add(TEXT("World.Ice"));
    ComposerPresentation->Sources.Add(TEXT("World.Ice.Surface"));
    WindSheet=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("VolumetricWindSheets"));
    WindSheet->SetupAttachment(Root);
    WindSheet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WindSheet->SetGenerateOverlapEvents(false);
    WindSheet->SetCastShadow(false);
    WindSheet->bReceivesDecals=false;
    WindSheet->SetVisibility(false);
    WindSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/NS_IceWindWisps.NS_IceWindWisps")));
    SnowSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/NS_IceWindSnow.NS_IceWindSnow")));
    MistSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/NS_IceImpactMist.NS_IceImpactMist")));
    ShardSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/NS_IceShardBurst.NS_IceShardBurst")));
    ChargeSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/NS_IceChargeMotes.NS_IceChargeMotes")));
    WindSheetMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/M_IceWindSheet.M_IceWindSheet")));
    WispMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/M_IceWindWisp.M_IceWindWisp")));
    SnowMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/M_IceSnowStreak.M_IceSnowStreak")));
    MistMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/M_IceImpactMist.M_IceImpactMist")));
    ChargeMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/M_IceChargeMote.M_IceChargeMote")));
    CrystalMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/SM_IceShard.SM_IceShard")));
    CrystalMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Art/Materials/ice/MI_Ice.MI_Ice")));
}

void AMCIceEventVFX::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer) {Destroy(); return;}
    // Authored defaults are soft references so missing prototype assets do not
    // fail CDO construction or change the authoritative gameplay path.
    // A soft pointer does not prevent GC. Burst-only systems also need explicit
    // ownership when a client has not spawned their first rendered instance yet.
    auto Retain=[this](UObject* Asset) {if(Asset) LoadedAssets.AddUnique(Asset);};
    LoadedAssets.Reserve(17);
    Retain(WindSystem.LoadSynchronous()); Retain(SnowSystem.LoadSynchronous()); Retain(MistSystem.LoadSynchronous());
    Retain(ShardSystem.LoadSynchronous()); Retain(ChargeSystem.LoadSynchronous());
    Retain(WindSheetMaterial.LoadSynchronous()); Retain(WispMaterial.LoadSynchronous()); Retain(SnowMaterial.LoadSynchronous());
    Retain(MistMaterial.LoadSynchronous()); Retain(ChargeMaterial.LoadSynchronous());
    Retain(CrystalMesh.LoadSynchronous()); Retain(CrystalMaterial.LoadSynchronous());
    // Keep the optional phase-front powder alive without changing the shared
    // wind/attack materials. /Game/Gameplay is included by the cook settings.
    auto* PhasePowderMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
        TEXT("/Game/Gameplay/Cold/VFX/M_IcePhasePowder.M_IcePhasePowder"))).LoadSynchronous();
    Retain(PhasePowderMaterial);
    auto* PhasePowderSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
        TEXT("/Game/Gameplay/Cold/VFX/NS_IcePhasePowder.NS_IcePhasePowder"))).LoadSynchronous();
    Retain(PhasePowderSystem);
    const TCHAR* PhasePowderMaterialPaths[]={
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderLow.MI_IcePowderLow"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderBody.MI_IcePowderBody"),
        TEXT("/Game/Gameplay/Cold/VFX/MI_IcePowderHigh.MI_IcePowderHigh")};
    UMaterialInterface* PhasePowderMaterials[PhaseFrontLayerCount];
    for(int32 Layer=0;Layer<PhaseFrontLayerCount;++Layer)
    {
        auto* Material=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(PhasePowderMaterialPaths[Layer])).LoadSynchronous();
        Retain(Material);
        PhasePowderMaterials[Layer]=Material?Material:PhasePowderMaterial?PhasePowderMaterial:WispMaterial.Get();
    }
    if(CrystalMesh.Get())
    {
        // The artist's material has no instanced-mesh usage. Ordinary shells
        // preserve that shared shader without silently changing its usage flags.
        for(int32 I=0;I<12;++I)
        {
            auto* Facet=NewObject<UStaticMeshComponent>(this,FName(*FString::Printf(TEXT("CrystalFacet%d"),I)));
            Facet->SetupAttachment(GetRootComponent());
            Facet->SetStaticMesh(CrystalMesh.Get()); Facet->SetMaterial(0,CrystalMaterial.Get());
            Facet->SetCollisionEnabled(ECollisionEnabled::NoCollision); Facet->SetGenerateOverlapEvents(false);
            Facet->bReceivesDecals=false; Facet->SetVisibility(false);
            Facet->RegisterComponent(); AddInstanceComponent(Facet); CrystalFacets.Add(Facet);
        }
        for(int32 I=0;I<3;++I)
        {
            auto* Facet=NewObject<UStaticMeshComponent>(this,FName(*FString::Printf(TEXT("IceBombFacet%d"),I)));
            Facet->SetupAttachment(GetRootComponent()); Facet->ComponentTags.Add(TEXT("MC_IceBombProjectile"));
            Facet->SetStaticMesh(CrystalMesh.Get()); Facet->SetMaterial(0,CrystalMaterial.Get());
            Facet->SetCollisionEnabled(ECollisionEnabled::NoCollision); Facet->SetGenerateOverlapEvents(false);
            Facet->SetCanEverAffectNavigation(false); Facet->SetCastShadow(false); Facet->bReceivesDecals=false;
            Facet->SetVisibility(false); Facet->RegisterComponent(); AddInstanceComponent(Facet); BombFacets.Add(Facet);
        }
    }
    Wind=MakeLoop(TEXT("WindWisps"),WindSystem.Get(),WispMaterial.Get());
    Snow=MakeLoop(TEXT("WindSnow"),SnowSystem.Get(),SnowMaterial.Get());
    CoreCharge=MakeLoop(TEXT("CoreCharge"),ChargeSystem.Get(),ChargeMaterial.Get());
    BombCharge=MakeLoop(TEXT("IceBombCharge"),ChargeSystem.Get(),ChargeMaterial.Get());
    // Dedicated front components let the powder drain naturally. Ambient wind
    // keeps its own four components, so live puffs never change material.
    for(int32 I=0;I<PhaseFrontComponentCount+AmbientWinterSectorCount;++I)
    {
        const bool bPhaseFront=I<PhaseFrontComponentCount;
        auto* Material=bPhaseFront?PhasePowderMaterials[I/PhaseFrontSectorCount]:WispMaterial.Get();
        auto* System=bPhaseFront && PhasePowderSystem?PhasePowderSystem:WindSystem.Get();
        WinterWisps.Add(MakeLoop(*FString::Printf(TEXT("GlobalWinterWisps%d"),I),System,Material));
        WinterSnow.Add(MakeLoop(*FString::Printf(TEXT("GlobalWinterSnow%d"),I),SnowSystem.Get(),SnowMaterial.Get()));
    }
    for(int32 I=0;I<3;++I)
    {
        VortexWisps.Add(MakeLoop(*FString::Printf(TEXT("VortexWisps%d"),I),WindSystem.Get(),WispMaterial.Get()));
        VortexSnow.Add(MakeLoop(*FString::Printf(TEXT("VortexSnow%d"),I),SnowSystem.Get(),SnowMaterial.Get()));
    }
    for(int32 I=0;I<2;++I)
    {
        CrystalCharges.Add(MakeLoop(*FString::Printf(TEXT("BlockedZoneCharge%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
        CrystalTrails.Add(MakeLoop(*FString::Printf(TEXT("FallingCrystalTrail%d"),I),WindSystem.Get(),WispMaterial.Get()));
        WarmMotes.Add(MakeLoop(*FString::Printf(TEXT("WarmMotes%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
    }
    // A prison can catch the whole party. Warning capacity matches the bounded
    // frozen-foot presentation instead of hiding the fourth player's danger.
    for(int32 I=0;I<8;++I)
        IcicleCharges.Add(MakeLoop(*FString::Printf(TEXT("IcicleCharge%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
    if(auto* Material=WindSheetMaterial.Get())
    {
        SheetMID=UMaterialInstanceDynamic::Create(Material,this);
        WindSheet->SetMaterial(0,SheetMID);
        SheetMID->SetVectorParameterValue(TEXT("Tint"),IceColor);
    }
    auto MakeHealthLabel=[this](const TCHAR* Name,float Size)
    {
        auto* Label=NewObject<UTextRenderComponent>(this,FName(Name));
        Label->SetupAttachment(GetRootComponent()); Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->SetHorizontalAlignment(EHTA_Center); Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(Size); Label->SetTextRenderColor(FColor(165,235,255)); Label->SetCastShadow(false);
        Label->RegisterComponent(); AddInstanceComponent(Label);
        auto* WorldLabelPresentation=NewObject<UMCWorldLabelPresenterComponent>(this,FName(*FString::Printf(TEXT("%sPresentation"),Name)));
        WorldLabelPresentation->SetupAttachment(Label);WorldLabelPresentation->SourceText=Label;
        AddInstanceComponent(WorldLabelPresentation);WorldLabelPresentation->RegisterComponent();return Label;
    };
    CoreHealthLabel=MakeHealthLabel(TEXT("CoreCrystalHealth"),32);
    for(int32 I=0;I<2;++I)
        CrystalHealthLabels.Add(MakeHealthLabel(*FString::Printf(TEXT("ZoneCrystalHealth%d"),I),25));
}

UNiagaraComponent* AMCIceEventVFX::MakeLoop(const TCHAR* Name, UNiagaraSystem* System, UMaterialInterface* Material)
{
    if(!System) return nullptr;
    auto* Component=NewObject<UNiagaraComponent>(this,FName(Name));
    Component->SetupAttachment(GetRootComponent());
    Component->SetAutoActivate(false);
    Component->SetAsset(System);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCastShadow(false);
    Component->SetVariableMaterial(MaterialParameter,Material);
    Component->RegisterComponent(); AddInstanceComponent(Component);
    return Component;
}

void AMCIceEventVFX::SetLoop(UNiagaraComponent* Component,bool bEnabled,FVector Position,FRotator Rotation,
    float SpawnRate,float Speed,FVector2D Size,FLinearColor Color,float NativeProgress,int32 NativeState)
{
    FComposerLoopCall Call;Call.Component=Component;Call.bEnabled=bEnabled;Call.Position=Position;Call.Rotation=Rotation;
    Call.Rate=SpawnRate;Call.Speed=Speed;Call.Size=Size;Call.Color=Color;Call.Progress=NativeProgress;Call.NativeState=NativeState;
    const FName Event=IdentifyComposerLoop(Component,Call.Family,Call.Ordinal);
    Call.SampleAge=float(ComposerEventAge);Call.PhaseRadius=ComposerPhaseRadius;
    if(!Event.IsNone() && IsValid(Component))
    {
        auto& Memory=ComposerLoopMemory.FindOrAdd(Component);
        if(Call.bEnabled)
        {
            Memory.Last=Call;Memory.bHasFrame=true;
        }
        else if(Memory.bHasFrame)
        {
            Call=Memory.Last;Call.bEnabled=false;
            // Disabled SetLoop calls never rewrite the native frame. Read its
            // retained component transform, including any parent movement.
            Call.Position=Component->GetComponentLocation();Call.Rotation=Component->GetComponentRotation();
        }
        else {Call.Position=Component->GetComponentLocation();Call.Rotation=Component->GetComponentRotation();}
        Memory.bEnabled=Call.bEnabled;Call.ActivationEpoch=Memory.Epoch;
    }
    if(bCollectComposerLoops&&!Event.IsNone())
    {
        auto& Calls=ComposerLoopCalls.FindOrAdd(Event);
        if(Calls.Num()<128){Calls.Add(Call);return;}
        ComposerLoopOverflow.Add(Event);
        // A bounded payload must never hide original artwork on overflow.
    }
    AdvanceOriginalLoop(Call);ApplyOriginalLoop(Call,true);
}

void AMCIceEventVFX::PlayBurst(UNiagaraSystem* System, UMaterialInterface* Material, FVector Position,
    float Speed, FVector Scale, FVector2D Size, FLinearColor Color,FName ComposerEvent,bool bNewBoundary)
{
    if(!ComposerEvent.IsNone()) {
        if(bNewBoundary){LastComposerBurstEvent=ComposerEvent;bLastComposerBurstUsesOriginal=UseOriginalBurst(ComposerEvent,Position);}
        if(LastComposerBurstEvent==ComposerEvent&&!bLastComposerBurstUsesOriginal)return;
    }
    if(!System || GetNetMode()==NM_DedicatedServer || Position.ContainsNaN()) return;
    // Every retained handle is manually released; even a late cancel can kill
    // all particles. Reclaim the oldest first to bound busy multiplayer scenes.
    if(Bursts.Num()>=8) RetireBurst(Bursts[0]);
    auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Position,FRotator::ZeroRotator,
        FVector::OneVector,false,false,ENCPoolMethod::ManualRelease,false);
    if(!Component) return;
    Component->SetVariableLinearColor(ColorParameter,Color);
    Component->SetVariableFloat(SpeedParameter,Speed);
    if(System==ShardSystem.Get()) Component->SetVariableVec3(MeshParameter,Scale);
    else
    {
        Component->SetVariableVec2(SizeParameter,Size);
        Component->SetVariableMaterial(MaterialParameter,Material);
    }
    Bursts.Add(Component);
    BurstDeadlines.Add(Component,GetWorld()->GetTimeSeconds()+3.5);
    Component->OnSystemFinished.AddDynamic(this,&AMCIceEventVFX::OnBurstFinished);
    Component->Activate(true);
}

void AMCIceEventVFX::RetireBurst(UNiagaraComponent* Component)
{
    Bursts.Remove(Component); BurstDeadlines.Remove(Component);
    if(!IsValid(Component)) return;
    Component->OnSystemFinished.RemoveDynamic(this,&AMCIceEventVFX::OnBurstFinished);
    Component->DeactivateImmediate();
    if(Component->PoolingMethod==ENCPoolMethod::ManualRelease) Component->ReleaseToPool();
    else Component->DestroyComponent(); // Pooling can be disabled by scalability/CVars.
}

void AMCIceEventVFX::OnBurstFinished(UNiagaraComponent* Component)
{
    RetireBurst(Component);
}

void AMCIceEventVFX::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Time=GetWorld()->GetTimeSeconds();
    for(int32 I=Bursts.Num()-1;I>=0;--I)
    {
        auto* Component=Bursts[I].Get();
        const double* Deadline=BurstDeadlines.Find(Component);
        if(!IsValid(Component) || (Deadline && Time>=*Deadline)) RetireBurst(Component);
    }
}

bool AMCIceEventVFX::HasWindVisuals() const
{
    return SheetMID && Wind && Snow;
}

bool AMCIceEventVFX::HasCrystalVisuals() const
{
    return CrystalFacets.Num()==12 && CrystalMaterial.Get()!=nullptr;
}

bool AMCIceEventVFX::HasRequiredAssets() const
{
    return WindSystem.Get() && SnowSystem.Get() && MistSystem.Get() && ShardSystem.Get() && ChargeSystem.Get()
        && WindSheetMaterial.Get() && WispMaterial.Get() && SnowMaterial.Get() && MistMaterial.Get()
        && ChargeMaterial.Get() && HasCrystalVisuals();
}

void AMCIceEventVFX::EnsureFrozenFootCapacity(int32 PlayerCount)
{
    PlayerCount=FMath::Clamp(PlayerCount,0,8);
    if(CrystalMesh.Get())
        while(FootFacets.Num()<PlayerCount*6)
        {
            auto* Facet=NewObject<UStaticMeshComponent>(this);
            Facet->SetupAttachment(GetRootComponent()); Facet->ComponentTags.Add(TEXT("MC_FrozenFootCrystal"));
            Facet->SetStaticMesh(CrystalMesh.Get()); Facet->SetMaterial(0,CrystalMaterial.Get());
            Facet->SetCollisionEnabled(ECollisionEnabled::NoCollision); Facet->SetGenerateOverlapEvents(false);
            Facet->SetCanEverAffectNavigation(false); Facet->SetCastShadow(false); Facet->bReceivesDecals=false;
            Facet->RegisterComponent(); AddInstanceComponent(Facet); FootFacets.Add(Facet);
        }
    while(FootMotes.Num()<PlayerCount*2)
        FootMotes.Add(MakeLoop(*FString::Printf(TEXT("FrozenFootMotes%d"),FootMotes.Num()),ChargeSystem.Get(),ChargeMaterial.Get()));
}


void AMCIceEventVFX::UpdateComposerFacets(FName Event,const TArray<TObjectPtr<UStaticMeshComponent>>& Facets,int32 Capacity)
{
    bool Valid=Facets.Num()<=Capacity,Active=false;
    FMCPerkVFXGameplayValues Values;Values.VisualSubject=GetRootComponent();Values.FrameComponent=GetRootComponent();
    auto Add=[&](const TCHAR* Name,EMCVFXNativeInputType Type)->FMCVFXNativeInput&
    {auto& Input=Values.NativeInputs.Entries.AddDefaulted_GetRef();Input.Name=Name;Input.Type=Type;return Input;};
    Values.NativeInputs.Entries.Reserve(6);
    Add(TEXT("User.Native.IceFacetPositions"),EMCVFXNativeInputType::PositionArray);
    Add(TEXT("User.Native.IceFacetQuaternionXYZ"),EMCVFXNativeInputType::VectorArray);
    Add(TEXT("User.Native.IceFacetQuaternionW"),EMCVFXNativeInputType::FloatArray);
    Add(TEXT("User.Native.IceFacetScales"),EMCVFXNativeInputType::VectorArray);
    for(const auto& Facet:Facets)
    {
        if(!IsValid(Facet)){Valid=false;break;}
        const FTransform Pose=Facet->GetComponentTransform();const FQuat Rotation=Pose.GetRotation();
        Valid&=!Pose.ContainsNaN()&&Rotation.IsNormalized();
        const bool Visible=Facet->IsVisible()&&!Pose.GetScale3D().IsNearlyZero();Active|=Visible;
        Values.NativeInputs.Entries[0].PositionValues.Add(Pose.GetLocation());
        Values.NativeInputs.Entries[1].VectorValues.Add(FVector(Rotation.X,Rotation.Y,Rotation.Z));
        Values.NativeInputs.Entries[2].FloatValues.Add(float(Rotation.W));
        Values.NativeInputs.Entries[3].VectorValues.Add(Visible?Pose.GetScale3D():FVector::ZeroVector);
    }
    Add(TEXT("User.Native.IceFacetCount"),EMCVFXNativeInputType::Int).IntValue=Facets.Num();
    Add(TEXT("User.Native.IceFacetContextReady"),EMCVFXNativeInputType::Bool).BoolValue=true;
    Active&=Valid;
    if(Active&&!ComposerFacetEpisodes.Contains(Event))
    {ComposerFacetEpisodes.Add(Event,FGuid::NewGuid());ComposerFacetStarted.Add(Event,ComposerServerTime);ObserveComposerEvent(Event);}
    Values.EpisodeID=Active?ComposerFacetEpisodes.FindChecked(Event):FGuid();
    Values.LocalStateAgeSeconds=Active?FMath::Max(0.f,float(ComposerServerTime-ComposerFacetStarted.FindChecked(Event))):0.f;
    FString Error;const bool Accepted=ComposerPresentation&&ComposerPresentation->UpdateState(TEXT("World.Ice"),Event,Active,
        FTransform::Identity,Values,GetRootComponent(),Error);
    auto* Channel=ComposerPresentation?ComposerPresentation->GetSource(TEXT("World.Ice")):nullptr;
    EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;bool UseOriginal=true;
    if(Active&&Accepted&&Channel&&Channel->GetPreparedEventMode(TEXT("World.Ice"),Event,Mode))
    {
        if(Mode==EMCPerkVFXSlotMode::Disabled)UseOriginal=false;
        else if((Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition)
            &&Channel->IsSourceEpisodeVisualActive(TEXT("World.Ice"),Values.EpisodeID)
            &&!ComposerPresentation->UsesNativeVisuals(TEXT("World.Ice"),Event))UseOriginal=false;
    }
    if(!UseOriginal)for(const auto& Facet:Facets)if(IsValid(Facet))Facet->SetVisibility(false);
    if(!Active){ComposerFacetEpisodes.Remove(Event);ComposerFacetStarted.Remove(Event);}
}

void AMCIceEventVFX::UpdateFrozenFeet(const AMCIceEvent& Event)
{
    const int32 PlayerCount=FMath::Min(Event.Players.Num(),8);
    EnsureFrozenFootCapacity(PlayerCount);
    for(int32 I=0;I<FootFacets.Num();++I) FootFacets[I]->SetVisibility(false);
    for(int32 PlayerIndex=0;PlayerIndex<PlayerCount;++PlayerIndex)
    {
        const auto* Hero=Event.Players[PlayerIndex].Hero.Get();
        const bool bFrozen=IsValid(Hero) && Hero->HasFrozenLegs() && Hero->Status && Hero->Status->IsAlive()
            && !Hero->IsMimicCaptured() && !Hero->SwallowedBy && !Hero->IsHidden();
        for(int32 Side=0;Side<2;++Side)
        {
            FVector Foot=FVector::ZeroVector,Knee=FVector::ZeroVector;
            if(bFrozen)
            {
                Foot=FeetAt(*Hero)+Hero->GetActorRightVector()*(Side?18.f:-18.f);
                Knee=Foot+FVector(0,0,45);
                const auto* Source=Hero->GetMesh();
                const FName FootBone=Hero->RigBone(Side?TEXT("foot_r"):TEXT("foot_l"));
                const FName KneeBone=Hero->RigBone(Side?TEXT("knee_r"):TEXT("knee_l"));
                if(Source && Source->GetBoneIndex(FootBone)!=INDEX_NONE) Foot=Source->GetSocketLocation(FootBone);
                if(Source && Source->GetBoneIndex(KneeBone)!=INDEX_NONE) Knee=Source->GetSocketLocation(KneeBone);
                const FVector Axis=(Knee-Foot).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
                const FQuat Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Axis);
                for(int32 Spire=0;Spire<3;++Spire)
                {
                    const int32 Index=PlayerIndex*6+Side*3+Spire;
                    if(!FootFacets.IsValidIndex(Index)) continue;
                    const FBox Bounds=CrystalMesh.Get()->GetBoundingBox();
                    const FVector Size=Bounds.GetSize();
                    const FVector Scale((Spire==1?49.f:31.f)/FMath::Max(1.,Size.X),
                        (Spire==1?44.f:29.f)/FMath::Max(1.,Size.Y),(Spire==1?86.f:65.f)/FMath::Max(1.,Size.Z));
                    const FVector Center=Foot+Axis*25+Hero->GetActorRightVector()*((Spire-1)*18)
                        +Hero->GetActorForwardVector()*(Spire==1?8.f:-5.f);
                    auto* Facet=FootFacets[Index].Get();
                    Facet->SetWorldLocationAndRotation(Center-Rotation.RotateVector(Bounds.GetCenter()*Scale),Rotation);
                    Facet->SetWorldScale3D(Scale); Facet->SetVisibility(true);
                }
            }
            SetLoop(FootMotes[PlayerIndex*2+Side],bFrozen,Foot+FVector(0,0,22),FRotator::ZeroRotator,
                12.f,.22f,FVector2D(8,8),FLinearColor(.48f,.91f,1.f,.9f));
        }
    }
    for(int32 I=PlayerCount*2;I<FootMotes.Num();++I)
        SetLoop(FootMotes[I],false,FVector::ZeroVector,FRotator::ZeroRotator,0,0,FVector2D::ZeroVector,IceColor);
    UpdateComposerFacets(TEXT("FrozenFeetShell"),FootFacets,48);
}

void AMCIceEventVFX::UpdateHealthLabels(const AMCIceEvent& Event,double ServerTime)
{
    const auto* Controller=GetWorld()->GetFirstPlayerController();
    const auto* Camera=Controller?Controller->PlayerCameraManager.Get():nullptr;
    auto SetLabel=[Camera](UTextRenderComponent* Label,bool bVisible,FVector Position,float Health,float MaxHealth)
    {
        if(!IsValid(Label)) return;
        Label->SetVisibility(bVisible);
        if(!bVisible) return;
        Label->SetWorldLocation(Position);
        if(Camera) Label->SetWorldRotation((Camera->GetCameraLocation()-Position).Rotation());
        MCWorldLabelLocalization::Apply(Label,MCWorldLabelLocalization::Format(Label,TEXT("UI.World.HealthValue"),NSLOCTEXT("MCWorldHealth","HealthValue","{Health} / {Maximum} HP"),
            {{TEXT("Health"),FMath::CeilToInt(Health)},{TEXT("Maximum"),FMath::CeilToInt(MaxHealth)}}));
        Label->SetTextRenderColor(Health<=MaxHealth*.25f?FColor(255,170,110):FColor(165,235,255));
    };
    const FVector CoreLabelPoint=Event.HasAuthoredCandy()
        ?Event.Body->Bounds.Origin+FVector(0,0,Event.Body->Bounds.BoxExtent.Z+45)
        :LastCorePoint+FVector(0,0,440);
    SetLabel(CoreHealthLabel,Event.CandyHealth>0,CoreLabelPoint,Event.CandyHealth,Event.MaxCandyHealth);
    for(int32 I=0;I<CrystalHealthLabels.Num();++I)
    {
        FHitResult Floor;
        const bool bVisible=Event.ZoneCrystals.IsValidIndex(I) && Event.ZoneCrystals[I].Health>0
            && FloorAt(Event,Event.ZoneCrystals[I].Anchor,Floor);
        SetLabel(CrystalHealthLabels[I],bVisible,bVisible?CrystalPosition(Event,Event.ZoneCrystals[I],Floor,ServerTime)+FVector(0,0,180):FVector::ZeroVector,
            bVisible?Event.ZoneCrystals[I].Health:0,bVisible?Event.ZoneCrystals[I].MaxHealth:Event.ZoneCrystalHealth);
    }
}

void AMCIceEventVFX::UpdateColdThronePresentation(const AMCIceEvent& Event,float DeltaSeconds,double ServerTime)
{
    const auto* PowderAsset=LoadedAssets.FindByPredicate([](const TObjectPtr<UObject>& Asset)
        {return IsValid(Asset.Get()) && Asset->GetFName()==PhasePowderName;});
    auto* PowderMaterial=PowderAsset?Cast<UMaterialInterface>(PowderAsset->Get()):nullptr;
    const bool bGlobal=Event.UsesColdThroneContract() && Event.IsGlobalColdActive();
    const float PhaseAge=Event.PhaseTransitionAge();
    const float ReturnAge=Event.PhaseReturnAge();
    const float Progress=Event.PhaseTransitionProgress();
    const bool bReturning=ReturnAge>=AMCIceEvent::PhaseTransitionClimateSeconds && Progress>0.f;
    const bool bFront=(PhaseAge>=AMCIceEvent::PhaseTransitionChargeSeconds && Progress<1.f) || bReturning;
    const float GlobalStrength=Event.UsesColdThroneContract()?Event.ClimateContribution():0.f;
    if(!bInitialized)
    {
        WinterStrength=GlobalStrength;
        bWasGlobalCold=bGlobal;
    }
    else WinterStrength=FMath::FInterpTo(WinterStrength,GlobalStrength,DeltaSeconds,4.f);
    if(bGlobal && !bWasGlobalCold)
        PlayBurst(MistSystem.Get(),PowderMaterial?PowderMaterial:MistMaterial.Get(),LastCorePoint+FVector(0,0,100),.45f,
            FVector::OneVector,FVector2D(330,270),FLinearColor(.9f,.97f,1.f,.8f),TEXT("ColdPhaseRelease"));
    bWasGlobalCold=bGlobal;

    FBox Bounds(ForceInit);
    if(IsValid(Event.Tongue) && Event.Tongue->Surface) Bounds=Event.Tongue->Surface->Bounds.GetBox();
    const FVector Center=Bounds.IsValid?Bounds.GetCenter():LastCorePoint;
    const FVector Extent=Bounds.IsValid?Bounds.GetExtent():FVector(900,900,0);
    const float FrontRadius=bFront?Event.PhaseTransitionRadius():0.f;
    ComposerPhaseRadius=FrontRadius;
    for(int32 Sector=0;Sector<WinterWisps.Num();++Sector)
    {
        if(bFront && Sector<PhaseFrontComponentCount)
        {
            // Stagger low spray, rolling body and rising powder around the
            // native front. Mirror trailing offsets when it travels inward.
            const int32 Layer=Sector/PhaseFrontSectorCount;
            const int32 LayerSector=Sector%PhaseFrontSectorCount;
            const float FlowSign=bReturning?-1.f:1.f;
            const float Age=float(ServerTime-Event.StartedAt);
            const float Ripple=FMath::Sin(Age*2.1f+LayerSector*1.73f+Layer*2.4f);
            const float Angle=(LayerSector+Layer*.31f)*2*PI/PhaseFrontSectorCount
                +.035f*FMath::Sin(Age*.9f+LayerSector+Layer*1.7f);
            const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
            const FVector Tangent(-Ray.Y,Ray.X,0);
            constexpr float RadiusOffsets[]={-90.f,15.f,-150.f};
            constexpr float Heights[]={35.f,110.f,190.f};
            constexpr float Lift[]={.12f,.35f,.6f};
            constexpr float Speeds[]={.16f,.20f,.12f};
            constexpr float Rates[]={3.f,3.f,2.f};
            constexpr float Widths[]={.82f,1.f,.7f};
            constexpr float Aspects[]={.9f,1.05f,.95f};
            const float Radius=FMath::Max(30.f,FrontRadius+FlowSign*RadiusOffsets[Layer]+Ripple*24.f);
            const FVector Direction=Ray*FlowSign+Tangent*(Ripple*.16f)+FVector(0,0,Lift[Layer]);
            FHitResult Floor;
            const bool bEnabled=IsValid(Event.Tongue)
                && Event.Tongue->SurfacePoint(LastCorePoint+Ray*Radius,Floor);
            const FVector Point=bEnabled?Floor.ImpactPoint+Floor.ImpactNormal*(Heights[Layer]+Ripple*18.f):FVector::ZeroVector;
            const float Fade=bReturning?FMath::SmoothStep(0.f,.12f,Progress):1-FMath::SmoothStep(.88f,1.f,Progress);
            const float PowderWidth=FMath::Clamp(FrontRadius*.42f,300.f,650.f)*Widths[Layer]*(1+Ripple*.08f);
            const FLinearColor PowderColors[]={FLinearColor(.66f,.8f,.91f,.65f),
                FLinearColor(.9f,.96f,1.f,.8f),FLinearColor(.97f,.99f,1.f,.5f)};
            // Powder clouds accompany the outward cold release only. The
            // returning frost front stays clear while its fine snow contracts.
            SetLoop(WinterWisps[Sector],bEnabled && !bReturning,Point,Direction.Rotation(),Rates[Layer]*Fade,Speeds[Layer],
                FVector2D(PowderWidth,PowderWidth*Aspects[Layer]),PowderColors[Layer],Fade);
            // Extra layers carry powder only; retain the original snow budget.
            SetLoop(WinterSnow[Sector],bEnabled && Layer==0,Point+FVector(0,0,45),Direction.Rotation(),24*Fade,2.2f,
                FVector2D(4.5f,28),FLinearColor(.85f,.97f,1.f,.95f),Fade);
            continue;
        }
        if(Sector<PhaseFrontComponentCount)
        {
            SetLoop(WinterWisps[Sector],false,FVector::ZeroVector,FRotator::ZeroRotator,0,0,FVector2D::ZeroVector,IceColor);
            SetLoop(WinterSnow[Sector],false,FVector::ZeroVector,FRotator::ZeroRotator,0,0,FVector2D::ZeroVector,IceColor);
            continue;
        }
        const int32 AmbientSector=Sector-PhaseFrontComponentCount;
        const FVector Offset((AmbientSector<2?-.45f:.45f)*Extent.X,(AmbientSector%2?-.4f:.4f)*Extent.Y,0);
        FHitResult Floor;
        const bool bEnabled=!bFront && WinterStrength>.02f && IsValid(Event.Tongue)
            && Event.Tongue->SurfacePoint(Center+Offset,Floor);
        const float Drift=.15f*FMath::Sin(float(ServerTime)*.45f+AmbientSector);
        const FVector Direction(.45f+Drift,-1.f,.08f);
        const FVector Point=bEnabled?Floor.ImpactPoint+FVector(0,0,145+(AmbientSector%2)*65):FVector::ZeroVector;
        SetLoop(WinterWisps[Sector],bEnabled,Point,Direction.Rotation(),16*WinterStrength,1.2f,
            FVector2D(145,30),FLinearColor(.64f,.86f,1.f,.55f),WinterStrength);
        SetLoop(WinterSnow[Sector],bEnabled,Point+FVector(0,0,65),Direction.Rotation(),24*WinterStrength,1.35f,
            FVector2D(3.5f,20),FLinearColor(.8f,.96f,1.f,.8f),WinterStrength);
    }
}

void AMCIceEventVFX::UpdateBombPresentation(const AMCIceEvent& Event,double ServerTime)
{
    const auto& Attack=Event.BossAttack;
    FHitResult Floor;
    const bool bBomb=Event.UsesColdThroneContract() && Attack.Kind==EMCCandyAttack::MiniCandy
        && ServerTime<Attack.EndsAt && FloorAt(Event,Attack.TargetAnchor,Floor);
    const bool bWarning=bBomb && ServerTime<Attack.ImpactAt;
    const float Progress=bWarning?FMath::Clamp(float((ServerTime-Attack.StartedAt)
        /FMath::Max(.1,Attack.ImpactAt-Attack.StartedAt)),0.f,1.f):0.f;
    SetLoop(BombCharge,bWarning,bWarning?Floor.ImpactPoint+Floor.ImpactNormal*26:FVector::ZeroVector,
        FRotator::ZeroRotator,9+24*Progress,.3f,FVector2D(7+5*Progress,7+5*Progress),DangerColor,Progress);

    // The existing shard mesh becomes a small tumbling cluster. Its collision
    // is disabled; the replicated attack owns the hit and push at the marker.
    const double ReleasedAt=Attack.StartedAt+13.f/24.f;
    const bool bFlight=bWarning && ServerTime>=ReleasedAt;
    const float Flight=bFlight?FMath::Clamp(float((ServerTime-ReleasedAt)
        /FMath::Max(.1,Attack.ImpactAt-ReleasedAt)),0.f,1.f):0.f;
    const FVector From=Event.Body?Event.Body->Bounds.Origin+FVector(0,0,Event.Body->Bounds.BoxExtent.Z*.65f):Event.GetActorLocation();
    const FVector Center=bFlight?FMath::Lerp(From,Floor.ImpactPoint+Floor.ImpactNormal*45,Flight)
        +FVector(0,0,FMath::Sin(Flight*PI)*240):FVector::ZeroVector;
    for(int32 I=0;I<BombFacets.Num();++I)
    {
        auto* Facet=BombFacets[I].Get();
        Facet->SetVisibility(bFlight);
        if(!bFlight || !CrystalMesh.Get()) continue;
        const FBox Bounds=CrystalMesh.Get()->GetBoundingBox();
        const FVector Size=Bounds.GetSize();
        const FVector Scale(48/FMath::Max(1.,Size.X),44/FMath::Max(1.,Size.Y),82/FMath::Max(1.,Size.Z));
        const float Angle=I*2*PI/3+float(ServerTime-ReleasedAt)*6;
        const FQuat Rotation=FRotator(Flight*360+I*30,Angle*180/PI,35+I*45).Quaternion();
        const FVector Position=Center+FVector(FMath::Cos(Angle)*18,FMath::Sin(Angle)*18,0);
        Facet->SetWorldLocationAndRotation(Position-Rotation.RotateVector(Bounds.GetCenter()*Scale),Rotation);
        Facet->SetWorldScale3D(Scale);
    }
    UpdateComposerFacets(TEXT("BombShell"),BombFacets,3);
    if(bBomb && ServerTime>=Attack.ImpactAt && Attack.Serial>LastBombImpactSerial)
    {
        LastBombImpactSerial=Attack.Serial;
        if(ServerTime<=Attack.ImpactAt+.5)
        {
            const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*22;
            PlayBurst(ShardSystem.Get(),nullptr,Point,1.35f,FVector(.11,.14,.24),FVector2D::ZeroVector,IceColor,TEXT("BombImpact"));
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.4f,FVector::OneVector,FVector2D(150,110),IceColor,TEXT("BombImpact"),false);
        }
    }
}

void AMCIceEventVFX::UpdateFromEvent(const AMCIceEvent& Event, float DeltaSeconds, double ServerTime)
{
    if(bFinishing || !Event.IsActive()) return;
    ComposerServerTime=ServerTime;ComposerEventAge=FMath::Max(0.,ServerTime-Event.StartedAt);ComposerPhaseRadius=0;
    FHitResult CoreFloor;
    if(!FloorAt(Event,Event.CandyAnchor,CoreFloor)) {if(ComposerPresentation)ComposerPresentation->StopStates();ComposerFacetEpisodes.Reset();ComposerFacetStarted.Reset();return;}
    ComposerLoopCalls.Reset();ComposerLoopOverflow.Reset();bCollectComposerLoops=true;
    LastCorePoint=CoreFloor.ImpactPoint;
    UpdateColdThronePresentation(Event,DeltaSeconds,ServerTime);
    UpdateBombPresentation(Event,ServerTime);
    UpdateFrozenFeet(Event); UpdateHealthLabels(Event,ServerTime);
    if(HasCrystalVisuals())
    {
        LastCrystalUpdate=ServerTime;
        for(int32 I=0;I<12;++I)
        {
            FTransform Transform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector);
            if(I<6 && !Event.HasAuthoredCandy())
            {
                const float Angle=I*2*PI/5;
                const FVector Offset=I==0?FVector::ZeroVector:FVector(FMath::Cos(Angle)*94,FMath::Sin(Angle)*94,-50+I*5);
                const FVector Scale=I==0?FVector(1.8,1.5,3.5):FVector(1.05,1.15,2.5+(I%2)*.45);
                Transform=FTransform(FRotator(I==0?0:8,Angle*180/PI,0),Event.GetActorLocation()+Offset,Scale);
            }
            else if(I>=6)
            {
                const int32 Zone=(I-6)/3,Spire=(I-6)%3;
                FHitResult Floor;
                if(Event.ZoneCrystals.IsValidIndex(Zone) && (!Event.ZoneCrystals[Zone].bLaunchedFromBoss
                    || ServerTime>=Event.ZoneCrystals[Zone].SpawnedAt) && FloorAt(Event,Event.ZoneCrystals[Zone].Anchor,Floor))
                {
                    const auto& Crystal=Event.ZoneCrystals[Zone];
                    const float Fall=CrystalFallProgress(Crystal,ServerTime);
                    const float Angle=Spire*2*PI/3+Fall*2*PI;
                    Transform=FTransform(FRotator(Spire*6,Angle*180/PI,0),
                        CrystalPosition(Event,Crystal,Floor,ServerTime)+FVector(FMath::Cos(Angle)*28,FMath::Sin(Angle)*28,0),
                        FVector(.78,.75,1.55+Spire*.05));
                }
            }
            CrystalFacets[I]->SetWorldTransform(Transform);
            CrystalFacets[I]->SetVisibility(!Transform.GetScale3D().IsNearlyZero());
        }
    }
    UpdateComposerFacets(TEXT("CrystalShell"),CrystalFacets,12);
    if(!bInitialized)
    {
        PreviousCoreHealth=Event.CandyHealth;
        bWasActive=Event.Stage==EMCIceEventStage::Active;
        if(Event.NovaImpactAt>0 && ServerTime>Event.NovaImpactAt+.5) LastNovaImpact=Event.NovaImpactAt;
        if(Event.NovaImpactAt>0 && ServerTime>=Event.NovaImpactAt && ServerTime<Event.NovaImpactAt+Event.BossVortexSeconds)
            BlastStartedAt=Event.NovaImpactAt;
        for(const auto& Strike:Event.Strikes)
            if(CanPresentStrike(Strike) && ServerTime>Strike.ImpactAt+.5) LastImpactStrikeId=FMath::Max(LastImpactStrikeId,Strike.Id);
        for(const auto& Crystal:Event.ZoneCrystals)
            PreviousCrystals.Add(Crystal.ZoneIndex,{Crystal.Anchor,Crystal.Health,Crystal.Id,Crystal.bLanded || ServerTime>=Crystal.ImpactAt});
        for(const auto& Player:Event.Players)
            if(IsValid(Player.Hero)) PreviousFeet.Add(Player.Hero.Get(),Player.Hero->IceLegHealth);
        bInitialized=true;
    }

    const bool bActive=Event.Stage==EMCIceEventStage::Active;
    if(bActive && !bWasActive)
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,20),.7f,FVector::OneVector,FVector2D(110,85),IceColor,TEXT("Arrival"));
    bWasActive=bActive;
    if(Event.CandyHealth<PreviousCoreHealth && Event.CandyHealth>0)
        PlayBurst(ShardSystem.Get(),nullptr,LastCorePoint+FVector(0,0,170),.55f,FVector(.05,.065,.12),FVector2D::ZeroVector,IceColor,TEXT("CoreHurt"));
    PreviousCoreHealth=Event.CandyHealth;

    for(const auto& Strike:Event.Strikes)
    {
        if(!CanPresentStrike(Strike) || Strike.Id<=LastImpactStrikeId || ServerTime<Strike.ImpactAt) continue;
        LastImpactStrikeId=Strike.Id;
        FHitResult Floor;
        if(ServerTime>Strike.ImpactAt+.5 || !FloorAt(Event,Strike.Anchor,Floor)) continue;
        const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*18;
        PlayBurst(ShardSystem.Get(),nullptr,Point,1.f,FVector(.075,.095,.19),FVector2D::ZeroVector,IceColor,TEXT("IcicleImpact"));
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.1f,FVector::OneVector,FVector2D(115,90),FLinearColor(.53f,.78f,.95f,.75f),TEXT("IcicleImpact"),false);
    }

    TMap<int32,FCrystalState> Crystals;
    for(const auto& Crystal:Event.ZoneCrystals)
    {
        const bool bLanded=Crystal.bLanded || ServerTime>=Crystal.ImpactAt;
        Crystals.Add(Crystal.ZoneIndex,{Crystal.Anchor,Crystal.Health,Crystal.Id,bLanded});
        const auto* Previous=PreviousCrystals.Find(Crystal.ZoneIndex);
        FHitResult Floor;
        if(!FloorAt(Event,Crystal.Anchor,Floor)) continue;
        const bool bNewCrystal=!Previous || Previous->Id!=Crystal.Id;
        if(bLanded && (bNewCrystal || !Previous->bLanded) && ServerTime<=Crystal.ImpactAt+.5)
        {
            const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*22;
            PlayBurst(ShardSystem.Get(),nullptr,Point,1.35f,FVector(.1,.13,.24),FVector2D::ZeroVector,IceColor,TEXT("ZoneCrystalLand"));
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.45f,FVector::OneVector,FVector2D(155,110),IceColor,TEXT("ZoneCrystalLand"),false);
        }
        else if(!bNewCrystal && Crystal.Health<Previous->Health)
            PlayBurst(ShardSystem.Get(),nullptr,Floor.ImpactPoint+FVector(0,0,90),.45f,FVector(.045,.06,.1),FVector2D::ZeroVector,IceColor,TEXT("ZoneCrystalHurt"));
    }
    for(const auto& Previous:PreviousCrystals)
    {
        // The old zone expiring is a fade, while a crystal disappearing from a
        // still-live zone is the player's successful pickaxe shatter.
        if(Crystals.Contains(Previous.Key) || Previous.Key<Event.CircleIndex) continue;
        FHitResult Floor;
        if(!FloorAt(Event,Previous.Value.Anchor,Floor)) continue;
        PlayBurst(ShardSystem.Get(),nullptr,Floor.ImpactPoint+FVector(0,0,80),.85f,FVector(.065,.085,.17),FVector2D::ZeroVector,IceColor,TEXT("ZoneCrystalBreak"));
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),Floor.ImpactPoint+FVector(0,0,20),.55f,FVector::OneVector,FVector2D(70,55),FLinearColor(.58f,.86f,1,.6f),TEXT("ZoneCrystalBreak"),false);
    }
    PreviousCrystals=MoveTemp(Crystals);
    for(int32 I=0;I<CrystalCharges.Num();++I)
    {
        FHitResult Floor;
        const bool bEnabled=Event.ZoneCrystals.IsValidIndex(I) && (!Event.ZoneCrystals[I].bLaunchedFromBoss
            || ServerTime>=Event.ZoneCrystals[I].SpawnedAt) && FloorAt(Event,Event.ZoneCrystals[I].Anchor,Floor);
        const FVector Center=bEnabled?CrystalPosition(Event,Event.ZoneCrystals[I],Floor,ServerTime):FVector::ZeroVector;
        const bool bFalling=bEnabled && ServerTime<Event.ZoneCrystals[I].ImpactAt;
        SetLoop(CrystalCharges[I],bEnabled,Center+FVector(0,0,85),FRotator::ZeroRotator,
            bFalling?23.f:12.f,bFalling?.6f:.35f,FVector2D(bFalling?13:9,bFalling?13:9),FLinearColor(.2f,.64f,1,.9f),0,bFalling?1:0);
        const bool bVortex=bEnabled && ServerTime<Event.ZoneCrystals[I].ImpactAt+Event.ZoneCrystalVortexSeconds;
        const float Angle=bEnabled?float(ServerTime-Event.ZoneCrystals[I].SpawnedAt)*4.5f:0.f;
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector Tangent(-Ray.Y,Ray.X,bFalling?-.4f:.15f);
        SetLoop(CrystalTrails[I],bVortex,Center+Ray*(bFalling?95.f:Event.ZoneCrystalVortexRadius*.65f),Tangent.Rotation(),
            bFalling?28.f:19.f,bFalling?1.1f:.85f,FVector2D(90,26),FLinearColor(.42f,.8f,1.f,.75f),0,bFalling?1:0);
    }
    for(int32 I=0;I<WarmMotes.Num();++I)
    {
        FHitResult Floor;
        const int32 Zone=Event.CircleIndex+I;
        const bool bEnabled=(I==0 || Event.bNextCircle) && !Event.IsCircleBlocked(Zone)
            && FloorAt(Event,I==0?Event.SafeAnchor:Event.NextSafeAnchor,Floor);
        SetLoop(WarmMotes[I],bEnabled,bEnabled?Floor.ImpactPoint+FVector(0,0,22):FVector::ZeroVector,
            FRotator::ZeroRotator,5.f,.2f,FVector2D(7,7),FLinearColor(1.f,.48f,.16f,.65f));
    }
    int32 ChargeIndex=0;
    for(const auto& Strike:Event.Strikes)
    {
        FHitResult Floor;
        if(ChargeIndex>=IcicleCharges.Num() || !CanPresentStrike(Strike) || ServerTime>=Strike.ImpactAt || !FloorAt(Event,Strike.Anchor,Floor)) continue;
        const float WarningSeconds=Strike.bPrisonPunishment?Event.IcePrisonEscapeSeconds:Event.IcicleWarningSeconds;
        const float Progress=FMath::Clamp(1-float((Strike.ImpactAt-ServerTime)/FMath::Max(.1f,WarningSeconds)),0.f,1.f);
        SetLoop(IcicleCharges[ChargeIndex++],true,Floor.ImpactPoint+Floor.ImpactNormal*35,
            FRotator::ZeroRotator,6+18*Progress,.28f,FVector2D(6+5*Progress,6+5*Progress),
            Strike.bPrisonPunishment?DangerColor:FLinearColor(.35f,.8f,1,.75f),Progress,Strike.bPrisonPunishment?1:0);
    }
    while(ChargeIndex<IcicleCharges.Num())
        SetLoop(IcicleCharges[ChargeIndex++],false,FVector::ZeroVector,FRotator::ZeroRotator,0,0,FVector2D::ZeroVector,IceColor);

    TMap<TWeakObjectPtr<AMCToothCharacter>,float> Feet;
    for(const auto& Player:Event.Players)
    {
        auto* Hero=Player.Hero.Get();
        if(!IsValid(Hero) || !Hero->Status || !Hero->Status->IsAlive()) continue;
        const float Health=Hero->IceLegHealth;
        Feet.Add(Hero,Health);
        const float* Previous=PreviousFeet.Find(Hero);
        if(!Previous) continue;
        const FVector Point=FeetAt(*Hero);
        if(Health>0 && *Previous<=0)
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,.35f,FVector::OneVector,FVector2D(50,40),IceColor,TEXT("FootFreeze"));
        else if(Health<*Previous)
        {
            PlayBurst(ShardSystem.Get(),nullptr,Point,Health>0?.35f:.6f,FVector(.035,.05,.08),FVector2D::ZeroVector,IceColor,Health>0?FName(TEXT("FootHurt")):FName(TEXT("FootBreak")));
            if(Health<=0)
                PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,.4f,FVector::OneVector,FVector2D(42,35),FLinearColor(.6f,.88f,1,.5f),TEXT("FootBreak"),false);
        }
    }
    PreviousFeet=MoveTemp(Feet);

    if(Event.NovaImpactAt>LastNovaImpact && ServerTime>=Event.NovaImpactAt && Event.NovaImpactAt>0)
    {
        LastNovaImpact=Event.NovaImpactAt;
        if(ServerTime<=Event.NovaImpactAt+.5)
        {
            BlastStartedAt=Event.NovaImpactAt;
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,120),.75f,FVector::OneVector,FVector2D(90,65),IceColor,TEXT("NovaRelease"));
        }
    }
    const bool bWarning=Event.bNovaWarning && ServerTime<Event.NovaImpactAt;
    const float BlastAge=float(ServerTime-BlastStartedAt);
    const bool bFloorWave=(Event.UsesColdThroneContract() || Event.bUseAnimatedBossAttacks)
        && Event.BossAttack.Kind==EMCCandyAttack::FloorHit;
    const bool bBlast=BlastAge>=0 && BlastAge<(bFloorWave?Event.WaveTravelSeconds:Event.BossVortexSeconds);
    const float ChargeProgress=bWarning?FMath::Clamp(1-float((Event.NovaImpactAt-ServerTime)/(bFloorWave?.75f:Event.NovaWarningSeconds)),0.f,1.f):0.f;
    const bool bColdThrone=Event.UsesColdThroneContract();
    const float Ambient=bColdThrone?.34f:bActive?.3f:0.f;
    const float Strength=bColdThrone?Ambient+(bWarning?.2f*ChargeProgress:bBlast?.15f:0.f):
        bWarning?.5f+.35f*ChargeProgress:bBlast?Ambient+.7f*(1-FMath::SmoothStep(Event.BossVortexSeconds*.6f,Event.BossVortexSeconds,BlastAge)):Ambient;
    // The permanent throne stays at its true radius while the separate low
    // crest travels outward. Its aura no longer pretends to expand with a wave.
    const float Reach=bColdThrone?Event.BossFrostRadius:
        bBlast?(bFloorWave?FMath::Max(60.f,Event.GroundWaveRadius(ServerTime)):Event.NovaRange):Event.BossFrostRadius;
    const float PhaseAge=Event.PhaseTransitionAge();
    const bool bPhaseCharge=PhaseAge>=0 && PhaseAge<AMCIceEvent::PhaseTransitionChargeSeconds;
    const float PhaseCharge=bPhaseCharge?PhaseAge/AMCIceEvent::PhaseTransitionChargeSeconds:0.f;
    SetLoop(CoreCharge,true,Event.GetActorLocation()+FVector(0,0,200),FRotator::ZeroRotator,
        bPhaseCharge?35+65*PhaseCharge:bWarning?14+28*ChargeProgress:7.f,
        bPhaseCharge?1.2f:bWarning?.75f:.3f,
        FVector2D(bPhaseCharge?24+18*PhaseCharge:bWarning?14:8,bPhaseCharge?24+18*PhaseCharge:bWarning?14:8),IceColor,bPhaseCharge?PhaseCharge:ChargeProgress,bPhaseCharge?2:bWarning?1:0);
    // Four simultaneous tangent emitters surround the crystal. The procedural
    // spiral below supplies continuous coverage between those particle trails.
    for(int32 Sector=0;Sector<4;++Sector)
    {
        const float Angle=float(ServerTime-Event.StartedAt)*(bBlast?2.1f:1.f)+Sector*PI*.5f;
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0),Tangent(-Ray.Y,Ray.X,.08f);
        auto* Wisp=Sector==0?Wind.Get():VortexWisps[Sector-1].Get();
        auto* SnowTrail=Sector==0?Snow.Get():VortexSnow[Sector-1].Get();
        const FVector Position=LastCorePoint+Ray*Reach*.55f+FVector(0,0,85+(Sector%2)*80);
        SetLoop(Wisp,Strength>0,Position,Tangent.Rotation(),10+22*Strength,bBlast?1.7f:.65f,
            FVector2D(bBlast?150:105,bBlast?40:29),FLinearColor(.4f,.76f,1,.8f),Strength,bBlast?1:0);
        SetLoop(SnowTrail,Strength>0,Position+FVector(0,0,35),Tangent.Rotation(),6+20*Strength,bBlast?1.5f:.55f,
            FVector2D(3,bBlast?20:12),FLinearColor(.7f,.91f,1,.8f),Strength,bBlast?1:0);
    }
    WindSheet->SetVisibility(Strength>0 && SheetMID);
    if(Strength>0 && SheetMID)
    {
        SheetMID->SetScalarParameterValue(TEXT("Age"),float(ServerTime-Event.StartedAt));
        SheetMID->SetScalarParameterValue(TEXT("FlowSpeed"),bColdThrone?.6f+WinterStrength*.6f:bWarning?.8f:bBlast?1.7f:.5f);
        SheetMID->SetScalarParameterValue(TEXT("Opacity"),bColdThrone?.42f:bWarning?.5f:bBlast?.65f:.4f);
        const double SheetInterval=bColdThrone && !(bFloorWave && bBlast)?.1:.05;
        if(ServerTime>=LastSheetUpdate+SheetInterval)
        {
            LastSheetUpdate=ServerTime;
            UpdateWindSheet(Event,ServerTime,Strength,bWarning);
        }
    }
    FlushComposerLoops();
}

FName AMCIceEventVFX::IdentifyComposerLoop(const UNiagaraComponent* Component,int32& Family,int32& Ordinal) const
{
    Family=0;Ordinal=0;if(!Component)return NAME_None;
    auto Index=[Component](const auto& List){return List.IndexOfByPredicate([Component](const auto& P){return P.Get()==Component;});};
    if(Component==BombCharge.Get())return TEXT("BombWarning");
    if(Component==CoreCharge.Get())return TEXT("Charge");
    if(Index(FootMotes)!=INDEX_NONE)return TEXT("FrozenFeet");
    if(Index(CrystalCharges)!=INDEX_NONE)return TEXT("CrystalCharge");
    if(Index(CrystalTrails)!=INDEX_NONE)return TEXT("CrystalTrail");
    if(Index(WarmMotes)!=INDEX_NONE)return TEXT("WarmMotes");
    if(Index(IcicleCharges)!=INDEX_NONE)return TEXT("IcicleWarning");
    if(const int32 I=Index(WinterWisps);I!=INDEX_NONE){Family=I<36?I/12:4;Ordinal=I%12;return TEXT("ColdThrone");}
    if(const int32 I=Index(WinterSnow);I!=INDEX_NONE){Family=I<36?3:5;Ordinal=I%12;return TEXT("ColdThrone");}
    if(Component==Wind.Get()||Index(VortexWisps)!=INDEX_NONE)return TEXT("Wind");
    if(Component==Snow.Get()||Index(VortexSnow)!=INDEX_NONE){Family=1;return TEXT("Wind");}
    return NAME_None;
}

void AMCIceEventVFX::AdvanceOriginalLoop(FComposerLoopCall& Call)
{
    auto* Component=Call.Component.Get();if(!IsValid(Component))return;
    if(!Call.bEnabled){if(Component->IsActive())Component->Deactivate();return;}
    Component->SetWorldLocationAndRotation(Call.Position,Call.Rotation);
    Component->SetVariableFloat(RateParameter,Call.Rate);Component->SetVariableFloat(SpeedParameter,Call.Speed);
    Component->SetVariableVec2(SizeParameter,Call.Size);Component->SetVariableLinearColor(ColorParameter,Call.Color);
    if(!Component->IsActive())
    {
        auto& Memory=ComposerLoopMemory.FindOrAdd(Component);
        if(Memory.Epoch>=16777215u){int32 Family=0,Ordinal=0;ComposerLoopOverflow.Add(IdentifyComposerLoop(Component,Family,Ordinal));}
        else ++Memory.Epoch;
        Call.ActivationEpoch=Memory.Epoch;Component->Activate(true);
    }
}

void AMCIceEventVFX::ApplyOriginalLoop(const FComposerLoopCall& Call,bool bUseOriginal)
{
    // Keep the actual original stateless lifecycle as the bounded drain witness.
    // It supplies real completion, not a second artistic timeout in native code.
    // This migration does not claim that original simulation work is eliminated.
    if(auto* Component=Call.Component.Get())Component->SetRenderingEnabled(bUseOriginal);
}

void AMCIceEventVFX::ObserveComposerEvent(FName Event)
{
    if(Event.IsNone()||(!ComposerEventSerials.Contains(Event)&&ComposerEventSerials.Num()>=32))return;
    uint64& Serial=ComposerEventSerials.FindOrAdd(Event);if(Serial<MAX_uint64)++Serial;
}

void AMCIceEventVFX::FlushComposerLoops()
{
    bCollectComposerLoops=false;
    auto* Channel=ComposerPresentation?ComposerPresentation->GetSource(TEXT("World.Ice")):nullptr;
    TSet<FName> Active;
    for(auto& Pair:ComposerLoopCalls)
    {
        const FName Event=Pair.Key;auto& Calls=Pair.Value;
        TArray<FComposerLoopCall> Rows=Calls;
        bool Valid=Rows.Num()<=96&&!ComposerLoopOverflow.Contains(Event),AnyLive=false;
        TSet<TWeakObjectPtr<UNiagaraComponent>> Seen;
        for(auto& Call:Rows)
        {
            const auto Key=Call.Component;Valid&=!Seen.Contains(Key);Seen.Add(Key);
            Valid&=Key.IsValid()&&!Call.Position.ContainsNaN()&&!Call.Rotation.ContainsNaN()&&FMath::IsFinite(Call.Progress)
                &&FMath::IsFinite(Call.SampleAge)&&FMath::IsFinite(Call.PhaseRadius)
                &&Call.Family>=0&&Call.Family<6&&Call.NativeState>=0&&Call.NativeState<=4;
            // The source call sites use zero roll and unit scale. Do not
            // silently discard another frame if a future caller changes it.
            if(const auto* Component=Key.Get())Valid&=Component->GetComponentScale().Equals(FVector::OneVector)
                &&FMath::Abs(Call.Rotation.Roll)<=KINDA_SMALL_NUMBER;
            AdvanceOriginalLoop(Call);
            if(auto* Component=Key.Get())AnyLive|=Call.bEnabled||!Component->IsComplete();
        }
        // All source rows have a stable order even while inactive, so a living
        // particle cannot silently jump to another native component's anchor.
        Rows.StableSort([](const auto& A,const auto& B){
            if(A.Family!=B.Family)return A.Family<B.Family;
            const auto* CA=A.Component.Get();const auto* CB=B.Component.Get();
            return CA&&CB?CA->GetFName().LexicalLess(CB->GetFName()):CA!=nullptr;
        });
        Valid&=!ComposerLoopOverflow.Contains(Event);
        const bool Enabled=AnyLive&&Valid;
        if(Enabled)
        {
            Active.Add(Event);
            if(!ComposerLoopEpisodes.Contains(Event))
            {ComposerLoopEpisodes.Add(Event,FGuid::NewGuid());ComposerLoopStarted.Add(Event,ComposerServerTime);ObserveComposerEvent(Event);}
        }
        FMCPerkVFXGameplayValues Values;Values.VisualSubject=GetRootComponent();Values.FrameComponent=GetRootComponent();
        Values.LocalStateAgeSeconds=Enabled?FMath::Max(0.f,float(ComposerServerTime-ComposerLoopStarted.FindChecked(Event))):0.f;
        Values.EpisodeID=Enabled?ComposerLoopEpisodes.FindChecked(Event):FGuid();
        auto Array=[&](const TCHAR* Name,EMCVFXNativeInputType Type)->FMCVFXNativeInput&
        {auto& Input=Values.NativeInputs.Entries.AddDefaulted_GetRef();Input.Name=Name;Input.Type=Type;return Input;};
        Values.NativeInputs.Entries.Reserve(13);
        Array(TEXT("User.Native.IcePositions"),EMCVFXNativeInputType::PositionArray);
        const int32 PositionIndex=0;
        const int32 DirectionIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceDirections"),EMCVFXNativeInputType::VectorArray);
        const int32 ProgressIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceProgress"),EMCVFXNativeInputType::FloatArray);
        const int32 StateIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceState"),EMCVFXNativeInputType::FloatArray);
        const int32 OrdinalIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceOrdinal"),EMCVFXNativeInputType::FloatArray);
        const int32 EpochIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceEpoch"),EMCVFXNativeInputType::FloatArray);
        const int32 AgeIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceSampleAge"),EMCVFXNativeInputType::FloatArray);
        const int32 RadiusIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceRadius"),EMCVFXNativeInputType::FloatArray);
        const int32 ActiveIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceActiveIndices"),EMCVFXNativeInputType::FloatArray);
        const int32 StartIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceFamilyStarts"),EMCVFXNativeInputType::FloatArray).FloatValues.Init(0,6);
        const int32 CountIndex=Values.NativeInputs.Entries.Num();Array(TEXT("User.Native.IceFamilyCounts"),EMCVFXNativeInputType::FloatArray).FloatValues.Init(0,6);
        for(int32 RowIndex=0;RowIndex<Rows.Num();++RowIndex)if(Valid)
        {
            const auto& Row=Rows[RowIndex];
            if(Row.bEnabled && Row.Component.IsValid() && Row.Component->GetRequestedExecutionState()==ENiagaraExecutionState::Active)
            {
                auto& Count=Values.NativeInputs.Entries[CountIndex].FloatValues[Row.Family];
                if(Count==0)Values.NativeInputs.Entries[StartIndex].FloatValues[Row.Family]=Values.NativeInputs.Entries[ActiveIndex].FloatValues.Num();
                ++Count;Values.NativeInputs.Entries[ActiveIndex].FloatValues.Add(float(RowIndex));
            }
            Values.NativeInputs.Entries[PositionIndex].PositionValues.Add(Row.Position);
            Values.NativeInputs.Entries[DirectionIndex].VectorValues.Add(Row.Rotation.Vector());
            Values.NativeInputs.Entries[ProgressIndex].FloatValues.Add(Row.Progress);
            Values.NativeInputs.Entries[StateIndex].FloatValues.Add(float(Row.NativeState));
            Values.NativeInputs.Entries[OrdinalIndex].FloatValues.Add(float(Row.Ordinal));
            Values.NativeInputs.Entries[EpochIndex].FloatValues.Add(float(Row.ActivationEpoch));
            Values.NativeInputs.Entries[AgeIndex].FloatValues.Add(Row.SampleAge);
            Values.NativeInputs.Entries[RadiusIndex].FloatValues.Add(Row.PhaseRadius);
        }
        auto& Clock=Array(TEXT("User.Native.IceEventAge"),EMCVFXNativeInputType::Float);Clock.FloatValue=float(ComposerEventAge);
        Array(TEXT("User.Native.IceContextReady"),EMCVFXNativeInputType::Bool).BoolValue=true;
        FString Error;bool Accepted=false;
        if(ComposerPresentation)Accepted=ComposerPresentation->UpdateState(TEXT("World.Ice"),Event,Enabled,
            FTransform::Identity,Values,GetRootComponent(),Error);
        bool UseOriginal=true;EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
        if(Enabled&&Accepted&&Channel&&Channel->GetPreparedEventMode(TEXT("World.Ice"),Event,Mode))
        {
            if(Mode==EMCPerkVFXSlotMode::Disabled)UseOriginal=false;
            else if((Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition)
                &&Channel->IsSourceEpisodeVisualActive(TEXT("World.Ice"),Values.EpisodeID)
                &&!ComposerPresentation->UsesNativeVisuals(TEXT("World.Ice"),Event))UseOriginal=false;
        }
        for(const auto& Call:Calls)ApplyOriginalLoop(Call,UseOriginal);
    }
    TArray<FName> Retired;
    for(const auto& Pair:ComposerLoopEpisodes)if(!Active.Contains(Pair.Key))Retired.Add(Pair.Key);
    for(FName Event:Retired)
    {
        FString Error;if(ComposerPresentation)ComposerPresentation->UpdateState(TEXT("World.Ice"),Event,false,
            FTransform::Identity,FMCPerkVFXGameplayValues(),GetRootComponent(),Error);
        ComposerLoopEpisodes.Remove(Event);ComposerLoopStarted.Remove(Event);
    }
    ComposerLoopCalls.Reset();
}

bool AMCIceEventVFX::UseOriginalBurst(FName Event,FVector Position)
{
    ObserveComposerEvent(Event);
    auto* Channel=ComposerPresentation?ComposerPresentation->GetSource(TEXT("World.Ice")):nullptr;
    if(!Channel)return true;
    FMCPerkVFXGameplayValues Values;Values.EpisodeID=FGuid::NewGuid();Values.LocalStateAgeSeconds=0;
    Values.VisualSubject=GetRootComponent();Values.Duration=3.5f;
    const auto Result=Channel->PlayLocalSourceEvent(TEXT("World.Ice"),Event,FTransform(FQuat::Identity,Position),Values);
    return Result==EMCVFXPresentationResult::UseDefault||Result==EMCVFXPresentationResult::Rejected;
}

bool AMCIceEventVFX::UseOriginalSheet(FName Event,FMCPerkVFXGameplayValues& Values,bool bActive)
{
    const int32 Slot=Event==TEXT("WindSheetVortex")?0:1;
    if(bActive&&!ComposerSheetEpisodes[Slot].IsValid())
    {ComposerSheetEpisodes[Slot]=FGuid::NewGuid();ComposerSheetStarted[Slot]=ComposerServerTime;ObserveComposerEvent(Event);}
    Values.VisualSubject=GetRootComponent();Values.FrameComponent=GetRootComponent();
    Values.EpisodeID=ComposerSheetEpisodes[Slot];
    Values.LocalStateAgeSeconds=bActive?FMath::Max(0.f,float(ComposerServerTime-ComposerSheetStarted[Slot])):0.f;
    FString Error;
    const bool Accepted=ComposerPresentation&&ComposerPresentation->UpdateState(TEXT("World.Ice"),Event,bActive,
        FTransform::Identity,Values,GetRootComponent(),Error);
    auto* Channel=ComposerPresentation?ComposerPresentation->GetSource(TEXT("World.Ice")):nullptr;
    bool UseOriginal=true;EMCPerkVFXSlotMode Mode=EMCPerkVFXSlotMode::UseDefault;
    if(bActive&&Accepted&&Channel&&Channel->GetPreparedEventMode(TEXT("World.Ice"),Event,Mode))
    {
        if(Mode==EMCPerkVFXSlotMode::Disabled)UseOriginal=false;
        else if((Mode==EMCPerkVFXSlotMode::Replace||Mode==EMCPerkVFXSlotMode::Composition)
            &&Channel->IsSourceEpisodeVisualActive(TEXT("World.Ice"),Values.EpisodeID)
            &&!ComposerPresentation->UsesNativeVisuals(TEXT("World.Ice"),Event))UseOriginal=false;
    }
    if(!bActive){ComposerSheetEpisodes[Slot].Invalidate();ComposerSheetStarted[Slot]=0;}
    return UseOriginal;
}


void AMCIceEventVFX::UpdateWindSheet(const AMCIceEvent& Event, double ServerTime, float Strength, bool bWarning)
{
    constexpr int32 Steps=16;
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Indices;
    // Preserve all projected damage/safe rings. Only these decorative index ranges
    // may be omitted after an already prepared Composer source accepts ownership.
    struct FDecorativeRange {int32 First=0,Count=0,Kind=0;};
    TArray<FDecorativeRange> DecorativeRanges;
    FMCPerkVFXGameplayValues VortexValues,AmbientValues;
    auto Arrays=[](FMCPerkVFXGameplayValues& Values,const TCHAR* Prefix)
    {
        const EMCVFXNativeInputType Types[]={EMCVFXNativeInputType::PositionArray,EMCVFXNativeInputType::PositionArray,
            EMCVFXNativeInputType::VectorArray,EMCVFXNativeInputType::VectorArray,
            EMCVFXNativeInputType::FloatArray,EMCVFXNativeInputType::FloatArray,
            EMCVFXNativeInputType::FloatArray,EMCVFXNativeInputType::FloatArray};
        const TCHAR* Suffixes[]={TEXT("A"),TEXT("B"),TEXT("NormalA"),TEXT("NormalB"),TEXT("X"),TEXT("Y"),TEXT("Z"),TEXT("W")};
        Values.NativeInputs.Entries.Reserve(12);
        for(int32 I=0;I<UE_ARRAY_COUNT(Types);++I)
        {auto& Input=Values.NativeInputs.Entries.AddDefaulted_GetRef();Input.Name=FName(FString(Prefix)+Suffixes[I]);Input.Type=Types[I];}
    };
    Arrays(VortexValues,TEXT("User.Native.IceSheet"));Arrays(AmbientValues,TEXT("User.Native.IceSheet"));
    bool VortexPayloadValid=true,AmbientPayloadValid=true;
    Vertices.Reserve(24*(Steps+1)*2); Normals.Reserve(Vertices.Max());
    UV.Reserve(Vertices.Max()); Colors.Reserve(Vertices.Max()); Tangents.Reserve(Vertices.Max());
    const FTransform Transform=WindSheet->GetComponentTransform();
    auto Vortex=[&](FVector Center,float Radius,float Height,float Age,float Weight,int32 Bands,bool bFalling)
    {
        const int32 RangeStart=Indices.Num();
        // One compact row per original band; never upload a resampled hazard mesh.
        for(int32 Band=0;Band<Bands;++Band)
        {
            if(VortexValues.NativeInputs.Entries[0].PositionValues.Num()>=48)
            {VortexPayloadValid=false;break;}
            auto& E=VortexValues.NativeInputs.Entries;
            E[0].PositionValues.Add(Center);E[1].PositionValues.Add(Center);
            E[2].VectorValues.Add(FVector(Radius,Height,Age));
            E[3].VectorValues.Add(FVector(Band,Bands,bFalling?1:0));
            E[4].FloatValues.Add(Weight);E[5].FloatValues.Add(bWarning?1:0);
            E[6].FloatValues.Add(0);E[7].FloatValues.Add(0);
        }
        for(int32 Band=0;Band<Bands;++Band)
        {
            const int32 First=Vertices.Num();
            for(int32 Step=0;Step<=Steps;++Step)
            {
                const float Fraction=Step/float(Steps);
                const float Angle=Band*2*PI/Bands+Fraction*2*PI*1.1f+Age*(bFalling?4.f:bWarning?1.8f:1.5f);
                const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0),Tangent(-Ray.Y,Ray.X,0);
                const float Distance=Radius*(.22f+.78f*Fraction);
                const float Width=(12+Radius*.033f)*(1+.45f*FMath::Sin(Fraction*9+Band+Age));
                const float Z=bFalling?30+Height*Fraction:40+(Band%3)*Height*.28f+FMath::Sin(Fraction*6+Band+Age)*25;
                for(int32 Side=0;Side<2;++Side)
                {
                    const FVector Point=Center+Ray*Distance+FVector(0,0,Z+(Side?Width:-Width));
                    Vertices.Add(Transform.InverseTransformPosition(Point));
                    Normals.Add(Transform.InverseTransformVectorNoScale(Ray).GetSafeNormal());
                    UV.Add(FVector2D(Fraction,Side));
                    const float EdgeAlpha=FMath::SmoothStep(0.f,.13f,Fraction)*(1-FMath::SmoothStep(.8f,1.f,Fraction));
                    Colors.Add(FLinearColor(.65f,.88f,1.f,EdgeAlpha*Weight));
                    Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(),false));
                }
                if(Step>0)
                {
                    const int32 A=First+(Step-1)*2;
                    Indices.Append({A,A+3,A+1,A,A+2,A+3});
                }
            }
        }
        DecorativeRanges.Add({RangeStart,Indices.Num()-RangeStart,0});
    };
    const float BlastAge=float(ServerTime-BlastStartedAt);
    const bool bFloorWave=(Event.UsesColdThroneContract() || Event.bUseAnimatedBossAttacks)
        && Event.BossAttack.Kind==EMCCandyAttack::FloorHit;
    const bool bBlast=BlastAge>=0 && BlastAge<(bFloorWave?Event.WaveTravelSeconds:Event.BossVortexSeconds);
    const bool bColdThrone=Event.UsesColdThroneContract();
    const float Reach=bColdThrone?Event.BossFrostRadius:bBlast?(bFloorWave?FMath::Max(60.f,Event.GroundWaveRadius(ServerTime)):
        FMath::Lerp(Event.BossFrostRadius,Event.NovaRange,FMath::Clamp(BlastAge/.65f,0.f,1.f))):Event.BossFrostRadius;
    Vortex(LastCorePoint,Reach,bColdThrone?165.f:bBlast?(bFloorWave?95.f:380.f):260.f,float(ServerTime-Event.StartedAt),Strength,12,false);
    if(bFloorWave && bBlast && IsValid(Event.Tongue))
    {
        // A low continuous crest follows the same radius as the foot-freeze hit.
        // Project each segment onto the tissue so it follows the tongue slope.
        constexpr int32 Segments=96;
        for(int32 Segment=0;Segment<Segments;++Segment)
        {
            FHitResult A,B;
            const float Angle=Segment*2*PI/Segments,NextAngle=(Segment+1)*2*PI/Segments;
            const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0),NextRay(FMath::Cos(NextAngle),FMath::Sin(NextAngle),0);
            const float WaveRadius=FMath::Max(60.f,Event.GroundWaveRadius(ServerTime));
            if(!Event.Tongue->SurfacePoint(LastCorePoint+Ray*WaveRadius,A) || !Event.Tongue->SurfacePoint(LastCorePoint+NextRay*WaveRadius,B)) continue;
            const int32 First=Vertices.Num();
            const float Height=bColdThrone?10+2*FMath::Sin(Angle*13+BlastAge*8):55+14*FMath::Sin(Angle*13+BlastAge*8);
            const FVector Points[]={A.ImpactPoint+A.ImpactNormal*4,A.ImpactPoint+A.ImpactNormal*Height,
                B.ImpactPoint+B.ImpactNormal*Height,B.ImpactPoint+B.ImpactNormal*4};
            for(int32 Corner=0;Corner<4;++Corner)
            {
                Vertices.Add(Transform.InverseTransformPosition(Points[Corner]));
                Normals.Add(Transform.InverseTransformVectorNoScale(Ray).GetSafeNormal());
                UV.Add(FVector2D(Corner>=2?1.f:0.f,Corner==1 || Corner==2?1.f:0.f));
                Colors.Add(FLinearColor(.7f,.94f,1.f,.95f));
                Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(FVector(-Ray.Y,Ray.X,0)).GetSafeNormal(),false));
            }
            Indices.Append({First,First+2,First+1,First,First+3,First+2});
        }
    }
    auto GroundRing=[&](FVector Center,float Radius,float Height,FLinearColor Color,int32 Segments)
    {
        if(!IsValid(Event.Tongue) || Radius<1) return;
        FHitResult Previous;
        bool bPrevious=Event.Tongue->SurfacePoint(Center+FVector(Radius,0,0),Previous);
        for(int32 Segment=1;Segment<=Segments;++Segment)
        {
            const float Angle=Segment*2*PI/Segments;
            const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
            FHitResult Current;
            const bool bCurrent=Event.Tongue->SurfacePoint(Center+Ray*Radius,Current);
            if(bPrevious && bCurrent)
            {
                const int32 First=Vertices.Num();
                const FVector Points[]={Previous.ImpactPoint+Previous.ImpactNormal*5,
                    Previous.ImpactPoint+Previous.ImpactNormal*Height,
                    Current.ImpactPoint+Current.ImpactNormal*Height,
                    Current.ImpactPoint+Current.ImpactNormal*5};
                for(int32 Corner=0;Corner<4;++Corner)
                {
                    Vertices.Add(Transform.InverseTransformPosition(Points[Corner]));
                    Normals.Add(Transform.InverseTransformVectorNoScale(Ray).GetSafeNormal());
                    UV.Add(FVector2D(Corner>=2?1.f:0.f,Corner==1 || Corner==2?1.f:0.f));
                    Colors.Add(Color);
                    Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(FVector(-Ray.Y,Ray.X,0)).GetSafeNormal(),false));
                }
                Indices.Append({First,First+2,First+1,First,First+3,First+2});
            }
            Previous=Current; bPrevious=bCurrent;
        }
    };
    if(bColdThrone)
    {
        GroundRing(LastCorePoint,Event.BossFrostRadius,25,FLinearColor(.3f,.8f,1.f,.7f),48);
        const float PhaseAge=Event.PhaseTransitionAge(),PhaseProgress=Event.PhaseTransitionProgress();
        const float ReturnAge=Event.PhaseReturnAge();
        const bool bReturning=ReturnAge>=0 && ReturnAge<AMCIceEvent::PhaseTransitionClimateSeconds+AMCIceEvent::PhaseTransitionSweepSeconds;
        if((PhaseAge>=0 && PhaseProgress<1.f) || bReturning)
        {
            // The visual ring uses the material's synchronized radius, never
            // the gameplay wave radius. Project it onto the breathing tongue.
            const float Radius=Event.PhaseTransitionRadius();
            const float Charge=bReturning?1.f:FMath::Clamp(PhaseAge/AMCIceEvent::PhaseTransitionChargeSeconds,0.f,1.f);
            const float Fade=bReturning?FMath::SmoothStep(0.f,.12f,PhaseProgress):1-FMath::SmoothStep(.9f,1.f,PhaseProgress);
            GroundRing(LastCorePoint,Radius,55+45*Charge,FLinearColor(.65f,.9f,1.f,.8f*Fade),96);
            GroundRing(LastCorePoint,FMath::Max(Event.BossFrostRadius,Radius-65.f),35,
                FLinearColor(.82f,.97f,1.f,.55f*Fade),96);
        }
        FHitResult Floor;
        if(Event.BossAttack.Kind==EMCCandyAttack::MiniCandy && ServerTime<Event.BossAttack.EndsAt
            && FloorAt(Event,Event.BossAttack.TargetAnchor,Floor))
        {
            const auto& Attack=Event.BossAttack;
            if(ServerTime<Attack.ImpactAt)
            {
                const float Progress=FMath::Clamp(float((ServerTime-Attack.StartedAt)
                    /FMath::Max(.1,Attack.ImpactAt-Attack.StartedAt)),0.f,1.f);
                GroundRing(Floor.ImpactPoint,Attack.TargetRadius,28,DangerColor,48);
                GroundRing(Floor.ImpactPoint,Attack.TargetRadius*(.25f+.75f*Progress),13,DangerColor,32);
            }
            else
            {
                const float Age=float(ServerTime-Attack.ImpactAt);
                GroundRing(Floor.ImpactPoint,Attack.TargetRadius*(.4f+.6f*FMath::Clamp(Age/.3f,0.f,1.f)),
                    40,FLinearColor(.7f,.94f,1.f,FMath::Clamp(1-Age/.5f,0.f,1.f)),48);
            }
        }
        for(const auto& Strike:Event.Strikes)
        {
            if(!Strike.bPrisonPunishment || !CanPresentStrike(Strike) || ServerTime>=Strike.ImpactAt
                || !FloorAt(Event,Strike.Anchor,Floor)) continue;
            const float Progress=FMath::Clamp(1-float((Strike.ImpactAt-ServerTime)
                /FMath::Max(.1f,Event.IcePrisonEscapeSeconds)),0.f,1.f);
            const float Pulse=.7f+.3f*FMath::Sin(float(ServerTime)*8+Progress*6);
            GroundRing(Floor.ImpactPoint,Event.IcicleRadius,20,DangerColor*Pulse,32);
            GroundRing(Floor.ImpactPoint,Event.IcicleRadius*(.15f+.85f*Progress),12,DangerColor,24);
        }
        if(WinterStrength>.02f && IsValid(Event.Tongue) && Event.Tongue->Surface)
        {
            // Sparse ribbons cross the whole tissue while the global phase is
            // active. Project each point to the live tongue; leave holes outside
            // it rather than showing wind across unreachable space.
            const FBox Bounds=Event.Tongue->Surface->Bounds.GetBox();
            const FVector Center=Bounds.GetCenter(),Extent=Bounds.GetExtent();
            constexpr int32 Segments=24;
            for(int32 Band=0;Band<3;++Band)
            {
                FHitResult Previous; bool bPrevious=false;
                for(int32 Segment=0;Segment<=Segments;++Segment)
                {
                    const float Fraction=Segment/float(Segments);
                    const FVector Point=Center+FVector(Extent.X*(Fraction*1.8f-.9f),
                        Extent.Y*((Band-1)*.48f+.12f*FMath::Sin(Fraction*PI*2+float(ServerTime)*.5f+Band)),0);
                    FHitResult Current;
                    const bool bCurrent=Event.Tongue->SurfacePoint(Point,Current);
                    if(bPrevious && bCurrent)
                    {
                        const int32 RangeStart=Indices.Num();
                        const int32 First=Vertices.Num();
                        const float Height=80+Band*50,Width=16+6*FMath::Sin(float(ServerTime)+Fraction*9);
                        auto& E=AmbientValues.NativeInputs.Entries;
                        if(E[0].PositionValues.Num()>=72)AmbientPayloadValid=false;
                        else {
                            E[0].PositionValues.Add(Previous.ImpactPoint);E[1].PositionValues.Add(Current.ImpactPoint);
                            E[2].VectorValues.Add(Previous.ImpactNormal);E[3].VectorValues.Add(Current.ImpactNormal);
                            // Artistic height/width are evaluated in the NS. These
                            // are the accepted phase clock, ordinal and native strength.
                            E[4].FloatValues.Add(float(Segment));E[5].FloatValues.Add(float(Band));
                            E[6].FloatValues.Add(float(ServerTime));E[7].FloatValues.Add(WinterStrength);
                        }
                        const FVector Points[]={Previous.ImpactPoint+Previous.ImpactNormal*(Height-Width),
                            Previous.ImpactPoint+Previous.ImpactNormal*(Height+Width),
                            Current.ImpactPoint+Current.ImpactNormal*(Height+Width),
                            Current.ImpactPoint+Current.ImpactNormal*(Height-Width)};
                        const FVector Tangent=(Current.ImpactPoint-Previous.ImpactPoint).GetSafeNormal();
                        for(int32 Corner=0;Corner<4;++Corner)
                        {
                            Vertices.Add(Transform.InverseTransformPosition(Points[Corner]));
                            Normals.Add(Transform.InverseTransformVectorNoScale(FVector::RightVector));
                            UV.Add(FVector2D((Segment-1+(Corner>=2?1:0))/float(Segments),Corner==1 || Corner==2?1.f:0.f));
                            Colors.Add(FLinearColor(.75f,.92f,1.f,WinterStrength*.65f));
                            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Tangent),false));
                        }
                        Indices.Append({First,First+2,First+1,First,First+3,First+2});
                        DecorativeRanges.Add({RangeStart,Indices.Num()-RangeStart,1});
                    }
                    Previous=Current; bPrevious=bCurrent;
                }
            }
        }
    }
    for(const auto& Crystal:Event.ZoneCrystals)
    {
        FHitResult Floor;
        if(Crystal.bLaunchedFromBoss && ServerTime<Crystal.SpawnedAt) continue;
        if(!FloorAt(Event,Crystal.Anchor,Floor)) continue;
        const float ImpactAge=float(ServerTime-Crystal.ImpactAt);
        if(ImpactAge>=Event.ZoneCrystalVortexSeconds) continue;
        const bool bFalling=ImpactAge<0;
        const float Weight=bFalling?.85f:1-FMath::SmoothStep(Event.ZoneCrystalVortexSeconds*.5f,Event.ZoneCrystalVortexSeconds,ImpactAge);
        const FVector Center=bFalling?CrystalPosition(Event,Crystal,Floor,ServerTime):Floor.ImpactPoint;
        Vortex(Center,bFalling?145.f:Event.ZoneCrystalVortexRadius,bFalling?280.f:180.f,
            float(ServerTime-Crystal.SpawnedAt),Weight,6,bFalling);
    }
    if(!bColdThrone && Event.BossAttack.Kind==EMCCandyAttack::Scream && ServerTime>=Event.BossAttack.ImpactAt && ServerTime<Event.BossAttack.EndsAt)
    {
        FHitResult Floor;
        if(FloorAt(Event,Event.BossAttack.TargetAnchor,Floor))
            Vortex(Floor.ImpactPoint,Event.BossAttack.TargetRadius,100,float(ServerTime-Event.BossAttack.ImpactAt),.9f,8,false);
    }
    auto Scalar=[](FMCPerkVFXGameplayValues& Values,const TCHAR* Name,float Value)
    {auto& Input=Values.NativeInputs.Entries.AddDefaulted_GetRef();Input.Name=Name;Input.Type=EMCVFXNativeInputType::Float;Input.FloatValue=Value;};
    const float Flow=bColdThrone?.6f+WinterStrength*.6f:bWarning?.8f:bBlast?1.7f:.5f;
    const float Opacity=bColdThrone?.42f:bWarning?.5f:bBlast?.65f:.4f;
    for(auto* Values:{&VortexValues,&AmbientValues})
    {Scalar(*Values,TEXT("User.Native.IceEventAge"),float(ComposerEventAge));
        Scalar(*Values,TEXT("User.Native.IceSheetFlow"),Flow);Scalar(*Values,TEXT("User.Native.IceSheetOpacity"),Opacity);
        auto& Count=Values->NativeInputs.Entries.AddDefaulted_GetRef();Count.Name=TEXT("User.Native.IceSheetCount");
        Count.Type=EMCVFXNativeInputType::Int;Count.IntValue=Values->NativeInputs.Entries[0].PositionValues.Num();}
    const bool OriginalVortex=UseOriginalSheet(TEXT("WindSheetVortex"),VortexValues,VortexPayloadValid&&!VortexValues.NativeInputs.Entries[0].PositionValues.IsEmpty());
    const bool OriginalAmbient=UseOriginalSheet(TEXT("WindSheetAmbient"),AmbientValues,AmbientPayloadValid&&!AmbientValues.NativeInputs.Entries[0].PositionValues.IsEmpty());
    if(!OriginalVortex||!OriginalAmbient)
    {
        TArray<int32> Retained;Retained.Reserve(Indices.Num());int32 Cursor=0;
        for(const auto& Range:DecorativeRanges)
        {
            if(Range.First>Cursor)Retained.Append(Indices.GetData()+Cursor,Range.First-Cursor);
            if(Range.Kind==0?OriginalVortex:OriginalAmbient)Retained.Append(Indices.GetData()+Range.First,Range.Count);
            Cursor=Range.First+Range.Count;
        }
        if(Cursor<Indices.Num())Retained.Append(Indices.GetData()+Cursor,Indices.Num()-Cursor);
        Indices=MoveTemp(Retained);
    }
    const auto* Existing=WindSheet->GetProcMeshSection(0);
    bool SameTopology=Existing && Existing->ProcIndexBuffer.Num()==Indices.Num();
    if(SameTopology)for(int32 I=0;I<Indices.Num();++I)if(Existing->ProcIndexBuffer[I]!=uint32(Indices[I])){SameTopology=false;break;}
    if(Existing && Existing->ProcVertexBuffer.Num()==Vertices.Num() && SameTopology)
        WindSheet->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,Tangents);
    else WindSheet->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}

void AMCIceEventVFX::StopLoops()
{
    bCollectComposerLoops=false;ComposerLoopCalls.Reset();ComposerLoopMemory.Reset();ComposerLoopOverflow.Reset();ComposerLoopEpisodes.Reset();ComposerLoopStarted.Reset();
    for(int32 I=0;I<2;++I){ComposerSheetEpisodes[I].Invalidate();ComposerSheetStarted[I]=0;}
    if(ComposerPresentation)ComposerPresentation->StopStates();
    for(auto* Component:{Wind.Get(),Snow.Get(),CoreCharge.Get(),BombCharge.Get()})
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:WinterWisps)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:WinterSnow)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:CrystalCharges)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:CrystalTrails)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:VortexWisps)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:VortexSnow)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:FootMotes)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:WarmMotes)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:IcicleCharges)
        if(IsValid(Component)) Component->DeactivateImmediate();
    WindSheet->SetVisibility(false);
    for(UStaticMeshComponent* Facet:CrystalFacets)
        if(IsValid(Facet)) Facet->SetVisibility(false);
    for(UStaticMeshComponent* Facet:BombFacets)
        if(IsValid(Facet)) Facet->SetVisibility(false);
    for(UStaticMeshComponent* Facet:FootFacets)
        if(IsValid(Facet)) Facet->SetVisibility(false);
    if(IsValid(CoreHealthLabel)) CoreHealthLabel->SetVisibility(false);
    for(UTextRenderComponent* Label:CrystalHealthLabels)
        if(IsValid(Label)) Label->SetVisibility(false);
    ComposerFacetEpisodes.Reset();ComposerFacetStarted.Reset();
    PreviousCrystals.Reset(); PreviousFeet.Reset(); WinterStrength=0;
}

bool AMCIceEventVFX::PrepareForLocation(FGuid Token)
{
    if(!Token.IsValid()||!HasActorBegunPlay()||bPreparedForLocation||bWasActive||bFinishing||!Bursts.IsEmpty())return false;
    // The location manifest has already loaded this original parent. Do not
    // synchronously load it or construct an Ice gameplay actor during prewarm.
    auto* FrostParent=FindObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
    if(!FrostParent)return false;
    PreparedFrostSurfaceMID=UMaterialInstanceDynamic::Create(FrostParent,this);
    if(!PreparedFrostSurfaceMID)return false;
    PreparedFrostSurfaceFactory=NewObject<UProceduralMeshComponent>(this,NAME_None,RF_Transient);
    PreparedFrostSurfaceFactory->SetupAttachment(GetRootComponent());
    PreparedFrostSurfaceFactory->SetAbsolute(true,true,true);
    PreparedFrostSurfaceFactory->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreparedFrostSurfaceFactory->SetCanEverAffectNavigation(false);
    PreparedFrostSurfaceFactory->SetCastShadow(false);
    PreparedFrostSurfaceFactory->SetMaterial(0,PreparedFrostSurfaceMID);
    PreparedFrostSurfaceFactory->SetVisibility(false);
    EnsureFrozenFootCapacity(8);
    // Use the exact procedural component/MID and native vertex factory before
    // readiness. This hidden factory quad never represents gameplay geometry.
    const TArray<FVector> V={FVector(-1,-1,0),FVector(1,-1,0),FVector(1,1,0),FVector(-1,1,0)};
    const TArray<int32> I={0,1,2,0,2,3};
    const TArray<FVector> N={FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector};
    const TArray<FVector2D> UV={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};
    const TArray<FLinearColor> C={FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White};
    const TArray<FProcMeshTangent> T={FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0),FProcMeshTangent(1,0,0)};
    WindSheet->CreateMeshSection_LinearColor(0,V,I,N,UV,C,T,false);
    PreparedFrostSurfaceFactory->CreateMeshSection_LinearColor(0,V,I,N,UV,C,T,false);
    PreparedFrostSurfaceFactory->RegisterComponent();
    StopLoops();SetActorHiddenInGame(true);SetActorTickEnabled(false);SetLifeSpan(0);
    PreparedClaimToken=Token;bPreparedForLocation=true;
    return HasRequiredAssets()&&FootFacets.Num()==48&&FootMotes.Num()==16
        &&!FootFacets.ContainsByPredicate([](const auto& Part){return !IsValid(Part.Get());})
        &&!FootMotes.ContainsByPredicate([](const auto& Part){return !IsValid(Part.Get());});
}

void AMCIceEventVFX::GetLocationPreparedResources(TArray<UObject*>& Out) const
{
    for(const auto& Asset:LoadedAssets)if(IsValid(Asset.Get()))Out.AddUnique(Asset.Get());
    if(IsValid(SheetMID.Get()))Out.AddUnique(SheetMID.Get());
    if(IsValid(PreparedFrostSurfaceMID.Get()))Out.AddUnique(PreparedFrostSurfaceMID.Get());
}

bool AMCIceEventVFX::ClaimPreparedPresentation(AMCIceEvent* Source,FGuid Token)
{
    if(!bPreparedForLocation||bPreparedClaimed||Token!=PreparedClaimToken||!Token.IsValid()
        ||!IsValid(Source)||Source->GetWorld()!=GetWorld()||GetWorld()->bIsTearingDown)return false;
    // Transfer the exact already-prepared variant. Refuse a foreign material
    // or MID instead of silently replacing its parent or authored overrides.
    if(!IsValid(PreparedFrostSurfaceMID.Get())||!IsValid(Source->FrostSurface.Get())
        ||Source->FrostSurface->GetMaterial(0)!=PreparedFrostSurfaceMID->Parent.Get())return false;
    Source->FrostSurface->SetMaterial(0,PreparedFrostSurfaceMID);
    PreparedClaimSource=Source;bPreparedClaimed=true;SetOwner(Source);
    SetActorHiddenInGame(false);SetActorTickEnabled(true);return true;
}

bool AMCIceEventVFX::CancelPresentationFor(const AMCIceEvent* Source,FGuid Token)
{
    if(bPreparedForLocation)
    {
        if(!bPreparedClaimed||Token!=PreparedClaimToken||PreparedClaimSource.Get()!=Source)return false;
    }
    else if(GetOwner()!=Source)return false;
    // Only this actor's source/channel and original bursts. Other actors' World
    // one-shots are not stopped. Successful native1.65 retirement does not call this.
    if(ComposerPresentation){if(auto* Channel=ComposerPresentation->GetSource(TEXT("World.Ice")))Channel->StopPresentation(true);
        if(auto* Surface=ComposerPresentation->GetSource(TEXT("World.Ice.Surface")))Surface->StopPresentation(true);}
    StopLoops();while(!Bursts.IsEmpty())RetireBurst(Bursts.Last());return true;
}

void AMCIceEventVFX::FinishPresentation(FVector CoreFloorPoint)
{
    if(bFinishing) return;
    if(!CoreFloorPoint.ContainsNaN()) LastCorePoint=CoreFloorPoint;
    bFinishing=true;
    StopLoops();
    PlayBurst(ShardSystem.Get(),nullptr,LastCorePoint+FVector(0,0,180),1.2f,FVector(.18,.23,.45),FVector2D::ZeroVector,IceColor,TEXT("Finish"));
    PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,40),1.05f,FVector::OneVector,FVector2D(190,140),FLinearColor(.55f,.84f,1,.7f),TEXT("Finish"),false);
    SetLifeSpan(1.65f);
}

void AMCIceEventVFX::EndPlay(const EEndPlayReason::Type Reason)
{
    StopLoops();
    while(!Bursts.IsEmpty()) RetireBurst(Bursts.Last());
    Super::EndPlay(Reason);
}
