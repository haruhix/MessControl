#include "MCIceEvent.h"
#include "MCEnemyWeaponContact.h"
#include "MCPerkComponent.h"
#include "MCIceEventVFX.h"
#include "Algo/MinElement.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "MCColdCola.h"
#include "MCFreezeBarWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCGameDirector.h"
#include "MCInventoryComponent.h"
#include "MCBrushContactComponent.h"
#include "MCLocomotionSurface.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float MintRadius=180.f;

struct FIceGuideMesh
{
    TArray<FVector> Vertices,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    void Ring(AMCTongue* Tongue,const FTransform& Transform,FVector Center,float Radius,float Width,FLinearColor Color,bool bDashed=false)
    {
        constexpr int32 Segments=48;
        const int32 First=Vertices.Num();
        for(int32 I=0;I<=Segments;++I)
        {
            const float Angle=I*2*PI/Segments;
            const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
            for(int32 Side=0;Side<2;++Side)
            {
                const FVector Point=Center+Direction*(Radius+(Side?Width:-Width));
                FHitResult Floor;
                const bool bFloor=Tongue && Tongue->SurfacePoint(Point,Floor);
                const FVector Up=bFloor?Floor.ImpactNormal:FVector::UpVector;
                Vertices.Add(Transform.InverseTransformPosition((bFloor?Floor.ImpactPoint:Point)+Up*5));
                Normals.Add(Transform.InverseTransformVectorNoScale(Up).GetSafeNormal());
                UV.Add(FVector2D(I/float(Segments),Side)); Colors.Add(Color);
                Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Direction).GetSafeNormal(),false));
            }
            if(I>0 && (!bDashed || (I-1)%4<3))
            {
                const int32 A=First+(I-1)*2;
                Indices.Append({A,A+3,A+1,A,A+2,A+3});
            }
        }
    }

    void Strip(AMCTongue* Tongue,const FTransform& Transform,FVector A,FVector B,float Width,FLinearColor Color,float Height=6)
    {
        const FVector Across=FVector::CrossProduct((B-A).GetSafeNormal2D(),FVector::UpVector)*Width;
        const int32 First=Vertices.Num();
        for(FVector Point:{A-Across,A+Across,B-Across,B+Across})
        {
            FHitResult Floor;
            if(Tongue && !Tongue->SurfacePoint(Point,Floor)) return;
            const FVector Up=Tongue?Floor.ImpactNormal:FVector::UpVector;
            Vertices.Add(Transform.InverseTransformPosition((Tongue?Floor.ImpactPoint:Point)+Up*Height));
            Normals.Add(Transform.InverseTransformVectorNoScale(Up).GetSafeNormal());
            UV.Add(FVector2D::ZeroVector); Colors.Add(Color);
            Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale((B-A).GetSafeNormal()),false));
        }
        Indices.Append({First,First+3,First+1,First,First+2,First+3});
    }
};
}

AMCIceEvent::AMCIceEvent()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    SetNetUpdateFrequency(10); SetMinNetUpdateFrequency(5);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MintCandy")); SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Candy(TEXT("/Game/Gameplay/Cold/Candy/SM_WinterCandy.SM_WinterCandy"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Colors(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ice(TEXT("/Game/Art/Materials/ice/MI_Ice.MI_Ice"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Coating(TEXT("/Game/Gameplay/Cold/M_PlayerIceCoating.M_PlayerIceCoating"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TongueFrost(TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
    static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> MouthClimate(TEXT("/Game/Gameplay/Cold/MPC_MouthClimate.MPC_MouthClimate"));
    Body->SetStaticMesh(Candy.Succeeded()?Candy.Object:Cone.Object);
    if(!Candy.Succeeded()) Body->SetMaterial(0,Ice.Object);
    CandyVisual=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AnimatedCandy"));
    CandyVisual->SetupAttachment(Body); CandyVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CandyVisual->SetGenerateOverlapEvents(false); CandyVisual->SetVisibility(false);
    CandyVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> AnimatedCandy(TEXT("/Game/Gameplay/Cold/Candy/Animations/SK_WinterCandy.SK_WinterCandy"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleClip(TEXT("/Game/Gameplay/Cold/Candy/Animations/A_WinterCandy_Idle.A_WinterCandy_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> MiniClip(TEXT("/Game/Gameplay/Cold/Candy/Animations/A_WinterCandy_MiniCandy.A_WinterCandy_MiniCandy"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> FloorClip(TEXT("/Game/Gameplay/Cold/Candy/Animations/A_WinterCandy_FloorHit.A_WinterCandy_FloorHit"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ScreamClip(TEXT("/Game/Gameplay/Cold/Candy/Animations/A_WinterCandy_Scream.A_WinterCandy_Scream"));
    CandyVisual->SetSkeletalMeshAsset(AnimatedCandy.Object);
    IdleAnimation=IdleClip.Object; MiniCandyAnimation=MiniClip.Object; FloorHitAnimation=FloorClip.Object; ScreamAnimation=ScreamClip.Object;
    ConeMesh=Cone.Object; GuideMaterial=Colors.Object; IcicleMaterial=Ice.Object; Climate=MouthClimate.Object;
    PlayerIceMaterial=Coating.Object;
    Body->SetCollisionProfileName(TEXT("BlockAllDynamic")); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore); Body->SetVisibility(false);
    CandyFace=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MintStripes")); CandyFace->SetupAttachment(Body);
    CandyFace->SetCollisionEnabled(ECollisionEnabled::NoCollision); CandyFace->SetMaterial(0,GuideMaterial);
    FloorGuides=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WarmthAndIcicleGuides")); FloorGuides->SetupAttachment(Body);
    FloorGuides->SetCollisionEnabled(ECollisionEnabled::NoCollision); FloorGuides->SetCastShadow(false);
    FloorGuides->SetAbsolute(true,true,true);
    FloorGuides->SetMaterial(0,GuideMaterial);
    FrostSurface=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrozenTongue")); FrostSurface->SetupAttachment(Body);
    FrostSurface->SetAbsolute(true,true,true); FrostSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FrostSurface->SetCastShadow(false); FrostSurface->SetMaterial(0,TongueFrost.Object);
    NovaWind=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FrostNovaWind")); NovaWind->SetupAttachment(Body);
    NovaWind->SetAbsolute(true,true,true); NovaWind->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NovaWind->SetCastShadow(false); NovaWind->SetMaterial(0,GuideMaterial);
}

void AMCIceEvent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer) BuildCandyFace();
    OnRep_State();
}

double AMCIceEvent::Now() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

bool AMCIceEvent::AnchorFloor(FVector Anchor,FHitResult& Hit) const
{
    return IsValid(Tongue) && !Tongue->IsActorBeingDestroyed()
        && Tongue->SurfacePoint(Tongue->GetActorTransform().TransformPosition(Anchor),Hit);
}

bool AMCIceEvent::IsParticipant(const AMCToothCharacter* Hero) const
{
    return !bLabEvent || LabParticipants.ContainsByPredicate([Hero](const auto& Subject) { return Subject.Get()==Hero; });
}

void AMCIceEvent::Start()
{
    if(!HasAuthority()) return;
    Stop(); bFailed=false;
    // Keep a single winter owner even when F3 starts it during another slot.
    if(!bLabEvent) {
        for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
            if(*It!=this && !It->bLabEvent && It->IsActive()) { It->Stop(); It->Destroy(); }
        Tongue=nullptr;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
            if(!It->CurrentVertices().IsEmpty()) { Tongue=*It; break; }
    }
    if(!IsValid(Tongue)) { Finish(true); return; }
    int32 TeamSize=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(IsParticipant(*It) && It->GetController() && It->Status && It->Status->IsAlive()) ++TeamSize;
    // Upgrade only generated defaults retained by existing saved instances.
    if(FMath::IsNearlyEqual(CandyHealthPerPlayer,960.f)) CandyHealthPerPlayer=1440.f;
    if(FMath::IsNearlyEqual(ZoneCrystalHealth,80.f)) ZoneCrystalHealth=160.f;
    if(FMath::IsNearlyEqual(NovaFootHealth,80.f)) NovaFootHealth=160.f;
    MaxCandyHealth=(FMath::IsFinite(CandyHealthPerPlayer)?FMath::Clamp(CandyHealthPerPlayer,1.f,10000.f):1440.f)*FMath::Clamp(TeamSize,1,8);
    ArrivalSeconds=FMath::IsFinite(ArrivalSeconds)?FMath::Clamp(ArrivalSeconds,1.f,10.f):3.f;
    FreezeSeconds=FMath::IsFinite(FreezeSeconds)?FMath::Clamp(FreezeSeconds,4.f,60.f):12.f;
    ThawSeconds=FMath::IsFinite(ThawSeconds)?FMath::Clamp(ThawSeconds,1.f,20.f):4.f;
    CircleSeconds=FMath::IsFinite(CircleSeconds)?FMath::Clamp(CircleSeconds,20.f,120.f):45.f;
    MinCircleSeconds=FMath::IsFinite(MinCircleSeconds)?FMath::Clamp(MinCircleSeconds,20.f,CircleSeconds):20.f;
    CircleStepSeconds=FMath::IsFinite(CircleStepSeconds)?FMath::Clamp(CircleStepSeconds,0.f,30.f):5.f;
    CircleOverlapSeconds=FMath::IsFinite(CircleOverlapSeconds)?FMath::Clamp(CircleOverlapSeconds,1.f,MinCircleSeconds*.5f):10.f;
    CircleRadius=FMath::IsFinite(CircleRadius)?FMath::Clamp(CircleRadius,100.f,450.f):330.f;
    MinCircleRadius=FMath::IsFinite(MinCircleRadius)?FMath::Clamp(MinCircleRadius,80.f,CircleRadius):200.f;
    CircleMinGap=FMath::IsFinite(CircleMinGap)?FMath::Clamp(CircleMinGap,0.f,3000.f):1100.f;
    IcicleWarningSeconds=FMath::IsFinite(IcicleWarningSeconds)?FMath::Clamp(IcicleWarningSeconds,1.f,5.f):2.2f;
    IcicleSeriesSpacingSeconds=FMath::IsFinite(IcicleSeriesSpacingSeconds)?FMath::Clamp(IcicleSeriesSpacingSeconds,.5f,4.f):1.2f;
    IcicleIntervalSeconds=FMath::IsFinite(IcicleIntervalSeconds)?FMath::Clamp(IcicleIntervalSeconds,IcicleSeriesSpacingSeconds*2+1,300.f):12.f;
    IcicleRadius=FMath::IsFinite(IcicleRadius)?FMath::Clamp(IcicleRadius,50.f,220.f):155.f;
    IcicleDamage=FMath::IsFinite(IcicleDamage)?FMath::Clamp(IcicleDamage,0.f,100.f):35.f;
    ZoneCrystalIntervalSeconds=FMath::IsFinite(ZoneCrystalIntervalSeconds)?FMath::Clamp(ZoneCrystalIntervalSeconds,4.f,300.f):16.f;
    ZoneCrystalHealth=FMath::IsFinite(ZoneCrystalHealth)?FMath::Clamp(ZoneCrystalHealth,1.f,2000.f):160.f;
    ZoneCrystalFallSeconds=FMath::IsFinite(ZoneCrystalFallSeconds)?FMath::Clamp(ZoneCrystalFallSeconds,.5f,5.f):1.4f;
    ZoneCrystalFallHeight=FMath::IsFinite(ZoneCrystalFallHeight)?FMath::Clamp(ZoneCrystalFallHeight,100.f,2000.f):900.f;
    ZoneCrystalVortexRadius=FMath::IsFinite(ZoneCrystalVortexRadius)?FMath::Clamp(ZoneCrystalVortexRadius,50.f,900.f):440.f;
    ZoneCrystalVortexSeconds=FMath::IsFinite(ZoneCrystalVortexSeconds)?FMath::Clamp(ZoneCrystalVortexSeconds,.5f,8.f):2.5f;
    ZoneCrystalPush=FMath::IsFinite(ZoneCrystalPush)?FMath::Clamp(ZoneCrystalPush,0.f,1500.f):650.f;
    NovaIntervalSeconds=FMath::IsFinite(NovaIntervalSeconds)?FMath::Clamp(NovaIntervalSeconds,4.f,300.f):18.f;
    NovaWarningSeconds=FMath::IsFinite(NovaWarningSeconds)?FMath::Clamp(NovaWarningSeconds,1.f,6.f):2.5f;
    NovaRange=FMath::IsFinite(NovaRange)?FMath::Clamp(NovaRange,100.f,5000.f):2600.f;
    NovaHalfAngleDegrees=FMath::IsFinite(NovaHalfAngleDegrees)?FMath::Clamp(NovaHalfAngleDegrees,10.f,70.f):35.f;
    NovaFootHealth=FMath::IsFinite(NovaFootHealth)?FMath::Clamp(NovaFootHealth,1.f,1000.f):160.f;
    BossVortexSeconds=FMath::IsFinite(BossVortexSeconds)?FMath::Clamp(BossVortexSeconds,.5f,12.f):5.f;
    BossFrostRadius=FMath::IsFinite(BossFrostRadius)?FMath::Clamp(BossFrostRadius,100.f,1500.f):700.f;
    BossFreezeMultiplier=FMath::IsFinite(BossFreezeMultiplier)?FMath::Clamp(BossFreezeMultiplier,1.f,5.f):2.f;
    auto Seconds=[](float Value,float Default,float Maximum) {return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,1.f,Maximum);};
    ColdThroneActiveSeconds=Seconds(ColdThroneActiveSeconds,40,120);
    ColdThroneRestSeconds=Seconds(ColdThroneRestSeconds,40,120);
    IceBombCooldownSeconds=Seconds(IceBombCooldownSeconds,5,90);
    ColdWaveCooldownSeconds=Seconds(ColdWaveCooldownSeconds,10,90);
    IcePrisonCooldownSeconds=Seconds(IcePrisonCooldownSeconds,15,90);
    IcePrisonEscapeSeconds=FMath::Clamp(FMath::IsFinite(IcePrisonEscapeSeconds)?IcePrisonEscapeSeconds:4.f,3.f,5.f);
    IceBombRadius=FMath::Clamp(FMath::IsFinite(IceBombRadius)?IceBombRadius:300.f,50.f,900.f);
    IcePrisonRadius=FMath::Clamp(FMath::IsFinite(IcePrisonRadius)?IcePrisonRadius:300.f,50.f,900.f);
    IceBombDamage=FMath::Clamp(FMath::IsFinite(IceBombDamage)?IceBombDamage:20.f,0.f,100.f);
    IcePrisonDamage=FMath::Clamp(FMath::IsFinite(IcePrisonDamage)?IcePrisonDamage:10.f,0.f,100.f);
    IceBombPush=FMath::Clamp(FMath::IsFinite(IceBombPush)?IceBombPush:650.f,0.f,2000.f);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize((State?State->RunSeed:41)^FMath::Rand()^0x1ce2026);
    FHitResult Floor;
    const FVector Center=Tongue->Surface->Bounds.Origin;
    if(!Tongue->InteriorSurfacePoint(Center,MintRadius+35,Floor)
        && !Tongue->RandomInteriorPoint(Random,MintRadius+35,0,TArray<FVector>(),Floor)) { Finish(true); return; }
    CandyAnchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    CandyHealth=MaxCandyHealth; CircleIndex=0; StrikeSerial=0; CrystalSerial=0;
    if(!ChooseCircle(SafeAnchor,true)) { Finish(true); return; }
    StartedAt=Now(); CircleStartedAt=StartedAt+ArrivalSeconds; NextIcicleAt=CircleStartedAt+5;
    NextCircleStartedAt=0; NextZoneCrystalAt=CircleStartedAt+ZoneCrystalIntervalSeconds;
    NextNovaAt=CircleStartedAt+NovaIntervalSeconds;
    BossAttack=FMCCandyAttackState(); ExtinguishedZones.Reset(); WaveHitHeroes.Reset(); bSecondPhase=false;
    NextBossAttackAt=CircleStartedAt+6; NextScreamAt=0;
    bColdThroneActive=false; ColdThroneEndsAt=NextColdThroneAt=0;
    NextIceBombAt=CircleStartedAt+IceBombCooldownSeconds;
    NextColdWaveAt=CircleStartedAt+ColdWaveCooldownSeconds;
    NextIcePrisonAt=0;
    if(UsesColdThroneContract())
    {
        NextZoneCrystalAt=CircleStartedAt+6; NextNovaAt=CircleStartedAt+12;
        AttackRecoverySeconds=FMath::Clamp(FMath::IsFinite(AttackRecoverySeconds)?AttackRecoverySeconds:5.f,2.f,12.f);
        CrystalFlightSeconds=FMath::Clamp(FMath::IsFinite(CrystalFlightSeconds)?CrystalFlightSeconds:3.f,1.5f,6.f);
        WaveTravelSeconds=FMath::Clamp(FMath::IsFinite(WaveTravelSeconds)?WaveTravelSeconds:2.8f,1.f,5.f);
        ScreamWarningSeconds=FMath::Clamp(FMath::IsFinite(ScreamWarningSeconds)?ScreamWarningSeconds:2.f,1.5f,5.f);
        ScreamCooldownSeconds=FMath::Clamp(FMath::IsFinite(ScreamCooldownSeconds)?ScreamCooldownSeconds:24.f,16.f,90.f);
    }
    if(UsesColdThroneContract()) NextBossAttackAt=CircleStartedAt;
    Stage=EMCIceEventStage::Arrival;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    SlipperyFloor=GetWorld()->SpawnActor<AMCLocomotionSurface>(Bounds.GetCenter(),FRotator::ZeroRotator);
    if(SlipperyFloor)
    {
        SlipperyFloor->HalfExtent=Bounds.GetExtent()+FVector(0,0,700);
        SlipperyFloor->SetOwner(this);
        SlipperyFloor->Priority=150; SlipperyFloor->Surface=EMCGroundSurface::Slippery;
        SlipperyFloor->RefreshBounds(); SlipperyFloor->ForceNetUpdate();
    }
    OnRep_State(); RefreshPresentation(.1f); PublishManualHUD(); ForceNetUpdate();
}

