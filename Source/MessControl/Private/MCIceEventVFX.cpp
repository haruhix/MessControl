#include "MCIceEventVFX.h"
#include "MCIceEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

bool FloorAt(const AMCIceEvent& Event, FVector Anchor, FHitResult& Hit)
{
    return IsValid(Event.Tongue) && Event.Tongue->SurfacePoint(Event.Tongue->GetActorTransform().TransformPosition(Anchor),Hit);
}

FVector FeetAt(const AMCToothCharacter& Hero)
{
    return Hero.GetActorLocation()-FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-20);
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
    LoadedAssets.Reserve(12);
    Retain(WindSystem.LoadSynchronous()); Retain(SnowSystem.LoadSynchronous()); Retain(MistSystem.LoadSynchronous());
    Retain(ShardSystem.LoadSynchronous()); Retain(ChargeSystem.LoadSynchronous());
    Retain(WindSheetMaterial.LoadSynchronous()); Retain(WispMaterial.LoadSynchronous()); Retain(SnowMaterial.LoadSynchronous());
    Retain(MistMaterial.LoadSynchronous()); Retain(ChargeMaterial.LoadSynchronous());
    Retain(CrystalMesh.LoadSynchronous()); Retain(CrystalMaterial.LoadSynchronous());
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
    }
    Wind=MakeLoop(TEXT("WindWisps"),WindSystem.Get(),WispMaterial.Get());
    Snow=MakeLoop(TEXT("WindSnow"),SnowSystem.Get(),SnowMaterial.Get());
    CoreCharge=MakeLoop(TEXT("CoreCharge"),ChargeSystem.Get(),ChargeMaterial.Get());
    for(int32 I=0;I<2;++I)
    {
        CrystalCharges.Add(MakeLoop(*FString::Printf(TEXT("BlockedZoneCharge%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
        WarmMotes.Add(MakeLoop(*FString::Printf(TEXT("WarmMotes%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
    }
    for(int32 I=0;I<3;++I)
        IcicleCharges.Add(MakeLoop(*FString::Printf(TEXT("IcicleCharge%d"),I),ChargeSystem.Get(),ChargeMaterial.Get()));
    if(auto* Material=WindSheetMaterial.Get())
    {
        SheetMID=UMaterialInstanceDynamic::Create(Material,this);
        WindSheet->SetMaterial(0,SheetMID);
        SheetMID->SetVectorParameterValue(TEXT("Tint"),IceColor);
    }
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

void AMCIceEventVFX::UpdateFromEvent(const AMCIceEvent& Event, float DeltaSeconds, double ServerTime)
{
    if(bFinishing || !Event.IsActive()) return;
    FHitResult CoreFloor;
    if(!FloorAt(Event,Event.CandyAnchor,CoreFloor)) return;
    LastCorePoint=CoreFloor.ImpactPoint;
    if(HasCrystalVisuals() && ServerTime>=LastCrystalUpdate+.1)
    {
        LastCrystalUpdate=ServerTime;
        for(int32 I=0;I<12;++I)
        {
            FTransform Transform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector);
            if(I<6)
            {
                const float Angle=I*2*PI/5;
                const FVector Offset=I==0?FVector::ZeroVector:FVector(FMath::Cos(Angle)*94,FMath::Sin(Angle)*94,-50+I*5);
                const FVector Scale=I==0?FVector(1.8,1.5,3.5):FVector(1.05,1.15,2.5+(I%2)*.45);
                Transform=FTransform(FRotator(I==0?0:8,Angle*180/PI,0),Event.GetActorLocation()+Offset,Scale);
            }
            else
            {
                const int32 Zone=(I-6)/3,Spire=(I-6)%3;
                FHitResult Floor;
                if(Event.ZoneCrystals.IsValidIndex(Zone) && FloorAt(Event,Event.ZoneCrystals[Zone].Anchor,Floor))
                {
                    const float Angle=Spire*2*PI/3;
                    Transform=FTransform(FRotator(Spire*6,Angle*180/PI,0),
                        Floor.ImpactPoint+FVector(FMath::Cos(Angle)*19,FMath::Sin(Angle)*19,80),FVector(.58,.55,1.15+Spire*.03));
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
        for(const auto& Strike:Event.Strikes)
            if(ServerTime>Strike.ImpactAt+.5) LastImpactStrikeId=FMath::Max(LastImpactStrikeId,Strike.Id);
        for(const auto& Crystal:Event.ZoneCrystals) PreviousCrystals.Add(Crystal.ZoneIndex,{Crystal.Anchor,Crystal.Health});
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
        if(Strike.Id<=LastImpactStrikeId || ServerTime<Strike.ImpactAt) continue;
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
        Crystals.Add(Crystal.ZoneIndex,{Crystal.Anchor,Crystal.Health});
        const auto* Previous=PreviousCrystals.Find(Crystal.ZoneIndex);
        FHitResult Floor;
        if(!FloorAt(Event,Crystal.Anchor,Floor)) continue;
        if(!Previous)
            PlayBurst(MistSystem.Get(),MistMaterial.Get(),Floor.ImpactPoint+FVector(0,0,20),.45f,FVector::OneVector,FVector2D(65,50),IceColor);
        else if(Crystal.Health<Previous->Health)
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
        const bool bEnabled=Event.ZoneCrystals.IsValidIndex(I) && FloorAt(Event,Event.ZoneCrystals[I].Anchor,Floor);
        SetLoop(CrystalCharges[I],bEnabled,bEnabled?Floor.ImpactPoint+FVector(0,0,185):FVector::ZeroVector,
            FRotator::ZeroRotator,9.f,.35f,FVector2D(9,9),FLinearColor(.2f,.64f,1,.8f));
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
        if(ChargeIndex>=IcicleCharges.Num() || ServerTime>=Strike.ImpactAt || !FloorAt(Event,Strike.Anchor,Floor)) continue;
        const float Progress=FMath::Clamp(1-float((Strike.ImpactAt-ServerTime)/Event.IcicleWarningSeconds),0.f,1.f);
        SetLoop(IcicleCharges[ChargeIndex++],true,Floor.ImpactPoint+Floor.ImpactNormal*35,
            FRotator::ZeroRotator,6+12*Progress,.28f,FVector2D(6+4*Progress,6+4*Progress),FLinearColor(.35f,.8f,1,.75f));
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
    const bool bBlast=BlastAge>=0 && BlastAge<1.25f;
    const float ChargeProgress=bWarning?FMath::Clamp(1-float((Event.NovaImpactAt-ServerTime)/Event.NovaWarningSeconds),0.f,1.f):0.f;
    const float Strength=bWarning?.22f+.35f*ChargeProgress:bBlast?FMath::Clamp(1-BlastAge/1.25f,0.f,1.f):0.f;
    const FVector Direction=Event.Tongue->GetActorTransform().TransformVectorNoScale(Event.NovaDirection).GetSafeNormal2D();
    SetLoop(CoreCharge,true,Event.GetActorLocation()+FVector(0,0,200),FRotator::ZeroRotator,
        bWarning?14+28*ChargeProgress:7.f,bWarning?.75f:.3f,FVector2D(bWarning?14:8,bWarning?14:8),IceColor);
    SetLoop(Wind,bWarning || bBlast,LastCorePoint+Direction*145+FVector(0,0,85),Direction.Rotation(),
        bWarning?18+14*ChargeProgress:52*Strength,bWarning?.4f:1.7f,FVector2D(bWarning?85:155,bWarning?25:38),FLinearColor(.4f,.76f,1,bWarning?.55f:.8f));
    SetLoop(Snow,bBlast,LastCorePoint+Direction*180+FVector(0,0,110),Direction.Rotation(),
        78*Strength,1.4f,FVector2D(3,17),FLinearColor(.7f,.91f,1,.8f));
    WindSheet->SetVisibility(Strength>0 && SheetMID);
    if(Strength>0 && SheetMID)
    {
        SheetMID->SetScalarParameterValue(TEXT("Age"),float(ServerTime-Event.NovaImpactAt+Event.NovaWarningSeconds));
        SheetMID->SetScalarParameterValue(TEXT("FlowSpeed"),bWarning?.45f:1.7f);
        SheetMID->SetScalarParameterValue(TEXT("Opacity"),bWarning?.22f+.18f*ChargeProgress:Strength*.58f);
        if(ServerTime>=LastSheetUpdate+.05)
        {
            LastSheetUpdate=ServerTime;
            UpdateWindSheet(Event,ServerTime,Strength,bWarning);
        }
    }
}

void AMCIceEventVFX::UpdateWindSheet(const AMCIceEvent& Event, double ServerTime, float Strength, bool bWarning)
{
    constexpr int32 Bands=10,Steps=6;
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Indices;
    Vertices.Reserve(Bands*(Steps+1)*2); Normals.Reserve(Vertices.Max());
    UV.Reserve(Vertices.Max()); Colors.Reserve(Vertices.Max()); Tangents.Reserve(Vertices.Max());
    const FVector Direction=Event.Tongue->GetActorTransform().TransformVectorNoScale(Event.NovaDirection).GetSafeNormal2D();
    const float Time=float(ServerTime-Event.NovaImpactAt);
    const float Reach=bWarning?Event.NovaRange*.82f:Event.NovaRange*FMath::Clamp(.35f+Time*1.8f,.35f,1.f);
    for(int32 Band=0;Band<Bands;++Band)
    {
        const int32 First=Vertices.Num();
        const float Fan=(Band%5)/4.f*2-1;
        const FVector Ray=Direction.RotateAngleAxis(Fan*Event.NovaHalfAngleDegrees*.8f,FVector::UpVector);
        const FVector Across=FVector::CrossProduct(FVector::UpVector,Ray);
        for(int32 Step=0;Step<=Steps;++Step)
        {
            const float Fraction=Step/float(Steps);
            const float Distance=120+Fraction*FMath::Max(1.f,Reach-120);
            const float Sway=FMath::Sin(Fraction*5+Time*2+Band*1.9f)*35*Fraction;
            const float Width=(bWarning?13.f:35.f)+(bWarning?24.f:70.f)*Fraction;
            const float Height=45+(Band/5)*105+FMath::Sin(Fraction*4+Band)*30*Fraction;
            for(int32 Side=0;Side<2;++Side)
            {
                FVector Point=LastCorePoint+Ray*Distance+Across*(Sway+(Side?Width:-Width));
                FHitResult Floor;
                if(Event.Tongue->SurfacePoint(Point,Floor)) Point.Z=Floor.ImpactPoint.Z;
                Point.Z+=Height;
                Vertices.Add(Point); Normals.Add(FVector::UpVector);
                UV.Add(FVector2D(Fraction,Side));
                const float EdgeAlpha=FMath::Clamp(Fraction*5,0.f,1.f)*(1-Fraction);
                Colors.Add(FLinearColor(.6f,.84f,1.f,EdgeAlpha*(bWarning?1.f:Strength)));
                Tangents.Add(FProcMeshTangent(Ray,false));
            }
            if(Step>0)
            {
                const int32 A=First+(Step-1)*2;
                Indices.Append({A,A+3,A+1,A,A+2,A+3});
            }
        }
    }
    if(WindSheet->GetNumSections()>0)
        WindSheet->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,Tangents);
    else WindSheet->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}

void AMCIceEventVFX::StopLoops()
{
    for(auto* Component:{Wind.Get(),Snow.Get(),CoreCharge.Get()})
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:CrystalCharges)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:WarmMotes)
        if(IsValid(Component)) Component->DeactivateImmediate();
    for(UNiagaraComponent* Component:IcicleCharges)
        if(IsValid(Component)) Component->DeactivateImmediate();
    WindSheet->SetVisibility(false);
    for(UStaticMeshComponent* Facet:CrystalFacets)
        if(IsValid(Facet)) Facet->SetVisibility(false);
    PreviousCrystals.Reset(); PreviousFeet.Reset();
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
