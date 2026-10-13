#include "MCIceEventVFX.h"
#include "MCIceEvent.h"
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
        Label->RegisterComponent(); AddInstanceComponent(Label); return Label;
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

void AMCIceEventVFX::SetLoop(UNiagaraComponent* Component, bool bEnabled, FVector Position, FRotator Rotation,
    float SpawnRate, float Speed, FVector2D Size, FLinearColor Color)
{
    if(!IsValid(Component)) return;
    if(!bEnabled)
    {
        if(Component->IsActive()) Component->Deactivate();
        return;
    }
    Component->SetWorldLocationAndRotation(Position,Rotation);
    Component->SetVariableFloat(RateParameter,SpawnRate);
    Component->SetVariableFloat(SpeedParameter,Speed);
    Component->SetVariableVec2(SizeParameter,Size);
    Component->SetVariableLinearColor(ColorParameter,Color);
    if(!Component->IsActive()) Component->Activate(true);
}

void AMCIceEventVFX::PlayBurst(UNiagaraSystem* System, UMaterialInterface* Material, FVector Position,
    float Speed, FVector Scale, FVector2D Size, FLinearColor Color)
{
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

void AMCIceEventVFX::UpdateFrozenFeet(const AMCIceEvent& Event)
{
    const int32 PlayerCount=FMath::Min(Event.Players.Num(),8);
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
        Label->SetText(FText::FromString(FString::Printf(TEXT("%d / %d HP"),FMath::CeilToInt(Health),FMath::CeilToInt(MaxHealth))));
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
            FVector::OneVector,FVector2D(330,270),FLinearColor(.9f,.97f,1.f,.8f));
    bWasGlobalCold=bGlobal;

    FBox Bounds(ForceInit);
    if(IsValid(Event.Tongue) && Event.Tongue->Surface) Bounds=Event.Tongue->Surface->Bounds.GetBox();
    const FVector Center=Bounds.IsValid?Bounds.GetCenter():LastCorePoint;
    const FVector Extent=Bounds.IsValid?Bounds.GetExtent():FVector(900,900,0);
    const float FrontRadius=bFront?Event.PhaseTransitionRadius():0.f;
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
                FVector2D(PowderWidth,PowderWidth*Aspects[Layer]),PowderColors[Layer]);
            // Extra layers carry powder only; retain the original snow budget.
            SetLoop(WinterSnow[Sector],bEnabled && Layer==0,Point+FVector(0,0,45),Direction.Rotation(),24*Fade,2.2f,
                FVector2D(4.5f,28),FLinearColor(.85f,.97f,1.f,.95f));
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
            FVector2D(145,30),FLinearColor(.64f,.86f,1.f,.55f));
        SetLoop(WinterSnow[Sector],bEnabled,Point+FVector(0,0,65),Direction.Rotation(),24*WinterStrength,1.35f,
            FVector2D(3.5f,20),FLinearColor(.8f,.96f,1.f,.8f));
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
        FRotator::ZeroRotator,9+24*Progress,.3f,FVector2D(7+5*Progress,7+5*Progress),DangerColor);

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
    if(bBomb && ServerTime>=Attack.ImpactAt && Attack.Serial>LastBombImpactSerial)
    {
        LastBombImpactSerial=Attack.Serial;
        if(ServerTime<=Attack.ImpactAt+.5)
        {
            const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*22;
            PlayBurst(ShardSystem.Get(),nullptr,Point,1.35f,FVector(.11,.14,.24),FVector2D::ZeroVector,IceColor);
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.4f,FVector::OneVector,FVector2D(150,110),IceColor);
        }
    }
}

