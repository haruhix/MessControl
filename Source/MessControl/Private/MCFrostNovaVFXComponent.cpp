#include "MCFrostNovaVFXComponent.h"

#include "MCHazardWave.h"
#include "MCSmallMintCandy.h"
#include "MCTongue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCFrostNova, Log, All);

UMCFrostNovaVFXComponent::UMCFrostNovaVFXComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(false);
    FrontMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/FrostNova/M_FrostNovaFront.M_FrostNovaFront")));
    CrystalMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/FrostNova/M_FrostNovaCrystal.M_FrostNovaCrystal")));
    CrystalMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/SM_IceShard.SM_IceShard")));
    BurstSystem=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Gameplay/Cold/VFX/FrostNova/NS_FrostNovaBurst.NS_FrostNovaBurst")));
}

double UMCFrostNovaVFXComponent::ServerNow() const
{
    const UWorld* World=GetWorld();
    if(!World) return 0;
    const AGameStateBase* State=World->GetGameState();
    return State?State->GetServerWorldTimeSeconds():World->GetTimeSeconds();
}

bool UMCFrostNovaVFXComponent::HasRequiredAssets() const
{
    return IsValid(Front) && IsValid(FrontMID) && IsValid(Crystals) && CrystalMesh.IsValid();
}

bool UMCFrostNovaVFXComponent::PrepareVisuals()
{
    if(HasRequiredAssets()) return true;
    if(bTriedAssets || !GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer || !IsValid(GetOwner())) return false;
    bTriedAssets=true;
    UMaterialInterface* Ground=FrontMaterial.LoadSynchronous();
    UMaterialInterface* Crystal=CrystalMaterial.LoadSynchronous();
    UStaticMesh* Mesh=CrystalMesh.LoadSynchronous();
    UNiagaraSystem* System=BurstSystem.LoadSynchronous();
    if(!Ground || !Crystal || !Mesh)
    {
        UE_LOG(LogMCFrostNova, Warning, TEXT("MC_FROST_NOVA_ART_MISSING owner=%s legacyWarningPreserved=1"), *GetNameSafe(GetOwner()));
        return false;
    }
    LoadedAssets.Add(Ground);LoadedAssets.Add(Crystal);LoadedAssets.Add(Mesh);
    if(System) LoadedAssets.Add(System);
    AActor* Owner=GetOwner();
    Front=NewObject<UMCFrostNovaFrontComponent>(Owner,NAME_None,RF_Transient);
    Front->ComponentTags.Add(TEXT("MC.FrostNova.Front"));
    Front->SetupAttachment(Owner->GetRootComponent());
    Front->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Front->SetGenerateOverlapEvents(false);
    Front->SetCanEverAffectNavigation(false);
    Front->SetCastShadow(false);
    Front->bReceivesDecals=false;
    FrontMID=UMaterialInstanceDynamic::Create(Ground,this);
    Front->SetMaterial(0,FrontMID);Front->SetMaterial(1,FrontMID);
    Front->RegisterComponent();Owner->AddInstanceComponent(Front);
    Crystals=NewObject<UInstancedStaticMeshComponent>(Owner,NAME_None,RF_Transient);
    Crystals->ComponentTags.Add(TEXT("MC.FrostNova.Crystals"));
    Crystals->SetupAttachment(Owner->GetRootComponent());
    Crystals->SetStaticMesh(Mesh);Crystals->SetMaterial(0,Crystal);
    Crystals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Crystals->SetGenerateOverlapEvents(false);
    Crystals->SetCanEverAffectNavigation(false);
    Crystals->SetCastShadow(false);Crystals->bReceivesDecals=false;
    Crystals->SetNumCustomDataFloats(1);
    CrystalCount=FMath::Clamp(CrystalCount,8,32);
    CrystalPoses.SetNum(CrystalCount);
    for(int32 I=0;I<CrystalCount;++I)
        Crystals->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector));
    // Keep a render proxy alive during preparation. Zero-scale instances and
    // zero dissolve data have no visible footprint, but initialize the actual
    // instance buffers instead of deferring that work until the first pulse.
    Crystals->SetVisibility(true);
    Crystals->RegisterComponent();Owner->AddInstanceComponent(Crystals);
    Crystals->PrecachePSOs();
    if(System)
    {
        Burst=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient);
        Burst->ComponentTags.Add(TEXT("MC.FrostNova.Burst"));
        Burst->SetupAttachment(Owner->GetRootComponent());Burst->SetAutoActivate(false);Burst->SetAsset(System);
        // Candy body scale is an art choice; particle sizes/speeds stay in cm.
        Burst->SetAbsolute(false,true,true);
        Burst->SetAutoDestroy(false);Burst->SetCollisionEnabled(ECollisionEnabled::NoCollision);Burst->SetCastShadow(false);
        Burst->RegisterComponent();Owner->AddInstanceComponent(Burst);
        // The candy prepares this during its fall; generic callers use warning.
        // This does not advance simulation or show particles before the wave.
        if(System->IsReadyToRun())
        {
            Burst->InitializeSystem();
            Burst->PrecachePSOs();
        }
    }
    return true;
}

