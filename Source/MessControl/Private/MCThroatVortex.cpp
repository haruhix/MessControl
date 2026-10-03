#include "MCThroatVortex.h"
#include "MCFoodActor.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
constexpr int32 MaxFoodWakes=24;
constexpr int32 WakePoints=11;

struct FVortexSection
{
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Triangles;
    FVector View=FVector::ForwardVector, Right=FVector::RightVector;
    bool bBuildTopology=true;

    void Reset()
    {
        Vertices.Reset(); Normals.Reset(); UVs.Reset(); Colors.Reset(); Tangents.Reset();
    }
    int32 Add(FVector Point, FVector2D UV, FLinearColor Color)
    {
        const int32 Index=Vertices.Add(Point);
        Normals.Add(View); UVs.Add(UV); Colors.Add(Color); Tangents.Emplace(Right, false);
        return Index;
    }
    void Quad(FVector Center, FVector R, FVector U, float Size, FLinearColor Color)
    {
        const int32 First=Vertices.Num();
        Add(Center+(-R-U)*Size, FVector2D(0,0), Color);
        Add(Center+( R-U)*Size, FVector2D(1,0), Color);
        Add(Center+( R+U)*Size, FVector2D(1,1), Color);
        Add(Center+(-R+U)*Size, FVector2D(0,1), Color);
        if(bBuildTopology) Triangles.Append({First,First+1,First+2,First,First+2,First+3});
    }
    void Ribbon(const TArray<FVector>& Path, float Width, FLinearColor Color, bool bTaper=true)
    {
        const int32 First=Vertices.Num();
        for(int32 I=0; I<Path.Num(); ++I)
        {
            const float T=float(I)/FMath::Max(1,Path.Num()-1);
            const FVector Along=(Path[FMath::Min(I+1,Path.Num()-1)]-Path[FMath::Max(0,I-1)]).GetSafeNormal();
            FVector Side=FVector::CrossProduct(View,Along).GetSafeNormal();
            if(Side.IsNearlyZero()) Side=Right;
            const float Profile=bTaper?FMath::Pow(FMath::Max(.002f,FMath::Sin(T*PI)),.55f):1.f;
            FLinearColor Tint=Color;
            if(bTaper) Tint.A*=FMath::SmoothStep(0.f,.12f,T)*(1-FMath::SmoothStep(.82f,1.f,T));
            Add(Path[I]-Side*Width*Profile,FVector2D(T,0),Tint);
            Add(Path[I]+Side*Width*Profile,FVector2D(T,1),Tint);
            if(bBuildTopology && I>0)
            {
                const int32 A=First+(I-1)*2;
                Triangles.Append({A,A+1,A+3,A,A+3,A+2});
            }
        }
    }
    void Upload(UProceduralMeshComponent* Mesh, int32 Section)
    {
        // Keep the same topology and render proxy during the whole swallow.
        if(bBuildTopology)
        {
            Mesh->CreateMeshSection_LinearColor(Section,Vertices,Triangles,Normals,UVs,Colors,Tangents,false,false);
            bBuildTopology=false;
        }
        else Mesh->UpdateMeshSection_LinearColor(Section,Vertices,Normals,UVs,Colors,Tangents,false);
    }
};

struct FFoodWake
{
    FVector History[WakePoints];
    bool bInitialized=false;
    float Visibility=0;
    FFoodWake() { for(auto& Point:History) Point=FVector::ZeroVector; }
};
}

struct FMCThroatVortexGeometry
{
    FVortexSection Ribbons, Mist, Glow;
    FFoodWake Wakes[MaxFoodWakes];
    TArray<FVector> Path;
    bool bLoggedStart=false,bLoggedPeak=false;
    FMCThroatVortexGeometry() { Path.Reserve(65); }
};
void FMCThroatVortexGeometryDeleter::operator()(FMCThroatVortexGeometry* Geometry) const { delete Geometry; }