float AMCIceEvent::FrostAmount() const
{
    return IsActive()?FMath::Clamp(float((Now()-StartedAt)/FMath::Max(1.f,ArrivalSeconds)),0.f,1.f):0;
}

float AMCIceEvent::PhaseTransitionAge() const
{
    // EndsAt is updated by the existing server clock. Late clients enter at
    // its current age, and the next cold cycle has the same readable front.
    return IsGlobalColdActive()?float(FMath::Max(0.,Now()-(ColdThroneEndsAt-ColdThroneActiveSeconds))):-1.f;
}

float AMCIceEvent::PhaseTransitionProgress() const
{
    const float Age=PhaseTransitionAge();
    if(Age>=0) return FMath::Clamp((Age-PhaseTransitionChargeSeconds)/PhaseTransitionSweepSeconds,0.f,1.f);
    const float ReturnAge=PhaseReturnAge();
    // First clear the environment blanket, then retract the white tongue
    // frost to the permanent local aura during the forty-second rest.
    return ReturnAge>=0?1-FMath::Clamp((ReturnAge-PhaseTransitionClimateSeconds)/PhaseTransitionSweepSeconds,0.f,1.f):0.f;
}

float AMCIceEvent::PhaseReturnAge() const
{
    const double Age=Now()-ColdThroneEndsAt;
    return UsesColdThroneContract() && Stage==EMCIceEventStage::Active && bSecondPhase
        && !IsGlobalColdActive() && ColdThroneEndsAt>0 && Age>=0?float(Age):-1.f;
}

float AMCIceEvent::PhaseTransitionRadius() const
{
    FHitResult Floor;
    if(!IsValid(Tongue) || !Tongue->Surface || !AnchorFloor(CandyAnchor,Floor)) return BossFrostRadius;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float X=float(FMath::Max(FMath::Abs(Bounds.Min.X-Floor.ImpactPoint.X),FMath::Abs(Bounds.Max.X-Floor.ImpactPoint.X)));
    const float Y=float(FMath::Max(FMath::Abs(Bounds.Min.Y-Floor.ImpactPoint.Y),FMath::Abs(Bounds.Max.Y-Floor.ImpactPoint.Y)));
    const float EndRadius=FMath::Max(BossFrostRadius,FMath::Sqrt(X*X+Y*Y)+100.f);
    return FMath::Lerp(BossFrostRadius,EndRadius,PhaseTransitionProgress());
}

float AMCIceEvent::ClimateContribution() const
{
    if(!UsesColdThroneContract()) return FrostAmount();
    if(!bSecondPhase) return 0.f;
    const float Age=PhaseTransitionAge();
    if(Age<0)
    {
        const float ReturnAge=PhaseReturnAge();
        return ReturnAge>=0?FrostAmount()*(1-FMath::SmoothStep(0.f,PhaseTransitionClimateSeconds,ReturnAge)):0.f;
    }
    // Preserve the radial tongue front before fading in the environment frost.
    const float ClimateBlend=FMath::SmoothStep(PhaseTransitionChargeSeconds+PhaseTransitionSweepSeconds,
        PhaseTransitionChargeSeconds+PhaseTransitionSweepSeconds+PhaseTransitionClimateSeconds,Age);
    return FrostAmount()*ClimateBlend;
}

bool AMCIceEvent::IsGlobalColdActive() const
{
    return UsesColdThroneContract() && Stage==EMCIceEventStage::Active && bSecondPhase
        && bColdThroneActive && Now()<ColdThroneEndsAt;
}

float AMCIceEvent::ColdThroneSecondsRemaining() const
{
    return IsGlobalColdActive()?float(FMath::Max(0.,ColdThroneEndsAt-Now())):0.f;
}

float AMCIceEvent::ColdThroneCooldownRemaining() const
{
    return UsesColdThroneContract() && IsActive() && bSecondPhase && !IsGlobalColdActive()
        ?float(FMath::Max(0.,NextColdThroneAt-Now())):0.f;
}

bool AMCIceEvent::IsInsideColdThrone(FVector WorldPoint) const
{
    FHitResult Floor;
    return IsActive() && !WorldPoint.ContainsNaN() && AnchorFloor(CandyAnchor,Floor)
        && FVector::DistSquaredXY(WorldPoint,Floor.ImpactPoint)<=FMath::Square(BossFrostRadius)
        && FMath::Abs(WorldPoint.Z-Floor.ImpactPoint.Z)<180;
}

float AMCIceEvent::GetAbilityCooldownRemaining(EMCCandyAttack Kind) const
{
    const double Due=Kind==EMCCandyAttack::MiniCandy?NextIceBombAt:
        Kind==EMCCandyAttack::FloorHit?NextColdWaveAt:Kind==EMCCandyAttack::Scream?NextIcePrisonAt:0;
    return float(FMath::Max(0.,Due-Now()));
}

void AMCIceEvent::UpdateColdThrone(double Time)
{
    if(!HasAuthority() || !UsesColdThroneContract()) return;
    if(!bSecondPhase && CandyHealth<=MaxCandyHealth*.5f)
    {
        bSecondPhase=true;
        bColdThroneActive=true;
        ColdThroneEndsAt=Time+ColdThroneActiveSeconds;
        NextColdThroneAt=ColdThroneEndsAt+ColdThroneRestSeconds;
        NextIcePrisonAt=Time;
        ForceNetUpdate();
    }
    if(!bSecondPhase) return;
    if(bColdThroneActive && Time>=ColdThroneEndsAt)
    {bColdThroneActive=false; ForceNetUpdate();}
    if(!bColdThroneActive && Time>=NextColdThroneAt)
    {
        // Preserve the authored 40/40 rhythm even after a long paused frame.
        const double Period=double(ColdThroneActiveSeconds)+ColdThroneRestSeconds;
        const double Start=NextColdThroneAt+FMath::FloorToDouble((Time-NextColdThroneAt)/Period)*Period;
        ColdThroneEndsAt=Start+ColdThroneActiveSeconds;
        NextColdThroneAt=ColdThroneEndsAt+ColdThroneRestSeconds;
        bColdThroneActive=Time<ColdThroneEndsAt;
        ForceNetUpdate();
    }
}

float AMCIceEvent::CircleDuration(int32 Index) const
{
    return FMath::Max(MinCircleSeconds,CircleSeconds-CircleStepSeconds*FMath::Max(0,Index));
}

float AMCIceEvent::SafeRadius() const
{
    const float Age=FMath::Clamp(float((Now()-CircleStartedAt)/FMath::Max(1.f,CircleDuration(CircleIndex))),0.f,1.f);
    return FMath::Lerp(CircleRadius,MinCircleRadius,Age);
}

float AMCIceEvent::NextSafeRadius() const
{
    const float Age=bNextCircle?FMath::Clamp(float((Now()-NextCircleStartedAt)/FMath::Max(1.f,CircleDuration(CircleIndex+1))),0.f,1.f):0;
    return FMath::Lerp(CircleRadius,MinCircleRadius,Age);
}

bool AMCIceEvent::IsCircleBlocked(int32 Index) const
{
    if(UsesColdThroneContract())
    {
        FHitResult Floor;
        const FVector Anchor=Index==CircleIndex?SafeAnchor:NextSafeAnchor;
        if((Index==CircleIndex || (bNextCircle && Index==CircleIndex+1))
            && AnchorFloor(Anchor,Floor) && IsInsideColdThrone(Floor.ImpactPoint)) return true;
    }
    return ExtinguishedZones.Contains(Index) || ZoneCrystals.ContainsByPredicate([Index](const FMCIceZoneCrystal& Crystal)
        {return Crystal.ZoneIndex==Index && Crystal.bLanded && Crystal.Health>0;});
}

bool AMCIceEvent::IsSafePoint(FVector WorldPoint) const
{
    if(!IsActive() || WorldPoint.ContainsNaN()) return false;
    if(UsesColdThroneContract() && IsInsideColdThrone(WorldPoint)) return false;
    auto Inside=[&](FVector Anchor,float Radius)
    {
        FHitResult Floor;
        return AnchorFloor(Anchor,Floor)
            && FVector::DistSquaredXY(WorldPoint,Floor.ImpactPoint)<=FMath::Square(Radius)
            && FMath::Abs(WorldPoint.Z-Floor.ImpactPoint.Z)<150;
    };
    const double Time=Now();
    for(const auto& Patch:BrushedWarmPatches)
        if(BrushedWarmRadius(Patch)>=65 && Inside(Patch.Anchor,BrushedWarmRadius(Patch))) return true;
    return ((!bNextCircle || Time<FMath::Max(CircleStartedAt+CircleDuration(CircleIndex),CircleHandoffAt))
            && !IsCircleBlocked(CircleIndex) && Inside(SafeAnchor,SafeRadius()))
        || (bNextCircle && Time<NextCircleStartedAt+CircleDuration(CircleIndex+1)
            && !IsCircleBlocked(CircleIndex+1) && Inside(NextSafeAnchor,NextSafeRadius()));
}

float AMCIceEvent::BrushedWarmRadius(const FMCBrushedWarmPatch& Patch) const
{
    FHitResult Floor;
    if(UsesColdThroneContract() && AnchorFloor(Patch.Anchor,Floor) && IsInsideColdThrone(Floor.ImpactPoint)) return 0;
    const float Clean=FMath::Clamp(Patch.Cleaned-float(FMath::Max(0.,Now()-Patch.LastBrushedAt))/FMath::Max(1.f,Patch.RefreezeSeconds),0.f,1.f);
    return FMath::Max(50.f,BrushWarmRadius)*Patch.RadiusMultiplier*FMath::Sqrt(Clean);
}

bool AMCIceEvent::CanBrushIceUnderfoot(const AMCToothCharacter* Hero) const
{
    if(Stage!=EMCIceEventStage::Active || !IsValid(Tongue) || !IsValid(Hero)
        || !IsParticipant(Hero) || !Hero->CanWork() || !Hero->HasBrush()
        || Hero->HeldFood || Hero->bInCoffee || !Hero->BrushContact) return false;
    const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FHitResult Floor;
    return Tongue->SurfacePoint(Feet,Floor) && FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)<=30;
}