bool UMCFrostNovaVFXComponent::UpdateWave(const AMCHazardWave& Wave)
{
    if(!CachedTongue.IsValid())
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It) {CachedTongue=*It;break;}
    const double ImpactAt=Wave.StartedAt+Wave.WarningSeconds;
    const float WarningProgress=FMath::Clamp(1.f-float((ImpactAt-ServerNow())/FMath::Max(.01f,Wave.WarningSeconds)),0.f,1.f);
    return PresentWave(CachedTongue.Get(),Wave.GetActorLocation(),Wave.Radius(),Wave.MaxRadius,
        ImpactAt,Wave.TravelSeconds,WarningProgress);
}

bool UMCFrostNovaVFXComponent::PresentWave(AMCTongue* Tongue,FVector Origin,float Radius,float MaxRadius,
    double ImpactAt,float TravelSeconds,float WarningProgress)
{
    if(!IsValid(Tongue) || Origin.ContainsNaN() || !FMath::IsFinite(Radius) || !FMath::IsFinite(MaxRadius)
        || !FMath::IsFinite(ImpactAt) || !FMath::IsFinite(TravelSeconds) || TravelSeconds<=0 || MaxRadius<=0 || !PrepareVisuals()) return false;
    const double Time=ServerNow();
    const float Age=float(Time-ImpactAt);
    if(WaveImpactAt!=ImpactAt)
    {
        Stop();WaveImpactAt=ImpactAt;
        WaveSeed=HashCombine(GetTypeHash(FMath::RoundToInt(Origin.X+Origin.Y)),GetTypeHash(int64(ImpactAt*1000)));
        for(FCrystalPose& Pose:CrystalPoses) Pose=FCrystalPose();
        UE_LOG(LogMCFrostNova, Verbose, TEXT("MC_FROST_NOVA_BEGIN owner=%s radius=%.1f travel=%.2f impact=%.3f crystals=%d"),
            *GetNameSafe(GetOwner()),MaxRadius,TravelSeconds,ImpactAt,CrystalCount);
    }
    const bool bWarning=Age<0;
    if(Age>TravelSeconds+.12f) {Stop();return true;}
    Front->SetVisibility(true);
    const float Fade=1.f-FMath::SmoothStep(TravelSeconds-.10f,TravelSeconds+.12f,Age);
    FrontMID->SetScalarParameterValue(TEXT("Opacity"),bWarning?.43f:.95f*Fade);
    FrontMID->SetScalarParameterValue(TEXT("Warning"),bWarning?1.f:0.f);
    FrontMID->SetScalarParameterValue(TEXT("Age"),FMath::Max(0.f,Age));
    FrontMID->SetVectorParameterValue(TEXT("Tint"),bWarning?FLinearColor(.35f,.78f,1.f):FLinearColor(.72f,.94f,1.f));
    if(!bWarning && !bBurstPlayed)
    {
        bBurstPlayed=true;
        // Allow ordinary activation/readiness latency without dropping the first
        // short release. Truly late relevance still gets the travelling front.
        if(Age<.8f && IsValid(Burst))
        {
            FHitResult Floor;
            const FVector Point=Tongue->SurfacePoint(Origin,Floor)?Floor.ImpactPoint+Floor.ImpactNormal*42:Origin+FVector(0,0,42);
            Burst->SetWorldLocation(Point);
            Burst->SetVariableLinearColor(TEXT("User.ParticleColor"),FLinearColor::White);
            Burst->Activate(true);
            UE_LOG(LogMCFrostNova,Display,TEXT("MC_FROST_NOVA_BURST owner=%s age=%.3f active=%d systemReady=%d crystalPSOPending=%d position=%s"),
                *GetNameSafe(GetOwner()),Age,Burst->IsActive(),Burst->GetAsset() && Burst->GetAsset()->IsReadyToRun(),
                Crystals->IsPSOPrecaching(),*Point.ToCompactString());
        }
        else UE_LOG(LogMCFrostNova,Verbose,TEXT("MC_FROST_NOVA_BURST_SKIPPED owner=%s age=%.3f hasComponent=%d"),*GetNameSafe(GetOwner()),Age,IsValid(Burst));
    }
    if(Time>=NextGeometryAt)
    {
        NextGeometryAt=Time+1./30.;
        DrawFront(Tongue,Origin,Radius,MaxRadius,Age,bWarning,WarningProgress);
        UpdateCrystals(Tongue,Origin,MaxRadius,Age,TravelSeconds);
    }
    return true;
}

