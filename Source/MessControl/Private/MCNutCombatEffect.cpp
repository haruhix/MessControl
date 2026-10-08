#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCTongue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#if WITH_EDITOR
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/Material.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#endif

namespace
{
float Bounded(float Value,float Default,float Min,float Max) {return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Min,Max);}
UMaterialInstanceDynamic* Material(UPrimitiveComponent* Part,UMaterialInterface* Parent)
{
    return Parent?Part->CreateDynamicMaterialInstance(0,Parent):nullptr;
}
float MeshRadius(UStaticMesh* Mesh) {return Mesh?FMath::Max(.1f,Mesh->GetBounds().BoxExtent.GetMax()):1.f;}
void ReleaseBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component)) return;
    // A world without Niagara pooling can return an ordinary component for a manual-pool request.
    if(Component->PoolingMethod==ENCPoolMethod::ManualRelease) Component->ReleaseToPool();
    else Component->DestroyComponent();
}
}

AMCNutCombatEffect::AMCNutCombatEffect()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(false);SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=1.f/30;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("CueRoot"));SetRootComponent(Root);
    Warning=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundTelegraph"));Warning->SetupAttachment(Root);
    DropWarnings=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DropTelegraphs"));DropWarnings->SetupAttachment(Root);
    DropShadows=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SoftLandingShadows"));DropShadows->SetupAttachment(Root);
    Flames=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MagicFlames"));Flames->SetupAttachment(Root);
    Magic=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ArcaneRibbons"));Magic->SetupAttachment(Root);
    Storm=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StormCloudAndStreaks"));Storm->SetupAttachment(Root);
    SustainedParticles=CreateDefaultSubobject<UNiagaraComponent>(TEXT("SustainedCueParticles"));SustainedParticles->SetupAttachment(Root);
    SustainedParticles->SetAutoActivate(false);
    RainNuts=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FallingWalnuts"));RainNuts->SetupAttachment(Root);
    Debris=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ShellFragments"));Debris->SetupAttachment(Root);
    UPrimitiveComponent* Parts[]={Warning.Get(),DropWarnings.Get(),DropShadows.Get(),Flames.Get(),Magic.Get(),Storm.Get(),SustainedParticles.Get(),RainNuts.Get(),Debris.Get()};
    for(auto* Part:Parts) MCNutCombatVisuals::Configure(Part);
    Warning->SetTranslucentSortPriority(3);DropWarnings->SetTranslucentSortPriority(4);DropShadows->SetTranslucentSortPriority(2);
    Magic->SetTranslucentSortPriority(5);
    TelegraphMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutTelegraph.M_NutTelegraph"));
    ShadowMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutSoftShadow.M_NutSoftShadow"));
    FlameMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"));
    MagicMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutArcane.M_NutArcane"));
    StormMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutStorm.M_NutStorm"));
    DustImpactSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutDustImpact.NS_NutDustImpact"));
    FireImpactSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutFireImpact.NS_NutFireImpact"));
    ArcaneBurstSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutArcaneBurst.NS_NutArcaneBurst"));
    CastChargeSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutCastCharge.NS_NutCastCharge"));
    MotionDustSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutMotionDust.NS_NutMotionDust"));
    StormMotesSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutStormMotes.NS_NutStormMotes"));
    ShieldHitSystem=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutShieldHit.NS_NutShieldHit"));
}

double AMCNutCombatEffect::Now() const
{const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}

FVector AMCNutCombatEffect::GetNutRainDropPoint(FVector Center,float Radius,int32 Seed,int32 Index)
{
    if(Center.ContainsNaN()) return FVector::ZeroVector;
    FRandomStream Random{int32(HashCombineFast(uint32(Seed),uint32(Index)))};
    const float Angle=Random.FRand()*2*PI,Distance=FMath::Sqrt(Random.FRand())*Bounded(Radius,300,20,1200)*.86f;
    return Center+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,0);
}