bool AMCIceEvent::BrushIceUnderfoot(AMCToothCharacter* Hero,float Seconds)
{
    if(!HasAuthority() || !CanBrushIceUnderfoot(Hero) || !Hero->bBrushing || !FMath::IsFinite(Seconds) || Seconds<=0) return false;
    const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FHitResult Floor;if(!Tongue->SurfacePoint(Feet,Floor)) return false;
    const double Time=Now();
    FHitResult BrushFloor;
    const FVector Aim=Floor.ImpactPoint+Hero->GetActorForwardVector()*65+Hero->GetActorRightVector()*(FMath::Sin(float(Time)*22)*6);
    if(!Tongue->SurfacePoint(Aim,BrushFloor)) return false;
    Hero->BrushContact->Contact(this,BrushFloor.ImpactPoint,BrushFloor.ImpactNormal);
    if(!Hero->BrushContact->IsWorkReady()) return true;
    // The permanent strong aura immediately closes a brushed warm patch.
    if(UsesColdThroneContract() && IsInsideColdThrone(Floor.ImpactPoint)) return true;
    BrushedWarmPatches.RemoveAll([this](const FMCBrushedWarmPatch& Patch){return BrushedWarmRadius(Patch)<1;});
    int32 Index=INDEX_NONE;
    for(int32 I=0;I<BrushedWarmPatches.Num();++I) {
        FHitResult Existing;
        if(AnchorFloor(BrushedWarmPatches[I].Anchor,Existing) && FVector::DistSquared2D(Existing.ImpactPoint,Floor.ImpactPoint)<FMath::Square(75.f)) {Index=I;break;}
    }
    if(Index==INDEX_NONE) {
        if(BrushedWarmPatches.Num()>=32) return false;
        Index=BrushedWarmPatches.AddDefaulted();
        BrushedWarmPatches[Index].Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
        BrushedWarmPatches[Index].LastBrushedAt=Time;
    }
    auto& Patch=BrushedWarmPatches[Index];
    const float Before=FMath::Square(BrushedWarmRadius(Patch)/(FMath::Max(50.f,BrushWarmRadius)*Patch.RadiusMultiplier));
    const auto* Perks=UMCPerkComponent::ForActor(Hero);
    Patch.RefreezeSeconds=Perks?Perks->RogueValue(EMCRogueStat::WarmDuration,BrushRefreezeSeconds):BrushRefreezeSeconds;
    Patch.RadiusMultiplier=Perks?Perks->RogueValue(EMCRogueStat::WarmRadius):1.f;
    Patch.Cleaned=FMath::Clamp(Before+FMath::Min(Seconds,.1f)*Hero->Inventory->CleaningSpeedMultiplier()/FMath::Max(.5f,BrushWarmSeconds),0.f,1.f);
    Patch.LastBrushedAt=Time;
    ForceNetUpdate(); return true;
}

bool AMCIceEvent::ChooseCircle(FVector& Anchor,bool bFirst)
{
    if(!IsValid(Tongue)) return false;
    FHitResult Candy,Previous,Floor;
    if(!AnchorFloor(CandyAnchor,Candy)) return false;
    const bool bPrevious=!bFirst && AnchorFloor(SafeAnchor,Previous);
    if(!bFirst && !bPrevious) return false;
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    const float Reach=float(Bounds.GetExtent().Size2D());
    // The gap is measured between the shrinking old circle and the full new
    // circle, rather than between their centers. Never relax it on a small arena.
    const float Separation=bPrevious?SafeRadius()+CircleRadius+CircleMinGap:0.f;
    const double SeparationSquared=FMath::Square(Separation);
    TArray<FVector> Candidates;
    auto AddCandidate=[&](FVector Point)
    {
        if(bPrevious && FVector::DistSquaredXY(Point,Previous.ImpactPoint)<SeparationSquared) return false;
        Candidates.Add(Point); return true;
    };
    const float Offset=Random.FRandRange(-PI,PI);
    // Sample the actual tissue, excluding throat/brush delivery lanes. An inset
    // of the bounds alone would let a zone hang over a concave tongue edge.
    for(int32 Ray=0;Ray<48;++Ray)
    {
        const float Angle=Offset+(Ray+Random.FRandRange(-.35f,.35f))*2*PI/48;
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        for(int32 Step=20;Step>0;--Step)
        {
            const FVector Point=Candy.ImpactPoint+Direction*(Reach*Step/20);
            if(!Tongue->GameplaySpawnFootprint(Point,CircleRadius,Floor)) continue;
            if(AddCandidate(Floor.ImpactPoint)) break;
        }
    }
    // Supplement the edge rays on irregular tissue. The shared sampler checks
    // the complete physical footprint; gameplay bands still exclude delivery.
    TArray<FVector> Excluded;
    if(bPrevious) Excluded.Add(Previous.ImpactPoint);
    for(int32 Attempt=0;Attempt<24;++Attempt)
        if(Tongue->RandomInteriorPoint(Random,CircleRadius,Separation,Excluded,Floor)
            && Tongue->GameplaySpawnFootprint(Floor.ImpactPoint,CircleRadius,Floor)) AddCandidate(Floor.ImpactPoint);
    double Farthest=0;
    for(const FVector& Point:Candidates)
        Farthest=FMath::Max(Farthest,FVector::DistSquaredXY(Point,Candy.ImpactPoint));
    TArray<FVector> OuterCandidates;
    for(const FVector& Point:Candidates)
        if(FVector::DistSquaredXY(Point,Candy.ImpactPoint)>=Farthest*.65) OuterCandidates.Add(Point);
    if(!OuterCandidates.IsEmpty())
    {
        const FVector Point=OuterCandidates[Random.RandRange(0,OuterCandidates.Num()-1)];
        Anchor=Tongue->GetActorTransform().InverseTransformPosition(Point); return true;
    }
    // A small test/replacement arena may not have authored gameplay bands.
    // Only the first zone may use its center; later zones keep the required gap.
    if(bFirst && Tongue->InteriorSurfacePoint(Candy.ImpactPoint,CircleRadius,Floor)) {Anchor=CandyAnchor; return true;}
    return false;
}

void AMCIceEvent::UpdateCircles(double Time)
{
    bool bChanged=false;
    for(int32 Catchup=0;Catchup<32;++Catchup)
    {
        const double Ends=FMath::Max(CircleStartedAt+CircleDuration(CircleIndex),CircleHandoffAt);
        if(!bNextCircle && Time>=Ends-CircleOverlapSeconds && Time>=NextCircleRetryAt)
        {
            bNextCircle=ChooseCircle(NextSafeAnchor);
            NextCircleRetryAt=Time+2;
            if(bNextCircle)
            {
                // A late successful retry still gives workers the complete
                // crossing window, while the old circle stays at its minimum.
                NextCircleStartedAt=Time;
                CircleHandoffAt=FMath::Max(Ends,Time+CircleOverlapSeconds);
                bChanged=true;
            }
        }
        if(!bNextCircle || Time<FMath::Max(Ends,CircleHandoffAt)) break;
        SafeAnchor=NextSafeAnchor; CircleStartedAt=NextCircleStartedAt;
        bNextCircle=false; CircleHandoffAt=0; NextCircleRetryAt=0; ++CircleIndex; bChanged=true;
        ZoneCrystals.RemoveAll([this](const FMCIceZoneCrystal& Crystal){return Crystal.ZoneIndex<CircleIndex;});
        ExtinguishedZones.RemoveAll([this](int32 Zone){return Zone<CircleIndex;});
    }
    if(bChanged) ForceNetUpdate();
}

void AMCIceEvent::QueueZoneCrystal()
{
    TArray<int32> Eligible;
    auto HasCrystal=[&](int32 Index){return ZoneCrystals.ContainsByPredicate([Index](const FMCIceZoneCrystal& Crystal)
        {return Crystal.ZoneIndex==Index && Crystal.Health>0;});};
    if(!HasCrystal(CircleIndex) && !ExtinguishedZones.Contains(CircleIndex)
        && Now()<CircleStartedAt+CircleDuration(CircleIndex)-FMath::Max(ZoneCrystalFallSeconds,CrystalFlightSeconds)-3) Eligible.Add(CircleIndex);
    if(bNextCircle && !HasCrystal(CircleIndex+1) && !ExtinguishedZones.Contains(CircleIndex+1)) Eligible.Add(CircleIndex+1);
    if(Eligible.IsEmpty()) return;
    const int32 Index=Eligible[Random.RandRange(0,Eligible.Num()-1)];
    FHitResult Floor;
    if(!AnchorFloor(Index==CircleIndex?SafeAnchor:NextSafeAnchor,Floor)) return;
    auto& Crystal=ZoneCrystals.AddDefaulted_GetRef();
    Crystal.ZoneIndex=Index; Crystal.Health=Crystal.MaxHealth=ZoneCrystalHealth;
    Crystal.Id=++CrystalSerial; Crystal.SpawnedAt=Now(); Crystal.ImpactAt=Crystal.SpawnedAt+ZoneCrystalFallSeconds;
    Crystal.Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    ForceNetUpdate();
}

void AMCIceEvent::ResolveZoneCrystal(FMCIceZoneCrystal& Crystal)
{
    if(!HasAuthority() || Crystal.bLanded || Crystal.Health<=0) return;
    Crystal.bLanded=true;
    FHitResult Floor;
    if(!AnchorFloor(Crystal.Anchor,Floor)) {Crystal.Health=0; ForceNetUpdate(); return;}
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const float Reach=ZoneCrystalVortexRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        if(FVector::DistSquared2D(Feet,Floor.ImpactPoint)>FMath::Square(Reach) || FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)>200) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceCrystalLanding),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        FHitResult Obstacle;
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Floor.ImpactPoint+Floor.ImpactNormal*80,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const FVector Out=(Hero->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D(UE_SMALL_NUMBER,Hero->GetActorForwardVector());
        const FVector Tangent=FVector::CrossProduct(FVector::UpVector,Out);
        const FVector Impulse=(Out+Tangent*.35f).GetSafeNormal2D()*ZoneCrystalPush+Floor.ImpactNormal*180;
        if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Impulse,Hero->GetActorLocation());
        else Hero->LaunchCharacter(Impulse,false,false);
    }
    ForceNetUpdate();
}

bool AMCIceEvent::HitObstructionWithPickaxe(AMCToothCharacter* Hero,float Damage,FVector& Point)
{
    if(!HasAuthority() || Stage!=EMCIceEventStage::Active || !IsValid(Hero) || !IsParticipant(Hero) || !Hero->CanWork()
        || !Hero->Inventory || Hero->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    if(const auto* Perks=UMCPerkComponent::ForActor(Hero)) Damage=Perks->WeaponDamage(Damage,this);
    if(Hero->Inventory->HasUpgrade(EMCToolUpgrade::Buffer))
    {
        FVector Center; float Radius;
        if(!Hero->Inventory->BufferContactSphere(Center,Radius)) return false;
        bool HitAny=false;
        for(int32 I=ZoneCrystals.Num()-1;I>=0;--I) {
            auto& Crystal=ZoneCrystals[I]; FHitResult Floor;
            if(Crystal.Health<=0 || !Crystal.bLanded || !AnchorFloor(Crystal.Anchor,Floor)) continue;
            const FVector Origin=Floor.ImpactPoint+Floor.ImpactNormal*85;
            const FVector Contact=FBox(Origin-FVector(65,65,85),Origin+FVector(65,65,85)).GetClosestPointTo(Center);
            if(!Hero->Inventory->CanBufferContact(this,Contact)) continue;
            Crystal.Health=FMath::Max(0.f,Crystal.Health-Damage);
            Point=Contact; HitAny=true;
            if(Crystal.Health<=0) {Hero->NotifyTaskFeedback(true,Point); ZoneCrystals.RemoveAt(I);}
        }
        if(HitAny) {RefreshZoneCrystals();ForceNetUpdate();}
        return HitAny;
    }
    int32 Best=INDEX_NONE; double Distance=FMath::Square(180.f);
    for(int32 I=0;I<ZoneCrystals.Num();++I)
    {
        const auto& Crystal=ZoneCrystals[I]; FHitResult Floor;
        if(Crystal.Health<=0 || !Crystal.bLanded || !AnchorFloor(Crystal.Anchor,Floor)) continue;
        const FVector Center=Floor.ImpactPoint+Floor.ImpactNormal*85;
        const FVector Contact=FBox(Center-FVector(65,65,85),Center+FVector(65,65,85)).GetClosestPointTo(Hero->GetActorLocation());
        const FVector Offset=Contact-Hero->GetActorLocation();
        if(Offset.SizeSquared()>Distance || FVector::DotProduct(Offset.GetSafeNormal2D(),Hero->GetActorForwardVector())<.25f) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceCrystalContact),false,Hero); Query.AddIgnoredActor(this);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Hero->GetActorLocation(),Contact,ECC_Visibility,Query)) continue;
        Best=I; Distance=Offset.SizeSquared(); Point=Contact;
    }
    if(Best==INDEX_NONE) return false;
    auto& Crystal=ZoneCrystals[Best]; Crystal.Health=FMath::Max(0.f,Crystal.Health-Damage);
    if(Crystal.Health<=0) {Hero->NotifyTaskFeedback(true,Point); ZoneCrystals.RemoveAt(Best);}
    RefreshZoneCrystals(); ForceNetUpdate(); return true;
}

float AMCIceEvent::FreezeAmount(const AMCToothCharacter* Hero) const
{
    for(const auto& Player:Players) if(Player.Hero==Hero) return Player.Amount;
    return 0;
}

void AMCIceEvent::UpdateFreeze(float DeltaSeconds)
{
    TArray<FMCPlayerFreeze> Updated;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive() || Hero->IsActorBeingDestroyed()
            || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        auto& Player=Updated.AddDefaulted_GetRef(); Player.Hero=Hero;
        if(Hero->IsFreezingToDeath()) {Player.Amount=1.f;Player.bSafe=false;continue;}
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        Player.bSafe=IsSafePoint(Feet);
        const bool NearBoss=IsInsideColdThrone(Feet);
        // Eternal winter makes the floor slippery; only the throne aura and
        // the second-stage global cold grow the lethal meter.
        const bool AmbientCold=!UsesColdThroneContract() || IsGlobalColdActive();
        const float Rate=Player.bSafe?-.5f/FMath::Max(1.f,ThawSeconds):
            NearBoss?BossFreezeMultiplier/FMath::Max(1.f,FreezeSeconds):
            AmbientCold?1.f/FMath::Max(1.f,FreezeSeconds):0.f;
        const auto* Perks=UMCPerkComponent::ForActor(Hero);
        const float Gain=Rate>0 && Perks?Perks->RogueValue(EMCRogueStat::FreezeRate):1.f;
        Player.Amount=FMath::Clamp(FreezeAmount(Hero)+DeltaSeconds*Rate*Gain,0.f,1.f);
        if(Player.Amount>=1)
        {
            Hero->BeginFreezeDeath();
        }
    }
    Players=MoveTemp(Updated);
    FreezeSendElapsed+=DeltaSeconds;
    if(FreezeSendElapsed>=.1f) { FreezeSendElapsed=0; PublishManualHUD(); ForceNetUpdate(); }
}