AMCThroatVortex::AMCThroatVortex()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(false); SetActorEnableCollision(false);
    SetNetUpdateFrequency(5); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=1.f/60.f;
    Airflow=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SharedAirflow")); SetRootComponent(Airflow);
    Airflow->SetCollisionEnabled(ECollisionEnabled::NoCollision); Airflow->SetCastShadow(false);
    Airflow->SetCanEverAffectNavigation(false); Airflow->SetReceivesDecals(false);
    Airflow->SetVisibleInRayTracing(false); Airflow->SetAffectDistanceFieldLighting(false);
    SuctionAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("RoomSuction"));SuctionAudio->SetupAttachment(Airflow);
    SuctionAudio->bAutoActivate=false;SuctionAudio->bStopWhenOwnerDestroyed=true;
    SuctionAudio->bAllowSpatialization=false;SuctionAudio->bOverrideAttenuation=true;
    SuctionAudio->AttenuationOverrides.bAttenuate=false;SuctionAudio->AttenuationOverrides.bSpatialize=false;
    RibbonMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/Throat/M_ThroatVortexRibbon.M_ThroatVortexRibbon"));
    MistMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/Throat/M_ThroatVortexMist.M_ThroatVortexMist"));
    GlowMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/Throat/M_ThroatVortexGlow.M_ThroatVortexGlow"));
    IntakeSound=FSoftObjectPath(TEXT("/Game/Audio/S_Whoosh.S_Whoosh"));
    AirflowSound=FSoftObjectPath(TEXT("/Game/Audio/S_FoodSuctionWind.S_FoodSuctionWind"));
}
AMCThroatVortex::~AMCThroatVortex()=default;

double AMCThroatVortex::ServerNow() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}
AMCThroatVortex* AMCThroatVortex::Spawn(UWorld* World, FVector Inlet, FVector Direction,
    float InRadius, float Span, float Seconds, const TArray<AMCFoodActor*>& Sources, float HalfWidth, float Height)
{
    if(!World || World->GetNetMode()==NM_Client) return nullptr;
    const FTransform Transform(Inlet);
    auto* Effect=World->SpawnActorDeferred<AMCThroatVortex>(StaticClass(),Transform);
    if(!Effect) return nullptr;
    Effect->InwardDirection=Direction.GetSafeNormal();
    if(Effect->InwardDirection.IsNearlyZero()) Effect->InwardDirection=FVector::ForwardVector;
    Effect->Radius=FMath::Clamp(InRadius,80.f,600.f); Effect->FlowLength=FMath::Clamp(Span,160.f,20000.f);
    Effect->FlowHalfWidth=FMath::Clamp(HalfWidth>0?HalfWidth:FMath::Max(Effect->Radius,Effect->FlowLength*.33f),80.f,12000.f);
    Effect->FlowHeight=FMath::Clamp(Height>0?Height:Effect->Radius,30.f,3000.f);
    Effect->Duration=FMath::Max(.5f,Seconds); Effect->StartedAt=Effect->ServerNow();
    const int32 Samples=FMath::Min(MaxFoodWakes,Sources.Num());
    for(int32 I=0; I<Samples; ++I) Effect->FoodSources.Add(Sources[I*Sources.Num()/Samples]);
    Effect->FinishSpawning(Transform); Effect->ForceNetUpdate();
    return Effect;
}