AMCNutCombatEffect* AMCNutCombatEffect::Spawn(AActor* Source,AMCTongue* Surface,EMCNutCombatCue Type,
    FVector Origin,FVector Target,float Radius,float Windup,float Active,int32 Seed,float DetailRadius,int32 DropCount,float DropCadence)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Origin.ContainsNaN() || Target.ContainsNaN() || uint8(Type)>uint8(EMCNutCombatCue::DeathBurst)) return nullptr;
    const FTransform Pose(Target);
    auto* Effect=Source->GetWorld()->SpawnActorDeferred<AMCNutCombatEffect>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Effect) return nullptr;
    Effect->SourceActor=Source;Effect->Tongue=Surface;
    Effect->Cue.Type=Type;Effect->Cue.Role=Type==EMCNutCombatCue::FireCast || Type==EMCNutCombatCue::FireImpact
        || Type==EMCNutCombatCue::NutRain || Type==EMCNutCombatCue::SummonTell || Type==EMCNutCombatCue::CastCharge
        || Type==EMCNutCombatCue::CastRelease || Type==EMCNutCombatCue::RitualCast?EMCNutBossRole::Mage:EMCNutBossRole::Tank;
    if(const auto* Boss=Cast<AMCNutBoss>(Source)) Effect->Cue.Role=Boss->BossRole;
    Effect->Cue.Origin=Origin;Effect->Cue.Target=Target;Effect->Cue.Radius=Bounded(Radius,100,15,1200);
    Effect->Cue.DetailRadius=Bounded(DetailRadius,90,15,250);Effect->Cue.WindupSeconds=Bounded(Windup,1,0,12);
    Effect->Cue.ActiveSeconds=Bounded(Active,1,0,16);Effect->Cue.Seed=Seed;
    Effect->Cue.DropCount=FMath::Clamp(DropCount,1,MaxRainDrops);Effect->Cue.DropCadence=Bounded(DropCadence,.5f,.12f,2);
    Effect->Cue.StartedAt=Effect->Now();Effect->FinishSpawning(Pose);return Effect;
}

void AMCNutCombatEffect::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority()) SetLifeSpan(FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds)+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    if(GetNetMode()==NM_DedicatedServer) return;
    InitializeMaterials();
}

void AMCNutCombatEffect::InitializeMaterials()
{
    if(WarningMID) return;
    WarningMID=Material(Warning,TelegraphMaterial.LoadSynchronous());
    SmallWarningMID=Material(DropWarnings,TelegraphMaterial.LoadSynchronous());
    ShadowMID=Material(DropShadows,ShadowMaterial.LoadSynchronous());
    FireMID=Material(Flames,FlameMaterial.LoadSynchronous());
    MagicMID=Material(Magic,MagicMaterial.LoadSynchronous());
    StormMID=Material(Storm,StormMaterial.LoadSynchronous());
    RainNuts->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
}

void AMCNutCombatEffect::PreviewAtAge(float Age)
{
#if WITH_EDITOR
    if(GetWorld() && !GetWorld()->IsGameWorld() && IsValid(Tongue) && FMath::IsFinite(Age)) {
        InitializeMaterials();Present(FMath::Max(0.f,Age));
    }
#endif
}

void AMCNutCombatEffect::Burst(FVector Point,bool bFire,float Scale)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    PlayBurst(bFire?FireImpactSystem.LoadSynchronous():DustImpactSystem.LoadSynchronous(),Point,FVector::UpVector,Scale);
}

void AMCNutCombatEffect::PlayBurst(UNiagaraSystem* System,FVector Point,FVector Direction,float Scale)
{
    if(GetNetMode()!=NM_DedicatedServer && System && Bursts.Num()<8) {
        auto* Component=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Point,Direction.Rotation(),FVector(FMath::Clamp(Scale,.3f,2.f)),
            false,true,ENCPoolMethod::ManualRelease,true);
        if(Component) {Bursts.Add(Component);Component->OnSystemFinished.AddDynamic(this,&AMCNutCombatEffect::FinishedBurst);}
    }
}