AMCToothCharacter* AMCIceEvent::ChooseTarget()
{
    TArray<AMCToothCharacter*> Candidates;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        FHitResult Floor;
        if(!IsParticipant(*It) || !It->GetController() || !It->Status || !It->Status->IsAlive()
            || It->IsFreezingToDeath() || It->IsActorBeingDestroyed() || It->SwallowedBy || It->IsMimicCaptured()) continue;
        const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(Tongue->SurfacePoint(Feet,Floor) && FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)<150) Candidates.Add(*It);
    }
    if(Candidates.IsEmpty()) return nullptr;
    if(UsesColdThroneContract())
    {
        if(auto* Director=AMCGameDirector::Find(GetWorld())) return Director->ChooseBossTarget(this,Candidates,FacingPlayer);
        return *Algo::MinElementBy(Candidates,[this](const AMCToothCharacter* Hero)
            {return FVector::DistSquaredXY(Hero->GetActorLocation(),GetActorLocation());});
    }
    return Candidates[Random.RandRange(0,Candidates.Num()-1)];
}

void AMCIceEvent::QueueIcicle(AMCToothCharacter* Target)
{
    FHitResult Floor;
    if(!IsValid(Target) || !IsParticipant(Target) || !Target->Status || !Target->Status->IsAlive() || Target->SwallowedBy || Target->IsMimicCaptured()
        || !Tongue->SurfacePoint(Target->GetActorLocation(),Floor)) return;
    auto& Strike=Strikes.AddDefaulted_GetRef();
    Strike.Anchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
    Strike.ImpactAt=Now()+IcicleWarningSeconds; Strike.Id=++StrikeSerial;
    ForceNetUpdate();
}

void AMCIceEvent::UpdateIcicleSeries(double Time)
{
    if(SeriesShotsLeft==0 && Time>=NextIcicleAt)
    {
        SeriesTarget=ChooseTarget(); SeriesShotsLeft=3; NextSeriesShotAt=Time;
        NextIcicleAt=Time+IcicleIntervalSeconds;
    }
    // At most one fresh warning per frame: a hitch must not turn a sequential
    // series into three simultaneous unavoidable strikes at the same position.
    if(SeriesShotsLeft>0 && Time>=NextSeriesShotAt)
    {
        QueueIcicle(SeriesTarget.Get()); --SeriesShotsLeft;
        NextSeriesShotAt=Time+IcicleSeriesSpacingSeconds;
    }
}

void AMCIceEvent::BeginNova()
{
    auto* Target=ChooseTarget(); FHitResult Floor;
    if(!Target || !AnchorFloor(CandyAnchor,Floor)) {NextNovaAt=Now()+NovaIntervalSeconds; return;}
    const FVector WorldDirection=(Target->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D();
    NovaDirection=Tongue->GetActorTransform().InverseTransformVectorNoScale(WorldDirection).GetSafeNormal();
    if(NovaDirection.IsNearlyZero()) NovaDirection=FVector::ForwardVector;
    bNovaWarning=true; NovaImpactAt=Now()+NovaWarningSeconds;
    NextNovaAt=NovaImpactAt+NovaIntervalSeconds; ForceNetUpdate();
}

bool AMCIceEvent::IsInsideNova(FVector WorldPoint) const
{
    FHitResult Center,Floor;
    if(!IsActive() || WorldPoint.ContainsNaN() || !AnchorFloor(CandyAnchor,Center)
        || !Tongue->SurfacePoint(WorldPoint,Floor) || FMath::Abs(WorldPoint.Z-Floor.ImpactPoint.Z)>150) return false;
    const FVector Offset=Floor.ImpactPoint-Center.ImpactPoint;
    return !IsSafePoint(WorldPoint) && Offset.SizeSquared2D()<=FMath::Square(NovaRange);
}

void AMCIceEvent::ResolveNova()
{
    if(!HasAuthority() || !bNovaWarning) return;
    bNovaWarning=false; FHitResult Center;
    if(!AnchorFloor(CandyAnchor,Center)) return;
    if(UsesColdThroneContract() || (bUseAnimatedBossAttacks && !bLabEvent)) {UpdateGroundWave(Now()); ForceNetUpdate(); return;}
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->CanWork() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(!IsInsideNova(Feet)) continue;
        if(Hero->ClearsGroundWave(12.f)) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceNovaContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center.ImpactPoint+Center.ImpactNormal*100,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        Hero->FreezeLegs(NovaFootHealth);
        if(Hero->HasFrozenLegs()) FrozenLegHeroes.AddUnique(Hero);
    }
    ForceNetUpdate();
}

float AMCIceEvent::GroundWaveRadius(double Time) const
{
    return NovaImpactAt>0 && Time>=NovaImpactAt
        ?NovaRange*FMath::Clamp(float((Time-NovaImpactAt)/FMath::Max(.5f,WaveTravelSeconds)),0.f,1.f):0.f;
}

FVector AMCIceEvent::CrystalWorldPosition(const FMCIceZoneCrystal& Crystal,const FHitResult& Floor,double Time) const
{
    const FVector End=Floor.ImpactPoint+Floor.ImpactNormal*85;
    const float Progress=Crystal.bLanded?1.f:FMath::Clamp(float((Time-Crystal.SpawnedAt)/FMath::Max(.05,Crystal.ImpactAt-Crystal.SpawnedAt)),0.f,1.f);
    if(Crystal.bLaunchedFromBoss && IsValid(Tongue))
        return FMath::Lerp(Tongue->GetActorTransform().TransformPosition(Crystal.LaunchAnchor),End,Progress)
            +FVector(0,0,FMath::Sin(Progress*PI)*ZoneCrystalFallHeight);
    return End+FVector(0,0,ZoneCrystalFallHeight*(1-Progress*Progress));
}

void AMCIceEvent::UpdateGroundWave(double Time)
{
    if(NovaImpactAt<=0 || Time<NovaImpactAt || Time>NovaImpactAt+WaveTravelSeconds+.25) return;
    FHitResult Center; if(!AnchorFloor(CandyAnchor,Center)) return;
    const float Radius=GroundWaveRadius(Time);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(WaveHitHeroes.Contains(Hero) || !IsParticipant(Hero) || !Hero->GetController() || !Hero->Status
            || !Hero->Status->IsAlive() || Hero->IsFreezingToDeath() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        FHitResult HeroFloor;
        if(!Tongue->SurfacePoint(Feet,HeroFloor) || !IsInsideNova(HeroFloor.ImpactPoint)
            || FVector::DistSquaredXY(Feet,Center.ImpactPoint)>FMath::Square(Radius)) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCandyWave),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Center.ImpactPoint+Center.ImpactNormal*60,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        WaveHitHeroes.Add(Hero);
        if(Hero->ClearsGroundWave(12.f)) continue;
        Hero->FreezeLegs(NovaFootHealth); FrozenLegHeroes.AddUnique(Hero);
    }
}

bool AMCIceEvent::BeginBossAttack(EMCCandyAttack Kind)
{
    if(UsesColdThroneContract())
    {
        if(!HasAuthority() || Stage!=EMCIceEventStage::Active || Kind==EMCCandyAttack::None
            || (Kind==EMCCandyAttack::Scream && !bSecondPhase)
            || GetAbilityCooldownRemaining(Kind)>0 || Now()<BossAttack.EndsAt) return false;
        auto* Target=ChooseTarget(); FHitResult Floor;
        if(!Target || !Tongue->SurfacePoint(Target->GetActorLocation(),Floor)) return false;
        FMCCandyAttackState Attack; Attack.Kind=Kind; Attack.Serial=BossAttack.Serial+1;
        Attack.StartedAt=Attack.AnimationAt=Now();
        Attack.TargetAnchor=Tongue->GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
        if(Kind==EMCCandyAttack::MiniCandy)
        {
            Attack.TargetRadius=IceBombRadius;
            Attack.ImpactAt=Attack.StartedAt+13.f/24.f+CrystalFlightSeconds;
            Attack.EndsAt=Attack.ImpactAt+.3;
            NextIceBombAt=Attack.StartedAt+IceBombCooldownSeconds;
        }
        else if(Kind==EMCCandyAttack::FloorHit)
        {
            Attack.TargetRadius=NovaRange;
            Attack.ImpactAt=Attack.StartedAt+.75;
            Attack.EndsAt=Attack.ImpactAt+WaveTravelSeconds;
            bNovaWarning=true; NovaImpactAt=Attack.ImpactAt; WaveHitHeroes.Reset();
            NextColdWaveAt=Attack.StartedAt+ColdWaveCooldownSeconds;
        }
        else if(Kind==EMCCandyAttack::Scream)
        {
            Attack.TargetRadius=IcePrisonRadius;
            Attack.ImpactAt=Attack.StartedAt+ScreamWarningSeconds;
            Attack.AnimationAt=Attack.ImpactAt-.5;
            Attack.EndsAt=FMath::Max(Attack.ImpactAt+.35,
                Attack.AnimationAt+(ScreamAnimation?ScreamAnimation->GetPlayLength():1.583f));
            NextIcePrisonAt=Attack.StartedAt+IcePrisonCooldownSeconds;
        }
        else return false;
        FacingPlayer=Target;
        BossAttack=Attack;
        // Separate ability cooldowns choose the next available cast. A brief
        // recovery avoids overlapping clips without adding another 5 s delay.
        NextBossAttackAt=Attack.EndsAt+.35;
        ForceNetUpdate(); return true;
    }
    FMCCandyAttackState Attack; Attack.Kind=Kind; Attack.Serial=BossAttack.Serial+1;
    Attack.StartedAt=Attack.AnimationAt=Now();
    if(Kind==EMCCandyAttack::MiniCandy)
    {
        const int32 Before=ZoneCrystals.Num(); QueueZoneCrystal();
        if(ZoneCrystals.Num()==Before) return false;
        auto& Crystal=ZoneCrystals.Last();
        Crystal.bLaunchedFromBoss=true; Crystal.SpawnedAt=Attack.StartedAt+13.f/24.f;
        Crystal.ImpactAt=Crystal.SpawnedAt+CrystalFlightSeconds;
        Crystal.LaunchAnchor=Tongue->GetActorTransform().InverseTransformPosition(GetActorLocation()+FVector(0,0,Body->Bounds.BoxExtent.Z+30));
        Attack.ImpactAt=Crystal.ImpactAt; Attack.EndsAt=Crystal.ImpactAt+.3;
        Attack.TargetAnchor=Crystal.Anchor; Attack.TargetZone=Crystal.ZoneIndex;
        NextZoneCrystalAt=Attack.StartedAt+ZoneCrystalIntervalSeconds;
    }
    else if(Kind==EMCCandyAttack::FloorHit)
    {
        Attack.ImpactAt=Attack.StartedAt+.75; Attack.EndsAt=Attack.ImpactAt+WaveTravelSeconds;
        bNovaWarning=true; NovaImpactAt=Attack.ImpactAt; WaveHitHeroes.Reset();
        NextNovaAt=Attack.StartedAt+NovaIntervalSeconds;
    }
    else if(Kind==EMCCandyAttack::Scream)
    {
        int32 BestCount=0;
        for(int32 Slot=0;Slot<(bNextCircle?2:1);++Slot)
        {
            const int32 Zone=CircleIndex+Slot;
            FHitResult Floor; const FVector Anchor=Slot?NextSafeAnchor:SafeAnchor;
            const float Radius=Slot?NextSafeRadius():SafeRadius();
            if(IsCircleBlocked(Zone) || !AnchorFloor(Anchor,Floor)) continue;
            if(!Slot && Now()+ScreamWarningSeconds>=FMath::Max(CircleStartedAt+CircleDuration(CircleIndex),CircleHandoffAt)) continue;
            int32 Count=0;
            for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            {
                if(!IsParticipant(*It) || !It->GetController() || !It->Status || !It->Status->IsAlive() || It->IsFreezingToDeath() || It->SwallowedBy || It->IsMimicCaptured()) continue;
                const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                if(FVector::DistSquaredXY(Feet,Floor.ImpactPoint)<=FMath::Square(Radius) && FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)<150) ++Count;
            }
            if(Count>BestCount) {BestCount=Count; Attack.TargetZone=Zone; Attack.TargetAnchor=Anchor; Attack.TargetRadius=Radius;}
        }
        if(BestCount==0) return false;
        Attack.ImpactAt=Attack.StartedAt+ScreamWarningSeconds;
        // The source morph first opens fully at frame 13 (animation starts at frame 1).
        Attack.AnimationAt=Attack.ImpactAt-.5;
        Attack.EndsAt=Attack.AnimationAt+(ScreamAnimation?ScreamAnimation->GetPlayLength():1.583f)+.2;
        NextScreamAt=Attack.StartedAt+ScreamCooldownSeconds;
        // Give workers an escape destination during the warning, before extinguishing.
        if(Attack.TargetZone==CircleIndex && !bNextCircle && ChooseCircle(NextSafeAnchor))
        {bNextCircle=true; NextCircleStartedAt=Now(); CircleHandoffAt=FMath::Max(CircleStartedAt+CircleDuration(CircleIndex),Now()+CircleOverlapSeconds);}
    }
    else return false;
    BossAttack=Attack;
    NextBossAttackAt=Attack.EndsAt+FMath::Max(2.f,AttackRecoverySeconds-(bSecondPhase?1.f:0.f));
    ForceNetUpdate(); return true;
}