void UMCFrostNovaVFXComponent::DrawFront(AMCTongue* Tongue,FVector Origin,float Radius,float MaxRadius,
    float Age,bool bWarning,float WarningProgress)
{
    constexpr int32 Segments=64;
    const float R=bWarning?FMath::Max(25.f,MaxRadius*.15f)*(.8f+.2f*WarningProgress):FMath::Max(4.f,Radius);
    const float Width=bWarning?9.f:FMath::Min(R-1.f,FMath::Clamp(MaxRadius*.06f,110.f,170.f));
    TArray<FVector> Vertices,Normals,CrestVertices,CrestNormals;
    TArray<FVector2D> UV,CrestUV;
    TArray<int32> Indices,CrestIndices;
    TArray<FLinearColor> Colors,CrestColors;
    TArray<FProcMeshTangent> Tangents,CrestTangents;
    const FTransform Transform=Front->GetComponentTransform();
    FHitResult Samples[Segments+1][2];
    FVector SampleRays[Segments+1];
    bool Supported[Segments+1][2];
    // Adjacent quads share their two ground samples: at most 130 queries per
    // geometry refresh, instead of projecting all four corners independently.
    for(int32 I=0;I<=Segments;++I)
    {
        const float Angle=I*2*PI/Segments;
        SampleRays[I]=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0);
        for(int32 Edge=0;Edge<2;++Edge)
            Supported[I][Edge]=Tongue->SurfacePoint(Origin+SampleRays[I]*FMath::Max(1.f,R+(Edge?0.f:-Width)),Samples[I][Edge]);
    }
    for(int32 I=0;I<Segments;++I)
    {
        if(!Supported[I][0] || !Supported[I][1] || !Supported[I+1][0] || !Supported[I+1][1]) continue;
        const FHitResult* Floors[]={&Samples[I][0],&Samples[I][1],&Samples[I+1][1],&Samples[I+1][0]};
        const FVector Rays[]={SampleRays[I],SampleRays[I+1]};
        const int32 First=Vertices.Num();
        for(int32 Corner=0;Corner<4;++Corner)
        {
            const int32 Side=Corner>=2?1:0;
            const float Cross=Corner==1 || Corner==2?1.f:0.f;
            Vertices.Add(Transform.InverseTransformPosition(Floors[Corner]->ImpactPoint+Floors[Corner]->ImpactNormal*4.f));
            Normals.Add(Transform.InverseTransformVectorNoScale(Floors[Corner]->ImpactNormal));
            UV.Add(FVector2D(float(I+Side)/Segments,Cross));Colors.Add(FLinearColor(0,0,0,1));
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(FVector(-Rays[Side].Y,Rays[Side].X,0)),false));
        }
        Indices.Append({First,First+1,First+2,First,First+2,First+3});
        if(bWarning) continue;
        const int32 CrestFirst=CrestVertices.Num();
        for(int32 Corner=0;Corner<4;++Corner)
        {
            const int32 Side=Corner>=2?1:0;
            const FHitResult& Floor=*Floors[Side?2:1];
            const float Angle=(I+Side)*2*PI/Segments;
            const bool bTop=Corner==1 || Corner==2;
            const float Height=bTop?FMath::Max(18.f,34.f+13.f*FMath::Sin(Angle*17+Age*11)+7.f*FMath::Sin(Angle*29-Age*6)):3.f;
            // The translucent low veil leans behind the true leading edge.
            CrestVertices.Add(Transform.InverseTransformPosition(Floor.ImpactPoint+Floor.ImpactNormal*Height-Rays[Side]*(bTop?Height*.28f:0.f)));
            CrestNormals.Add(Transform.InverseTransformVectorNoScale(Rays[Side]));
            CrestUV.Add(FVector2D(float(I+Side)/Segments,bTop?1.f:0.f));
            CrestColors.Add(FLinearColor(1,0,0,.75f));
            CrestTangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(FVector(-Rays[Side].Y,Rays[Side].X,0)),false));
        }
        CrestIndices.Append({CrestFirst,CrestFirst+2,CrestFirst+1,CrestFirst,CrestFirst+3,CrestFirst+2});
    }
    Front->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
    if(CrestIndices.IsEmpty()) Front->ClearMeshSection(1);
    else Front->CreateMeshSection_LinearColor(1,CrestVertices,CrestIndices,CrestNormals,CrestUV,CrestColors,CrestTangents,false);
}