void AMCNutCombatEffect::PresentMagic(float Age,FVector Floor,FVector Normal,FVector Right,FVector Side,float Fade)
{
    using namespace MCNutCombatVisuals;
    const bool Charge=Cue.Type==EMCNutCombatCue::CastCharge || Cue.Type==EMCNutCombatCue::FireCast;
    const bool Release=Cue.Type==EMCNutCombatCue::CastRelease;
    const bool Ritual=Cue.Type==EMCNutCombatCue::RitualCast || Cue.Type==EMCNutCombatCue::SummonTell;
    const bool Transform=Cue.Type==EMCNutCombatCue::Transform;
    const bool Roll=Cue.Type==EMCNutCombatCue::RollTell;
    const bool Shield=Cue.Type==EMCNutCombatCue::ShieldHit;
    const bool Death=Cue.Type==EMCNutCombatCue::DeathBurst;
    const bool Rain=Cue.Type==EMCNutCombatCue::NutRain;
    const bool Motion=Cue.Type==EMCNutCombatCue::ChargeTell && Age>=Cue.WindupSeconds;
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds);
    const float Progress=FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f);
    const auto* Boss=Cast<AMCNutBoss>(SourceActor);
    const FVector Caster=Boss?Boss->GetCastOrigin():Cue.Origin;
    const FVector Source=IsValid(SourceActor)?SourceActor->GetActorLocation():Cue.Origin;
    const FVector Direction=(Cue.Target-Cue.Origin).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Forward=Boss && (Roll || Motion)?Boss->AttackForward:Direction;
    const FLinearColor Tint=Cue.Role==EMCNutBossRole::Mage?FLinearColor(.56f,.19f,1.f):FLinearColor(1.f,.53f,.09f);
    FMesh Detail,Cloud;
    if(MagicMID) {
        MagicMID->SetScalarParameterValue(TEXT("Age"),Age);
        MagicMID->SetScalarParameterValue(TEXT("Progress"),Progress);
        MagicMID->SetVectorParameterValue(TEXT("Tint"),Tint);
        MagicMID->SetScalarParameterValue(TEXT("Beam"),0);
    }
    const FVector LocalFloor=Floor-GetActorLocation();
    if(Charge || Cue.Type==EMCNutCombatCue::RitualCast) {
        const FVector Center=Caster-GetActorLocation();
        const float Radius=FMath::Lerp(14.f,40.f,Progress)*(1.f+.035f*FMath::Sin(Age*19));
        // Three crossed planes form a compact focus visible from every player camera.
        Detail.Quad(Center,FVector::ForwardVector,FVector::UpVector,Radius,Radius,FLinearColor(1,1,1,Fade));
        Detail.Quad(Center,FVector::RightVector,FVector::UpVector,Radius,Radius,FLinearColor(1,1,1,Fade));
        Detail.Quad(Center,FVector::ForwardVector,FVector::RightVector,Radius,Radius,FLinearColor(1,1,1,Fade));
        for(int32 Arm=0;Arm<2;++Arm) for(int32 Segment=0;Segment<12;++Segment) {
            const float Phase=Arm*PI+Segment*.36f-Age*3.2f;
            const float Distance=Radius*(1.65f-Segment*.045f);
            const FVector P=Center+FVector(FMath::Cos(Phase)*Distance,FMath::Sin(Phase)*Distance,Segment*3.f-18.f);
            Detail.Quad(P,FVector::UpVector,FVector(-FMath::Sin(Phase),FMath::Cos(Phase),0),2.2f,4.2f,FLinearColor(1,1,1,Fade*.65f));
        }
    }
    if(Ritual) {
        const float Radius=Cue.Type==EMCNutCombatCue::SummonTell?Cue.Radius*1.18f:55.f;
        const FVector Base=Cue.Type==EMCNutCombatCue::SummonTell?LocalFloor:Source-GetActorLocation();
        Detail.Quad(Base+Normal*9,Right,Side,Radius,Radius,FLinearColor(1,1,1,Fade*.82f));
        const float Rise=30+Progress*85;
        for(int32 Arm=0;Arm<3;++Arm) for(int32 Segment=0;Segment<12;++Segment) {
            const float Fraction=Segment/11.f,Phase=Arm*2*PI/3+Fraction*4.5f-Age*2.2f;
            const FVector P=Base+(Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase))*Radius*.8f+Normal*(12+Fraction*Rise);
            Detail.Quad(P,Normal,Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase),2.2f,4.5f,FLinearColor(1,1,1,Fade*(1-Fraction*.55f)));
        }
        if(Age>=Cue.WindupSeconds && !bReleasePresented) {
            bReleasePresented=true;
            if(Age-Cue.WindupSeconds<.25f) PlayBurst(ArcaneBurstSystem.LoadSynchronous(),Floor+Normal*16,Normal,.65f);
        }
    }
    if(Release) {
        const FVector BeamSide=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        const FVector Base=Cue.Origin-GetActorLocation();
        const float Length=FMath::Lerp(35.f,110.f,FMath::Clamp(Age/Total,0.f,1.f));
        Detail.Quad(Base+Direction*Length*.5f,Direction,BeamSide,Length*.5f,12.f*Fade,FLinearColor(1,1,1,Fade));
        Detail.Quad(Base+Direction*Length*.5f,Direction,FVector::UpVector,Length*.5f,12.f*Fade,FLinearColor(1,1,1,Fade));
        if(MagicMID) MagicMID->SetScalarParameterValue(TEXT("Beam"),1);
        if(!bReleasePresented) {bReleasePresented=true;if(Age<.2f) PlayBurst(ArcaneBurstSystem.LoadSynchronous(),Cue.Origin,Direction,.65f);}
    }
    if(Transform || Roll || Motion || Shield || Death) {
        const FVector Base=Shield?Cue.Origin-GetActorLocation():Source-GetActorLocation();
        const float Expansion=Death?FMath::Lerp(.4f,1.65f,FMath::Clamp(Age/Total,0.f,1.f)):1.f;
        const float Radius=FMath::Clamp(Cue.Radius,35.f,150.f)*Expansion;
        if(Shield) {
            const FVector Facing=(Cue.Origin-Cue.Target).GetSafeNormal(SMALL_NUMBER,Forward);
            const FVector Horizontal=FVector::CrossProduct(Facing,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
            Detail.Quad(Base,Horizontal,FVector::UpVector,Radius,Radius*.85f,FLinearColor(1,1,1,Fade));
            if(!bReleasePresented) {bReleasePresented=true;if(Age<.2f) PlayBurst(ShieldHitSystem.LoadSynchronous(),Cue.Origin,Facing,.8f);}
        } else {
            for(int32 Arm=0;Arm<3;++Arm) for(int32 Segment=0;Segment<14;++Segment) {
                const float Fraction=Segment/13.f,Phase=Arm*2*PI/3+Fraction*5.4f-Age*5;
                const float R=Radius*(.65f+.35f*FMath::Sin(Fraction*PI));
                const FVector P=Base+(Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase))*R+Normal*(Fraction*Radius*1.8f-Radius*.6f);
                Detail.Quad(P,Normal,Right*FMath::Cos(Phase)+Side*FMath::Sin(Phase),2.5f,5.2f,FLinearColor(1,1,1,Fade*.7f));
            }
            if(Transform && !bReleasePresented) {
                bReleasePresented=true;
                if(Age<.2f) PlayBurst(ShieldHitSystem.LoadSynchronous(),Source,Normal,.7f);
            }
            if(Death && !bReleasePresented) {
                bReleasePresented=true;
                if(Age<.25f) {
                    Burst(Floor+Normal*15,false,1.25f);
                    PlayBurst(Cue.Role==EMCNutBossRole::Mage?ArcaneBurstSystem.LoadSynchronous():ShieldHitSystem.LoadSynchronous(),Source,Normal,1.2f);
                }
            }
        }
    }
    if(Cue.Type==EMCNutCombatCue::SlamImpact || Cue.Type==EMCNutCombatCue::EntranceImpact) {
        const float Phase=FMath::Clamp(Age/Total,0.f,1.f);
        for(int32 Ring=0;Ring<2;++Ring) {
            const float Radius=Cue.Radius*FMath::Lerp(.22f,1.1f,Phase)*(1.f-Ring*.24f);
            Detail.Quad(LocalFloor+Normal*(7+Ring*2),Right,Side,Radius,Radius,FLinearColor(1,1,1,Fade*(.72f-Ring*.22f)));
        }
    }
    if(Rain && StormMID) {
        // A shallow cloud lid stays above the fight. The actual falling nut
        // streaks below use the same points and schedule as server damage.
        const FVector CloudCenter=LocalFloor+FVector(0,0,610);
        for(int32 Panel=0;Panel<5;++Panel) {
            const float Angle=Panel*2.39996f+Cue.Seed*.001f;
            const FVector Offset=FVector(FMath::Cos(Angle),FMath::Sin(Angle),.08f*Panel)*Cue.Radius*.32f;
            Cloud.Quad(CloudCenter+Offset,FVector::ForwardVector,FVector::RightVector,Cue.Radius*.52f,Cue.Radius*.48f,FLinearColor(1,1,1,Fade*.28f));
        }
        StormMID->SetScalarParameterValue(TEXT("Age"),Age);
    }
    if(!Detail.Points.IsEmpty()) Detail.Upload(Magic);
    Magic->SetVisibility(!Detail.Points.IsEmpty() && Age<=Total && MagicMID);
    if(!Cloud.Points.IsEmpty()) Cloud.Upload(Storm);
    Storm->SetVisibility(Rain && Age<=Total && StormMID);
    UNiagaraSystem* Sustained=nullptr;
    FVector ParticlePoint=Source;
    if(Charge || Cue.Type==EMCNutCombatCue::RitualCast) {Sustained=CastChargeSystem.LoadSynchronous();ParticlePoint=Caster;}
    else if(Rain) {Sustained=StormMotesSystem.LoadSynchronous();ParticlePoint=Floor+FVector(0,0,480);}
    else if(Transform || Roll || Motion) {Sustained=MotionDustSystem.LoadSynchronous();ParticlePoint=Source-Forward*Cue.Radius*.6f-FVector(0,0,Cue.Radius*.3f);}
    if(Sustained && Age<Total) {
        if(!bSustainedStarted) {bSustainedStarted=true;SustainedParticles->SetAsset(Sustained);SustainedParticles->Activate(true);}
        SustainedParticles->SetWorldLocation(ParticlePoint);SustainedParticles->SetWorldRotation(Forward.Rotation());
    } else if(bSustainedStarted) SustainedParticles->Deactivate();
}