void AMCIceEvent::ResolveIceBomb()
{
    if(!HasAuthority() || BossAttack.bResolved || BossAttack.Kind!=EMCCandyAttack::MiniCandy) return;
    BossAttack.bResolved=true;
    FHitResult Floor; if(!AnchorFloor(BossAttack.TargetAnchor,Floor)) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive()
            || Hero->IsFreezingToDeath() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::DistSquaredXY(Feet,Floor.ImpactPoint)>FMath::Square(BossAttack.TargetRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius())
            || FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)>260) continue;
        FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIceBombContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Floor.ImpactPoint+Floor.ImpactNormal*80,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const FVector Out=(Hero->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D(UE_SMALL_NUMBER,Hero->GetActorForwardVector());
        if(IceBombDamage>0 && !Hero->Status->Damage(IceBombDamage,Out,this)) continue;
        if(!Hero->Status->IsAlive()) continue;
        const FVector Impulse=Out*IceBombPush+Floor.ImpactNormal*180;
        if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Impulse,Hero->GetActorLocation());
        else Hero->LaunchCharacter(Impulse,false,false);
    }
    ForceNetUpdate();
}

bool AMCIceEvent::CanStrikePrisoner(const FMCIcicleStrike& Strike) const
{
    const auto* Hero=Strike.Prisoner.Get();
    return !Strike.bCancelled && (!Strike.bPrisonPunishment || (IsValid(Hero) && IsParticipant(Hero)
        && Hero->Status && Hero->Status->IsAlive() && Hero->HasFrozenLegs()
        && !Hero->IsFreezingToDeath() && !Hero->SwallowedBy && !Hero->IsMimicCaptured()));
}

void AMCIceEvent::CancelPrisonFor(const AMCToothCharacter* Hero)
{
    if(!HasAuthority() || !Hero) return;
    bool bChanged=false;
    for(auto& Strike:Strikes)
        if(Strike.bPrisonPunishment && Strike.Prisoner==Hero && !Strike.bImpacted && !Strike.bCancelled)
        {Strike.bCancelled=true;bChanged=true;}
    // A clear can be called while a strike is resolving. Defer removals to Tick.
    if(bChanged) ForceNetUpdate();
}

void AMCIceEvent::ResolveScream()
{
    if(BossAttack.bResolved) return;
    BossAttack.bResolved=true;
    if(UsesColdThroneContract())
    {
        if(!HasAuthority() || !bSecondPhase) return;
        FHitResult Floor; if(!AnchorFloor(BossAttack.TargetAnchor,Floor)) return;
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It;
            if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive()
                || Hero->IsFreezingToDeath() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
            const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FHitResult HeroFloor;
            if(!Tongue->SurfacePoint(Feet,HeroFloor)
                || FVector::DistSquaredXY(Feet,Floor.ImpactPoint)>FMath::Square(BossAttack.TargetRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius())
                || FMath::Abs(Feet.Z-HeroFloor.ImpactPoint.Z)>150) continue;
            FHitResult Obstacle; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIcePrisonContact),false,this);
            Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
            if(GetWorld()->LineTraceSingleByChannel(Obstacle,Floor.ImpactPoint+Floor.ImpactNormal*60,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
            const FVector Out=(Hero->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D(UE_SMALL_NUMBER,FVector::ForwardVector);
            if(IcePrisonDamage>0 && !Hero->Status->Damage(IcePrisonDamage,Out,this)) continue;
            Hero->FreezeLegs(NovaFootHealth);
            if(!Hero->HasFrozenLegs()) continue;
            FrozenLegHeroes.AddUnique(Hero);
            auto& Strike=Strikes.AddDefaulted_GetRef();
            Strike.Anchor=Tongue->GetActorTransform().InverseTransformPosition(HeroFloor.ImpactPoint);
            Strike.ImpactAt=Now()+FMath::Clamp(IcePrisonEscapeSeconds,3.f,5.f);
            Strike.Id=++StrikeSerial; Strike.Prisoner=Hero; Strike.bPrisonPunishment=true;
        }
        ForceNetUpdate(); return;
    }
    if(BossAttack.TargetZone<CircleIndex || BossAttack.TargetZone>CircleIndex+(bNextCircle?1:0)) return;
    FHitResult Floor; if(!AnchorFloor(BossAttack.TargetAnchor,Floor)) return;
    ExtinguishedZones.AddUnique(BossAttack.TargetZone);
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive()
            || Hero->IsFreezingToDeath() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::DistSquaredXY(Feet,Floor.ImpactPoint)>FMath::Square(BossAttack.TargetRadius)
            || FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)>150) continue;
        Hero->FreezeLegs(NovaFootHealth); FrozenLegHeroes.AddUnique(Hero);
    }
    ForceNetUpdate();
}

void AMCIceEvent::UpdateBossAttacks(double Time)
{
    if(UsesColdThroneContract())
    {
        UpdateColdThrone(Time);
        if(!BossAttack.bResolved && Time>=BossAttack.ImpactAt)
        {
            if(BossAttack.Kind==EMCCandyAttack::MiniCandy) ResolveIceBomb();
            else if(BossAttack.Kind==EMCCandyAttack::FloorHit)
            {BossAttack.bResolved=true; ResolveNova(); ForceNetUpdate();}
            else if(BossAttack.Kind==EMCCandyAttack::Scream) ResolveScream();
        }
        if(BossAttack.Kind==EMCCandyAttack::FloorHit) UpdateGroundWave(Time);
        if(Time<NextBossAttackAt) return;
        EMCCandyAttack Choice=EMCCandyAttack::None; double Oldest=Time+1;
        auto Ready=[&](EMCCandyAttack Kind,double Due) {if(Due<=Time && Due<Oldest){Oldest=Due;Choice=Kind;}};
        if(bSecondPhase) Ready(EMCCandyAttack::Scream,NextIcePrisonAt);
        Ready(EMCCandyAttack::FloorHit,NextColdWaveAt);
        Ready(EMCCandyAttack::MiniCandy,NextIceBombAt);
        if(Choice!=EMCCandyAttack::None && !BeginBossAttack(Choice)) NextBossAttackAt=Time+.2;
        return;
    }
    if(!bSecondPhase && CandyHealth<=MaxCandyHealth*.5f)
    {bSecondPhase=true; NextScreamAt=Time+3; ForceNetUpdate();}
    if(BossAttack.Kind==EMCCandyAttack::FloorHit)
    {
        if(!BossAttack.bResolved && Time>=BossAttack.ImpactAt)
        {BossAttack.bResolved=true; ResolveNova(); ForceNetUpdate();}
        UpdateGroundWave(Time);
    }
    if(BossAttack.Kind==EMCCandyAttack::Scream && Time>=BossAttack.ImpactAt) ResolveScream();
    if(Time<NextBossAttackAt) return;
    // Oldest ready attack goes first; one complete cast/flight/wave owns this window.
    EMCCandyAttack Choice=EMCCandyAttack::None; double Oldest=Time+1;
    auto Ready=[&](EMCCandyAttack Kind,double Due) {if(Due<=Time && Due<Oldest){Oldest=Due; Choice=Kind;}};
    if(bSecondPhase) Ready(EMCCandyAttack::Scream,NextScreamAt);
    Ready(EMCCandyAttack::FloorHit,NextNovaAt); Ready(EMCCandyAttack::MiniCandy,NextZoneCrystalAt);
    if(Choice!=EMCCandyAttack::None && !BeginBossAttack(Choice))
    {
        if(Choice==EMCCandyAttack::Scream) NextScreamAt=Time+2;
        if(Choice==EMCCandyAttack::MiniCandy) NextZoneCrystalAt=Time+2;
        NextBossAttackAt=Time+.2;
    }
}

void AMCIceEvent::PresentCandyAnimation(double Time)
{
    if(!CandyVisual || !CandyVisual->GetSkeletalMeshAsset() || !IdleAnimation) return;
    UAnimSequence* Clip=IdleAnimation; double Start=StartedAt; bool bLoop=true;
    if(Time>=BossAttack.AnimationAt && Time<BossAttack.EndsAt)
    {
        UAnimSequence* AttackClip=BossAttack.Kind==EMCCandyAttack::MiniCandy?MiniCandyAnimation:
            BossAttack.Kind==EMCCandyAttack::FloorHit?FloorHitAnimation:BossAttack.Kind==EMCCandyAttack::Scream?ScreamAnimation:nullptr;
        if(AttackClip && Time<BossAttack.AnimationAt+AttackClip->GetPlayLength())
        {Clip=AttackClip; Start=BossAttack.AnimationAt; bLoop=false;}
    }
    auto* Instance=CandyVisual->GetSingleNodeInstance();
    if(!Instance || Instance->GetCurrentAsset()!=Clip) {CandyVisual->PlayAnimation(Clip,bLoop); Instance=CandyVisual->GetSingleNodeInstance();}
    if(Instance)
    {Instance->SetPlaying(false); Instance->SetPosition(bLoop?FMath::Fmod(FMath::Max(0.f,float(Time-Start)),Clip->GetPlayLength()):FMath::Clamp(float(Time-Start),0.f,Clip->GetPlayLength()),false);}
}

void AMCIceEvent::ResolveIcicle(FMCIcicleStrike& Strike)
{
    if(!HasAuthority() || Strike.bImpacted) return;
    if(!CanStrikePrisoner(Strike)) {Strike.bCancelled=true;ForceNetUpdate();return;}
    Strike.bImpacted=true;
    FHitResult Floor;
    if(!AnchorFloor(Strike.Anchor,Floor)) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        auto* Hero=*It;
        if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const float Reach=IcicleRadius+Hero->GetCapsuleComponent()->GetScaledCapsuleRadius();
        if(FVector::DistSquaredXY(Feet,Floor.ImpactPoint)>FMath::Square(Reach) || FMath::Abs(Feet.Z-Floor.ImpactPoint.Z)>260) continue;
        FHitResult Obstacle;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCIcicleContact),false,this);
        Query.AddIgnoredActor(Tongue); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Obstacle,Floor.ImpactPoint+Floor.ImpactNormal*45,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        FVector Direction=(Hero->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D();
        if(Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
        if(Hero->Status->Damage(IcicleDamage,Direction,this) && Hero->ToothPhysics)
            Hero->ToothPhysics->ApplyHit(Direction*220+Floor.ImpactNormal*90,Hero->GetActorLocation());
    }
    ForceNetUpdate();
}

FVector AMCIceEvent::CandyContactPoint(FVector From) const
{
    FVector Point;
    if(Body->GetClosestPointOnCollision(From,Point)>=0) return Point;
    return Body->Bounds.GetBox().GetClosestPointTo(From);
}

bool AMCIceEvent::HasAuthoredCandy() const
{
    return Body && Body->GetStaticMesh() && Body->GetStaticMesh()!=ConeMesh;
}

bool AMCIceEvent::HitWithPickaxe(AMCToothCharacter* Hero,float Damage)
{
    if(!HasAuthority() || Stage!=EMCIceEventStage::Active || !IsValid(Hero) || !IsParticipant(Hero) || !Hero->CanWork()
        || !Hero->Inventory || Hero->Inventory->Selected!=EMCToolSlot::Pickaxe || !FMath::IsFinite(Damage) || Damage<=0) return false;
    if(Hero->Inventory->HasUpgrade(EMCToolUpgrade::Buffer))
    {
        FVector Center; float Radius;
        if(!Hero->Inventory->BufferContactSphere(Center,Radius) || !Hero->Inventory->CanBufferContact(this,CandyContactPoint(Center))) return false;
    }
    else if(!MCEnemyWeaponContact::CanHit(Hero,this,CandyContactPoint(Hero->GetActorLocation()))) return false;
    auto* Perks=UMCPerkComponent::ForActor(Hero);
    if(Perks) Damage=Perks->WeaponDamage(Damage,this);
    CandyHealth=FMath::Max(0.f,CandyHealth-Damage); ForceNetUpdate();
    if(Perks) Perks->NotifyWeaponHit(this);
    if(CandyHealth<=0)
    {
        Hero->NotifyTaskFeedback(true,GetActorLocation());
        if(auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>()) Mode->AwardTask(Hero,EMCScoreTask::Ice);
        Finish();
    }
    return true;
}

void AMCIceEvent::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!IsActive()) { OnRep_State(); return; }
    if(HasAuthority())
    {
        const auto* State=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()
            || (State && (State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost))) { Finish(true); return; }
        const double Time=Now();
        if(Stage==EMCIceEventStage::Arrival && Time>=StartedAt+ArrivalSeconds) { Stage=EMCIceEventStage::Active; OnRep_State(); ForceNetUpdate(); }
        if(Stage==EMCIceEventStage::Active)
        {
            if(CandyHealth<=0) { Finish(); return; }
            UpdateCircles(Time);
            if(!IsActive()) return;
            const bool bAnimated=UsesColdThroneContract() || (bUseAnimatedBossAttacks && !bLabEvent);
            if(bAnimated) UpdateBossAttacks(Time);
            else if(Time>=NextZoneCrystalAt) {QueueZoneCrystal(); NextZoneCrystalAt=Time+ZoneCrystalIntervalSeconds;}
            for(auto& Crystal:ZoneCrystals) if(!Crystal.bLanded && Time>=Crystal.ImpactAt) ResolveZoneCrystal(Crystal);
            if(!bAnimated && bNovaWarning && Time>=NovaImpactAt) ResolveNova();
            else if(!bAnimated && !bNovaWarning && Time>=NextNovaAt) BeginNova();
            UpdateFreeze(DeltaSeconds);
            if(!bAnimated) UpdateIcicleSeries(Time);
            Strikes.RemoveAll([this](const FMCIcicleStrike& Strike){return Strike.bCancelled || (!Strike.bImpacted && !CanStrikePrisoner(Strike));});
            for(auto& Strike:Strikes) if(!Strike.bImpacted && !Strike.bCancelled && Time>=Strike.ImpactAt) ResolveIcicle(Strike);
            Strikes.RemoveAll([Time](const FMCIcicleStrike& Strike){ return Strike.bCancelled || Time>Strike.ImpactAt+1; });
        }
    }
    RefreshPresentation(DeltaSeconds);
}