void AMCIceEventVFX::UpdateFromEvent(const AMCIceEvent& Event, float DeltaSeconds, double ServerTime)
{
    if(bFinishing || !Event.IsActive()) return;
    FHitResult CoreFloor;
    if(!FloorAt(Event,Event.CandyAnchor,CoreFloor)) return;
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
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,20),.7f,FVector::OneVector,FVector2D(110,85),IceColor);
    bWasActive=bActive;
    if(Event.CandyHealth<PreviousCoreHealth && Event.CandyHealth>0)
        PlayBurst(ShardSystem.Get(),nullptr,LastCorePoint+FVector(0,0,170),.55f,FVector(.05,.065,.12),FVector2D::ZeroVector,IceColor);
    PreviousCoreHealth=Event.CandyHealth;

    for(const auto& Strike:Event.Strikes)
    {
        if(!CanPresentStrike(Strike) || Strike.Id<=LastImpactStrikeId || ServerTime<Strike.ImpactAt) continue;
        LastImpactStrikeId=Strike.Id;
        FHitResult Floor;
        if(ServerTime>Strike.ImpactAt+.5 || !FloorAt(Event,Strike.Anchor,Floor)) continue;
        const FVector Point=Floor.ImpactPoint+Floor.ImpactNormal*18;
        PlayBurst(ShardSystem.Get(),nullptr,Point,1.f,FVector(.075,.095,.19),FVector2D::ZeroVector,IceColor);
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.1f,FVector::OneVector,FVector2D(115,90),FLinearColor(.53f,.78f,.95f,.75f));
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
            PlayBurst(ShardSystem.Get(),nullptr,Point,1.35f,FVector(.1,.13,.24),FVector2D::ZeroVector,IceColor);
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,1.45f,FVector::OneVector,FVector2D(155,110),IceColor);
        }
        else if(!bNewCrystal && Crystal.Health<Previous->Health)
            PlayBurst(ShardSystem.Get(),nullptr,Floor.ImpactPoint+FVector(0,0,90),.45f,FVector(.045,.06,.1),FVector2D::ZeroVector,IceColor);
    }
    for(const auto& Previous:PreviousCrystals)
    {
        // The old zone expiring is a fade, while a crystal disappearing from a
        // still-live zone is the player's successful pickaxe shatter.
        if(Crystals.Contains(Previous.Key) || Previous.Key<Event.CircleIndex) continue;
        FHitResult Floor;
        if(!FloorAt(Event,Previous.Value.Anchor,Floor)) continue;
        PlayBurst(ShardSystem.Get(),nullptr,Floor.ImpactPoint+FVector(0,0,80),.85f,FVector(.065,.085,.17),FVector2D::ZeroVector,IceColor);
        PlayBurst(MistSystem.Get(),MistMaterial.Get(),Floor.ImpactPoint+FVector(0,0,20),.55f,FVector::OneVector,FVector2D(70,55),FLinearColor(.58f,.86f,1,.6f));
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
            bFalling?23.f:12.f,bFalling?.6f:.35f,FVector2D(bFalling?13:9,bFalling?13:9),FLinearColor(.2f,.64f,1,.9f));
        const bool bVortex=bEnabled && ServerTime<Event.ZoneCrystals[I].ImpactAt+Event.ZoneCrystalVortexSeconds;
        const float Angle=bEnabled?float(ServerTime-Event.ZoneCrystals[I].SpawnedAt)*4.5f:0.f;
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector Tangent(-Ray.Y,Ray.X,bFalling?-.4f:.15f);
        SetLoop(CrystalTrails[I],bVortex,Center+Ray*(bFalling?95.f:Event.ZoneCrystalVortexRadius*.65f),Tangent.Rotation(),
            bFalling?28.f:19.f,bFalling?1.1f:.85f,FVector2D(90,26),FLinearColor(.42f,.8f,1.f,.75f));
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
            Strike.bPrisonPunishment?DangerColor:FLinearColor(.35f,.8f,1,.75f));
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
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,.35f,FVector::OneVector,FVector2D(50,40),IceColor);
        else if(Health<*Previous)
        {
            PlayBurst(ShardSystem.Get(),nullptr,Point,Health>0?.35f:.6f,FVector(.035,.05,.08),FVector2D::ZeroVector,IceColor);
            if(Health<=0)
                PlayBurst(MistSystem.Get(),MistMaterial.Get(),Point,.4f,FVector::OneVector,FVector2D(42,35),FLinearColor(.6f,.88f,1,.5f));
        }
    }
    PreviousFeet=MoveTemp(Feet);

    if(Event.NovaImpactAt>LastNovaImpact && ServerTime>=Event.NovaImpactAt && Event.NovaImpactAt>0)
    {
        LastNovaImpact=Event.NovaImpactAt;
        if(ServerTime<=Event.NovaImpactAt+.5)
        {
            BlastStartedAt=Event.NovaImpactAt;
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,120),.75f,FVector::OneVector,FVector2D(90,65),IceColor);
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
        FVector2D(bPhaseCharge?24+18*PhaseCharge:bWarning?14:8,bPhaseCharge?24+18*PhaseCharge:bWarning?14:8),IceColor);
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
            FVector2D(bBlast?150:105,bBlast?40:29),FLinearColor(.4f,.76f,1,.8f));
        SetLoop(SnowTrail,Strength>0,Position+FVector(0,0,35),Tangent.Rotation(),6+20*Strength,bBlast?1.5f:.55f,
            FVector2D(3,bBlast?20:12),FLinearColor(.7f,.91f,1,.8f));
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
}

void AMCIceEventVFX::UpdateWindSheet(const AMCIceEvent& Event, double ServerTime, float Strength, bool bWarning)
{
    constexpr int32 Steps=16;
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Indices;
    Vertices.Reserve(24*(Steps+1)*2); Normals.Reserve(Vertices.Max());
    UV.Reserve(Vertices.Max()); Colors.Reserve(Vertices.Max()); Tangents.Reserve(Vertices.Max());
    const FTransform Transform=WindSheet->GetComponentTransform();
    auto Vortex=[&](FVector Center,float Radius,float Height,float Age,float Weight,int32 Bands,bool bFalling)
    {
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
                        const int32 First=Vertices.Num();
                        const float Height=80+Band*50,Width=16+6*FMath::Sin(float(ServerTime)+Fraction*9);
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
    const auto* Existing=WindSheet->GetProcMeshSection(0);
    if(Existing && Existing->ProcVertexBuffer.Num()==Vertices.Num() && Existing->ProcIndexBuffer.Num()==Indices.Num())
        WindSheet->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,Tangents);
    else WindSheet->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}

void AMCIceEventVFX::StopLoops()
{
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
    PreviousCrystals.Reset(); PreviousFeet.Reset(); WinterStrength=0;
}

void AMCIceEventVFX::FinishPresentation(FVector CoreFloorPoint)
{
    if(bFinishing) return;
    if(!CoreFloorPoint.ContainsNaN()) LastCorePoint=CoreFloorPoint;
    bFinishing=true;
    StopLoops();
    PlayBurst(ShardSystem.Get(),nullptr,LastCorePoint+FVector(0,0,180),1.2f,FVector(.18,.23,.45),FVector2D::ZeroVector,IceColor);
    PlayBurst(MistSystem.Get(),MistMaterial.Get(),LastCorePoint+FVector(0,0,40),1.05f,FVector::OneVector,FVector2D(190,140),FLinearColor(.55f,.84f,1,.7f));
    SetLifeSpan(1.65f);
}

void AMCIceEventVFX::EndPlay(const EEndPlayReason::Type Reason)
{
    StopLoops();
    while(!Bursts.IsEmpty()) RetireBurst(Bursts.Last());
    Super::EndPlay(Reason);
}