void AMCNutCombatEffect::FinishedBurst(UNiagaraComponent* Component)
{
    if(!IsValid(Component) || !Bursts.Contains(Component)) return;
    Component->OnSystemFinished.RemoveDynamic(this,&AMCNutCombatEffect::FinishedBurst);
    Bursts.Remove(Component);ReleaseBurst(Component);
}

void AMCNutCombatEffect::Present(float Age)
{
    using namespace MCNutCombatVisuals;
    const bool Impact=Cue.Type==EMCNutCombatCue::SlamImpact || Cue.Type==EMCNutCombatCue::FireImpact || Cue.Type==EMCNutCombatCue::EntranceImpact
        || Cue.Type==EMCNutCombatCue::DeathBurst;
    const bool Rain=Cue.Type==EMCNutCombatCue::NutRain,Line=Cue.Type==EMCNutCombatCue::ChargeTell;
    const bool Roll=Cue.Type==EMCNutCombatCue::RollTell,Rolling=Roll && Age>=Cue.WindupSeconds;
    const float Total=FMath::Max(.2f,Cue.WindupSeconds+Cue.ActiveSeconds),T=FMath::Clamp(Age/Total,0.f,1.f);
    const float Fade=Impact || Cue.Type==EMCNutCombatCue::ShieldHit || Cue.Type==EMCNutCombatCue::CastRelease
        ?1-FMath::SmoothStep(.15f,1.f,T):1-FMath::SmoothStep(.88f,1.f,T);
    FHitResult Ground;const FVector Center=(Roll || Cue.Type==EMCNutCombatCue::Transform || Cue.Type==EMCNutCombatCue::DeathBurst) && IsValid(SourceActor)
        ?SourceActor->GetActorLocation():Line?(Cue.Origin+Cue.Target)*.5f:Cue.Target;
    if(Cue.Type==EMCNutCombatCue::FireImpact) {
        Ground.ImpactPoint=Cue.Target;
        Ground.ImpactNormal=(Cue.Origin-Cue.Target).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    } else if(!Tongue->SurfacePoint(Center,Ground)) {Warning->SetVisibility(false);Magic->SetVisibility(false);Storm->SetVisibility(false);return;}
    FVector Facing=Line || Roll?Cue.Target-Cue.Origin:FVector::ForwardVector;
    if(Rolling) if(const auto* Boss=Cast<AMCNutBoss>(SourceActor)) Facing=Boss->AttackForward;
    const FVector Up=Ground.ImpactNormal;
    FVector Right=FVector::VectorPlaneProject(Facing,Up).GetSafeNormal();
    if(Right.IsNearlyZero()) Right=FVector::VectorPlaneProject(FVector::RightVector,Up).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const FVector Side=FVector::CrossProduct(Up,Right).GetSafeNormal();
    FMesh Area;
    const float Size=Impact?Cue.Radius*FMath::Lerp(.18f,1.25f,T):Cue.Radius;
    Area.Quad(Ground.ImpactPoint-GetActorLocation()+Up*5,Right,Side,Line?FMath::Max(Cue.Radius,FVector::Dist(Cue.Origin,Cue.Target)*.5f+Cue.Radius):Size,Line?Cue.Radius:Size,FLinearColor(1,1,1,Fade));
    const bool GroundCue=uint8(Cue.Type)<=uint8(EMCNutCombatCue::RollTell) || Cue.Type==EMCNutCombatCue::DeathBurst;
    Area.Upload(Warning);Warning->SetVisibility(GroundCue && Age<=Total && WarningMID);
    if(WarningMID) {
        WarningMID->SetScalarParameterValue(TEXT("Age"),Age);
        WarningMID->SetScalarParameterValue(TEXT("Progress"),Roll?FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f):T);
        WarningMID->SetScalarParameterValue(TEXT("IsLine"),Line?1:0);WarningMID->SetScalarParameterValue(TEXT("Impact"),Impact?1:0);
        const bool Arcane=Cue.Type==EMCNutCombatCue::NutRain || Cue.Type==EMCNutCombatCue::SummonTell || Cue.Type==EMCNutCombatCue::DeathBurst;
        WarningMID->SetVectorParameterValue(TEXT("Tint"),Cue.Role==EMCNutBossRole::Mage && Arcane?FLinearColor(.58f,.18f,1.f):FLinearColor(1,.56f,.10f));
    }
    const bool Fire=Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::FireImpact || Cue.Type==EMCNutCombatCue::CastCharge;
    Flames->SetVisibility(Fire && Age<=Total && FireMID);
    if(Fire && FireMID) {
        const auto* Caster=Cast<AMCNutBoss>(SourceActor);
        const bool Charging=Cue.Type==EMCNutCombatCue::FireCast || Cue.Type==EMCNutCombatCue::CastCharge;
        Flames->SetRelativeLocation(Charging?(Caster?Caster->GetCastOrigin():Cue.Origin)-GetActorLocation():Ground.ImpactPoint-GetActorLocation());
        FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        const float Charge=FMath::Clamp(Age/FMath::Max(.1f,Cue.WindupSeconds),0.f,1.f);
        RenderFlames(Flames,Charging?FMath::Lerp(8.f,24.f,Charge):FMath::Min(Cue.Radius*.45f,65.f),Age,Fade);
    }
    PresentMagic(Age,Ground.ImpactPoint,Up,Right,Side,Fade);
    if(Impact && !bBurstPresented) {
        bBurstPresented=true;
        // A late joiner observes the remaining ring rather than replaying an old burst.
        if(Age<.25f && Cue.Type!=EMCNutCombatCue::DeathBurst) Burst(Ground.ImpactPoint+Up*10,Fire,FMath::Clamp(Cue.Radius/160.f,.5f,1.5f));
        Debris->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Fire?TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Kernel.SM_Walnut_Kernel")
            :TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_HalfShell.SM_Walnut_HalfShell")));
        for(int32 I=0;I<6;++I) Debris->AddInstance(FTransform(FVector::ZeroVector));
    }
    Debris->SetVisibility(Impact && Age<=Total);
    if(Impact) for(int32 I=0;I<Debris->GetInstanceCount();++I) {
        const float A=I*2.39996f+Cue.Seed*.01f;
        const FVector P=Ground.ImpactPoint-GetActorLocation()+(Right*FMath::Cos(A)+Side*FMath::Sin(A))*Cue.Radius*.8f*T+Up*(8+FMath::Sin(T*PI)*65);
        const float Scale=Cue.Radius*.08f/MeshRadius(Debris->GetStaticMesh())*(1-T);
        Debris->UpdateInstanceTransform(I,FTransform(FRotator(Age*160+I*31,Age*90+I*60,I*37),P,FVector(Scale)),false,I==Debris->GetInstanceCount()-1,true);
    }
    if(Roll) {
        FMesh Arrow;
        Arrow.Quad(Ground.ImpactPoint-GetActorLocation()+Up*8+Right*Cue.Radius*1.15f,
            Right,Side,Cue.Radius*.9f,Cue.Radius*.3f,FLinearColor(1,1,1,Fade));
        Arrow.Upload(DropWarnings);DropWarnings->SetVisibility(Age<=Total && SmallWarningMID);
        if(SmallWarningMID) {
            SmallWarningMID->SetScalarParameterValue(TEXT("IsLine"),1);
            SmallWarningMID->SetScalarParameterValue(TEXT("Age"),Age);
            SmallWarningMID->SetScalarParameterValue(TEXT("Progress"),T);
            SmallWarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.56f,.10f));
        }
        // One sustained emitter supplies dust throughout the roll; no repeated
        // actor/component spawns are needed for movement presentation.
        DropShadows->SetVisibility(false);RainNuts->SetVisibility(false);
        return;
    }
    DropWarnings->SetVisibility(Rain && Age<Total && SmallWarningMID);DropShadows->SetVisibility(Rain && Age<Total && ShadowMID);
    RainNuts->SetVisibility(Rain && Age<Total);
    if(!Rain) return;
    const int32 Count=FMath::Clamp(Cue.DropCount,1,MaxRainDrops);
    if(BuiltDropCount!=Count) {
        RainNuts->ClearInstances();for(int32 I=0;I<Count;++I) RainNuts->AddInstance(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector::ZeroVector));
        BuiltDropCount=Count;
    }
    FMesh Small,Shadows,Streaks;
    for(int32 I=0;I<Count;++I) {
        const float ImpactAt=Cue.WindupSeconds+I*Cue.DropCadence,FlightAge=Age-(ImpactAt-DropFlightSeconds);
        const float P=FMath::Clamp(FlightAge/DropFlightSeconds,0.f,1.f);
        FHitResult Landing;const FVector Candidate=GetNutRainDropPoint(Cue.Target,Cue.Radius,Cue.Seed,I);
        const bool Supported=Tongue->InteriorSurfacePoint(Candidate,Cue.DetailRadius,Landing);
        const bool Falling=Supported && FlightAge>=0 && Age<ImpactAt && ImpactAt<=Total;
        const FVector Location=Supported?Landing.ImpactPoint:GetActorLocation();
        const FVector N=Supported?Landing.ImpactNormal:FVector::UpVector;
        const FVector R=FVector::VectorPlaneProject(FVector::ForwardVector,N).GetSafeNormal(),S=FVector::CrossProduct(N,R).GetSafeNormal();
        Small.Quad(Location-GetActorLocation()+N*7,R,S,Cue.DetailRadius,Cue.DetailRadius,FLinearColor(1,1,1,Falling?.40f+.60f*P:0));
        Shadows.Quad(Location-GetActorLocation()+N*4,R,S,42*(1.25f-.25f*P),42*(1.25f-.25f*P),FLinearColor(1,1,1,Falling?.35f+.65f*P:0));
        const float NutScale=Falling?25.f/MeshRadius(RainNuts->GetStaticMesh()):0;
        const FVector Air=Location-GetActorLocation()+FVector(0,0,660*(1-P*P)+25);
        RainNuts->UpdateInstanceTransform(I,FTransform(FRotator(FlightAge*150+I*37,FlightAge*95+I*61,I*43),Air,FVector(NutScale)),false,I==Count-1,true);
        if(Falling) {
            const float Length=FMath::Lerp(18.f,65.f,P);
            Streaks.Quad(Air+FVector(0,0,Length),FVector::RightVector,FVector::UpVector,2.5f,Length,FLinearColor(1,1,1,.40f+.30f*P));
            Streaks.Quad(Air+FVector(0,0,Length),FVector::ForwardVector,FVector::UpVector,2.5f,Length,FLinearColor(1,1,1,.40f+.30f*P));
        }
        if(Supported && Age>=ImpactAt && (ImpactMask&(1u<<I))==0) {
            ImpactMask|=1u<<I;
            if(Age-ImpactAt<.2f && ImpactAt<=Total) Burst(Location+N*7,false,.55f);
        }
    }
    Small.Upload(DropWarnings);Shadows.Upload(DropShadows);
    if(!Streaks.Points.IsEmpty()) Streaks.Upload(Magic);
    Magic->SetVisibility(!Streaks.Points.IsEmpty() && Age<Total && MagicMID);
    if(MagicMID) {MagicMID->SetScalarParameterValue(TEXT("Beam"),1);MagicMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.65f,.30f,1.f));}
    if(SmallWarningMID) {SmallWarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.35f,.055f));SmallWarningMID->SetScalarParameterValue(TEXT("Age"),Age);}
}

void AMCNutCombatEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority()) {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(SourceActor) || SourceActor->IsActorBeingDestroyed() || !IsValid(Tongue)
            || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) {Destroy();return;}
    }
    if(GetNetMode()!=NM_DedicatedServer && IsValid(Tongue) && Cue.StartedAt>=0) Present(FMath::Max(0.f,float(Now()-Cue.StartedAt)));
}

void AMCNutCombatEffect::Cancel() {if(HasAuthority()) Destroy();}

void AMCNutCombatEffect::EndPlay(const EEndPlayReason::Type Reason)
{
    SustainedParticles->DeactivateImmediate();
    for(UNiagaraComponent* Component:Bursts) if(IsValid(Component)) {
        Component->OnSystemFinished.RemoveDynamic(this,&AMCNutCombatEffect::FinishedBurst);
        Component->DeactivateImmediate();ReleaseBurst(Component);
    }
    Bursts.Reset();Super::EndPlay(Reason);
}

void AMCNutCombatEffect::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCNutCombatEffect,Cue);
    DOREPLIFETIME(AMCNutCombatEffect,Tongue);DOREPLIFETIME(AMCNutCombatEffect,SourceActor);
}

bool AMCNutCombatEffect::AuthorNiagaraAssets()
{
#if WITH_EDITOR
    auto* Template=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Niagara/DefaultAssets/Templates/Systems/FountainLightweight.FountainLightweight"));
    if(!Template) return false;
    struct FEffect
    {
        const TCHAR* Name;
        const TCHAR* MaterialName;
        bool bRate;
        int32 Amount;
        float LifetimeMin,LifetimeMax,Size,SpeedMin,SpeedMax,Gravity,Radius,Angle;
        FVector3f Direction;
        bool bLocalVelocity;
    };
    const FEffect Effects[]={
        {TEXT("NS_NutDustImpact"),TEXT("M_NutDust"),false,24,.45f,.85f,30,50,160,-80,5,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutFireImpact"),TEXT("M_NutEmber"),false,32,.18f,.55f,9,30,280,-450,5,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutEmberTrail"),TEXT("M_NutEmber"),true,36,.18f,.38f,7,30,70,40,12,75,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutArcaneBurst"),TEXT("M_NutArcaneSpark"),false,28,.22f,.65f,10,80,240,0,8,28,FVector3f::XAxisVector,true},
        {TEXT("NS_NutCastCharge"),TEXT("M_NutArcaneSpark"),true,24,.25f,.55f,5,8,24,0,32,120,FVector3f::ZAxisVector,false},
        {TEXT("NS_NutMotionDust"),TEXT("M_NutDust"),true,28,.25f,.65f,22,20,70,15,12,45,-FVector3f::XAxisVector,true},
        {TEXT("NS_NutStormMotes"),TEXT("M_NutArcaneSpark"),true,24,.35f,.85f,3,150,250,-180,120,16,-FVector3f::ZAxisVector,false},
        {TEXT("NS_NutShieldHit"),TEXT("M_NutEmber"),false,18,.15f,.50f,5,100,230,-180,6,70,FVector3f::XAxisVector,true}
    };
    for(const FEffect& Effect:Effects) {
        const FString Name=Effect.Name;
        const FString Path=TEXT("/Game/Gameplay/VFX/NutCombat/")+Name;
        if(FPackageName::DoesPackageExist(Path)) continue;
        const FString MaterialPath=FString(TEXT("/Game/Gameplay/VFX/NutCombat/"))+Effect.MaterialName+TEXT(".")+Effect.MaterialName;
        auto* Parent=LoadObject<UMaterial>(nullptr,*MaterialPath);
        if(!Parent) return false;
        auto* Package=CreatePackage(*Path);auto* System=DuplicateObject<UNiagaraSystem>(Template,Package,*Name);
        System->SetFlags(RF_Public|RF_Standalone);System->ClearFlags(RF_Transient);
        auto* Emitter=System->GetEmitterHandle(0).GetStatelessEmitter();if(!Emitter) return false;
        const auto* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
        if(!StateProperty) return false;
        auto* EmitterState=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);
        EmitterState->LoopBehavior=Effect.bRate?ENiagaraLoopBehavior::Infinite:ENiagaraLoopBehavior::Once;
        EmitterState->LoopDurationMode=ENiagaraLoopDurationMode::Fixed;
        EmitterState->LoopDuration=FNiagaraDistributionRangeFloat(1.f);
        auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Emitter->GetModule(UNiagaraStatelessModule_InitializeParticle::StaticClass()));
        auto* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Emitter->GetModule(UNiagaraStatelessModule_AddVelocity::StaticClass()));
        auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Emitter->GetModule(UNiagaraStatelessModule_ShapeLocation::StaticClass()));
        auto* Gravity=Cast<UNiagaraStatelessModule_GravityForce>(Emitter->GetModule(UNiagaraStatelessModule_GravityForce::StaticClass()));
        if(!Init || !Velocity || !Shape || !Gravity) return false;
        for(int32 I=0;I<Emitter->GetNumSpawnInfos();++I) {
            auto* Spawn=Emitter->GetSpawnInfoByIndex(I);Spawn->bEnabled=I==0;Spawn->SpawnTime=0;Spawn->bSpawnProbabilityEnabled=false;
            Spawn->Type=Effect.bRate?ENiagaraStatelessSpawnInfoType::Rate:ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->Amount=FNiagaraDistributionRangeInt(Effect.Amount);Spawn->Rate=FNiagaraDistributionRangeFloat(float(Effect.Amount));
            Spawn->bLoopCountLimitEnabled=!Effect.bRate;Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1);
        }
        Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(Effect.LifetimeMin,Effect.LifetimeMax);
        Init->SpriteSizeDistribution.InitConstant(FVector2f(Effect.Size));
        Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360);
        Init->ColorDistribution=FNiagaraDistributionColor(FLinearColor::White);
        Shape->SetIsModuleEnabled(true);Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere;Shape->SphereRadius=FNiagaraDistributionRangeFloat(0,Effect.Radius);
        Velocity->SetIsModuleEnabled(true);Velocity->VelocityType=ENSM_VelocityType::InCone;
        Velocity->ConeRotationType=ENSM_ConeRotationType::Direction;Velocity->ConeDirection.InitConstant(Effect.Direction);
        Velocity->CoordinateSpace=Effect.bLocalVelocity?ENiagaraCoordinateSpace::Local:ENiagaraCoordinateSpace::World;
        Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(Effect.SpeedMin,Effect.SpeedMax);Velocity->ConeAngle=Effect.Angle;
        Gravity->SetIsModuleEnabled(true);Gravity->GravityDistribution.InitConstant(FVector3f(0,0,Effect.Gravity));
        if(auto* Scale=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter->GetModule(UNiagaraStatelessModule_ScaleSpriteSize::StaticClass()))) {
            Scale->SetIsModuleEnabled(true);Scale->ScaleDistribution.InitConstant(FVector2f(1.f));
        }
        if(auto* Scale=Emitter->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass())) Scale->SetIsModuleEnabled(false);
        if(auto* Curl=Emitter->GetModule(UNiagaraStatelessModule_CurlNoiseForce::StaticClass())) Curl->SetIsModuleEnabled(false);
        if(auto* Drag=Cast<UNiagaraStatelessModule_Drag>(Emitter->GetModule(UNiagaraStatelessModule_Drag::StaticClass()))) {
            Drag->SetIsModuleEnabled(true);Drag->DragDistribution=FNiagaraDistributionRangeFloat(FCString::Strcmp(Effect.MaterialName,TEXT("M_NutDust"))==0?2.f:.8f);
        }
        const auto Old=Emitter->GetRenderers();for(auto* Renderer:Old) Emitter->RemoveRenderer(Renderer,FGuid());
        auto* Renderer=NewObject<UNiagaraSpriteRendererProperties>(Emitter,NAME_None,RF_Transactional);Renderer->Material=Parent;Emitter->AddRenderer(Renderer,FGuid());
        System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-350),FVector(350)));
        System->MaxPoolSize=16;System->PoolPrimeSize=0;
        Emitter->PostEditChange();System->PostEditChange();System->RequestCompile(true);System->WaitForCompilationComplete(false,false);
        FAssetRegistryModule::AssetCreated(System);System->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;Save.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,System,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Save)) return false;
    }
    return true;
#else
    return false;
#endif
}