void AMCIceEvent::BuildCandyFace()
{
    FIceGuideMesh Mesh;
    constexpr int32 Segments=64;
    for(int32 Side=0;Side<2;++Side)
    {
        const float Sign=Side?1.f:-1.f;
        for(int32 I=0;I<Segments;++I)
        {
            const float A=I*2*PI/Segments,B=(I+1)*2*PI/Segments;
            const FLinearColor Color=(I/4)%2?FLinearColor(.16f,.86f,.63f):FLinearColor(.97f,1.f,.98f);
            const int32 First=Mesh.Vertices.Num();
            Mesh.Vertices.Append({FVector(0,0,Sign*50.2f),FVector(FMath::Cos(A)*50,FMath::Sin(A)*50,Sign*50.2f),FVector(FMath::Cos(B)*50,FMath::Sin(B)*50,Sign*50.2f)});
            for(int32 J=0;J<3;++J)
            { Mesh.Normals.Add(FVector(0,0,Sign)); Mesh.UV.Add(FVector2D::ZeroVector); Mesh.Colors.Add(Color); Mesh.Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false)); }
            if(Side) Mesh.Indices.Append({First,First+2,First+1}); else Mesh.Indices.Append({First,First+1,First+2});
        }
    }
    CandyFace->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshFloorGuides()
{
    if(!IsActive()) { FloorGuides->ClearAllMeshSections(); return; }
    FIceGuideMesh Mesh; FHitResult Floor;
    const FTransform Transform=FloorGuides->GetComponentTransform();
    const double Time=Now();
    const float WarmPulse=.88f+.12f*FMath::Sin(float(Time)*2.4f);
    for(const auto& Patch:BrushedWarmPatches) {
        const float Radius=BrushedWarmRadius(Patch);
        if(Radius>1 && AnchorFloor(Patch.Anchor,Floor))
            Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,Radius,5,FLinearColor(1.f,.67f,.3f)*WarmPulse);
    }
    if(AnchorFloor(SafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,SafeRadius(),7,
            (IsCircleBlocked(CircleIndex)?FLinearColor(.12f,.5f,1.f):FLinearColor(1.f,.58f,.17f))*WarmPulse,
            IsCircleBlocked(CircleIndex));
    if(bNextCircle && AnchorFloor(NextSafeAnchor,Floor))
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,NextSafeRadius(),7,
            (IsCircleBlocked(CircleIndex+1)?FLinearColor(.12f,.5f,1.f):FLinearColor(1.f,.74f,.27f))*WarmPulse,
            IsCircleBlocked(CircleIndex+1));
    for(const auto& Strike:Strikes)
    {
        if(Strike.bCancelled || (!Strike.bImpacted && !CanStrikePrisoner(Strike))
            || !AnchorFloor(Strike.Anchor,Floor) || Time>Strike.ImpactAt+1) continue;
        const float Warning=Strike.bPrisonPunishment?IcePrisonEscapeSeconds:IcicleWarningSeconds;
        const float Progress=FMath::Clamp(1.f-float((Strike.ImpactAt-Time)/FMath::Max(1.f,Warning)),0.f,1.f);
        const float Pulse=.82f+.18f*FMath::Sin(Progress*PI*(4+Progress*6));
        const FLinearColor Color=Strike.bImpacted?FLinearColor(.25f,.8f,1.f):FLinearColor(1.f,.12f+.18f*Progress,.08f)*Pulse;
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,IcicleRadius,6+2*Progress,Color,!Strike.bImpacted);
        if(!Strike.bImpacted) Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(8.f,IcicleRadius*Progress),3,Color);
    }
    for(const auto& Crystal:ZoneCrystals) {
        if(Crystal.Health<=0 || Crystal.bLanded || !AnchorFloor(Crystal.Anchor,Floor)) continue;
        const float Fall=FMath::Clamp(float((Time-Crystal.SpawnedAt)/FMath::Max(.01,Crystal.ImpactAt-Crystal.SpawnedAt)),0.f,1.f);
        const FLinearColor Warning=FLinearColor(1.f,.15f,.12f)*(.8f+.2f*FMath::Sin(Fall*PI*10));
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,ZoneCrystalVortexRadius,6,Warning,true);
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(10.f,ZoneCrystalVortexRadius*Fall),3,Warning);
    }
    if(Stage==EMCIceEventStage::Active && AnchorFloor(CandyAnchor,Floor))
    {
        const FVector Center=Floor.ImpactPoint;
        Mesh.Ring(Tongue,Transform,Center,BossFrostRadius,3,FLinearColor(.2f,.7f,1.f),true);
        if(bNovaWarning || (NovaImpactAt>0 && Time<NovaImpactAt+BossVortexSeconds)) {
            const FLinearColor Color=bNovaWarning?FLinearColor(.35f,.8f,1.f):FLinearColor(.85f,.97f,1.f);
            Mesh.Ring(Tongue,Transform,Center,NovaRange,6,Color,bNovaWarning);
            if(bNovaWarning) {
                const float Progress=FMath::Clamp(1.f-float((NovaImpactAt-Time)/NovaWarningSeconds),0.f,1.f);
                Mesh.Ring(Tongue,Transform,Center,FMath::Max(MintRadius,NovaRange*Progress),4,Color);
            }
        }
    }
    if((UsesColdThroneContract() || bUseAnimatedBossAttacks) && BossAttack.Kind==EMCCandyAttack::FloorHit && Time>=NovaImpactAt && Time<=NovaImpactAt+WaveTravelSeconds)
    {
        if(AnchorFloor(CandyAnchor,Floor))
            Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(8.f,GroundWaveRadius(Time)),28,FLinearColor(.65f,.93f,1.f));
    }
    if(UsesColdThroneContract() && BossAttack.Kind==EMCCandyAttack::MiniCandy && Time<BossAttack.ImpactAt
        && AnchorFloor(BossAttack.TargetAnchor,Floor))
    {
        const float Progress=FMath::Clamp(float((Time-BossAttack.StartedAt)/FMath::Max(.1,BossAttack.ImpactAt-BossAttack.StartedAt)),0.f,1.f);
        const FLinearColor Color=FLinearColor(1.f,.2f,.08f)*(.85f+.15f*FMath::Sin(float(Time)*12));
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,BossAttack.TargetRadius,9,Color,true);
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(5.f,BossAttack.TargetRadius*Progress),4,Color);
    }
    if(BossAttack.Kind==EMCCandyAttack::Scream && Time<BossAttack.EndsAt
        && (!UsesColdThroneContract() || Time<BossAttack.ImpactAt) && AnchorFloor(BossAttack.TargetAnchor,Floor))
    {
        const bool bWarning=Time<BossAttack.ImpactAt;
        const float Progress=FMath::Clamp(float((Time-BossAttack.StartedAt)/FMath::Max(.1,BossAttack.ImpactAt-BossAttack.StartedAt)),0.f,1.f);
        const FLinearColor Color=bWarning?FLinearColor(1,.15f,.08f):FLinearColor(.35f,.8f,1);
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,BossAttack.TargetRadius,12,Color);
        Mesh.Ring(Tongue,Transform,Floor.ImpactPoint,FMath::Max(1.f,BossAttack.TargetRadius*Progress*.5f),BossAttack.TargetRadius*Progress*.5f,Color*.45f);
    }
    FloorGuides->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshZoneCrystals()
{
    const double Time=Now();
    while(ZoneCrystalMeshes.Num()<ZoneCrystals.Num())
    {
        auto* Part=NewObject<UStaticMeshComponent>(this); Part->SetupAttachment(Body);
        Part->SetStaticMesh(ConeMesh); Part->SetMaterial(0,IcicleMaterial);
        Part->SetCollisionProfileName(TEXT("NoCollision"));
        Part->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part->SetGenerateOverlapEvents(false); Part->SetCastShadow(true);
        Part->RegisterComponent(); AddInstanceComponent(Part); ZoneCrystalMeshes.Add(Part);
    }
    for(int32 I=0;I<ZoneCrystalMeshes.Num();++I)
    {
        FHitResult Floor; auto* Part=ZoneCrystalMeshes[I].Get();
        const bool bVisible=IsActive() && ZoneCrystals.IsValidIndex(I) && ZoneCrystals[I].Health>0
            && (!ZoneCrystals[I].bLaunchedFromBoss || Time>=ZoneCrystals[I].SpawnedAt) && AnchorFloor(ZoneCrystals[I].Anchor,Floor);
        Part->SetVisibility(bVisible && !(IsValid(EventVFX) && EventVFX->HasCrystalVisuals()));
        Part->SetCollisionEnabled(bVisible && ZoneCrystals[I].bLanded?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        if(!bVisible) continue;
        const auto& Crystal=ZoneCrystals[I];
        const float Progress=Crystal.bLanded?1.f:FMath::Clamp(float((Time-Crystal.SpawnedAt)/FMath::Max(.05,Crystal.ImpactAt-Crystal.SpawnedAt)),0.f,1.f);
        const FVector Position=CrystalWorldPosition(Crystal,Floor,Time);
        const FQuat Alignment=(Floor.ImpactNormal.Rotation()+FRotator(-90,0,0)).Quaternion();
        Part->SetWorldLocationAndRotation(Position,FQuat(Floor.ImpactNormal,Progress*2*PI)*Alignment);
        Part->SetWorldScale3D(FVector(1.6f,1.6f,2.1f));
    }
}

void AMCIceEvent::RefreshNovaWind()
{
    if(IsValid(EventVFX) && EventVFX->HasWindVisuals())
    {
        if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections();
        return;
    }
    const double Time=Now(); FHitResult Floor;
    if(!IsActive() || !AnchorFloor(CandyAnchor,Floor))
    {if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections(); return;}
    FIceGuideMesh Mesh;
    const FTransform Transform=NovaWind->GetComponentTransform();
    const FVector Center=Floor.ImpactPoint;
    const bool bBlast=NovaImpactAt>0 && Time>=NovaImpactAt && Time<NovaImpactAt+BossVortexSeconds;
    const bool bWarning=bNovaWarning && Time<NovaImpactAt;
    if(Stage!=EMCIceEventStage::Active && !bWarning && !bBlast)
    {if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections(); return;}
    const float Age=float(Time-StartedAt);
    const float Radius=bBlast?NovaRange:BossFrostRadius;
    for(int32 Ribbon=0;Ribbon<36;++Ribbon)
    {
        const float Angle=(Ribbon%12)*2*PI/12+Age*(bBlast?2.f:1.f);
        const FVector Ray(FMath::Cos(Angle),FMath::Sin(Angle),0),Tangent(-Ray.Y,Ray.X,0);
        const float Travel=FMath::Frac(Age*.45f+Ribbon*.618f);
        const float Distance=Radius*(.35f+.65f*Travel);
        const FVector A=Center+Ray*Distance,B=A+Tangent*(bBlast?260.f:140.f);
        Mesh.Strip(Tongue,Transform,A,B,bBlast?13.f:bWarning?8.f:5.f,
            FLinearColor(.65f+.3f*Travel,.87f+.1f*Travel,1.f),35+(Ribbon/12)*75);
    }
    if(Mesh.Indices.IsEmpty()) {if(NovaWind->GetNumSections()>0) NovaWind->ClearAllMeshSections();}
    else NovaWind->CreateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshFreezeBars()
{
    for(int32 I=FreezeBars.Num()-1;I>=0;--I)
    {
        const auto* Hero=BarHeroes[I].Get();
        if(!Hero || !Hero->Status || !Hero->Status->IsAlive() || !Players.ContainsByPredicate([Hero](const FMCPlayerFreeze& Player){return Player.Hero==Hero;}))
        { if(IsValid(FreezeBars[I])) FreezeBars[I]->DestroyComponent(); FreezeBars.RemoveAt(I); BarHeroes.RemoveAt(I); }
    }
    for(const auto& Player:Players)
    {
        auto* Hero=Player.Hero.Get();
        if(!IsValid(Hero) || !Hero->Status || !Hero->Status->IsAlive()) continue;
        int32 Index=BarHeroes.IndexOfByPredicate([Hero](const TWeakObjectPtr<AMCToothCharacter>& Item){return Item.Get()==Hero;});
        const FVector Feet=Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const bool bShow=!UsesColdThroneContract() || Player.Amount>.002f || IsGlobalColdActive() || IsInsideColdThrone(Feet);
        if(Index==INDEX_NONE && !bShow) continue;
        if(Index==INDEX_NONE)
        {
            auto* Bar=NewObject<UWidgetComponent>(this);
            Bar->SetupAttachment(Hero->GetRootComponent()); Bar->SetWidgetSpace(EWidgetSpace::Screen);
            Bar->SetDrawSize(FVector2D(240,64)); Bar->SetDrawAtDesiredSize(true); Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Bar->SetPivot(FVector2D(.5f,1.f));
            Bar->SetRelativeLocation(FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+170));
            Bar->SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("FreezeBar"),UMCFreezeBarWidget::StaticClass())); Bar->RegisterComponent(); AddInstanceComponent(Bar);
            Index=FreezeBars.Add(Bar); BarHeroes.Add(Hero); Bar->InitWidget();
        }
        FreezeBars[Index]->SetVisibility(bShow);
        if(auto* Widget=Cast<UMCFreezeBarWidget>(FreezeBars[Index]->GetUserWidgetObject())) Widget->SetFreeze(Player.Amount,Player.bSafe);
    }
}