void AMCThroatVortex::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority()) SetLifeSpan(Duration+.15f);
    if(GetNetMode()==NM_DedicatedServer) { SetActorTickEnabled(false); return; }
    const float AudioAge=FMath::Max(0.f,float(ServerNow()-StartedAt));
    // Late joiners enter the current draft without replaying its onset cue.
    if(AudioAge<.25f) if(auto* Sound=IntakeSound.LoadSynchronous())
        UGameplayStatics::PlaySound2D(this,Sound,.32f,.78f,0,nullptr,this,false);
    auto* WindSound=AirflowSound.LoadSynchronous();
    if(!WindSound) WindSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Audio/S_Pull.S_Pull"));
    SuctionAudio->SetSound(WindSound);
    NextAirflowCueAt=AudioAge;
    Geometry.Reset(new FMCThroatVortexGeometry());
    const TSoftObjectPtr<UMaterialInterface> Bases[]={RibbonMaterial,MistMaterial,GlowMaterial};
    for(int32 I=0; I<UE_ARRAY_COUNT(Bases); ++I)
    {
        auto* Base=Bases[I].LoadSynchronous();
        if(!Base) Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionSoft.M_ReactionSoft"));
        if(!Base) continue;
        auto* Dynamic=UMaterialInstanceDynamic::Create(Base,this); Materials.Add(Dynamic); Airflow->SetMaterial(I,Dynamic);
        Dynamic->SetVectorParameterValue(TEXT("Color"),FLinearColor::White);
        Dynamic->SetScalarParameterValue(TEXT("Opacity"),1);
    }
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("MCFoodPileCapture")))
        UE_LOG(LogTemp,Display,TEXT("MC_VORTEX_BEGIN net=%d tick=%d registered=%d visible=%d actor_hidden=%d mesh_hidden=%d start=%.3f now=%.3f location=%s direction=%s radius=%.1f span=%.1f half_width=%.1f height=%.1f duration=%.2f materials=%d"),
            int32(GetNetMode()),IsActorTickEnabled(),Airflow->IsRegistered(),Airflow->IsVisible(),IsHidden(),Airflow->bHiddenInGame,
            StartedAt,ServerNow(),*GetActorLocation().ToString(),*InwardDirection.ToString(),Radius,FlowLength,FlowHalfWidth,FlowHeight,Duration,Materials.Num());
#endif
}