void UMCFrostNovaVFXComponent::UpdateCrystals(AMCTongue* Tongue,FVector Origin,float MaxRadius,float Age,float TravelSeconds)
{
    const bool bActive=Age>=0 && Age<TravelSeconds;
    Crystals->SetVisibility(true);
    if(!bActive || !CrystalMesh.Get()) return;
    const FBox Bounds=CrystalMesh->GetBoundingBox();
    const FVector Size=Bounds.GetSize().ComponentMax(FVector(1));
    constexpr float PulseDuration=.38f;
    for(int32 I=0;I<CrystalPoses.Num();++I)
    {
        FRandomStream Random(uint32(WaveSeed)+uint32(I)*127u);
        const float Offset=Random.FRandRange(0.f,.9f);
        const int32 Pulse=FMath::FloorToInt(Age/PulseDuration+Offset);
        const float Birth=(Pulse-Offset)*PulseDuration;
        const float LocalAge=Age-Birth;
        FCrystalPose& Pose=CrystalPoses[I];
        if(Pulse!=Pose.Pulse)
        {
            Pose.Pulse=Pulse;
            const float Angle=I*2*PI/CrystalPoses.Num()+Random.FRandRange(-.07f,.07f);
            const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0);
            const float BirthRadius=MaxRadius*FMath::Clamp(Birth/TravelSeconds,0.f,1.f);
            FHitResult Floor;
            Pose.bOnFloor=Birth>=0 && BirthRadius>28 && Tongue->SurfacePoint(Origin+Ray*BirthRadius,Floor);
            if(Pose.bOnFloor)
            {
                Pose.Point=Floor.ImpactPoint+Floor.ImpactNormal*2;
                Pose.Normal=Floor.ImpactNormal;
                Pose.Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Pose.Normal)
                    *FRotator(Random.FRandRange(-15,15),Angle*180/PI,Random.FRandRange(-12,12)).Quaternion();
                Pose.Size=FVector(Random.FRandRange(14,23)/Size.X,Random.FRandRange(11,18)/Size.Y,Random.FRandRange(34,58)/Size.Z);
            }
        }
        const float Grow=FMath::SmoothStep(0.f,.065f,LocalAge);
        const float Fade=1.f-FMath::SmoothStep(.17f,.35f,LocalAge);
        const float Visible=Pose.bOnFloor && LocalAge>=0 && LocalAge<.35f?1.f:0.f;
        const FVector Scale=Pose.Size*FVector(1,1,Grow)*Visible;
        // Keep the base on the tissue rather than sinking a centred shard pivot.
        const FVector MeshBase(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z);
        const FVector Position=Pose.Point-Pose.Rotation.RotateVector(MeshBase*Scale);
        Crystals->UpdateInstanceTransform(I,FTransform(Pose.Rotation,Position,Scale),true,false,true);
        Crystals->SetCustomDataValue(I,0,Fade*Visible,false);
    }
    // UE 5.8 supports sending instance transforms/custom data without tearing
    // down and recreating the scene proxy on every geometry refresh.
    Crystals->MarkRenderInstancesDirty();
}

void UMCFrostNovaVFXComponent::Stop()
{
    if(IsValid(Front)) {Front->SetVisibility(false);Front->ClearAllMeshSections();}
    if(IsValid(Crystals))
    {
        for(int32 I=0;I<Crystals->GetInstanceCount();++I)
        {
            Crystals->UpdateInstanceTransform(I,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),false,false,true);
            Crystals->SetCustomDataValue(I,0,0,false);
        }
        Crystals->MarkRenderInstancesDirty();
    }
    if(IsValid(Burst)) Burst->DeactivateImmediate();
    bBurstPlayed=false;NextGeometryAt=-1;
}

void UMCFrostNovaVFXComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    // A mint wave borrows the already prepared presenter from its source.
    // Remote destruction can arrive before its last local Tick. Release only
    // that pulse; a late old-wave teardown must not erase a newer presentation.
    if(const auto* Wave=Cast<AMCHazardWave>(GetOwner()))
        if(const auto* Mint=Cast<AMCSmallMintCandy>(Wave->Source))
        {
            UMCFrostNovaVFXComponent* Shared=Mint->FrostNovaVFX;
            if(IsValid(Shared) && Shared!=this && Shared->WaveImpactAt==Wave->StartedAt+Wave->WarningSeconds)
                Shared->Stop();
        }
    Stop();
    if(IsValid(Burst)) Burst->DestroyComponent();
    if(IsValid(Crystals)) Crystals->DestroyComponent();
    if(IsValid(Front)) Front->DestroyComponent();
    Burst=nullptr;Crystals=nullptr;Front=nullptr;FrontMID=nullptr;
    LoadedAssets.Reset();
    Super::EndPlay(Reason);
}