void AMCIceEvent::RefreshIceCoatings()
{
    // This is local presentation of the existing replicated freeze meter.
    // A leader-pose shell shares the final animated/ragdoll pose and leaves all
    // body materials (including the player's dirt and face) in place.
    for(int32 I=IceCoatings.Num()-1;I>=0;--I)
    {
        auto* Part=IceCoatings[I].Get();
        auto* Source=IsValid(Part)?Cast<USkeletalMeshComponent>(Part->GetAttachParent()):nullptr;
        auto* Hero=Source?Cast<AMCToothCharacter>(Source->GetOwner()):nullptr;
        if(!IsValid(Hero) || Hero->IsActorBeingDestroyed() || Hero->IsFreezingToDeath() || !Hero->Status || !Hero->Status->IsAlive()
            || Source!=Hero->GetMesh() || !Source->GetSkeletalMeshAsset() || FreezeAmount(Hero)<=.002f)
        {
            if(IsValid(Hero)) RemoveTickPrerequisiteActor(Hero);
            if(IsValid(Part)) Part->DestroyComponent();
            IceCoatings.RemoveAt(I);
        }
    }
    if(!PlayerIceMaterial) return;
    for(const auto& Player:Players)
    {
        auto* Hero=Player.Hero.Get();
        if(!IsValid(Hero) || Hero->IsActorBeingDestroyed() || Hero->IsFreezingToDeath() || !Hero->Status || !Hero->Status->IsAlive()
            || !FMath::IsFinite(Player.Amount) || Player.Amount<=.002f) continue;
        auto* Source=Hero->GetMesh();
        if(!Source || !Source->GetSkeletalMeshAsset()) continue;
        auto* Found=IceCoatings.FindByPredicate([Source](const TObjectPtr<USkeletalMeshComponent>& Part)
            { return IsValid(Part.Get()) && Part->GetAttachParent()==Source; });
        USkeletalMeshComponent* Part=Found?Found->Get():nullptr;
        if(!Part)
        {
            Part=NewObject<USkeletalMeshComponent>(this);
            Part->ComponentTags.Add(TEXT("MC_PlayerIceCoating"));
            Part->SetupAttachment(Source);
            Part->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
            Part->SetLeaderPoseComponent(Source,true);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetGenerateOverlapEvents(false); Part->SetCastShadow(false);
            Part->bReceivesDecals=false;
            Part->PrimaryComponentTick.bCanEverTick=false;
            AddTickPrerequisiteActor(Hero);
            Part->RegisterComponent(); AddInstanceComponent(Part); IceCoatings.Add(Part);
        }
        if(Part->GetSkeletalMeshAsset()!=Source->GetSkeletalMeshAsset())
            Part->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
        Part->SetVisibility(Source->IsVisible() && !Source->bHiddenInGame && !Hero->IsHidden());
        for(int32 Slot=0;Slot<Source->GetNumMaterials();++Slot)
        {
            auto* Material=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(Slot));
            if(!Material || !Material->IsChildOf(PlayerIceMaterial))
            {
                Material=UMaterialInstanceDynamic::Create(PlayerIceMaterial,Part);
                Part->SetMaterial(Slot,Material);
            }
            Material->SetScalarParameterValue(TEXT("IceAmount"),FMath::Clamp(Player.Amount,0.f,1.f));
            // Leader pose already carries bones and morphs. Shader deformation
            // must also match the body, after its locomotion update this frame.
            static const FName ShapeParameters[]={TEXT("BodyStretch"),TEXT("Damage"),TEXT("DamageChipDepth")};
            const auto* SourceMaterial=Source->GetMaterial(Slot);
            for(FName Parameter:ShapeParameters)
            {
                float Value=0.f;
                if(SourceMaterial) SourceMaterial->GetScalarParameterValue(FMaterialParameterInfo(Parameter),Value);
                Material->SetScalarParameterValue(Parameter,Value);
            }
        }
    }
}

void AMCIceEvent::RefreshFrostSurface()
{
    if(!IsActive() || !IsValid(Tongue)) { FrostSurface->ClearAllMeshSections(); return; }
    const FProcMeshSection* SourceSection=Tongue->Surface->GetProcMeshSection(0);
    const auto& SourceIndices=Tongue->TriangleIndices();
    if(!SourceSection || SourceSection->ProcVertexBuffer.IsEmpty() || SourceIndices.IsEmpty())
    { FrostSurface->ClearAllMeshSections(); return; }
    AddTickPrerequisiteActor(Tongue);

    auto* Material=Cast<UMaterialInstanceDynamic>(FrostSurface->GetMaterial(0));
    if(!Material)
    {
        // Loading also supports existing editor instances after Live Coding;
        // the constructor holds the saved material reference for cooked games.
        auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/Cold/Frost/M_TongueFrost.M_TongueFrost"));
        if(!Parent) return;
        Material=FrostSurface->CreateDynamicMaterialInstance(0,Parent);
    }
    if(!Material) return;
    FHitResult SafeFloor,NextFloor,FrostFloor;
    const bool bSafe=!IsCircleBlocked(CircleIndex) && AnchorFloor(SafeAnchor,SafeFloor);
    const bool bNext=bNextCircle && !IsCircleBlocked(CircleIndex+1) && AnchorFloor(NextSafeAnchor,NextFloor);
    // Between global-cold cycles, slick ice remains and only the local aura
    // carries white frost. The sweep expands and retracts independently.
    const float TransitionAge=PhaseTransitionAge();
    const float ReturnAge=PhaseReturnAge();
    const bool bSweep=(TransitionAge>=0 && PhaseTransitionProgress()<1.f)
        || (ReturnAge>=0 && ReturnAge<PhaseTransitionClimateSeconds+PhaseTransitionSweepSeconds);
    const bool bLocalFrost=UsesColdThroneContract() && (!IsGlobalColdActive() || bSweep) && AnchorFloor(CandyAnchor,FrostFloor);
    Material->SetScalarParameterValue(TEXT("Frost Zone Enabled"),bLocalFrost?1.f:0.f);
    Material->SetVectorParameterValue(TEXT("Frost Zone Center"),FLinearColor(bLocalFrost?FrostFloor.ImpactPoint:FVector::ZeroVector));
    Material->SetScalarParameterValue(TEXT("Frost Zone Radius"),bSweep?PhaseTransitionRadius():BossFrostRadius);
    Material->SetScalarParameterValue(TEXT("Frost Zone Height"),180.f);
    Material->SetScalarParameterValue(TEXT("Frost Sweep"),bSweep?1.f:0.f);
    Material->SetScalarParameterValue(TEXT("Frost Aura Radius"),BossFrostRadius);
    Material->SetScalarParameterValue(TEXT("IceAmount"),FrostAmount());
    Material->SetScalarParameterValue(TEXT("Warm Enabled"),bSafe?1.f:0.f);
    Material->SetScalarParameterValue(TEXT("Warm Radius"),SafeRadius());
    Material->SetVectorParameterValue(TEXT("Warm Center"),FLinearColor(bSafe?SafeFloor.ImpactPoint:FVector::ZeroVector));
    Material->SetScalarParameterValue(TEXT("Next Warm Enabled"),bNext?1.f:0.f);
    Material->SetScalarParameterValue(TEXT("Next Warm Radius"),NextSafeRadius());
    Material->SetVectorParameterValue(TEXT("Next Warm Center"),FLinearColor(bNext?NextFloor.ImpactPoint:FVector::ZeroVector));

    // Copy the current surface pose, normals and authored UVs so the frost
    // follows breathing/pressure without sliding across the tongue.
    // The material fades warmth per pixel; triangle-sized cutouts are unnecessary.
    FIceGuideMesh Mesh;
    const int32 Count=SourceSection->ProcVertexBuffer.Num();
    Mesh.Vertices.Reserve(Count); Mesh.Normals.Reserve(Count);
    Mesh.UV.Reserve(Count); Mesh.Colors.Reserve(Count); Mesh.Tangents.Reserve(Count);
    const FTransform Transform=FrostSurface->GetComponentTransform();
    const FTransform TongueTransform=Tongue->Surface->GetComponentTransform();
    struct FLocalBrushPatch { FVector Center;float Radius; };
    TArray<FLocalBrushPatch,TInlineAllocator<32>> BrushPatches;
    for(const auto& Patch:BrushedWarmPatches) {
        const float Radius=BrushedWarmRadius(Patch);
        if(Radius>1) BrushPatches.Add({Tongue->GetActorTransform().TransformPosition(Patch.Anchor),Radius});
    }
    for(const FProcMeshVertex& Vertex:SourceSection->ProcVertexBuffer)
    {
        const FVector Normal=TongueTransform.TransformVectorNoScale(Vertex.Normal).GetSafeNormal();
        const FVector Point=TongueTransform.TransformPosition(Vertex.Position)+Normal*2.5f;
        Mesh.Vertices.Add(Transform.InverseTransformPosition(Point));
        Mesh.Normals.Add(Transform.InverseTransformVectorNoScale(Normal).GetSafeNormal());
        float FrostCoverage=1;
        for(const auto& Patch:BrushPatches)
            FrostCoverage=FMath::Min(FrostCoverage,FMath::SmoothStep(Patch.Radius*.7f,Patch.Radius,float(FVector::Dist2D(Point,Patch.Center))));
        Mesh.UV.Add(Vertex.UV0); Mesh.Colors.Add(FLinearColor(FrostCoverage,FrostCoverage,FrostCoverage,FrostCoverage));
        const FVector Tangent=TongueTransform.TransformVectorNoScale(Vertex.Tangent.TangentX);
        Mesh.Tangents.Add(FProcMeshTangent(Transform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(),Vertex.Tangent.bFlipTangentY));
    }
    const FProcMeshSection* Existing=FrostSurface->GetProcMeshSection(0);
    if(Existing && Existing->ProcVertexBuffer.Num()==Count && Existing->ProcIndexBuffer.Num()==SourceIndices.Num())
        FrostSurface->UpdateMeshSection_LinearColor(0,Mesh.Vertices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents);
    else
        FrostSurface->CreateMeshSection_LinearColor(0,Mesh.Vertices,SourceIndices,Mesh.Normals,Mesh.UV,Mesh.Colors,Mesh.Tangents,false);
}

void AMCIceEvent::RefreshPresentation(float DeltaSeconds)
{
    SetClimate();
    FHitResult Floor;
    const bool bCandyFloor=AnchorFloor(CandyAnchor,Floor);
    if(bCandyFloor)
    {
        const float Arrival=FMath::Clamp(float((Now()-StartedAt)/FMath::Max(1.f,ArrivalSeconds)),0.f,1.f);
        float HalfHeight=240;
        FVector CandyScale(MintRadius/50,MintRadius/50,4.8f);
        if(HasAuthoredCandy())
        {
            const FVector Extent=Body->GetStaticMesh()->GetBounds().BoxExtent;
            // The saved artist mesh already has its authored size in centimetres.
            CandyScale=FVector::OneVector;
            HalfHeight=Extent.Z;
        }
        const float Height=HalfHeight+FMath::Square(1-Arrival)*600;
        if(HasAuthority())
        {
            AMCToothCharacter* Nearest=nullptr;
            double NearestDistance=TNumericLimits<double>::Max();
            for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
            {
                auto* Hero=*It;
                if(!IsParticipant(Hero) || !Hero->GetController() || !Hero->Status || !Hero->Status->IsAlive()
                    || Hero->IsActorBeingDestroyed() || Hero->IsFreezingToDeath() || Hero->SwallowedBy || Hero->IsMimicCaptured()) continue;
                const double Distance=FVector::DistSquaredXY(Hero->GetActorLocation(),Floor.ImpactPoint);
                if(Distance<NearestDistance) {Nearest=Hero; NearestDistance=Distance;}
            }
            if(FacingPlayer!=Nearest) {FacingPlayer=Nearest; ForceNetUpdate();}
        }
        FRotator Rotation=Floor.ImpactNormal.Rotation()+FRotator(-90,0,0);
        FVector Direction=IsValid(FacingPlayer)?(FacingPlayer->GetActorLocation()-Floor.ImpactPoint).GetSafeNormal2D():GetActorForwardVector();
        // Only the current cast owns its aim. Idle and arrival face living players.
        if(Stage==EMCIceEventStage::Active && (BossAttack.TargetZone!=INDEX_NONE || (UsesColdThroneContract() && BossAttack.TargetRadius>0))
            && Now()<BossAttack.EndsAt && IsValid(Tongue))
        {
            Direction=(Tongue->GetActorTransform().TransformPosition(BossAttack.TargetAnchor)-Floor.ImpactPoint).GetSafeNormal2D();
        }
        if(!Direction.IsNearlyZero()) Rotation=FRotationMatrix::MakeFromXZ(Direction,Floor.ImpactNormal).Rotator();
        if(Stage==EMCIceEventStage::Active) Rotation=FMath::RInterpTo(GetActorRotation(),Rotation,DeltaSeconds,5.f);
        SetActorLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,Rotation);
        SetActorScale3D(CandyScale);
    }
    Body->SetCollisionEnabled(bCandyFloor && Stage==EMCIceEventStage::Active?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    RefreshZoneCrystals();
    if(GetNetMode()==NM_DedicatedServer) return;
    const bool bVisible=IsActive(); Body->SetVisibility(bVisible); CandyFace->SetVisibility(false);
    const bool bAnimatedVisual=bVisible && CandyVisual && CandyVisual->GetSkeletalMeshAsset();
    CandyVisual->SetVisibility(bAnimatedVisual);
    if(bAnimatedVisual) PresentCandyAnimation(Now());
    if(bVisible)
    {
        EnsurePresentationVFX();
        if(IsValid(EventVFX)) EventVFX->UpdateFromEvent(*this,DeltaSeconds,Now());
        Body->SetVisibility(!bAnimatedVisual && (HasAuthoredCandy() || !(IsValid(EventVFX) && EventVFX->HasCrystalVisuals())));
    }
    RefreshFrostSurface();
    GuideElapsed+=DeltaSeconds;
    if(GuideElapsed>=.1f) { GuideElapsed=0; RefreshFloorGuides(); }
    while(IcicleMeshes.Num()<Strikes.Num())
    {
        auto* Part=NewObject<UStaticMeshComponent>(this); Part->SetupAttachment(Body);
        Part->SetStaticMesh(ConeMesh); Part->SetMaterial(0,IcicleMaterial); Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCastShadow(false); Part->RegisterComponent(); AddInstanceComponent(Part); IcicleMeshes.Add(Part);
    }
    const double Time=Now();
    for(int32 I=0;I<IcicleMeshes.Num();++I)
    {
        const bool bShow=Strikes.IsValidIndex(I) && !Strikes[I].bCancelled
            && (Strikes[I].bImpacted || CanStrikePrisoner(Strikes[I])) && AnchorFloor(Strikes[I].Anchor,Floor)
            && Time>=Strikes[I].ImpactAt-.55 && Time<Strikes[I].ImpactAt+.18f;
        UStaticMeshComponent* Part=IcicleMeshes[I]; Part->SetVisibility(bShow);
        if(!bShow) continue;
        const float Fall=FMath::Clamp(1-float((Strikes[I].ImpactAt-Time)/.45),0.f,1.f);
        const float Height=220+(1-Fall*Fall)*620;
        Part->SetWorldLocationAndRotation(Floor.ImpactPoint+Floor.ImpactNormal*Height,FRotator(180,0,0));
        Part->SetWorldScale3D(FVector(1.1f,1.1f,4.4f));
    }
    RefreshFreezeBars();
    RefreshIceCoatings();
    RefreshNovaWind();
}

void AMCIceEvent::SetClimate()
{
    if(bLabEvent) return;
    if(GetNetMode()==NM_DedicatedServer || !Climate) return;
    // Phase one retains the tongue's slick ice but reserves dense frost for
    // its local aura. Cola and phase two keep their existing global climate.
    float Amount=IsActorBeingDestroyed()?0:ClimateContribution();
    for(TActorIterator<AMCColdColaEvent> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed()) Amount=FMath::Max(Amount,It->FrostAmount());
    for(TActorIterator<AMCIceEvent> It(GetWorld());It;++It)
        if(*It!=this && !It->bLabEvent && !It->IsActorBeingDestroyed()) Amount=FMath::Max(Amount,It->ClimateContribution());
    if(auto* Instance=GetWorld()->GetParameterCollectionInstance(Climate)) Instance->SetScalarParameterValue(TEXT("ColdAmount"),Amount);
}