void AMCThroatVortex::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!Geometry || GetNetMode()==NM_DedicatedServer) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MCThroatVortex);
    const float Age=FMath::Max(0.f,float(ServerNow()-StartedAt)), Progress=Age/FMath::Max(.5f,Duration);
    if(Progress>=1) { Airflow->SetVisibility(false); SuctionAudio->FadeOut(.1f,0); SetActorTickEnabled(false); return; }
    const float Strength=FMath::SmoothStep(.035f,.24f,Age)*(1-FMath::SmoothStep(.82f,1.f,Progress));
    SuctionAudio->SetVolumeMultiplier(.20f*Strength);
    if(Strength>.035f && Age>=NextAirflowCueAt && SuctionAudio->Sound && !SuctionAudio->IsPlaying())
    {
        // Let the original wind run continuously. Only the legacy short-clip
        // fallback uses a low breathing cadence when the new asset is absent.
        const bool ShortClip=SuctionAudio->Sound->GetDuration()<.5f;
        const float Pitch=(ShortClip?.62f:1.f)+.025f*FMath::Sin(Age*3.2f);
        SuctionAudio->SetPitchMultiplier(Pitch);SuctionAudio->Play();
        NextAirflowCueAt=Age+FMath::Max(.32f,SuctionAudio->Sound->GetDuration()/Pitch+.04f);
    }
    for(const auto& Material:Materials) if(Material)
    {
        Material->SetScalarParameterValue(TEXT("VortexAge"),Age);
        Material->SetScalarParameterValue(TEXT("Opacity"),Strength);
    }
    FVector Eye=GetActorLocation()-InwardDirection*FlowLength+FVector(0,0,400);
    if(const auto* Controller=GetWorld()->GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
        Eye=Controller->PlayerCameraManager->GetCameraLocation();
    const FVector View=(Eye-GetActorLocation()).GetSafeNormal();
    FVector Right=FVector::CrossProduct(FVector::UpVector,View).GetSafeNormal();
    if(Right.IsNearlyZero()) Right=FVector::RightVector;
    const FVector Up=FVector::CrossProduct(View,Right).GetSafeNormal();
    const FVector Axis=InwardDirection.GetSafeNormal();
    FVector R=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal();
    if(R.IsNearlyZero()) R=FVector::RightVector;
    const FVector V=FVector::CrossProduct(R,Axis).GetSafeNormal();
    auto& Ribbons=Geometry->Ribbons; auto& Mist=Geometry->Mist; auto& Glow=Geometry->Glow;
    for(auto* Section:{&Ribbons,&Mist,&Glow}) { Section->Reset(); Section->View=View; Section->Right=Right; }
    auto& Path=Geometry->Path;
    const float Collapse=1-FMath::SmoothStep(.74f,1.f,Progress)*.70f;
    const float BodySize=Radius*.50f*Collapse;
    const FVector Center=-Axis*(Radius*.92f*Collapse+12)+FVector(0,0,Radius*.28f*Collapse);
    // The field occupies the mouth's measured footprint, while the final curls
    // retain the inlet's own scale. Separate gusts converge without a helix shell.
    auto WindPoint=[&](float Travel,float Lane,float Height,float Seed)
    {
        const float Remaining=1-FMath::Clamp(Travel,0.f,1.f);
        const float Bend=FMath::Sin(Travel*PI);
        const float Lateral=FlowHalfWidth*(Lane*FMath::Pow(Remaining,.76f)
            +.10f*Bend*FMath::Sin(Travel*5.1f+Seed+Age*.92f));
        const float Lift=FlowHeight*Height*FMath::Pow(Remaining,.68f)
            +FMath::Min(FlowHeight*.12f,Radius*.22f)*Bend*FMath::Sin(Travel*7+Seed-Age*1.3f);
        return -Axis*(FlowLength*.92f*Remaining+8)+R*Lateral+FVector(0,0,FMath::Max(0.f,Lift));
    };
    // Twelve broad, broken packets sweep from distributed parts of the tongue.
    // Their heads move toward the inlet, with staggered restarts and soft fades.
    for(int32 I=0; I<12; ++I)
    {
        const float Seed=I*2.39996f;
        const float Head=FMath::Frac(Age*(.70f+I%4*.07f)+I/12.f);
        const float Tail=FMath::Max(0.f,Head-(.25f+I%3*.08f));
        const float Lane=(I/11.f*2-1)*.85f;
        const float Height=.13f+I%4*.16f;
        Path.Reset();
        for(int32 J=0; J<=32; ++J)
        {
            const float T=J/32.f, Travel=FMath::Lerp(Tail,Head,T);
            Path.Add(WindPoint(Travel,Lane,Height,Seed));
        }
        const float Fade=FMath::SmoothStep(0.f,.12f,Head)*(1-FMath::SmoothStep(.84f,1.f,Head));
        const bool bLead=I==2 || I==9;
        const float Width=FMath::Clamp(FlowHalfWidth*(.040f+I%3*.012f)*(bLead?1.35f:1.f),18.f,125.f);
        const float Density=bLead?.64f:(.30f+I%3*.035f);
        Ribbons.Ribbon(Path,Width*(1-Head*.38f),FLinearColor(.69f,.74f,.79f,Density*Fade));
    }
    // Low density full-length wisps keep the mouth-wide pull visible between
    // packets. FBM cuts each into feathered patches instead of continuous wires.
    for(int32 I=0; I<4; ++I)
    {
        const float Seed=I*2.39996f, Lane=(I/3.f*2-1)*.76f;
        Path.Reset();
        for(int32 J=0; J<=48; ++J)
        {
            const float T=J/48.f;
            Path.Add(WindPoint(T,Lane,.20f+I%3*.17f,Seed));
        }
        Ribbons.Ribbon(Path,FMath::Clamp(FlowHalfWidth*(.030f+I%2*.012f),14.f,85.f),FLinearColor(.67f,.72f,.77f,.24f));
    }
    // The final curl has an aperture-sized footprint, independent of field size.
    for(int32 I=0; I<4; ++I)
    {
        const float Seed=I*2.39996f, Travel=FMath::Frac(Age*(.80f+I*.075f)+I/4.f);
        const FVector Pole=(Axis*.7f+R*FMath::Cos(Seed)*.5f+V*FMath::Sin(Seed)*.5f).GetSafeNormal();
        FVector A=FVector::CrossProduct(Pole,FVector::UpVector).GetSafeNormal();
        if(A.IsNearlyZero()) A=R;
        const FVector B=FVector::CrossProduct(Pole,A).GetSafeNormal();
        const FVector Drift=FMath::Lerp(Center,-Axis*14+FVector(0,0,Radius*.06f),Travel*.75f);
        Path.Reset();
        for(int32 J=0; J<=32; ++J)
        {
            const float T=J/32.f, Angle=Seed+Age*2.9f+T*PI*(.70f+I%2*.20f);
            const float Orbit=BodySize*(.90f-Travel*.65f)*(1+.1f*FMath::Sin(T*11+Seed-Age*2));
            Path.Add(Drift+(A*FMath::Cos(Angle)+B*FMath::Sin(Angle))*Orbit);
        }
        Ribbons.Ribbon(Path,BodySize*(.10f+I%2*.035f),FLinearColor(.69f,.74f,.79f,FMath::Sin(Travel*PI)*.34f));
    }
    // Fixed wake slots follow real replicated food, with local history only.
    // Unused slots remain transparent, so a large delivery cannot multiply actors.
    for(int32 I=0; I<MaxFoodWakes; ++I)
    {
        auto& Wake=Geometry->Wakes[I];
        auto* Food=FoodSources.IsValidIndex(I)?FoodSources[I].Get():nullptr;
        const bool bVisible=IsValid(Food) && !Food->IsDisposed();
        if(bVisible)
        {
            const FVector Point=Food->GetActorLocation()-GetActorLocation();
            if(!Wake.bInitialized) { for(auto& Previous:Wake.History) Previous=Point; Wake.bInitialized=true; }
            for(int32 J=WakePoints-1; J>0; --J) Wake.History[J]=Wake.History[J-1];
            Wake.History[0]=Point;
        }
        Wake.Visibility=FMath::FInterpTo(Wake.Visibility,bVisible?1.f:0.f,Dt,12);
        Path.Reset(); for(int32 J=WakePoints-1; J>=0; --J) Path.Add(Wake.History[J]);
        Ribbons.Ribbon(Path,Radius*.019f,FLinearColor(.69f,.75f,.80f,.08f*Wake.Visibility));
    }
    // Soft inner curls fill the rotating volume; no additive core or ring stack.
    for(int32 I=0; I<5; ++I)
    {
        const float Seed=I*2.39996f, Turn=Seed+Age*(2.5f+I%3*.3f);
        const FVector A=(R*FMath::Cos(Seed)+Axis*FMath::Sin(Seed)).GetSafeNormal();
        const FVector B=FVector::CrossProduct(A,V).GetSafeNormal();
        Path.Reset();
        for(int32 J=0; J<=24; ++J)
        {
            const float T=J/24.f, Angle=Turn+T*PI*1.12f;
            const float Orbit=BodySize*(.38f+I%3*.10f)*(1-T*.27f);
            Path.Add(Center+(A*FMath::Cos(Angle)+B*FMath::Sin(Angle))*Orbit+V*(BodySize*.09f*FMath::Sin(T*7+Age*3+Seed)));
        }
        Glow.Ribbon(Path,BodySize*(.085f+I%2*.03f),FLinearColor(.69f,.74f,.79f,.13f));
    }
    // Sparse billows occupy the entire field and move with the visible pull.
    for(int32 I=0; I<32; ++I)
    {
        const float Seed=I*2.39996f, Travel=FMath::Frac(Age*(.61f+I%4*.065f)+I/32.f);
        const float Lane=(FMath::Frac(I*.618034f)*2-1)*.85f;
        const FVector Point=WindPoint(Travel,Lane,.11f+I%5*.13f,Seed);
        const float Roll=Seed+Age*.42f;
        const FVector Q=Right*FMath::Cos(Roll)+Up*FMath::Sin(Roll), W=Up*FMath::Cos(Roll)-Right*FMath::Sin(Roll);
        const float Size=FMath::Clamp(FlowHalfWidth*(.065f+I%3*.015f),25.f,140.f)*(1-Travel*.62f);
        Mist.Quad(Point,Q,W,Size,FLinearColor(.64f,.70f,.74f,(.11f+I%3*.02f)*FMath::Sin(Travel*PI)));
    }
    for(int32 I=0; I<16; ++I)
    {
        const float Seed=I*2.39996f, Travel=FMath::Frac(Age*(.68f+I%4*.08f)+I/16.f);
        const float Orbit=BodySize*(.65f-Travel*.45f), Turn=Seed+Age*2.3f;
        const FVector Point=FMath::Lerp(Center,-Axis*12,Travel*.72f)
            +(R*FMath::Cos(Turn)+V*FMath::Sin(Turn))*Orbit;
        Mist.Quad(Point,Right,Up,BodySize*(.24f+I%3*.06f),FLinearColor(.64f,.70f,.74f,FMath::Sin(Travel*PI)*.14f));
    }
    const FLinearColor CrumbColors[]={FLinearColor(.33f,.29f,.23f,1),FLinearColor(.43f,.41f,.36f,1),FLinearColor(.63f,.62f,.59f,1)};
    for(int32 I=0; I<32; ++I)
    {
        const float U=FMath::Frac(Age*(.64f+I%3*.065f)+I/32.f), Seed=I*2.39996f;
        const FVector Point=WindPoint(U,FMath::Sin(Seed)*.82f,.10f+I%4*.14f,Seed);
        const float Roll=I+Age*(I%2?3.2f:-2.8f);
        const FVector Q=Right*FMath::Cos(Roll)+Up*FMath::Sin(Roll), W=Up*FMath::Cos(Roll)-Right*FMath::Sin(Roll);
        FLinearColor Color=CrumbColors[I%UE_ARRAY_COUNT(CrumbColors)]; Color.A=FMath::Sin(U*PI)*.38f;
        Glow.Quad(Point,Q,W,1.3f+I%3*.65f,Color);
    }
    Ribbons.Upload(Airflow,0); Mist.Upload(Airflow,1); Glow.Upload(Airflow,2);
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("MCFoodPileCapture")) && (!Geometry->bLoggedStart || (Age>=.45f && !Geometry->bLoggedPeak)))
    {
        Geometry->bLoggedStart=true;if(Age>=.45f) Geometry->bLoggedPeak=true;
        UE_LOG(LogTemp,Display,TEXT("MC_VORTEX_GEOMETRY age=%.3f strength=%.3f visible=%d actor_hidden=%d mesh_hidden=%d component=%s bounds_origin=%s bounds_extent=%s eye=%s"),
            Age,Strength,Airflow->IsVisible(),IsHidden(),Airflow->bHiddenInGame,*Airflow->GetComponentLocation().ToString(),
            *Airflow->Bounds.Origin.ToString(),*Airflow->Bounds.BoxExtent.ToString(),*Eye.ToString());
        const FVortexSection* Buffers[]={&Ribbons,&Mist,&Glow};
        for(int32 I=0;I<3;++I)
        {
            const auto* Section=Airflow->GetProcMeshSection(I);float MaxAlpha=0;
            for(const auto& Color:Buffers[I]->Colors) MaxAlpha=FMath::Max(MaxAlpha,Color.A);
            auto* Dynamic=Cast<UMaterialInstanceDynamic>(Airflow->GetMaterial(I));
            UE_LOG(LogTemp,Display,TEXT("MC_VORTEX_SECTION section=%d vertices=%d indices=%d section_visible=%d local_bounds=%s max_alpha=%.3f material=%s opacity=%.3f"),
                I,Section?Section->ProcVertexBuffer.Num():0,Section?Section->ProcIndexBuffer.Num():0,Section?Section->bSectionVisible:false,
                Section?*Section->SectionLocalBox.ToString():TEXT("none"),MaxAlpha,*GetPathNameSafe(Airflow->GetMaterial(I)),Dynamic?Dynamic->K2_GetScalarParameterValue(TEXT("Opacity")):-1.f);
        }
    }
#endif
}

void AMCThroatVortex::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCThroatVortex,StartedAt); DOREPLIFETIME(AMCThroatVortex,Duration);
    DOREPLIFETIME(AMCThroatVortex,Radius); DOREPLIFETIME(AMCThroatVortex,FlowLength);
    DOREPLIFETIME(AMCThroatVortex,FlowHalfWidth); DOREPLIFETIME(AMCThroatVortex,FlowHeight);
    DOREPLIFETIME(AMCThroatVortex,InwardDirection); DOREPLIFETIME(AMCThroatVortex,FoodSources);
}