void AMCIceEvent::PublishManualHUD()
{
    if(bLabEvent) return;
    auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !State || !State->bDevManualEvents || !ActorHasTag(TEXT("MC_DevKeyEvent"))) return;
    auto& Status=State->DirectorState;
    const FString Title=IsComplete()?TEXT("ЛЕДЯНОЙ КРИСТАЛЛ РАЗБИТ"):bFailed?TEXT("ЗИМА НЕ ЗАПУСТИЛАСЬ"):TEXT("ЗИМА БЛИЗКО");
    const FString Instruction=IsComplete()?TEXT("Лёд растаял. Выбери следующее событие в F3."):
        Stage==EMCIceEventStage::Arrival?TEXT("Падает ледяной кристалл. Найди тёплую зону у края; приготовь кирку (слот 2)."):
        UsesColdThroneContract()?TEXT("Перепрыгивай волну холода. Согревайся в тёплых зонах и чисти лёд щёткой; рядом с боссом тепло сразу закрывается. Из тюрьмы освобождай ноги киркой до падения сосулек."):
        bUseAnimatedBossAttacks?TEXT("Разбей ледяную конфету киркой. Тёплая зона защищает от волны. Уходи из отмеченной зоны перед падением льда и криком; разбивай лёд на ногах."):
        TEXT("Разбей центральный кристалл. Перебегай между тёплыми зонами; разбивай мешающие согреванию кристаллы и лёд на ногах.");
    const int32 Left=IsComplete()?0:FMath::CeilToInt(CandyHealth),Total=FMath::CeilToInt(MaxCandyHealth);
    if(Status.bEnabled && Status.CurrentTitle==Title && Status.Instruction==Instruction && State->TasksLeft==Left && State->TasksTotal==Total) return;
    Status.bEnabled=true; Status.CurrentTitle=Title; Status.Instruction=Instruction; Status.NextTitle.Empty();
    Status.SpawnedFood=Total; Status.FinishedFood=FMath::Max(0,Total-Left);
    Status.CompletionProgress=Total>0?1-float(Left)/Total:0;
    State->TasksLeft=Left; State->TasksTotal=Total; State->ForceNetUpdate();
}

void AMCIceEvent::ClearGameplay()
{
    BrushedWarmPatches.Reset();
    if(IsValid(SlipperyFloor)) SlipperyFloor->Destroy(); SlipperyFloor=nullptr;
    for(const auto& Hero:FrozenLegHeroes) if(Hero.IsValid()) Hero->ClearFrozenLegs();
    for(const auto& Player:Players) if(IsValid(Player.Hero)) Player.Hero->ClearFrozenLegs();
    FrozenLegHeroes.Reset();
    Players.Reset(); Strikes.Reset(); ZoneCrystals.Reset(); bNextCircle=false; bNovaWarning=false; NovaImpactAt=0;
    CircleHandoffAt=0; NextCircleRetryAt=0; CrystalSerial=0;
    BossAttack=FMCCandyAttackState(); ExtinguishedZones.Reset(); WaveHitHeroes.Reset();
    bSecondPhase=false; NextBossAttackAt=NextScreamAt=0; FacingPlayer=nullptr;
    bColdThroneActive=false; ColdThroneEndsAt=NextColdThroneAt=0;
    NextIceBombAt=NextColdWaveAt=NextIcePrisonAt=0;
    SeriesTarget.Reset(); SeriesShotsLeft=0; NextIcicleAt=NextSeriesShotAt=NextZoneCrystalAt=NextNovaAt=0;
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMCIceEvent::ClearPresentation()
{
    if(IsValid(EventVFX))
    {
        // The reliable completion cue is issued before state/actor removal. A
        // state-only late join never replays a historical shatter.
        if(!bCompletionVFXSignaled) {EventVFX->Destroy(); EventVFX=nullptr;}
    }
    if(IsValid(Tongue)) RemoveTickPrerequisiteActor(Tongue);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetVisibility(false); CandyVisual->SetVisibility(false); CandyFace->SetVisibility(false); FloorGuides->ClearAllMeshSections(); FrostSurface->ClearAllMeshSections(); NovaWind->ClearAllMeshSections();
    for(UStaticMeshComponent* Part:IcicleMeshes) if(IsValid(Part)) Part->DestroyComponent(); IcicleMeshes.Reset();
    for(UStaticMeshComponent* Part:ZoneCrystalMeshes) if(IsValid(Part)) Part->DestroyComponent(); ZoneCrystalMeshes.Reset();
    for(UWidgetComponent* Bar:FreezeBars) if(IsValid(Bar)) Bar->DestroyComponent(); FreezeBars.Reset(); BarHeroes.Reset();
    for(USkeletalMeshComponent* Part:IceCoatings) if(IsValid(Part))
    {
        if(auto* Source=Part->GetAttachParent())
            if(auto* Hero=Source->GetOwner()) RemoveTickPrerequisiteActor(Hero);
        Part->DestroyComponent();
    }
    IceCoatings.Reset();
    SetClimate();
}

void AMCIceEvent::OnRep_State()
{
    if(Stage==EMCIceEventStage::Arrival && bCompletionVFXSignaled)
    {
        // Start also supports reuse of this actor after successful completion.
        // Release the former local cue before presentation of the new run.
        bCompletionVFXSignaled=false;
        if(IsValid(EventVFX)) EventVFX->Destroy();
        EventVFX=nullptr;
    }
    if(Stage==EMCIceEventStage::Idle) bCompletionVFXSignaled=false;
    SetActorTickEnabled(IsActive());
    if(!IsActive()) ClearPresentation();
}

void AMCIceEvent::EnsurePresentationVFX()
{
    if(GetNetMode()==NM_DedicatedServer || IsValid(EventVFX) || !GetWorld() || GetWorld()->bIsTearingDown) return;
    FActorSpawnParameters Params;
    Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    EventVFX=GetWorld()->SpawnActor<AMCIceEventVFX>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
}

void AMCIceEvent::Multicast_FinishVFX_Implementation(FVector_NetQuantize CoreFloorPoint)
{
    if(bCompletionVFXSignaled) return;
    bCompletionVFXSignaled=true;
    if(GetNetMode()==NM_DedicatedServer || !GetWorld() || GetWorld()->bIsTearingDown) return;
    EnsurePresentationVFX();
    if(IsValid(EventVFX)) EventVFX->FinishPresentation(CoreFloorPoint);
}

void AMCIceEvent::Multicast_CancelVFX_Implementation()
{
    bCompletionVFXSignaled=false;
    if(IsValid(EventVFX)) EventVFX->Destroy();
    EventVFX=nullptr;
}

void AMCIceEvent::Finish(bool bFailure)
{
    if(!HasAuthority()) return;
    if(!bFailure)
    {
        FHitResult Floor;
        const FVector Point=AnchorFloor(CandyAnchor,Floor)?Floor.ImpactPoint:GetActorLocation()-FVector(0,0,240);
        Multicast_FinishVFX(Point);
    }
    bFailed=bFailure; Stage=EMCIceEventStage::Complete;
    ClearGameplay(); OnRep_State(); PublishManualHUD(); ForceNetUpdate();
}

void AMCIceEvent::Stop()
{
    if(!HasAuthority()) return;
    // Cancel is reliable too: a Stop immediately followed by Destroy cannot
    // leave a former success cue playing merely because Idle never replicated.
    Multicast_CancelVFX();
    Stage=EMCIceEventStage::Idle; ClearGameplay(); OnRep_State(); ForceNetUpdate();
}

void AMCIceEvent::EndPlay(const EEndPlayReason::Type Reason)
{
    // A successful event may be removed by its director immediately. Its local
    // one-shot shatter already has a short lifespan; cancellation keeps ownership
    // so ClearPresentation kills every loop and retained pooled burst instead.
    if(Reason==EEndPlayReason::Destroyed && bCompletionVFXSignaled && IsValid(EventVFX)) EventVFX=nullptr;
    else bCompletionVFXSignaled=false;
    // Tear down the local climate contribution even on streaming removal on a client.
    Stage=EMCIceEventStage::Idle;
    if(HasAuthority()) ClearGameplay();
    ClearPresentation(); Super::EndPlay(Reason);
}

void AMCIceEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCIceEvent,bLabEvent);
    DOREPLIFETIME(AMCIceEvent,Stage); DOREPLIFETIME(AMCIceEvent,bFailed);
    DOREPLIFETIME(AMCIceEvent,CandyHealth); DOREPLIFETIME(AMCIceEvent,MaxCandyHealth);
    DOREPLIFETIME(AMCIceEvent,ArrivalSeconds); DOREPLIFETIME(AMCIceEvent,FreezeSeconds); DOREPLIFETIME(AMCIceEvent,ThawSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleSeconds); DOREPLIFETIME(AMCIceEvent,CircleStepSeconds); DOREPLIFETIME(AMCIceEvent,MinCircleSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleOverlapSeconds);
    DOREPLIFETIME(AMCIceEvent,CircleRadius); DOREPLIFETIME(AMCIceEvent,MinCircleRadius);
    DOREPLIFETIME(AMCIceEvent,IcicleWarningSeconds); DOREPLIFETIME(AMCIceEvent,IcicleIntervalSeconds);
    DOREPLIFETIME(AMCIceEvent,IcicleSeriesSpacingSeconds);
    DOREPLIFETIME(AMCIceEvent,IcicleRadius); DOREPLIFETIME(AMCIceEvent,IcicleDamage);
    DOREPLIFETIME(AMCIceEvent,ZoneCrystalIntervalSeconds); DOREPLIFETIME(AMCIceEvent,ZoneCrystalHealth);
    DOREPLIFETIME(AMCIceEvent,CircleMinGap); DOREPLIFETIME(AMCIceEvent,CircleHandoffAt);
    DOREPLIFETIME(AMCIceEvent,ZoneCrystalFallSeconds); DOREPLIFETIME(AMCIceEvent,ZoneCrystalFallHeight);
    DOREPLIFETIME(AMCIceEvent,ZoneCrystalVortexRadius); DOREPLIFETIME(AMCIceEvent,ZoneCrystalVortexSeconds); DOREPLIFETIME(AMCIceEvent,ZoneCrystalPush);
    DOREPLIFETIME(AMCIceEvent,BossVortexSeconds); DOREPLIFETIME(AMCIceEvent,BossFrostRadius); DOREPLIFETIME(AMCIceEvent,BossFreezeMultiplier);
    DOREPLIFETIME(AMCIceEvent,NovaIntervalSeconds); DOREPLIFETIME(AMCIceEvent,NovaWarningSeconds);
    DOREPLIFETIME(AMCIceEvent,NovaRange); DOREPLIFETIME(AMCIceEvent,NovaHalfAngleDegrees); DOREPLIFETIME(AMCIceEvent,NovaFootHealth);
    DOREPLIFETIME(AMCIceEvent,Tongue); DOREPLIFETIME(AMCIceEvent,CandyAnchor);
    DOREPLIFETIME(AMCIceEvent,SafeAnchor); DOREPLIFETIME(AMCIceEvent,NextSafeAnchor); DOREPLIFETIME(AMCIceEvent,bNextCircle);
    DOREPLIFETIME(AMCIceEvent,StartedAt); DOREPLIFETIME(AMCIceEvent,CircleStartedAt); DOREPLIFETIME(AMCIceEvent,CircleIndex);
    DOREPLIFETIME(AMCIceEvent,NextCircleStartedAt); DOREPLIFETIME(AMCIceEvent,ZoneCrystals);
    DOREPLIFETIME(AMCIceEvent,bNovaWarning); DOREPLIFETIME(AMCIceEvent,NovaImpactAt); DOREPLIFETIME(AMCIceEvent,NovaDirection);
    DOREPLIFETIME(AMCIceEvent,Players); DOREPLIFETIME(AMCIceEvent,Strikes);
    DOREPLIFETIME(AMCIceEvent,BossAttack); DOREPLIFETIME(AMCIceEvent,bSecondPhase); DOREPLIFETIME(AMCIceEvent,ExtinguishedZones);
    DOREPLIFETIME(AMCIceEvent,bUseAnimatedBossAttacks); DOREPLIFETIME(AMCIceEvent,AttackRecoverySeconds);
    DOREPLIFETIME(AMCIceEvent,ScreamCooldownSeconds); DOREPLIFETIME(AMCIceEvent,ScreamWarningSeconds);
    DOREPLIFETIME(AMCIceEvent,CrystalFlightSeconds); DOREPLIFETIME(AMCIceEvent,WaveTravelSeconds);
    DOREPLIFETIME(AMCIceEvent,FacingPlayer);
    DOREPLIFETIME(AMCIceEvent,BrushedWarmPatches);
    DOREPLIFETIME(AMCIceEvent,ColdThroneActiveSeconds); DOREPLIFETIME(AMCIceEvent,ColdThroneRestSeconds);
    DOREPLIFETIME(AMCIceEvent,bColdThroneActive); DOREPLIFETIME(AMCIceEvent,ColdThroneEndsAt); DOREPLIFETIME(AMCIceEvent,NextColdThroneAt);
    DOREPLIFETIME(AMCIceEvent,IceBombCooldownSeconds); DOREPLIFETIME(AMCIceEvent,ColdWaveCooldownSeconds); DOREPLIFETIME(AMCIceEvent,IcePrisonCooldownSeconds);
    DOREPLIFETIME(AMCIceEvent,IcePrisonEscapeSeconds); DOREPLIFETIME(AMCIceEvent,IceBombRadius); DOREPLIFETIME(AMCIceEvent,IceBombDamage);
    DOREPLIFETIME(AMCIceEvent,IceBombPush); DOREPLIFETIME(AMCIceEvent,IcePrisonRadius); DOREPLIFETIME(AMCIceEvent,IcePrisonDamage);
    DOREPLIFETIME(AMCIceEvent,NextIceBombAt); DOREPLIFETIME(AMCIceEvent,NextColdWaveAt); DOREPLIFETIME(AMCIceEvent,NextIcePrisonAt);
}
